// Storage (s007): /proc/self/mountinfo, statvfs, /sys/block disk facts and
// the device -> whole-disk walk. Everything under a ROOT path, so the
// selftest builds an Asahi-shaped tree (btrfs root + home subvolumes on one
// NVMe partition, a LUKS dm device on another).
#include "core/probe/Probe.hpp"
#include "Parse.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <algorithm>

namespace trails::core::probe {
using namespace detail;

namespace {

std::vector<std::string> list_dir(const std::string& path) {
    std::vector<std::string> out;
    if (DIR* d = ::opendir(path.c_str())) {
        while (dirent* e = ::readdir(d)) {
            std::string n = e->d_name;
            if (n != "." && n != "..") out.push_back(std::move(n));
        }
        ::closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

std::string first_line(const std::string& path) {
    std::string t;
    if (!slurp(path, t)) return {};
    if (auto nl = t.find('\n'); nl != std::string::npos) t.resize(nl);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) t.pop_back();
    return t;
}

// mountinfo escapes space, tab, newline and backslash as \ooo.
std::string unescape(std::string_view s) {
    auto oct = [](char c) { return c >= '0' && c <= '7'; };
    std::string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 4 <= s.size() && oct(s[i + 1]) && oct(s[i + 2]) && oct(s[i + 3])) {
            o += static_cast<char>((s[i + 1] - '0') * 64 + (s[i + 2] - '0') * 8 + (s[i + 3] - '0'));
            i += 3;
            continue;
        }
        o += s[i];
    }
    return o;
}

}  // namespace

bool parse_mountinfo(std::string_view text, std::vector<Mount>& out) {
    out.clear();
    std::string_view s = text, line;
    while (next_line(s, line)) {
        // id parent maj:min root point opts [optional...] - fstype source superopts
        std::string_view r = line;
        std::string_view id = next_tok(r), parent = next_tok(r), mm = next_tok(r);
        std::string_view root = next_tok(r), point = next_tok(r), opts = next_tok(r);
        if (id.empty() || parent.empty() || mm.empty() || root.empty() || point.empty() || opts.empty())
            continue;
        std::string_view t;
        bool dash = false;
        while (!(t = next_tok(r)).empty())
            if (t == "-") { dash = true; break; }
        if (!dash) continue;
        std::string_view fstype = next_tok(r), source = next_tok(r), super = next_tok(r);
        if (fstype.empty()) continue;

        Mount m;
        const auto colon = mm.find(':');
        if (colon == std::string_view::npos || !to_num(mm.substr(0, colon), m.major) ||
            !to_num(mm.substr(colon + 1), m.minor))
            continue;
        m.root   = unescape(root);
        m.point  = unescape(point);
        m.fstype = std::string(fstype);
        m.source = unescape(source);
        m.read_only = opts == "ro" || opts.substr(0, 3) == "ro,";
        // "subvol=/home" among the super options (btrfs)
        std::string_view so = super;
        while (!so.empty()) {
            const auto c = so.find(',');
            std::string_view o = so.substr(0, c);
            if (o.substr(0, 7) == "subvol=") m.subvol = unescape(o.substr(7));
            if (c == std::string_view::npos) break;
            so.remove_prefix(c + 1);
        }
        out.push_back(std::move(m));
    }
    return true;
}

bool read_mounts(const std::string& root, std::vector<Mount>& out) {
    std::string text;
    if (!slurp(at(root, "proc/self/mountinfo"), text)) { out.clear(); return false; }
    return parse_mountinfo(text, out);
}

bool is_storage_mount(const Mount& m) {
    if (m.source.rfind("/dev/", 0) != 0) return false;
    const std::string dev = m.source.substr(5);
    if (dev.rfind("loop", 0) == 0) return false;   // snaps, mounted images
    for (const char* pseudo : {"devtmpfs", "tmpfs", "squashfs", "overlay", "proc", "sysfs", "devpts"})
        if (m.fstype == pseudo) return false;
    return true;
}

bool fs_usage(const std::string& root, const std::string& point, FsUsage& out) {
    out = {};
    struct statvfs v{};
    const std::string p = (root.empty() || root == "/")
                              ? point
                              : at(root, point.substr(!point.empty() && point[0] == '/' ? 1 : 0));
    if (::statvfs(p.c_str(), &v) != 0) return false;
    const std::uint64_t f = v.f_frsize ? v.f_frsize : v.f_bsize;
    out.size  = std::uint64_t(v.f_blocks) * f;
    out.avail = std::uint64_t(v.f_bavail) * f;
    out.used  = (std::uint64_t(v.f_blocks) - std::uint64_t(v.f_bfree)) * f;
    return true;
}

bool read_disk_info(const std::string& root, const std::string& name, DiskInfo& out) {
    out = {};
    out.name = name;
    const std::string dir = at(root, "sys/block/" + name);
    if (!exists(dir)) return false;
    std::uint64_t sectors = 0;
    if (to_num(std::string_view(first_line(dir + "/size")), sectors)) out.size_bytes = sectors * 512;
    out.model      = first_line(dir + "/device/model");
    out.rotational = first_line(dir + "/queue/rotational") == "1";
    out.removable  = first_line(dir + "/removable") == "1";
    for (const auto& e : list_dir(dir))
        if (e.rfind(name, 0) == 0 && e.size() > name.size() && exists(dir + "/" + e + "/partition"))
            out.parts.push_back(e);
    return true;
}

std::string dev_of_source(const std::string& root, const std::string& source) {
    if (source.rfind("/dev/", 0) != 0) return source;
    std::string dev = source.substr(5);
    if (dev.rfind("mapper/", 0) == 0) {
        const std::string want = dev.substr(7);
        for (const auto& e : list_dir(at(root, "sys/block")))
            if (e.rfind("dm-", 0) == 0 && first_line(at(root, "sys/block/" + e + "/dm/name")) == want)
                return e;
    }
    return dev;
}

std::vector<std::string> disks_under(const std::string& root, const std::string& dev) {
    std::vector<std::string> out;
    auto add = [&](const std::string& d) {
        if (std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
    };
    // Bounded walk (dm on md on partitions is three levels; 8 is plenty).
    std::vector<std::string> todo{dev};
    for (int guard = 0; !todo.empty() && guard < 64; ++guard) {
        const std::string d = todo.back();
        todo.pop_back();
        const std::string dir = at(root, "sys/block/" + d);
        if (exists(dir)) {
            const auto slaves = list_dir(dir + "/slaves");
            if (slaves.empty()) add(d);
            else for (const auto& s : slaves) todo.push_back(s);
            continue;
        }
        // A partition: it sits under its disk's directory.
        for (const auto& w : list_dir(at(root, "sys/block")))
            if (exists(at(root, "sys/block/" + w + "/" + d + "/partition"))) { add(w); break; }
    }
    return out;
}

}  // namespace trails::core::probe

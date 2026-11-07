// proc/swaps and sys/block/zram*/{disksize,mm_stat} (s004).
#include "core/probe/Probe.hpp"
#include "Parse.hpp"

#include <algorithm>
#include <dirent.h>

namespace trails::core::probe {
using namespace detail;

bool read_swaps(const std::string& root, std::vector<SwapDev>& out) {
    out.clear();
    std::string t;
    if (!slurp(at(root, "proc/swaps"), t)) return false;
    std::string_view s = t, line;
    next_line(s, line);   // header
    while (next_line(s, line)) {
        // Filename Type Size(KiB) Used(KiB) Priority. A filename with spaces
        // is escaped by the kernel (\040), so whitespace splitting is safe.
        std::string_view r = line;
        SwapDev d;
        d.name = std::string(next_tok(r));
        d.type = std::string(next_tok(r));
        std::uint64_t kb_size = 0, kb_used = 0;
        if (d.name.empty() || !next_num(r, kb_size) || !next_num(r, kb_used)) continue;
        next_num(r, d.priority);
        d.size = kb_size * 1024;
        d.used = kb_used * 1024;
        out.push_back(std::move(d));
    }
    return true;
}

std::vector<Zram> read_zram(const std::string& root) {
    std::vector<Zram> out;
    DIR* d = ::opendir(at(root, "sys/block").c_str());
    if (!d) return out;
    while (dirent* e = ::readdir(d)) {
        std::string n = e->d_name;
        if (n.rfind("zram", 0) != 0) continue;
        const std::string base = at(root, "sys/block/" + n + "/");
        std::string t;
        Zram z;
        z.name = n;
        if (slurp(base + "disksize", t)) { std::string_view s = t; next_num(s, z.disksize); }
        if (z.disksize == 0) continue;   // allocated but unused
        if (slurp(base + "mm_stat", t)) {
            // orig_data_size compr_data_size mem_used_total mem_limit ...
            std::string_view s = t;
            next_num(s, z.orig);
            next_num(s, z.compr);
            next_num(s, z.mem_used);
        }
        out.push_back(std::move(z));
    }
    ::closedir(d);
    std::sort(out.begin(), out.end(), [](const Zram& a, const Zram& b) { return a.name < b.name; });
    return out;
}

}  // namespace trails::core::probe

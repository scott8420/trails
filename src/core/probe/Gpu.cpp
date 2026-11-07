// GPU (s008): /sys/class/drm cards + connectors, and GPU time per engine from
// DRM fdinfo. On Scott's M1 Pro: card1 (+ renderD128) and card2 with eDP-1
// and HDMI-A-1 connectors. amdgpu adds gpu_busy_percent and VRAM in sysfs.
#include "core/probe/Probe.hpp"
#include "Parse.hpp"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <tuple>

namespace trails::core::probe {
using namespace detail;

namespace {
bool is_card(const std::string& n) {
    if (n.rfind("card", 0) != 0 || n.size() == 4) return false;
    for (std::size_t i = 4; i < n.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(n[i]))) return false;
    return true;
}
std::string uevent_driver(const std::string& path) {
    std::string t;
    if (!slurp(path, t)) return {};
    std::string_view s = t, line;
    while (next_line(s, line))
        if (line.rfind("DRIVER=", 0) == 0) return std::string(line.substr(7));
    return {};
}
}  // namespace

bool read_drm(const std::string& root, std::vector<DrmCard>& out) {
    out.clear();
    const std::string base = at(root, "sys/class/drm");
    if (!sys_exists(base)) return false;
    const auto names = sys_list(base);
    for (const auto& n : names) {
        if (!is_card(n)) continue;
        DrmCard c;
        c.name = n;
        const std::string dev = base + "/" + n + "/device";
        c.driver = uevent_driver(dev + "/uevent");
        for (const auto& e : sys_list(dev + "/drm"))
            if (e.rfind("renderD", 0) == 0) c.render = e;
        long long v = 0;
        if (sys_num(dev + "/gpu_busy_percent", v)) c.busy = int(v);
        if (sys_num(dev + "/mem_info_vram_used", v)) c.vram_used = std::uint64_t(v);
        if (sys_num(dev + "/mem_info_vram_total", v)) c.vram_total = std::uint64_t(v);
        const std::string pre = n + "-";
        for (const auto& e : names) {
            if (e.rfind(pre, 0) != 0) continue;
            DrmConnector k;
            k.name = e.substr(pre.size());
            const std::string d = base + "/" + e + "/";
            k.status = sys_line(d + "status");
            k.enabled = sys_line(d + "enabled") == "enabled";
            k.mode = sys_line(d + "modes");   // first listed = the preferred / current mode
            c.connectors.push_back(std::move(k));
        }
        // Connected displays first (the built-in panel, a plugged monitor),
        // then the empty ports, by name.
        std::stable_sort(c.connectors.begin(), c.connectors.end(), [](const auto& a, const auto& b) {
            const bool ca = a.status == "connected", cb = b.status == "connected";
            return ca != cb ? ca : a.name < b.name;
        });
        out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(), [](const DrmCard& a, const DrmCard& b) {
        return std::stol(a.name.substr(4)) < std::stol(b.name.substr(4));
    });
    return true;
}

std::vector<DrmEngineTime> read_drm_engines(const std::string& root) {
    std::map<std::pair<std::string, std::string>, std::uint64_t> sum;   // (driver, engine) -> ns
    std::set<std::tuple<std::string, std::string, std::string>> seen;   // (driver, pdev, client id)
    for (int pid : list_pids(root)) {
        const std::string pdir = at(root, "proc/" + std::to_string(pid));
        for (const auto& fd : sys_list(pdir + "/fd")) {
            char buf[256];
            const ssize_t n = ::readlink((pdir + "/fd/" + fd).c_str(), buf, sizeof buf - 1);
            if (n <= 0) continue;
            buf[n] = '\0';
            if (std::string_view(buf).rfind("/dev/dri/", 0) != 0) continue;
            std::string text;
            if (!slurp(pdir + "/fdinfo/" + fd, text)) continue;
            std::string driver, pdev, client;
            std::vector<std::pair<std::string, std::uint64_t>> engines;
            std::string_view s = text, line;
            while (next_line(s, line)) {
                const auto colon = line.find(':');
                if (colon == std::string_view::npos) continue;
                const std::string_view key = line.substr(0, colon);
                std::string_view val = line.substr(colon + 1);
                while (!val.empty() && is_ws(val.front())) val.remove_prefix(1);
                if (key == "drm-driver") driver = std::string(val);
                else if (key == "drm-pdev") pdev = std::string(val);
                else if (key == "drm-client-id") client = std::string(val);
                else if (key.rfind("drm-engine-", 0) == 0 && key.find("capacity") == std::string_view::npos) {
                    std::uint64_t ns = 0;
                    std::string_view num = next_tok(val);
                    if (to_num(num, ns)) engines.emplace_back(std::string(key.substr(11)), ns);
                }
            }
            if (driver.empty() || engines.empty()) continue;
            if (!client.empty() && !seen.insert({driver, pdev, client}).second) continue;   // a dup fd
            for (const auto& [e, ns] : engines) sum[{driver, e}] += ns;
        }
    }
    std::vector<DrmEngineTime> out;
    for (const auto& [k, ns] : sum) out.push_back({k.first, k.second, ns});
    return out;
}

}  // namespace trails::core::probe

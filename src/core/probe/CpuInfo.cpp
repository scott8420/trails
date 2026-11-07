// cpufreq, the CPU model string, and P / E clustering (s004).
#include "core/probe/Probe.hpp"
#include "Parse.hpp"

#include <algorithm>
#include <map>

namespace trails::core::probe {
using namespace detail;

namespace {
std::uint32_t read_u32(const std::string& path) {
    std::string t;
    std::uint32_t v = 0;
    if (!slurp(path, t)) return 0;
    std::string_view s = t;
    next_num(s, v);
    return v;
}
std::string trim(std::string_view s) {
    while (!s.empty() && (is_ws(s.front()) || s.front() == '\0')) s.remove_prefix(1);
    while (!s.empty() && (is_ws(s.back()) || s.back() == '\0')) s.remove_suffix(1);
    return std::string(s);
}
}  // namespace

std::vector<CpuFreq> read_cpufreq(const std::string& root, const std::vector<int>& cpus) {
    std::vector<CpuFreq> out;
    out.reserve(cpus.size());
    for (int c : cpus) {
        const std::string d = at(root, "sys/devices/system/cpu/cpu" + std::to_string(c) + "/cpufreq/");
        CpuFreq f;
        f.cpu = c;
        f.cur_khz = read_u32(d + "scaling_cur_freq");
        f.max_khz = read_u32(d + "cpuinfo_max_freq");
        out.push_back(f);
    }
    return out;
}

std::string read_cpu_model(const std::string& root) {
    std::string t;
    if (slurp(at(root, "proc/cpuinfo"), t)) {
        std::string_view s = t, line;
        while (next_line(s, line)) {
            const auto colon = line.find(':');
            if (colon == std::string_view::npos) continue;
            const std::string key = trim(line.substr(0, colon));
            if (key == "model name") {
                std::string v = trim(line.substr(colon + 1));
                if (!v.empty()) return v;
            }
        }
    }
    if (slurp(at(root, "proc/device-tree/model"), t)) return trim(t);
    return {};
}

std::vector<Cluster> clusters_from(const std::vector<CpuFreq>& freq) {
    std::map<std::uint32_t, std::vector<int>> by_max;   // ascending max kHz
    for (std::size_t i = 0; i < freq.size(); ++i) by_max[freq[i].max_khz].push_back(int(i));
    std::vector<Cluster> out;
    if (by_max.empty()) return out;
    // Any core without a max (no cpufreq) -> don't guess: one group.
    if (by_max.count(0) || by_max.size() == 1) {
        Cluster c;
        c.name = "All cores";
        c.max_khz = by_max.count(0) ? 0 : by_max.begin()->first;
        for (std::size_t i = 0; i < freq.size(); ++i) c.cores.push_back(int(i));
        out.push_back(std::move(c));
        return out;
    }
    int k = 0;
    for (auto& [mx, idx] : by_max) {
        Cluster c;
        c.max_khz = mx;
        c.cores = idx;
        if (by_max.size() == 2) c.name = (k == 0) ? "Efficiency" : "Performance";
        else                    c.name = "Cluster " + std::to_string(k + 1);
        out.push_back(std::move(c));
        ++k;
    }
    // Show the fast cores first: what people look at.
    std::reverse(out.begin(), out.end());
    return out;
}

}  // namespace trails::core::probe

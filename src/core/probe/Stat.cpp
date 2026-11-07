// /proc/stat, /proc/loadavg, and the shared file helpers.
#include "core/probe/Probe.hpp"
#include "Parse.hpp"

#include <fcntl.h>
#include <unistd.h>

namespace trails::core::probe {
using namespace detail;

std::string at(const std::string& root, std::string_view rel) {
    std::string p = root.empty() ? std::string("/") : root;
    if (p.back() != '/') p += '/';
    while (!rel.empty() && rel.front() == '/') rel.remove_prefix(1);
    p.append(rel);
    return p;
}

bool slurp(const std::string& path, std::string& out) {
    out.clear();
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[8192];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n > 0) { out.append(buf, static_cast<std::size_t>(n)); continue; }
        if (n == 0) break;
        ::close(fd);
        out.clear();
        return false;
    }
    ::close(fd);
    return true;
}

static bool parse_cpu_line(std::string_view rest, CpuTimes& t) {
    // Older kernels have fewer columns; the first four are always there.
    if (!next_num(rest, t.user) || !next_num(rest, t.nice) ||
        !next_num(rest, t.system) || !next_num(rest, t.idle)) return false;
    next_num(rest, t.iowait);
    next_num(rest, t.irq);
    next_num(rest, t.softirq);
    next_num(rest, t.steal);
    return true;
}

bool read_stat(const std::string& root, CpuStat& out) {
    out = {};
    std::string text;
    if (!slurp(at(root, "proc/stat"), text)) return false;
    std::string_view s = text, line;
    bool saw_all = false;
    while (next_line(s, line)) {
        std::string_view rest = line;
        std::string_view key = next_tok(rest);
        if (key == "cpu") {
            saw_all = parse_cpu_line(rest, out.all);
        } else if (key.size() > 3 && key.substr(0, 3) == "cpu") {
            CpuTimes t;
            int id = -1;
            if (parse_cpu_line(rest, t) && to_num(key.substr(3), id)) {
                out.cores.push_back(t);
                out.core_ids.push_back(id);
            }
        } else if (key == "ctxt") {
            next_num(rest, out.ctxt);
        } else if (key == "procs_running") {
            next_num(rest, out.procs_running);
        }
    }
    if (!saw_all) out = {};
    return saw_all;
}

CpuPct cpu_pct(const CpuTimes& a, const CpuTimes& b) {
    CpuPct p;
    const double total = static_cast<double>(delta(a.total(), b.total()));
    if (total <= 0) return p;
    const double idle = static_cast<double>(delta(a.idle_all(), b.idle_all()));
    p.busy   = 100.0 * (total - idle) / total;
    p.user   = 100.0 * static_cast<double>(delta(a.user + a.nice, b.user + b.nice)) / total;
    p.system = 100.0 * static_cast<double>(delta(a.system + a.irq + a.softirq,
                                                 b.system + b.irq + b.softirq)) / total;
    p.iowait = 100.0 * static_cast<double>(delta(a.iowait, b.iowait)) / total;
    if (p.busy < 0) p.busy = 0;
    if (p.busy > 100) p.busy = 100;
    return p;
}

bool read_loadavg(const std::string& root, LoadAvg& out) {
    out = {};
    std::string text;
    if (!slurp(at(root, "proc/loadavg"), text)) return false;
    // "0.03 0.01 0.00 1/92 1133" -- from_chars(double) is fine on GCC 11+.
    std::string_view s = text;
    if (!next_num(s, out.one) || !next_num(s, out.five) || !next_num(s, out.fifteen)) {
        out = {};
        return false;
    }
    std::string_view rt = next_tok(s);
    if (auto slash = rt.find('/'); slash != std::string_view::npos) {
        to_num(rt.substr(0, slash), out.running);
        to_num(rt.substr(slash + 1), out.total);
    }
    return true;
}

}  // namespace trails::core::probe

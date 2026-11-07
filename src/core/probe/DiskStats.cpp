// /proc/diskstats -> per-device I/O counters; whole-disk vs partition from
// root/sys/block/<name>.
#include "core/probe/Probe.hpp"
#include "Parse.hpp"

#include <sys/stat.h>

namespace trails::core::probe {
using namespace detail;

bool read_diskstats(const std::string& root, std::vector<DiskStat>& out) {
    out.clear();
    std::string text;
    if (!slurp(at(root, "proc/diskstats"), text)) return false;
    std::string_view s = text, line;
    while (next_line(s, line)) {
        // major minor name  reads r_merged sectors_read ms_reading
        //                   writes w_merged sectors_written ms_writing
        //                   in_progress io_ms weighted_ms  [discard..., flush...]
        std::string_view rest = line;
        DiskStat d;
        if (!next_num(rest, d.major) || !next_num(rest, d.minor)) continue;
        std::string_view name = next_tok(rest);
        if (name.empty()) continue;
        std::uint64_t f[10] = {};
        int got = 0;
        while (got < 10 && next_num(rest, f[got])) ++got;
        if (got < 10) continue;
        d.name = std::string(name);
        d.reads = f[0]; d.sectors_read = f[2];
        d.writes = f[4]; d.sectors_written = f[6];
        d.io_ms = f[9];
        // Block device names may contain '!' for '/' in sysfs (cciss!c0d0).
        std::string sysname = d.name;
        for (char& c : sysname) if (c == '/') c = '!';
        struct stat st{};
        d.whole_disk = ::stat(at(root, "sys/block/" + sysname).c_str(), &st) == 0;
        out.push_back(std::move(d));
    }
    return true;
}

bool disk_counts_in_total(const DiskStat& d) {
    if (!d.whole_disk) return false;
    std::string_view n = d.name;
    for (std::string_view skip : {"loop", "ram", "dm-", "md", "zram", "sr", "fd"})
        if (n.substr(0, skip.size()) == skip) return false;
    return true;
}

}  // namespace trails::core::probe

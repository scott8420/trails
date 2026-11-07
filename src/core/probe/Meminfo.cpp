// /proc/meminfo -> bytes.
#include "core/probe/Probe.hpp"
#include "Parse.hpp"

namespace trails::core::probe {
using namespace detail;

bool read_meminfo(const std::string& root, MemInfo& out) {
    out = {};
    std::string text;
    if (!slurp(at(root, "proc/meminfo"), text)) return false;
    std::string_view s = text, line;
    bool saw_total = false, saw_avail = false;
    while (next_line(s, line)) {
        std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        std::string_view key = line.substr(0, colon);
        std::string_view rest = line.substr(colon + 1);
        std::uint64_t kb = 0;
        if (!next_num(rest, kb)) continue;
        const std::uint64_t b = kb * 1024;   // the kernel's "kB" is KiB
        if      (key == "MemTotal")     { out.total = b; saw_total = true; }
        else if (key == "MemFree")        out.free = b;
        else if (key == "MemAvailable") { out.available = b; saw_avail = true; }
        else if (key == "Buffers")        out.buffers = b;
        else if (key == "Cached")         out.cached = b;
        else if (key == "SReclaimable")   out.sreclaimable = b;
        else if (key == "Shmem")          out.shmem = b;
        else if (key == "SwapTotal")      out.swap_total = b;
        else if (key == "SwapFree")       out.swap_free = b;
    }
    if (!saw_total) { out = {}; return false; }
    // Pre-3.14 kernels have no MemAvailable: approximate as free(1) did.
    if (!saw_avail) out.available = out.free + out.buffers + out.cached + out.sreclaimable;
    return true;
}

}  // namespace trails::core::probe

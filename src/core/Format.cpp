#include "core/Format.hpp"

#include <cmath>
#include <cstdio>

namespace trails::core::fmt {

std::string bytes(std::uint64_t b) {
    static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    if (b < 1024) return std::to_string(b) + " B";
    double v = static_cast<double>(b);
    int u = 0;
    while (v >= 1024.0 && u < 5) { v /= 1024.0; ++u; }
    // Three significant figures; if rounding carries to 1024, step up a unit.
    const char* f = v < 10 ? "%.2f %s" : v < 100 ? "%.1f %s" : "%.0f %s";
    char buf[32];
    std::snprintf(buf, sizeof buf, f, v, units[u]);
    if (v >= 1023.5 && u < 5) std::snprintf(buf, sizeof buf, "%.2f %s", v / 1024.0, units[u + 1]);
    return buf;
}

std::string rate(double bps) {
    if (!(bps > 0)) return "0 B/s";
    return bytes(static_cast<std::uint64_t>(std::llround(bps))) + "/s";
}

std::string pct(double p) {
    if (!(p > 0)) return "0%";
    if (p > 100) p = 100;
    char buf[16];
    if (p < 9.95) std::snprintf(buf, sizeof buf, "%.1f%%", p);
    else          std::snprintf(buf, sizeof buf, "%.0f%%", p);
    return buf;
}

}  // namespace trails::core::fmt

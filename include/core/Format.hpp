#pragma once
#include <cstdint>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// core::fmt -- numbers as people read them. Binary units (KiB, MiB, GiB) to
// match what the kernel counts and what btop / free -h print. Three
// significant figures at most so a tile's value doesn't jitter in width.
// GTK-free; the selftest pins the edges (0, 1023, 1024, rounding up a unit).
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::core::fmt {

std::string bytes(std::uint64_t b);     // "0 B", "512 B", "1.00 KiB", "15.6 GiB"
std::string rate(double bytes_per_s);   // bytes(...) + "/s"
std::string pct(double p);              // "0%", "7.5%", "42%", "100%"

}  // namespace trails::core::fmt

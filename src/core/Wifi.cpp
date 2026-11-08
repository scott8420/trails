#include "core/Wifi.hpp"

#include <cstdio>

namespace trails::core {

namespace {
bool valid_utf8(const std::vector<unsigned char>& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = s[i];
        int n;   // continuation bytes that follow
        if (c < 0x80)             n = 0;
        else if ((c >> 5) == 0x6) n = 1;
        else if ((c >> 4) == 0xE) n = 2;
        else if ((c >> 3) == 0x1E) n = 3;
        else return false;
        if (n == 0 && (c < 0x20 || c == 0x7F)) return false;   // control characters: escape them
        if (i + std::size_t(n) >= s.size() && n > 0) return false;
        for (int k = 1; k <= n; ++k)
            if ((s[i + std::size_t(k)] & 0xC0) != 0x80) return false;
        i += std::size_t(n) + 1;
    }
    return true;
}
}  // namespace

std::string ssid_text(const std::vector<unsigned char>& raw) {
    if (valid_utf8(raw)) return std::string(raw.begin(), raw.end());
    std::string out;
    for (unsigned char c : raw) {
        if (c >= 0x20 && c < 0x7F) { out += char(c); continue; }
        char b[8];
        std::snprintf(b, sizeof b, "\\x%02X", c);
        out += b;
    }
    return out;
}

std::string wifi_band(unsigned mhz) {
    if (mhz == 0) return {};
    if (mhz < 3000) return "2.4 GHz";
    if (mhz < 5925) return "5 GHz";
    if (mhz < 7200) return "6 GHz";
    return std::to_string(mhz) + " MHz";
}

std::string wifi_line(const WifiInfo& w) {
    if (!w.associated) return "not connected";
    std::string out = w.ssid.empty() ? std::string("hidden network") : "“" + w.ssid + "”";
    if (const auto b = wifi_band(w.freq_mhz); !b.empty()) out += " · " + b;
    if (w.bitrate_kbps > 0) {
        const unsigned mb = (w.bitrate_kbps + 500) / 1000;
        out += " · " + std::to_string(mb) + " Mb/s";
    }
    return out;
}

}  // namespace trails::core

#pragma once
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core::Wifi -- what NetworkManager knows about a Wi-Fi link (s015), and how
// the Network tab says it. GTK-free; the D-Bus side is src/NetMgr.cpp.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::core {

struct WifiInfo {
    std::string ssid;               // "" = hidden network / not associated
    unsigned    bitrate_kbps = 0;   // NM's Device.Wireless.Bitrate; 0 = unknown
    unsigned    freq_mhz = 0;       // the access point's channel frequency
    int         strength = -1;      // NM's 0..100; -1 = unknown
    bool        associated = false; // an active access point exists
};

// SSIDs are bytes, not text: valid UTF-8 passes through, anything else is
// shown as \xNN escapes. Empty -> "".
std::string ssid_text(const std::vector<unsigned char>& raw);
// 2412 -> "2.4 GHz", 5180 -> "5 GHz", 5955 -> "6 GHz"; 0 -> "".
std::string wifi_band(unsigned mhz);
// "“HomeNet” · 5 GHz · 866 Mb/s"; "not connected" when not associated.
std::string wifi_line(const WifiInfo& w);

}  // namespace trails::core

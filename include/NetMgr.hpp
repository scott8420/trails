#pragma once
#include "core/Wifi.hpp"

#include <giomm/dbusconnection.h>

#include <map>
#include <memory>
#include <optional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// NetMgr -- Wi-Fi facts from NetworkManager over the system bus (s015).
//
// poll(iface) starts (at most every few seconds per interface) an async chain:
//   GetDeviceByIpIface -> Device.Wireless {Bitrate, ActiveAccessPoint}
//                      -> AccessPoint {Ssid, Frequency, Strength}
// and get(iface) returns the last answer. Never blocks the UI. No
// NetworkManager (or no system bus) -> get() stays empty and the Network tab
// shows the signal line it always did. Answers that land after the owner
// is gone are dropped (the state is held by a weak pointer).
// ─────────────────────────────────────────────────────────────────────────────
namespace trails {

class NetMgr {
public:
    void poll(const std::string& iface);
    std::optional<core::WifiInfo> get(const std::string& iface) const;

private:
    struct State {
        std::map<std::string, core::WifiInfo> info;
        std::map<std::string, double>         asked;     // iface -> monotonic seconds
        std::map<std::string, bool>           inflight;
    };
    std::shared_ptr<State>              m_st = std::make_shared<State>();
    Glib::RefPtr<Gio::DBus::Connection> m_bus;
    bool                                m_dead = false;   // no system bus: stop trying
};

}  // namespace trails

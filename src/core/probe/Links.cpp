// Network links (s007): /sys/class/net/<if>/*, /proc/net/wireless, and the
// live addresses from getifaddrs (the one reader with no root path).
#include "core/probe/Probe.hpp"
#include "Parse.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/stat.h>

#include <bit>
#include <cstring>

namespace trails::core::probe {
using namespace detail;

namespace {
bool exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}
std::string first_line(const std::string& path) {
    std::string t;
    if (!slurp(path, t)) return {};
    if (auto nl = t.find('\n'); nl != std::string::npos) t.resize(nl);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\r')) t.pop_back();
    return t;
}
}  // namespace

bool read_netlink(const std::string& root, const std::string& name, NetLink& out) {
    out = {};
    out.name = name;
    const std::string dir = at(root, "sys/class/net/" + name);
    if (!exists(dir)) return false;
    out.operstate = first_line(dir + "/operstate");
    out.mac       = first_line(dir + "/address");
    long long v = 0;
    // speed: reading it on a down link is EINVAL -- the slurp fails, -1 stays.
    if (to_num(std::string_view(first_line(dir + "/speed")), v) && v > 0) out.speed_mbps = int(v);
    if (to_num(std::string_view(first_line(dir + "/mtu")), v)) out.mtu = int(v);
    out.wireless   = exists(dir + "/wireless") || exists(dir + "/phy80211");
    out.virtual_if = exists(at(root, "sys/devices/virtual/net/" + name));
    return true;
}

bool read_wireless(const std::string& root, std::vector<Wireless>& out) {
    out.clear();
    std::string text;
    if (!slurp(at(root, "proc/net/wireless"), text)) return false;
    //  wlan0: 0000   54.  -56.  -256        0      0      0      0     21        0
    std::string_view s = text, line;
    while (next_line(s, line)) {
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;   // the two header lines have '|'
        std::string_view name = line.substr(0, colon);
        while (!name.empty() && is_ws(name.front())) name.remove_prefix(1);
        if (name.empty() || name.find('|') != std::string_view::npos) continue;
        std::string_view r = line.substr(colon + 1);
        next_tok(r);   // status
        auto num = [&](double& d) {
            std::string t(next_tok(r));
            while (!t.empty() && t.back() == '.') t.pop_back();
            if (t.empty()) return false;
            try { d = std::stod(t); } catch (...) { return false; }
            return true;
        };
        Wireless w;
        w.name = std::string(name);
        if (!num(w.link) || !num(w.level_dbm)) continue;
        out.push_back(std::move(w));
    }
    return true;
}

std::vector<IfAddr> read_addrs() {
    std::vector<IfAddr> out;
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) return out;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || !a->ifa_name) continue;
        const int fam = a->ifa_addr->sa_family;
        if (fam != AF_INET && fam != AF_INET6) continue;
        IfAddr ia;
        ia.name = a->ifa_name;
        ia.v6 = fam == AF_INET6;
        char buf[INET6_ADDRSTRLEN] = {};
        if (fam == AF_INET) {
            ::inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, buf, sizeof buf);
            if (a->ifa_netmask)
                ia.prefix = std::popcount(reinterpret_cast<sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr);
        } else {
            ::inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(a->ifa_addr)->sin6_addr, buf, sizeof buf);
            if (a->ifa_netmask) {
                const auto* m = reinterpret_cast<sockaddr_in6*>(a->ifa_netmask)->sin6_addr.s6_addr;
                for (int i = 0; i < 16; ++i) ia.prefix += std::popcount(static_cast<unsigned>(m[i]));
            }
        }
        ia.addr = buf;
        out.push_back(std::move(ia));
    }
    ::freeifaddrs(list);
    return out;
}

}  // namespace trails::core::probe

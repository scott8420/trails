// /proc/net/dev -> per-interface byte / packet / error counters.
#include "core/probe/Probe.hpp"
#include "Parse.hpp"

namespace trails::core::probe {
using namespace detail;

bool read_netdev(const std::string& root, std::vector<NetIf>& out) {
    out.clear();
    std::string text;
    if (!slurp(at(root, "proc/net/dev"), text)) return false;
    std::string_view s = text, line;
    while (next_line(s, line)) {
        // Two header lines have '|' and no "name:"; data lines are
        //   "  eth0: rx_bytes rx_pkts errs drop fifo frame compressed multicast
        //            tx_bytes tx_pkts errs drop fifo colls carrier compressed"
        // and "eth0:123" (no space after the colon) happens with big counters.
        std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        std::string_view name = line.substr(0, colon);
        while (!name.empty() && is_ws(name.front())) name.remove_prefix(1);
        while (!name.empty() && is_ws(name.back()))  name.remove_suffix(1);
        if (name.empty()) continue;
        std::string_view rest = line.substr(colon + 1);
        std::uint64_t f[16] = {};
        int got = 0;
        while (got < 16 && next_num(rest, f[got])) ++got;
        if (got < 11) continue;   // malformed
        NetIf n;
        n.name = std::string(name);
        n.rx_bytes = f[0]; n.rx_packets = f[1]; n.rx_errs = f[2];
        n.tx_bytes = f[8]; n.tx_packets = f[9]; n.tx_errs = f[10];
        out.push_back(std::move(n));
    }
    return true;
}

}  // namespace trails::core::probe

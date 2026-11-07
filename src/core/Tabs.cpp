#include "core/Tabs.hpp"

namespace trails::core {

const std::vector<TabSpec>& tab_registry() {
    // Order = rail order = Alt+1 .. Alt+0.
    static const std::vector<TabSpec> kTabs = {
        {"overview",  "Overview",        "trails-tab-overview-symbolic",  "tab-overview",  "",
         "Everything at a glance."},
        {"cpu",       "CPU",             "trails-tab-cpu-symbolic",       "tab-cpu",       "s004",
         "Every core, its frequency, load, and the P / E clusters."},
        {"memory",    "Memory",          "trails-tab-memory-symbolic",    "tab-memory",    "s004",
         "Used, cached, buffers, swap and zram -- and who holds it."},
        {"processes", "Processes",       "trails-tab-processes-symbolic", "tab-processes", "s005",
         "Every process, tree or flat, sortable, with the actions to tame it."},
        {"disks",     "Disks",           "trails-tab-disks-symbolic",     "tab-disks",     "s007",
         "Per-device I/O, mounts, usage and btrfs subvolumes."},
        {"network",   "Network",         "trails-tab-network-symbolic",   "tab-network",   "s007",
         "Per-interface rates, totals, addresses and who is talking."},
        {"gpu",       "GPU",             "trails-tab-gpu-symbolic",       "tab-gpu",       "s008",
         "Utilisation and memory, as far as the driver tells."},
        {"sensors",   "Sensors & Power", "trails-tab-sensors-symbolic",   "tab-sensors",   "s008",
         "Every hwmon sensor found, battery and power draw."},
        {"services",  "Services",        "trails-tab-services-symbolic",  "tab-services",  "s011",
         "systemd units -- state, journal tail, start / stop / restart."},
        {"trails",    "Trails",          "trails-tab-trails-symbolic",    "tab-trails",    "s010",
         "Recordings: walk a spike back to the process that caused it."},
    };
    return kTabs;
}

const TabSpec* find_tab(std::string_view id) {
    for (const auto& t : tab_registry())
        if (t.id == id) return &t;
    return nullptr;
}

}  // namespace trails::core

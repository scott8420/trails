// Sensors & Power (s008): /sys/class/hwmon and /sys/class/power_supply,
// read data-driven -- whatever chips and files the kernel offers. Written
// against Scott's M1 Pro tree (macsmc_hwmon / _battery / _ac, nvme, six
// tas2764 speaker amps, four sensorless tps6598x USB-C controllers).
#include "core/probe/Probe.hpp"
#include "Parse.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace trails::core::probe {

namespace detail {
std::vector<std::string> sys_list(const std::string& path) {
    std::vector<std::string> out;
    if (DIR* d = ::opendir(path.c_str())) {
        while (dirent* e = ::readdir(d)) {
            std::string n = e->d_name;
            if (n != "." && n != "..") out.push_back(std::move(n));
        }
        ::closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}
bool sys_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}
std::string sys_line(const std::string& path) {
    std::string t;
    if (!slurp(path, t)) return {};
    if (auto nl = t.find('\n'); nl != std::string::npos) t.resize(nl);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) t.pop_back();
    return t;
}
bool sys_num(const std::string& path, long long& out) {
    const std::string t = sys_line(path);
    return !t.empty() && to_num(std::string_view(t), out);
}
}  // namespace detail

using namespace detail;

namespace {
// "hwmon10" -> 10, for numeric order (hwmon2 before hwmon10).
long suffix_num(const std::string& s) {
    std::size_t i = s.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(s[i - 1]))) --i;
    return i < s.size() ? std::stol(s.substr(i)) : -1;
}
int kind_rank(const std::string& k) {
    static const char* order[] = {"temp", "fan", "power", "in", "curr"};
    for (int i = 0; i < 5; ++i) if (k == order[i]) return i;
    return 9;
}
double scale(const std::string& kind) {
    if (kind == "temp" || kind == "in" || kind == "curr") return 1e-3;   // m-units
    if (kind == "power") return 1e-6;                                    // uW
    return 1.0;                                                          // fan RPM
}
}  // namespace

Alarm alarm(const Sensor& s) {
    if (s.fault || s.kind == "fan") return Alarm::None;
    if (!std::isnan(s.crit_at) && s.value >= s.crit_at) return Alarm::Crit;
    if (!std::isnan(s.warn_at) && s.value >= s.warn_at) return Alarm::Warn;
    return Alarm::None;
}

bool read_hwmon(const std::string& root, std::vector<HwmonChip>& out) {
    out.clear();
    const std::string base = at(root, "sys/class/hwmon");
    if (!sys_exists(base)) return false;
    for (const auto& id : sys_list(base)) {
        if (id.rfind("hwmon", 0) != 0) continue;
        const std::string dir = base + "/" + id;
        HwmonChip chip;
        chip.id = id;
        chip.name = sys_line(dir + "/name");
        for (const auto& f : sys_list(dir)) {
            // <kind><N>_input
            const auto us = f.find('_');
            if (us == std::string::npos || f.substr(us) != "_input") continue;
            const std::string key = f.substr(0, us);
            std::size_t k = 0;
            while (k < key.size() && std::isalpha(static_cast<unsigned char>(key[k]))) ++k;
            const std::string kind = key.substr(0, k);
            if (k == key.size() || kind_rank(kind) == 9) continue;
            Sensor s;
            s.kind  = kind;
            s.key   = key;
            s.label = sys_line(dir + "/" + key + "_label");
            long long v = 0;
            long long fault = 0;
            if (sys_num(dir + "/" + key + "_fault", fault) && fault != 0) s.fault = true;
            else if (sys_num(dir + "/" + f, v)) s.value = double(v) * scale(kind);
            else s.fault = true;   // EIO / ENODATA: the sensor answers nothing
            if (kind == "fan") {
                if (sys_num(dir + "/" + key + "_min", v))    s.min = double(v);
                if (sys_num(dir + "/" + key + "_max", v))    s.max = double(v);
                if (sys_num(dir + "/" + key + "_target", v)) s.target = double(v);
            } else {
                // s015: thresholds. A temperature limit at or below 0 degC is
                // a driver placeholder (nvme's -273.15 min, some chips' 0),
                // not a limit anyone means; power/in/curr use theirs as is.
                const auto limit = [&](const char* suf, double& out) {
                    if (!sys_num(dir + "/" + key + suf, v)) return;
                    const double d = double(v) * scale(kind);
                    if (kind == "temp" && d <= 0) return;
                    out = d;
                };
                limit("_max", s.warn_at);
                limit("_crit", s.crit_at);
                // max == crit (nvme does this): one line, the critical one.
                if (!std::isnan(s.warn_at) && !std::isnan(s.crit_at) && s.warn_at >= s.crit_at)
                    s.warn_at = std::numeric_limits<double>::quiet_NaN();
            }
            chip.sensors.push_back(std::move(s));
        }
        std::sort(chip.sensors.begin(), chip.sensors.end(), [](const Sensor& a, const Sensor& b) {
            const int ra = kind_rank(a.kind), rb = kind_rank(b.kind);
            if (ra != rb) return ra < rb;
            return suffix_num(a.key) < suffix_num(b.key);
        });
        out.push_back(std::move(chip));
    }
    std::sort(out.begin(), out.end(),
              [](const HwmonChip& a, const HwmonChip& b) { return suffix_num(a.id) < suffix_num(b.id); });
    return true;
}

std::string chip_title(const std::string& name) {
    static const std::map<std::string, std::string> titles = {
        {"macsmc_hwmon", "Apple SMC"},       {"macsmc_battery", "Battery (SMC)"},
        {"macsmc_ac", "Power adapter (SMC)"}, {"tas2764", "Speaker amplifier"},
        {"tps6598x", "USB-C port controller"}, {"nvme", "NVMe SSD"},
        {"k10temp", "AMD CPU"},              {"zenpower", "AMD CPU"},
        {"coretemp", "Intel CPU"},           {"amdgpu", "AMD GPU"},
        {"nouveau", "NVIDIA GPU"},           {"iwlwifi_1", "Intel Wi-Fi"},
        {"acpitz", "ACPI thermal zone"},     {"BAT0", "Battery"},
    };
    if (auto it = titles.find(name); it != titles.end()) return it->second;
    // tps6598x_source_psy_0_003a: match the family by prefix.
    for (const auto& [k, v] : titles)
        if (name.rfind(k + "_", 0) == 0) return v;
    return name.empty() ? "Unnamed chip" : name;
}

// ── power_supply ────────────────────────────────────────────────────────────

double PowerSupply::time_left() const {
    if (power <= 0.05) return -1;
    if (status == "Discharging" && energy_now > 0) return 3600.0 * energy_now / power;
    if (status == "Charging" && energy_full > energy_now) return 3600.0 * (energy_full - energy_now) / power;
    return -1;
}

bool read_power_supplies(const std::string& root, std::vector<PowerSupply>& out) {
    out.clear();
    const std::string base = at(root, "sys/class/power_supply");
    if (!sys_exists(base)) return false;
    for (const auto& n : sys_list(base)) {
        const std::string d = base + "/" + n + "/";
        PowerSupply p;
        p.name = n;
        p.type = sys_line(d + "type");
        p.status = sys_line(d + "status");
        p.model = sys_line(d + "model_name");
        p.maker = sys_line(d + "manufacturer");
        p.technology = sys_line(d + "technology");
        long long v = 0;
        if (sys_num(d + "online", v)) p.online = v != 0;
        if (sys_num(d + "capacity", v)) p.capacity = int(v);
        if (sys_num(d + "cycle_count", v)) p.cycles = int(v);
        if (sys_num(d + "voltage_now", v)) p.voltage = double(v) * 1e-6;
        double vdesign = p.voltage;
        if (sys_num(d + "voltage_min_design", v) && v > 0) vdesign = double(v) * 1e-6;
        // energy_* in uWh; else charge_* in uAh x a voltage.
        if (sys_num(d + "energy_now", v)) {
            p.energy_now = double(v) * 1e-6;
            if (sys_num(d + "energy_full", v)) p.energy_full = double(v) * 1e-6;
            if (sys_num(d + "energy_full_design", v)) p.energy_design = double(v) * 1e-6;
        } else if (sys_num(d + "charge_now", v)) {
            p.energy_now = double(v) * 1e-6 * vdesign;
            if (sys_num(d + "charge_full", v)) p.energy_full = double(v) * 1e-6 * vdesign;
            if (sys_num(d + "charge_full_design", v)) p.energy_design = double(v) * 1e-6 * vdesign;
        }
        if (sys_num(d + "power_now", v)) p.power = std::abs(double(v)) * 1e-6;
        else if (sys_num(d + "current_now", v)) p.power = std::abs(double(v)) * 1e-6 * p.voltage;
        out.push_back(std::move(p));
    }
    return true;
}

}  // namespace trails::core::probe

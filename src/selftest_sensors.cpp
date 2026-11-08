// selftest -- s008 sensors, power, GPU, on a fixture shaped from Scott's M1
// Pro listing (Nov 7): hwmon0 nvme, hwmon1 macsmc_hwmon (temps, fans, power,
// volts, current), hwmon2 macsmc_battery, hwmon3 macsmc_ac, hwmon4 + 11-13
// tps6598x (no sensors), hwmon5-10 tas2764 (one temp, with _fault). DRM:
// card1 + renderD128, card2 with eDP-1 and HDMI-A-1. Labels are invented --
// the listing showed which files exist, not what they say.
#include "core/probe/Probe.hpp"

#include <unistd.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

namespace probe = trails::core::probe;
namespace fs = std::filesystem;
using CheckFn = std::function<void(const std::string&, bool, const std::string&)>;

namespace {
struct Fx {
    fs::path root;
    Fx() {
        root = fs::temp_directory_path() /
               ("trails_se_" + std::to_string(::getpid()) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
    }
    ~Fx() { std::error_code ec; fs::remove_all(root, ec); }
    void file(const std::string& rel, const std::string& text) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel) << text;
    }
    void link(const std::string& rel, const std::string& target) const {
        fs::create_directories((root / rel).parent_path());
        fs::create_symlink(target, root / rel);
    }
    std::string str() const { return root.string(); }
};
bool near(double a, double b) { return std::fabs(a - b) < 1e-6 * std::max(1.0, std::fabs(b)); }
}  // namespace

void run_sensor_tests(const CheckFn& check_fn) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_fn(n, ok, d); };
    std::cout << "\n-- sensors, power, gpu (s008) --\n";

    Fx fx;
    const std::string H = "sys/class/hwmon/";
    fx.file(H + "hwmon0/name", "nvme\n");
    fx.file(H + "hwmon0/temp1_input", "38850\n");
    fx.file(H + "hwmon0/temp1_label", "Composite\n");
    fx.file(H + "hwmon0/temp1_alarm", "0\n");
    fx.file(H + "hwmon0/temp1_max", "84850\n");    // s015: nvme says max == crit
    fx.file(H + "hwmon0/temp1_crit", "84850\n");
    fx.file(H + "hwmon0/temp1_min", "-273150\n");
    const std::string S = H + "hwmon1/";
    fx.file(S + "name", "macsmc_hwmon\n");
    for (int i = 1; i <= 4; ++i) {
        fx.file(S + "temp" + std::to_string(i) + "_input", std::to_string(40000 + i * 1000) + "\n");
        fx.file(S + "temp" + std::to_string(i) + "_label", "Temp " + std::to_string(i) + "\n");
        fx.file(S + "power" + std::to_string(i) + "_input", std::to_string(i * 1500000) + "\n");
        fx.file(S + "power" + std::to_string(i) + "_label", "Power " + std::to_string(i) + "\n");
    }
    for (int i = 1; i <= 2; ++i) {
        const std::string f = "fan" + std::to_string(i);
        fx.file(S + f + "_input", "1250\n");
        fx.file(S + f + "_label", "Fan " + std::to_string(i) + "\n");
        fx.file(S + f + "_min", "1200\n");
        fx.file(S + f + "_max", "5800\n");
        fx.file(S + f + "_target", "1200\n");
    }
    fx.file(S + "in0_input", "12340\n");
    fx.file(S + "in0_label", "DC In\n");
    fx.file(S + "in1_input", "4100\n");
    fx.file(S + "in1_label", "Battery\n");
    fx.file(S + "curr1_input", "2500\n");
    fx.file(S + "curr1_label", "DC In\n");
    fx.file(H + "hwmon2/name", "macsmc_battery\n");
    fx.file(H + "hwmon2/temp1_input", "31000\n");
    fx.file(H + "hwmon2/temp1_max", "30000\n");    // s015: 31 degC is past "high"
    fx.file(H + "hwmon2/temp1_crit", "60000\n");
    fx.file(H + "hwmon2/curr1_crit", "500\n");     // 0.77 A past a 0.5 A crit
    fx.file(H + "hwmon2/in0_input", "12700\n");
    fx.file(H + "hwmon2/power1_input", "9800000\n");
    fx.file(H + "hwmon2/curr1_input", "770\n");
    fx.file(H + "hwmon3/name", "macsmc_ac\n");
    fx.file(H + "hwmon3/in0_input", "20100\n");
    for (const char* n : {"hwmon4", "hwmon11", "hwmon12", "hwmon13"})
        fx.file(H + n + "/name", "tps6598x_source_psy_0_003a\n");
    for (int i = 5; i <= 10; ++i) {
        const std::string d = H + "hwmon" + std::to_string(i) + "/";
        fx.file(d + "name", "tas2764\n");
        fx.file(d + "temp1_input", "33000\n");
        fx.file(d + "temp1_fault", i == 7 ? "1\n" : "0\n");
    }

    const std::string R = fx.str();
    std::vector<probe::HwmonChip> chips;
    check("hwmon: reads 14 chips", probe::read_hwmon(R, chips) && chips.size() == 14, std::to_string(chips.size()));
    if (chips.size() == 14) {
        check("hwmon: numeric order (hwmon2 before hwmon10)", chips[2].id == "hwmon2" && chips[10].id == "hwmon10");
        const auto& smc = chips[1];
        check("hwmon: SMC has 13 sensors", smc.sensors.size() == 13, std::to_string(smc.sensors.size()));
        std::string order;
        for (const auto& x : smc.sensors) order += x.key + " ";
        check("hwmon: temp, fan, power, in, curr; by number",
              order == "temp1 temp2 temp3 temp4 fan1 fan2 power1 power2 power3 power4 in0 in1 curr1 ", order);
        check("hwmon: millidegrees -> degC", near(smc.sensors[0].value, 41.0) && smc.sensors[0].label == "Temp 1");
        check("hwmon: fan RPM with min / max / target", near(smc.sensors[4].value, 1250) &&
                                                          near(smc.sensors[4].max, 5800) && near(smc.sensors[4].target, 1200));
        check("hwmon: uW -> W", near(smc.sensors[6].value, 1.5));
        check("hwmon: mV -> V, mA -> A", near(smc.sensors[10].value, 12.34) && near(smc.sensors[12].value, 2.5));
        check("hwmon: label optional", chips[2].sensors.size() == 4 && chips[2].sensors[0].label.empty());
        check("hwmon: sensorless chip kept, empty", chips[4].sensors.empty() && chips[4].name.rfind("tps6598x", 0) == 0);
        {   // s015: thresholds and alarms
            const auto& nv = chips[0].sensors[0];
            check("thresholds: max == crit folds to crit only", std::isnan(nv.warn_at) && near(nv.crit_at, 84.85) &&
                                                                    probe::alarm(nv) == probe::Alarm::None);
            const auto& bt = chips[2].sensors[0];
            check("thresholds: past high -> Warn", near(bt.warn_at, 30) && near(bt.crit_at, 60) &&
                                                     probe::alarm(bt) == probe::Alarm::Warn);
            const auto& cu = chips[2].sensors[3];
            check("thresholds: past crit -> Crit (current)", cu.kind == "curr" && probe::alarm(cu) == probe::Alarm::Crit);
            check("thresholds: none reported -> no alarm", std::isnan(smc.sensors[0].warn_at) &&
                                                             probe::alarm(smc.sensors[0]) == probe::Alarm::None);
            check("thresholds: fans never alarm on max", probe::alarm(smc.sensors[4]) == probe::Alarm::None);
        }
        check("hwmon: _fault = 1 -> fault, no value", chips[7].sensors.size() == 1 && chips[7].sensors[0].fault &&
                                                          !chips[6].sensors[0].fault);
    }
    check("title: SMC", probe::chip_title("macsmc_hwmon") == "Apple SMC");
    check("title: tps6598x family by prefix", probe::chip_title("tps6598x_source_psy_0_003a") == "USB-C port controller");
    check("title: unknown stays itself", probe::chip_title("it8628") == "it8628");

    // power_supply.
    const std::string P = "sys/class/power_supply/";
    fx.file(P + "macsmc-battery/type", "Battery\n");
    fx.file(P + "macsmc-battery/status", "Discharging\n");
    fx.file(P + "macsmc-battery/capacity", "80\n");
    fx.file(P + "macsmc-battery/energy_now", "50000000\n");
    fx.file(P + "macsmc-battery/energy_full", "60000000\n");
    fx.file(P + "macsmc-battery/energy_full_design", "70000000\n");
    fx.file(P + "macsmc-battery/power_now", "-10000000\n");
    fx.file(P + "macsmc-battery/voltage_now", "12700000\n");
    fx.file(P + "macsmc-battery/cycle_count", "120\n");
    fx.file(P + "macsmc-battery/model_name", "bq20z451\n");
    fx.file(P + "macsmc-ac/type", "Mains\n");
    fx.file(P + "macsmc-ac/online", "1\n");
    fx.file(P + "BAT1/type", "Battery\n");
    fx.file(P + "BAT1/status", "Charging\n");
    fx.file(P + "BAT1/charge_now", "2000000\n");          // 2 Ah
    fx.file(P + "BAT1/charge_full", "4000000\n");
    fx.file(P + "BAT1/voltage_min_design", "11000000\n"); // 11 V -> 22 / 44 Wh
    fx.file(P + "BAT1/voltage_now", "12000000\n");
    fx.file(P + "BAT1/current_now", "1000000\n");         // 1 A x 12 V = 12 W
    std::vector<probe::PowerSupply> ps;
    check("power: three supplies", probe::read_power_supplies(R, ps) && ps.size() == 3, std::to_string(ps.size()));
    if (ps.size() == 3) {
        const auto& b = ps[2];   // sorted: BAT1, macsmc-ac, macsmc-battery
        check("power: battery Wh from uWh", b.name == "macsmc-battery" && near(b.energy_now, 50) &&
                                                near(b.energy_full, 60) && near(b.energy_design, 70));
        check("power: power_now sign dropped", near(b.power, 10));
        check("power: 5 h to empty", near(b.time_left(), 5 * 3600.0), std::to_string(b.time_left()));
        check("power: health = full / design", std::fabs(b.health() - 85.714) < 0.01);
        check("power: cycles, capacity", b.cycles == 120 && b.capacity == 80);
        check("power: adapter online", ps[1].type == "Mains" && ps[1].online);
        const auto& c = ps[0];
        check("power: charge_* x design voltage", near(c.energy_now, 22) && near(c.energy_full, 44));
        check("power: current x voltage", near(c.power, 12));
        check("power: 1 h 50 m to full", near(c.time_left(), 3600.0 * 22 / 12), std::to_string(c.time_left()));
    }

    // DRM.
    const std::string D = "sys/class/drm/";
    fx.file(D + "card1/device/uevent", "DRIVER=asahi\nOF_NAME=gpu\n");
    fx.file(D + "card1/device/drm/card1/dev", "226:1\n");
    fx.file(D + "card1/device/drm/renderD128/dev", "226:128\n");
    fx.file(D + "card2/device/uevent", "DRIVER=apple-drm\n");
    fx.file(D + "card2-eDP-1/status", "connected\n");
    fx.file(D + "card2-eDP-1/enabled", "enabled\n");
    fx.file(D + "card2-eDP-1/modes", "3024x1964\n1512x982\n");
    fx.file(D + "card2-HDMI-A-1/status", "disconnected\n");
    fx.file(D + "card2-HDMI-A-1/enabled", "disabled\n");
    fx.file(D + "card10/device/uevent", "DRIVER=amdgpu\n");
    fx.file(D + "card10/device/gpu_busy_percent", "37\n");
    fx.file(D + "card10/device/mem_info_vram_used", "1073741824\n");
    fx.file(D + "card10/device/mem_info_vram_total", "8589934592\n");
    fx.file(D + "renderD128/dev", "226:128\n");
    fx.file(D + "version", "drm 1.1.0\n");
    std::vector<probe::DrmCard> cards;
    check("drm: three cards (not renderD / version / connectors)", probe::read_drm(R, cards) && cards.size() == 3,
          std::to_string(cards.size()));
    if (cards.size() == 3) {
        check("drm: numeric order", cards[0].name == "card1" && cards[2].name == "card10");
        check("drm: asahi with its render node", cards[0].driver == "asahi" && cards[0].render == "renderD128" &&
                                                    cards[0].busy == -1 && cards[0].connectors.empty());
        check("drm: card2's two connectors", cards[1].connectors.size() == 2);
        if (cards[1].connectors.size() == 2) {
            const auto& e = cards[1].connectors[0];
            check("drm: eDP-1 connected, enabled, first mode", e.name == "eDP-1" && e.status == "connected" &&
                                                                  e.enabled && e.mode == "3024x1964");
            check("drm: HDMI disconnected", cards[1].connectors[1].status == "disconnected" && !cards[1].connectors[1].enabled);
        }
        check("drm: amdgpu busy + VRAM", cards[2].busy == 37 && cards[2].vram_total == 8589934592ull);
    }

    // fdinfo: two clients, one fd duplicated, one non-DRM fd, a capacity key.
    fx.file("proc/100/stat", "100 (gnome-shell) S 1\n");
    fx.link("proc/100/fd/3", "/dev/dri/renderD128");
    fx.link("proc/100/fd/4", "/dev/dri/renderD128");
    fx.link("proc/100/fd/5", "/dev/null");
    const std::string info7 = "pos:\t0\ndrm-driver:\tasahi\ndrm-client-id:\t7\n"
                              "drm-engine-render:\t1000000 ns\ndrm-engine-compute:\t200 ns\n"
                              "drm-engine-capacity-render:\t1\n";
    fx.file("proc/100/fdinfo/3", info7);
    fx.file("proc/100/fdinfo/4", info7);
    fx.file("proc/100/fdinfo/5", "pos:\t0\n");
    fx.link("proc/200/fd/9", "/dev/dri/card1");
    fx.file("proc/200/fdinfo/9", "drm-driver:\tasahi\ndrm-client-id:\t8\ndrm-engine-render:\t500 ns\n");
    const auto eng = probe::read_drm_engines(R);
    check("fdinfo: two engines summed per driver", eng.size() == 2, std::to_string(eng.size()));
    for (const auto& e : eng)
        if (e.engine == "render")
            check("fdinfo: render = 1000000 + 500 (dup fd counted once)", e.driver == "asahi" && e.ns == 1000500,
                  std::to_string(e.ns));
}

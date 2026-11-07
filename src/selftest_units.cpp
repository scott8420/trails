// selftest -- s011 systemd units: the merge of ListUnits + ListUnitFiles,
// toggleability, and the verbs through the ONE journal on a fake bus (the
// gate, undo / redo, refusals, scopes, start / stop / restart as non-steps).
#include "core/AppStore.hpp"
#include "core/Units.hpp"

#include <functional>
#include <iostream>
#include <map>
#include <string>

namespace core = trails::core;
using CheckFn = std::function<void(const std::string&, bool, const std::string&)>;

namespace {
struct FakeBus : core::UnitBus {
    std::map<std::string, std::string> state;   // "<scope>:<name>" -> file state
    std::vector<std::string> ops;
    bool deny = false;
    static std::string key(const core::UnitKey& k) {
        return (k.scope == core::UnitScope::System ? "sys:" : "usr:") + k.name;
    }
    std::string file_state(const core::UnitKey& k, std::string* err) override {
        auto it = state.find(key(k));
        if (it == state.end()) { if (err) *err = "No such unit file"; return {}; }
        return it->second;
    }
    std::string set_enabled(const core::UnitKey& k, bool on) override {
        if (deny) return "Authentication was dismissed.";
        state[key(k)] = on ? "enabled" : "disabled";
        return {};
    }
    std::string control(const core::UnitKey& k, core::UnitOp op) override {
        if (deny) return "Authentication was dismissed.";
        ops.push_back(std::string(op == core::UnitOp::Start ? "start " : op == core::UnitOp::Stop ? "stop " : "restart ") + k.name);
        return {};
    }
};
struct NoOS : core::ProcOS {
    std::optional<core::ProcKnobs> read(const core::ProcKey&) const override { return std::nullopt; }
    std::string set_nice(const core::ProcKey&, int) override { return "no"; }
    std::string set_cpus(const core::ProcKey&, const std::vector<int>&) override { return "no"; }
    std::string signal(const core::ProcKey&, int) override { return "no"; }
};
struct Fix {
    FakeBus bus;
    NoOS os;
    core::ProcKnobStore ks{os};
    core::UnitStore us{&bus};
    core::AppStore app{ks};
    core::AppJournal j{app};
    const core::UnitKey off{core::UnitScope::System, "sshd.service"};
    const core::UnitKey on{core::UnitScope::System, "cups.service"};
    Fix() {
        bus.state["sys:sshd.service"] = "disabled";
        bus.state["sys:cups.service"] = "enabled";
        bus.state["sys:dbus.service"] = "static";
        bus.state["usr:sshd.service"] = "enabled";   // same name, other scope
        app.set_units(&us);
        app.set_door([this] { return j.open(); });
    }
};
}  // namespace

void run_unit_tests(const CheckFn& check_fn) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_fn(n, ok, d); };
    std::cout << "\n-- services (s011) --\n";

    {
        std::vector<core::UnitRow> loaded = {
            {"sshd.service", "OpenSSH server", "loaded", "active", "running", ""},
            {"boot.mount", "/boot", "loaded", "active", "mounted", ""},
        };
        std::vector<std::pair<std::string, std::string>> files = {
            {"/usr/lib/systemd/system/sshd.service", "enabled"},
            {"/usr/lib/systemd/system/cups.service", "disabled"},
        };
        const auto rows = core::merge_units(loaded, files);
        check("merge: loaded + unloaded files, by name", rows.size() == 3 && rows[0].name == "boot.mount" &&
                                                          rows[1].name == "cups.service" && rows[2].name == "sshd.service");
        if (rows.size() == 3) {
            check("merge: a loaded unit gets its file state", rows[2].file_state == "enabled" && rows[2].sub == "running");
            check("merge: an unloaded file shows inactive / dead", rows[1].active == "inactive" && rows[1].sub == "dead" &&
                                                                      rows[1].file_state == "disabled");
            check("merge: no file -> no file state", rows[0].file_state.empty());
        }
    }
    check("toggleable: enabled / disabled only", core::toggleable("enabled") && core::toggleable("disabled") &&
                                                     !core::toggleable("static") && !core::toggleable("masked") &&
                                                     !core::toggleable(""));
    check("type: from the name", core::unit_type("sshd.service") == "service" && core::unit_type("x") == "");

    // THE GATE over the unit verbs.
    for (const auto& v : core::units::verbs()) {
        Fix f;
        const core::UnitKey k = v.name == "disable" ? f.on : f.off;
        const auto before = f.us.get(k);
        const std::string err = v.run(f.j, f.app, k);
        const auto after = f.us.get(k);
        check("gate/unit " + v.name + ": no error", err.empty(), err);
        check("gate/unit " + v.name + ": through a door", f.app.strays() == 0);
        if (v.undoable) {
            const auto steps = f.j.size();
            check("gate/unit " + v.name + ": is a step", steps == 1 && after != before);
            f.j.undo();
            check("gate/unit " + v.name + ": undo puts it back", f.us.get(k) == before);
            f.j.redo();
            check("gate/unit " + v.name + ": redo reproduces it", f.us.get(k) == after);
        } else {
            check("gate/unit " + v.name + ": not a step (confirmed instead)", f.j.size() == 0 &&
                                                                               f.bus.ops.size() == 1 &&
                                                                               f.bus.ops[0] == v.name + " sshd.service",
                  f.bus.ops.empty() ? "" : f.bus.ops[0]);
        }
    }
    {   Fix f;
        f.app.put(core::Ref::of_unit(f.off), core::Rec{core::UnitKnobs{true}});
        check("gate/unit: a write around the door is a stray", f.app.strays() == 1);
    }
    {   Fix f;
        const auto e = core::units::enable(f.j, f.app, f.off);
        check("label: Enable “sshd.service”", e.empty() && f.j.undo_label() == "Enable “sshd.service”",
              f.j.undo_label());
        check("enable: twice is no second step", core::units::enable(f.j, f.app, f.off).empty() && f.j.size() == 1);
        check("scope: the user unit of the same name untouched", f.bus.state["usr:sshd.service"] == "enabled" &&
                                                                     f.bus.state["sys:sshd.service"] == "enabled");
        const core::UnitKey dbus{core::UnitScope::System, "dbus.service"};
        check("static: refused, no step", !core::units::enable(f.j, f.app, dbus).empty() && f.j.size() == 1);
        check("missing: refused", !core::units::disable(f.j, f.app, {core::UnitScope::System, "nope.service"}).empty());
    }
    {   Fix f;
        f.bus.deny = true;
        const auto e = core::units::enable(f.j, f.app, f.off);
        check("refused: error, no step", e == "Authentication was dismissed." && f.j.size() == 0, e);
        check("refused: start reports it", core::units::start(f.app, f.off) == "Authentication was dismissed.");
    }
    {   // One journal: a unit step and a process step share Ctrl+Z order.
        Fix f;
        core::units::enable(f.j, f.app, f.off);
        core::units::disable(f.j, f.app, f.on);
        f.j.undo();
        check("one journal: the last step undoes first", f.bus.state["sys:cups.service"] == "enabled" &&
                                                             f.bus.state["sys:sshd.service"] == "enabled");
    }
}

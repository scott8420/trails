#pragma once
// core::Units -- the Services tab's GTK-free half (s011).
//
// The D-Bus talk to systemd lives in the app (src/Systemd.cpp, giomm); this
// is the model and the verbs, so the selftest drives them with a fake bus.
//
// ── what is undoable (proposed s010, taken s011) ───────────────────────────
//   enable / disable: a unit FILE's state -- a record the journal can put back
//     (UnitKnobs{enabled}). Through the ONE journal (core/AppStore).
//   start / stop / restart: running state. A stop can't be undone (a restart
//     is a new process), so these confirm first, naming the unit, and go
//     through the store's other door, control(). Listed in verbs(), flagged
//     undoable = false, so the gate walks them too.
//
// Privilege: systemd checks polkit itself; the app's bus calls allow
// interactive authorization, so a system unit asks for the password the same
// way GNOME Settings does. No helper.
// ─────────────────────────────────────────────────────────────────────────────
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace trails::core {

enum class UnitScope { System, User };

struct UnitKey {
    UnitScope   scope = UnitScope::System;
    std::string name;   // "sshd.service"
    bool operator==(const UnitKey&) const = default;
};

struct UnitKnobs {
    bool enabled = false;
    bool operator==(const UnitKnobs&) const = default;
};

// One row of the list: ListUnits (loaded units) merged with ListUnitFiles
// (installed ones, loaded or not).
struct UnitRow {
    std::string name, description;
    std::string load;        // "loaded", "not-found", "masked", ... ("" = not loaded)
    std::string active;      // "active", "inactive", "failed", "activating", ...
    std::string sub;         // "running", "exited", "dead", ...
    std::string file_state;  // "enabled", "disabled", "static", "masked", "indirect", ... ("" = none)
};

// ListUnits rows (name, description, load, active, sub) + ListUnitFiles
// (path, state) -> one row per unit, by name. A unit file nobody loaded
// shows as inactive / dead. Sorted by name.
std::vector<UnitRow> merge_units(const std::vector<UnitRow>& loaded,
                                 const std::vector<std::pair<std::string, std::string>>& files);
// "enabled" / "disabled" can be toggled; static, masked, indirect, generated,
// alias ... cannot.
bool toggleable(const std::string& file_state);
// The unit's type from its name: "sshd.service" -> "service".
std::string unit_type(const std::string& name);

enum class UnitOp { Start, Stop, Restart };

// What touches systemd. Each returns "" on success, else a sentence.
class UnitBus {
public:
    virtual ~UnitBus() = default;
    virtual std::string file_state(const UnitKey& k, std::string* err = nullptr) = 0;   // "" on failure
    virtual std::string set_enabled(const UnitKey& k, bool on) = 0;
    virtual std::string control(const UnitKey& k, UnitOp op) = 0;
};

class UnitStore {
public:
    using Id    = UnitKey;
    using Value = UnitKnobs;
    explicit UnitStore(UnitBus* bus = nullptr) : m_bus(bus) {}
    void set_bus(UnitBus* b) { m_bus = b; }

    // nullopt: no such unit file, or one that can't be toggled.
    std::optional<UnitKnobs> get(const UnitKey& k) const;
    void put(const UnitKey& k, const std::optional<UnitKnobs>& v);
    std::string control(const UnitKey& k, UnitOp op);   // the other door: not a step

    const std::string& last_error() const { return m_err; }
    void clear_error() { m_err.clear(); }

private:
    UnitBus*    m_bus;
    std::string m_err;
};

}  // namespace trails::core

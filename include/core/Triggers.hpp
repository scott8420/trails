#pragma once
// core::Triggers -- start the trail before you notice (s012). GTK-free.
//
// A trigger is a rule: "<metric> above / below <threshold> for <N> seconds
// -> start a recording and / or drop a mark". The book of rules is a store
// the ONE journal writes (add, edit, remove, on / off are steps -- Ctrl+Z),
// saved to ~/.config/trails/triggers.json on every change.
//
// The evaluator watches each snapshot's this-tick values (Snapshot::now):
// a condition must hold continuously for the duration; it fires ONCE, then
// re-arms only after the condition clears. A missing metric (an unplugged
// disk's rate) counts as "not holding". Triggers run while Trails is open --
// there is no background daemon.
//
// A trigger's own mark is NOT a user choice and not undoable (the recorder
// takes it directly, kind "trigger"), like a recording's start / stop.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Sampler.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace trails::core {

struct Trigger {
    std::string name;                 // "CPU pegged"
    std::string metric = "cpu.total"; // any Snapshot::now id
    bool        above = true;         // false: below
    double      threshold = 90;
    int         seconds = 30;         // must hold this long (0 = at once)
    bool        record = true;        // start a recording (if none is running)
    bool        mark = true;          // drop a mark (in the recording, if any)
    bool        enabled = true;
    bool operator==(const Trigger&) const = default;
};

// "cpu.total above 90 for 30 s -> record + mark"
std::string describe(const Trigger& t);

class TriggerBook {
public:
    using Id    = std::int64_t;
    using Value = Trigger;

    std::optional<Trigger> get(Id id) const;
    void put(Id id, const std::optional<Trigger>& v);   // saves when a path is set
    std::vector<Id> ids() const;                         // in id order (= creation order)
    Id fresh_id() const;
    bool any_enabled() const;   // s015: an armed rule keeps the sampler awake when minimised

    // Persistence. load() is not a user choice (not a step) and replaces the
    // book; a missing file is an empty book, a bad one is reported.
    void        set_path(std::string p) { m_path = std::move(p); }
    std::string load();                      // "" ok
    std::string save() const;                // "" ok
    static std::string default_path();       // $XDG_CONFIG_HOME/trails/triggers.json

    void set_door(std::function<bool()> open) { m_door = std::move(open); }
    int  strays() const { return m_strays; }
    const std::string& last_error() const { return m_err; }

private:
    std::map<Id, Trigger> m_rules;
    std::string           m_path;
    std::function<bool()> m_door;
    int                   m_strays = 0;
    mutable std::string   m_err;
};

struct TriggerFire { TriggerBook::Id id; Trigger rule; double value = 0; };

class TriggerEval {
public:
    // One snapshot's worth: `t` is seconds on any steady clock. Returns the
    // rules that fire now.
    std::vector<TriggerFire> step(const TriggerBook& book, const Snapshot& s, double t);

    enum class State { Off, Armed, Holding, Fired };
    struct Status { State state = State::Armed; double held = 0; };   // held: seconds so far
    Status status(TriggerBook::Id id) const;
    void   forget(TriggerBook::Id id) { m_st.erase(id); }   // edited / removed: start over

private:
    struct S { double since = -1; bool fired = false; Status last; };
    std::map<TriggerBook::Id, S> m_st;
};

}  // namespace trails::core

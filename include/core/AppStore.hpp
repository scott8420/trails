#pragma once
// core::AppStore -- the ONE store the ONE journal writes through (s009).
//
// Undo is one stack: Ctrl+Z undoes the last thing the user did, whether that
// was a renice or a mark in a recording. Journal<Store> wants one Id and one
// Value type, so this facade gives it a Ref (which kind of record, and which)
// and a Rec (that record's value) and hands each get / put to the store that
// owns the record:
//
//   Ref::proc(key)  -> ProcKnobStore   (nice, affinity, stopped -- s006)
//   Ref::mark(id)   -> Recorder marks  (s009)
//   (s011 adds services here.)
//
// The one-door guard holds on every branch: a put with the door shut is a
// stray, counted, and the selftest gate fails on it.
#include "core/ProcActs.hpp"
#include "core/Recorder.hpp"
#include "core/Undo.hpp"

#include <functional>
#include <optional>
#include <variant>

namespace trails::core {

struct Ref {
    enum class Kind { Proc, Mark } kind = Kind::Proc;
    ProcKey      proc;
    std::int64_t mark = 0;
    static Ref of(const ProcKey& k) { return {Kind::Proc, k, 0}; }
    static Ref of_mark(std::int64_t id) { return {Kind::Mark, {}, id}; }
    bool operator==(const Ref&) const = default;
};
using Rec = std::variant<ProcKnobs, Marker>;

class AppStore {
public:
    using Id    = Ref;
    using Value = Rec;

    AppStore(ProcKnobStore& knobs, Recorder* rec = nullptr) : m_knobs(knobs), m_rec(rec) {}

    std::optional<Rec> get(const Ref& r) const;
    void put(const Ref& r, const std::optional<Rec>& v);

    ProcKnobStore& knobs() { return m_knobs; }
    Recorder*      recorder() { return m_rec; }

    void set_door(std::function<bool()> open) {
        m_door = open;
        m_knobs.set_door(std::move(open));
    }
    int strays() const { return m_strays + m_knobs.strays(); }

private:
    ProcKnobStore&        m_knobs;
    Recorder*             m_rec;
    std::function<bool()> m_door;
    int                   m_strays = 0;
};

using AppJournal = Journal<AppStore>;

namespace marks {
// Drop a mark at "now" in the open recording. Returns its id, or 0 (and
// `err`) when nothing is recording. One step: Ctrl+Z removes it.
std::int64_t add(AppJournal& j, AppStore& s, const std::string& text, std::string* err = nullptr);
}  // namespace marks

}  // namespace trails::core

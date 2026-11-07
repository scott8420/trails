#include "core/AppStore.hpp"

namespace trails::core {

std::optional<Rec> AppStore::get(const Ref& r) const {
    if (r.kind == Ref::Kind::Proc) {
        if (auto v = m_knobs.get(r.proc)) return Rec{*v};
        return std::nullopt;
    }
    if (r.kind == Ref::Kind::Unit) {
        if (m_units)
            if (auto u = m_units->get(r.unit)) return Rec{*u};
        return std::nullopt;
    }
    if (r.kind == Ref::Kind::TrailMark) {
        if (m_trail && m_trail->is_open() && m_trail->serial() == r.owner)
            if (auto m = m_trail->get_mark(r.mark)) return Rec{*m};
        return std::nullopt;
    }
    if (m_rec)
        if (auto m = m_rec->get_mark(r.mark)) return Rec{*m};
    return std::nullopt;
}

void AppStore::put(const Ref& r, const std::optional<Rec>& v) {
    if (r.kind == Ref::Kind::Proc) {   // the knob store counts its own strays
        std::optional<ProcKnobs> k;
        if (v) if (auto* p = std::get_if<ProcKnobs>(&*v)) k = *p;
        m_knobs.put(r.proc, k);
        return;
    }
    if (m_door && !m_door()) ++m_strays;
    if (r.kind == Ref::Kind::Unit) {
        if (!m_units) { m_err = "systemd is not reachable."; return; }
        std::optional<UnitKnobs> u;
        if (v) if (auto* p = std::get_if<UnitKnobs>(&*v)) u = *p;
        m_units->put(r.unit, u);
        if (!m_units->last_error().empty()) m_err = m_units->last_error();
        return;
    }
    std::optional<Marker> m;
    if (v) if (auto* p = std::get_if<Marker>(&*v)) m = *p;
    if (r.kind == Ref::Kind::TrailMark) {
        if (!m_trail || !m_trail->is_open() || m_trail->serial() != r.owner) {
            m_err = "That recording is no longer open.";
            return;
        }
        if (auto e = m_trail->put_mark(r.mark, m); !e.empty()) m_err = e;
        return;
    }
    if (!m_rec) return;
    m_rec->put_mark(r.mark, m);
}

namespace marks {
std::int64_t add(AppJournal& j, AppStore& s, const std::string& text, std::string* err) {
    Recorder* rec = s.recorder();
    const std::int64_t id = rec ? rec->fresh_mark_id() : 0;
    if (!id) {
        if (err) *err = "Nothing is recording.";
        return 0;
    }
    const Marker m{Recorder::wall_now(), "mark", text};
    j.run("Mark “" + text + "”", {Ref::of_mark(id)}, [&] { s.put(Ref::of_mark(id), Rec{m}); });
    return id;
}
}  // namespace marks

namespace trailmarks {
namespace {
std::string q(const std::string& t) { return "\u201C" + t + "\u201D"; }
}  // namespace

std::int64_t add(AppJournal& j, AppStore& s, double t, const std::string& text, std::string* err) {
    Trail* tr = s.trail();
    if (!tr || !tr->is_open()) { if (err) *err = "No recording is open."; return 0; }
    const std::int64_t id = tr->fresh_mark_id();
    const Ref r = Ref::of_trail_mark(tr->serial(), id);
    s.clear_error();
    j.run("Mark " + q(text), {r}, [&] { s.put(r, Rec{Marker{t, "mark", text}}); });
    if (!s.last_error().empty()) { if (err) *err = s.last_error(); return 0; }
    return id;
}

std::string rename(AppJournal& j, AppStore& s, std::int64_t id, const std::string& text) {
    Trail* tr = s.trail();
    if (!tr || !tr->is_open()) return "No recording is open.";
    const Ref r = Ref::of_trail_mark(tr->serial(), id);
    auto cur = tr->get_mark(id);
    if (!cur) return "That mark is gone.";
    Marker m = *cur;
    m.text = text;
    s.clear_error();
    j.run("Rename mark " + q(text), {r}, [&] { s.put(r, Rec{m}); });
    return s.last_error();
}

std::string remove(AppJournal& j, AppStore& s, std::int64_t id) {
    Trail* tr = s.trail();
    if (!tr || !tr->is_open()) return "No recording is open.";
    const Ref r = Ref::of_trail_mark(tr->serial(), id);
    auto cur = tr->get_mark(id);
    if (!cur) return "That mark is gone.";
    s.clear_error();
    j.run("Delete mark " + q(cur->text.empty() ? cur->kind : cur->text), {r}, [&] { s.put(r, std::nullopt); });
    return s.last_error();
}
}  // namespace trailmarks

namespace units {
namespace {
std::string q(const std::string& t) { return "\u201C" + t + "\u201D"; }
std::string set(AppJournal& j, AppStore& s, const UnitKey& k, bool on) {
    UnitStore* u = s.units();
    if (!u) return "systemd is not reachable.";
    u->clear_error();
    s.clear_error();
    if (!u->get(k)) return k.name + " can't be enabled or disabled.";
    const Ref r = Ref::of_unit(k);
    j.run((on ? "Enable " : "Disable ") + q(k.name), {r}, [&] { s.put(r, Rec{UnitKnobs{on}}); });
    return s.last_error();
}
std::string ctl(AppStore& s, const UnitKey& k, UnitOp op) {
    UnitStore* u = s.units();
    return u ? u->control(k, op) : std::string("systemd is not reachable.");
}
}  // namespace

std::string enable(AppJournal& j, AppStore& s, const UnitKey& k) { return set(j, s, k, true); }
std::string disable(AppJournal& j, AppStore& s, const UnitKey& k) { return set(j, s, k, false); }
std::string start(AppStore& s, const UnitKey& k) { return ctl(s, k, UnitOp::Start); }
std::string stop(AppStore& s, const UnitKey& k) { return ctl(s, k, UnitOp::Stop); }
std::string restart(AppStore& s, const UnitKey& k) { return ctl(s, k, UnitOp::Restart); }

const std::vector<Verb>& verbs() {
    static const std::vector<Verb> v = {
        // The gate runs enable on a disabled unit and disable on an enabled one.
        {"enable", true, [](AppJournal& j, AppStore& s, const UnitKey& k) { return enable(j, s, k); }},
        {"disable", true, [](AppJournal& j, AppStore& s, const UnitKey& k) { return disable(j, s, k); }},
        {"start", false, [](AppJournal&, AppStore& s, const UnitKey& k) { return start(s, k); }},
        {"stop", false, [](AppJournal&, AppStore& s, const UnitKey& k) { return stop(s, k); }},
        {"restart", false, [](AppJournal&, AppStore& s, const UnitKey& k) { return restart(s, k); }},
    };
    return v;
}
}  // namespace units

}  // namespace trails::core

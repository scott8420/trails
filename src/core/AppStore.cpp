#include "core/AppStore.hpp"

namespace trails::core {

std::optional<Rec> AppStore::get(const Ref& r) const {
    if (r.kind == Ref::Kind::Proc) {
        if (auto v = m_knobs.get(r.proc)) return Rec{*v};
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
    if (!m_rec) return;
    std::optional<Marker> m;
    if (v) if (auto* p = std::get_if<Marker>(&*v)) m = *p;
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

}  // namespace trails::core

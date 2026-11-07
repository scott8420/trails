#include "core/Units.hpp"

#include <algorithm>
#include <map>

namespace trails::core {

std::vector<UnitRow> merge_units(const std::vector<UnitRow>& loaded,
                                 const std::vector<std::pair<std::string, std::string>>& files) {
    std::map<std::string, UnitRow> by;
    for (const auto& r : loaded) by[r.name] = r;
    for (const auto& [path, state] : files) {
        const auto slash = path.rfind('/');
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        if (name.empty()) continue;
        auto it = by.find(name);
        if (it == by.end()) {
            UnitRow r;
            r.name = name;
            r.active = "inactive";
            r.sub = "dead";
            r.file_state = state;
            by.emplace(name, std::move(r));
        } else if (it->second.file_state.empty()) {
            it->second.file_state = state;
        }
    }
    std::vector<UnitRow> out;
    out.reserve(by.size());
    for (auto& [n, r] : by) out.push_back(std::move(r));
    return out;
}

bool toggleable(const std::string& s) { return s == "enabled" || s == "disabled"; }

std::string unit_type(const std::string& name) {
    const auto dot = name.rfind('.');
    return dot == std::string::npos ? std::string() : name.substr(dot + 1);
}

std::optional<UnitKnobs> UnitStore::get(const UnitKey& k) const {
    if (!m_bus) return std::nullopt;
    const std::string s = m_bus->file_state(k);
    if (!toggleable(s)) return std::nullopt;
    return UnitKnobs{s == "enabled"};
}

void UnitStore::put(const UnitKey& k, const std::optional<UnitKnobs>& v) {
    if (!v) return;   // unit files are never created or removed through the journal
    if (!m_bus) { m_err = "systemd is not reachable."; return; }
    const auto cur = get(k);
    if (!cur) { m_err = k.name + " can't be enabled or disabled."; return; }
    if (cur->enabled == v->enabled) return;
    if (auto e = m_bus->set_enabled(k, v->enabled); !e.empty()) m_err = e;
}

std::string UnitStore::control(const UnitKey& k, UnitOp op) {
    if (!m_bus) return m_err = "systemd is not reachable.";
    m_err = m_bus->control(k, op);
    return m_err;
}

}  // namespace trails::core

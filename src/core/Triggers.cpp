#include "core/Triggers.hpp"

#include "json.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace trails::core {

std::string describe(const Trigger& t) {
    char v[32];
    std::snprintf(v, sizeof v, "%g", t.threshold);
    std::string s = t.metric + (t.above ? " above " : " below ") + v;
    if (t.seconds > 0) s += " for " + std::to_string(t.seconds) + " s";
    s += " → ";
    if (t.record && t.mark) s += "record + mark";
    else if (t.record) s += "record";
    else if (t.mark) s += "mark";
    else s += "nothing";
    return s;
}

// ── the book ────────────────────────────────────────────────────────────────

std::optional<Trigger> TriggerBook::get(Id id) const {
    auto it = m_rules.find(id);
    if (it == m_rules.end()) return std::nullopt;
    return it->second;
}

void TriggerBook::put(Id id, const std::optional<Trigger>& v) {
    if (m_door && !m_door()) ++m_strays;
    if (v) m_rules[id] = *v;
    else m_rules.erase(id);
    if (!m_path.empty()) m_err = save();
}

std::vector<TriggerBook::Id> TriggerBook::ids() const {
    std::vector<Id> out;
    for (const auto& [id, r] : m_rules) out.push_back(id);
    return out;
}

bool TriggerBook::any_enabled() const {
    for (const auto& [id, t] : m_rules) if (t.enabled) return true;
    return false;
}

TriggerBook::Id TriggerBook::fresh_id() const { return m_rules.empty() ? 1 : m_rules.rbegin()->first + 1; }

std::string TriggerBook::default_path() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    const std::string base = (x && *x) ? x : std::string(std::getenv("HOME") ? std::getenv("HOME") : "/tmp") + "/.config";
    return base + "/trails/triggers.json";
}

std::string TriggerBook::save() const {
    nlohmann::json j = nlohmann::json::object();
    j["format"] = "trails-triggers";
    j["version"] = 1;
    auto& arr = j["triggers"] = nlohmann::json::array();
    for (const auto& [id, t] : m_rules)
        arr.push_back({{"id", id}, {"name", t.name}, {"metric", t.metric}, {"above", t.above},
                       {"threshold", t.threshold}, {"seconds", t.seconds}, {"record", t.record},
                       {"mark", t.mark}, {"enabled", t.enabled}});
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(m_path).parent_path(), ec);
    const std::string tmp = m_path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return "Could not write " + tmp;
        f << j.dump(2) << '\n';
        if (!f) return "Could not write " + tmp;
    }
    std::filesystem::rename(tmp, m_path, ec);   // never a half-written file
    return ec ? "Could not save " + m_path + ": " + ec.message() : std::string();
}

std::string TriggerBook::load() {
    m_rules.clear();
    std::ifstream f(m_path);
    if (!f) return {};   // no file yet: an empty book
    try {
        const auto j = nlohmann::json::parse(f);
        if (j.value("format", "") != "trails-triggers") return m_path + " is not a Trails triggers file.";
        for (const auto& e : j.at("triggers")) {
            Trigger t;
            t.name = e.value("name", "");
            t.metric = e.value("metric", "cpu.total");
            t.above = e.value("above", true);
            t.threshold = e.value("threshold", 90.0);
            t.seconds = e.value("seconds", 30);
            t.record = e.value("record", true);
            t.mark = e.value("mark", true);
            t.enabled = e.value("enabled", true);
            m_rules[e.at("id").get<Id>()] = t;
        }
    } catch (const std::exception& e) {
        m_rules.clear();
        return m_path + ": " + e.what();
    }
    return {};
}

// ── the evaluator ───────────────────────────────────────────────────────────

std::vector<TriggerFire> TriggerEval::step(const TriggerBook& book, const Snapshot& s, double t) {
    std::vector<TriggerFire> out;
    for (auto id : book.ids()) {
        const auto r = book.get(id);
        S& st = m_st[id];
        if (!r || !r->enabled) {
            st = S{};
            st.last.state = State::Off;
            continue;
        }
        auto it = s.now.find(r->metric);
        const bool holds = it != s.now.end() && (r->above ? it->second > r->threshold : it->second < r->threshold);
        if (!holds) {
            st.since = -1;
            st.fired = false;   // cleared: re-armed
            st.last = {State::Armed, 0};
            continue;
        }
        if (st.since < 0) st.since = t;
        const double held = t - st.since;
        if (st.fired) {
            st.last = {State::Fired, held};
            continue;
        }
        if (held + 1e-9 >= r->seconds) {
            st.fired = true;
            st.last = {State::Fired, held};
            out.push_back({id, *r, it->second});
        } else {
            st.last = {State::Holding, held};
        }
    }
    std::erase_if(m_st, [&](const auto& kv) { return !book.get(kv.first); });
    return out;
}

TriggerEval::Status TriggerEval::status(TriggerBook::Id id) const {
    auto it = m_st.find(id);
    return it == m_st.end() ? Status{} : it->second.last;
}

}  // namespace trails::core

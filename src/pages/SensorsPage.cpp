#include "pages/SensorsPage.hpp"
#include "Registry.hpp"
#include "core/Format.hpp"

#include <cstdio>
#include <map>

namespace trails::pages {

namespace fmt = core::fmt;
namespace probe = core::probe;

namespace {
void set(Gtk::Label& l, const std::string& t) {
    if (std::string(l.get_text()) != t) l.set_text(t);
}
std::vector<float> hist(const core::Snapshot& s, const std::string& id) {
    const auto* r = s.find(id);
    return r ? r->to_vector() : std::vector<float>{};
}
std::size_t cap(const core::Snapshot& s, const std::string& id) {
    const auto* r = s.find(id);
    return r ? r->capacity() : 300;
}
std::string num(double v, int digits) {
    char b[32];
    std::snprintf(b, sizeof b, "%.*f", digits, v);
    return b;
}
std::string value_text(const probe::Sensor& x) {
    if (x.fault) return "no reading";
    if (x.kind == "temp")  return num(x.value, 1) + " °C";
    if (x.kind == "fan")   return num(x.value, 0) + " RPM";
    if (x.kind == "power") return num(x.value, 2) + " W";
    if (x.kind == "in")    return num(x.value, 2) + " V";
    if (x.kind == "curr")  return num(x.value, 2) + " A";
    return num(x.value, 2);
}
std::string kind_word(const std::string& k) {
    if (k == "temp") return "Temperature";
    if (k == "fan") return "Fan";
    if (k == "power") return "Power";
    if (k == "in") return "Voltage";
    if (k == "curr") return "Current";
    return k;
}
std::string duration(double s) {
    if (s < 0) return {};
    const long m = long(s / 60 + 0.5);
    return std::to_string(m / 60) + " h " + (m % 60 < 10 ? "0" : "") + std::to_string(m % 60) + " m";
}
bool sparked(const std::string& kind) { return kind == "temp" || kind == "fan" || kind == "power"; }
}  // namespace

SensorsPage::SensorsPage() : m_graph("sensors.graph") {
    set_name("page.sensors");
    registry::add("page.sensors", this);
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_page.add_css_class("page");
    m_page.add_css_class("tab-sensors");

    m_panel.add_css_class("panel");
    m_icon.set_from_icon_name("trails-tab-sensors-symbolic");
    m_icon.set_pixel_size(48);
    m_icon.add_css_class("tile-icon");
    m_title.set_text("Sensors & Power");
    m_title.add_css_class("panel-title");
    m_title.set_xalign(0.0f);
    m_sub.add_css_class("dim-label");
    m_sub.set_xalign(0.0f);
    m_titles.append(m_title);
    m_titles.append(m_sub);
    m_titles.set_hexpand(true);
    m_value.add_css_class("tile-value");
    m_value.set_valign(Gtk::Align::CENTER);
    m_value.set_text("--");
    m_head.append(m_icon);
    m_head.append(m_titles);
    m_head.append(m_value);
    m_graph.set_content_height(100);
    m_graph.set_scale(widgets::Graph::Scale::Percent);   // 0..100 degC
    m_graph.set_tooltip_text("The hottest working sensor, 0 to 100 °C");
    m_sources.set_xalign(0.0f);
    m_sources.add_css_class("dim-label");
    m_sources.set_wrap(true);
    m_panel.append(m_head);
    m_panel.append(m_graph);
    m_panel.append(m_sources);
    m_page.append(m_panel);
    m_page.append(m_dyn);
    set_child(m_page);
}

SensorsPage::~SensorsPage() { registry::remove(this); }

void SensorsPage::rebuild(const core::Snapshot& s, const std::string& layout) {
    while (auto* c = m_dyn.get_first_child()) m_dyn.remove(*c);
    m_batts.clear();
    m_cards.clear();

    for (const auto& p : s.power) {
        if (p.type != "Battery") continue;
        auto b = std::make_unique<Batt>("sensors.battery." + p.name);
        b->name = p.name;
        b->panel.add_css_class("panel");
        b->title.add_css_class("section-title");
        b->title.set_xalign(0.0f);
        b->sub.add_css_class("dim-label");
        b->sub.set_xalign(0.0f);
        b->titles.append(b->title);
        b->titles.append(b->sub);
        b->titles.set_hexpand(true);
        b->value.add_css_class("tile-value");
        b->value.set_valign(Gtk::Align::CENTER);
        b->head.append(b->titles);
        b->head.append(b->value);
        b->bar.add_css_class("graph");
        b->graph.set_content_height(60);
        b->graph.set_scale(widgets::Graph::Scale::Auto, 10.0);   // W
        b->graph.set_tooltip_text("Battery draw (charge or discharge), watts");
        for (Gtk::Label* l : {&b->line1, &b->line2}) l->set_xalign(0.0f);
        b->line2.add_css_class("dim-label");
        b->panel.append(b->head);
        b->panel.append(b->bar);
        b->panel.append(b->line1);
        b->panel.append(b->line2);
        b->panel.append(b->graph);
        m_dyn.append(b->panel);
        m_batts.push_back(std::move(b));
    }

    // One card per chip NAME, chips in hwmon order.
    std::vector<std::string> names;
    std::map<std::string, int> count;
    for (const auto& c : s.hwmon) {
        if (c.sensors.empty()) continue;
        if (!count[c.name]++) names.push_back(c.name);
    }
    for (const auto& name : names) {
        auto card = std::make_unique<Card>();
        card->panel.add_css_class("panel");
        card->title.add_css_class("section-title");
        card->title.set_xalign(0.0f);
        const int n = count[name];
        card->title.set_text(probe::chip_title(name) + (n > 1 ? " × " + std::to_string(n) : std::string()) +
                             "   (" + name + ")");
        card->grid.set_column_spacing(18);
        card->grid.set_row_spacing(4);
        int idx = 0, r = 0;
        for (const auto& c : s.hwmon) {
            if (c.name != name || c.sensors.empty()) continue;
            ++idx;
            for (const auto& x : c.sensors) {
                auto row = std::make_unique<Row>();
                row->chip_id = c.id;
                row->key = x.key;
                row->kind = x.kind;
                row->prefix = n > 1 ? std::to_string(idx) + " · " : "";
                row->name.set_xalign(0.0f);
                row->name.set_hexpand(true);
                row->name.set_ellipsize(Pango::EllipsizeMode::END);
                row->value.set_xalign(1.0f);
                row->value.add_css_class("kv-value");
                row->extra.set_xalign(0.0f);
                row->extra.add_css_class("dim-label");
                card->grid.attach(row->name, 0, r);
                card->grid.attach(row->value, 1, r);
                card->grid.attach(row->extra, 2, r);
                if (sparked(x.kind)) {
                    row->spark = std::make_unique<widgets::Graph>("sensors." + c.id + "." + x.key);
                    row->spark->set_content_height(20);
                    row->spark->set_content_width(160);
                    row->spark->add_css_class("graph");
                    if (x.kind == "temp") row->spark->set_scale(widgets::Graph::Scale::Percent);
                    else row->spark->set_scale(widgets::Graph::Scale::Auto, x.kind == "fan" ? 1000.0 : 1.0);
                    card->grid.attach(*row->spark, 3, r);
                }
                ++r;
                card->rows.push_back(std::move(row));
            }
        }
        card->panel.append(card->title);
        card->panel.append(card->grid);
        m_dyn.append(card->panel);
        m_cards.push_back(std::move(card));
    }
    m_layout = layout;
}

void SensorsPage::paint(const core::Snapshot& s) {
    std::string layout;
    for (const auto& p : s.power) if (p.type == "Battery") layout += "B" + p.name + ";";
    for (const auto& c : s.hwmon) {
        layout += c.id + "=" + c.name + ":";
        for (const auto& x : c.sensors) layout += x.key + ",";
    }
    if (layout != m_layout) rebuild(s, layout);

    // Header.
    if (s.temp_max > 0) {
        set(m_value, num(s.temp_max, 1) + " °C");
        set(m_sub, "Hottest: " + s.temp_max_name);
    } else {
        set(m_value, "--");
        set(m_sub, "No temperature sensors found");
    }
    m_graph.set_series(hist(s, "temp.max"), {}, cap(s, "temp.max"));
    std::string src;
    for (const auto& p : s.power) {
        if (p.type == "Battery") continue;
        if (!src.empty()) src += "  ·  ";
        src += p.name + (p.type.empty() ? "" : " (" + p.type + ")") + ": " + (p.online ? "connected" : "not connected");
    }
    set(m_sources, src.empty() ? "" : "Power sources:  " + src);
    m_sources.set_visible(!src.empty());

    // Batteries.
    for (auto& b : m_batts) {
        const probe::PowerSupply* p = nullptr;
        for (const auto& q : s.power) if (q.name == b->name) p = &q;
        if (!p) continue;
        set(b->title, "Battery");
        std::string sub = p->name;
        if (!p->model.empty()) sub += " · " + p->model;
        if (!p->technology.empty() && p->technology != "Unknown") sub += " · " + p->technology;
        set(b->sub, sub);
        const int cap_pct = p->capacity >= 0 ? p->capacity
                          : (p->energy_full > 0 ? int(100.0 * p->energy_now / p->energy_full + 0.5) : -1);
        set(b->value, cap_pct >= 0 ? std::to_string(cap_pct) + "%" : "--");
        b->bar.set_parts({{cap_pct >= 0 ? cap_pct / 100.0 : 0.0, 1.0}});
        std::string l1 = p->status.empty() ? "Unknown" : p->status;
        if (p->power > 0.05) l1 += " · " + num(p->power, 1) + " W";
        const std::string tl = duration(p->time_left());
        if (!tl.empty()) l1 += " · " + tl + (p->status == "Charging" ? " to full" : " left");
        set(b->line1, l1);
        std::string l2;
        if (p->energy_full > 0) l2 = num(p->energy_now, 1) + " of " + num(p->energy_full, 1) + " Wh";
        if (p->health() > 0) l2 += (l2.empty() ? "" : " · ") + std::string("health ") + num(p->health(), 0) +
                                   "% of " + num(p->energy_design, 1) + " Wh design";
        if (p->cycles >= 0) l2 += (l2.empty() ? "" : " · ") + std::to_string(p->cycles) + " cycles";
        if (p->voltage > 0) l2 += (l2.empty() ? "" : " · ") + num(p->voltage, 2) + " V";
        set(b->line2, l2);
        b->graph.set_series(hist(s, "battery.power"), {}, cap(s, "battery.power"));
    }

    // Chip cards.
    for (auto& card : m_cards)
        for (auto& row : card->rows) {
            const probe::Sensor* x = nullptr;
            for (const auto& c : s.hwmon)
                if (c.id == row->chip_id)
                    for (const auto& y : c.sensors) if (y.key == row->key) x = &y;
            if (!x) continue;
            set(row->name, row->prefix + (x->label.empty() ? kind_word(x->kind) + " " + x->key.substr(x->kind.size())
                                                           : x->label));
            set(row->value, value_text(*x));
            std::string extra;
            if (x->kind == "fan") {
                if (x->min >= 0) extra += "min " + num(x->min, 0);
                if (x->max >= 0) extra += (extra.empty() ? "" : " · ") + std::string("max ") + num(x->max, 0);
                if (x->target >= 0) extra += (extra.empty() ? "" : " · ") + std::string("target ") + num(x->target, 0);
            }
            set(row->extra, extra);
            if (row->spark) {
                const std::string id = "hw." + row->chip_id + "." + row->key;
                row->spark->set_series(hist(s, id), {}, cap(s, id));
            }
        }
}

}  // namespace trails::pages

#include "pages/CpuPage.hpp"
#include "Registry.hpp"
#include "core/Format.hpp"

#include <cstdio>

namespace trails::pages {

namespace {
std::string ghz(double mhz) {
    if (mhz <= 0) return "--";
    char b[16];
    std::snprintf(b, sizeof b, "%.2f GHz", mhz / 1000.0);
    return b;
}
std::string two(double v) {
    char b[16];
    std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}
void set(Gtk::Label& l, const std::string& t) {
    if (std::string(l.get_text()) != t) l.set_text(t);
}
std::vector<float> hist(const core::Snapshot& s, const std::string& id) {
    const auto* r = s.find(id);
    return r ? r->to_vector() : std::vector<float>{};
}
}  // namespace

CpuPage::CoreCard::CoreCard(const std::string& id) : graph(id + ".graph") {
    box.add_css_class("core");
    name.add_css_class("core-name");
    name.set_xalign(0.0f);
    name.set_hexpand(true);
    pct.add_css_class("core-pct");
    mhz.add_css_class("core-mhz");
    mhz.set_xalign(0.0f);
    head.append(name);
    head.append(pct);
    graph.set_content_height(38);
    graph.set_scale(widgets::Graph::Scale::Percent);
    box.append(head);
    box.append(mhz);
    box.append(graph);
}

CpuPage::CpuPage() : m_graph("cpu.graph") {
    set_name("page.cpu");
    registry::add("page.cpu", this);
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

    m_page.add_css_class("page");
    m_page.add_css_class("tab-cpu");

    m_panel.add_css_class("panel");
    m_icon.set_from_icon_name("trails-tab-cpu-symbolic");
    m_icon.set_pixel_size(48);
    m_icon.add_css_class("tile-icon");
    m_title.set_text("CPU");
    m_title.add_css_class("panel-title");
    m_title.set_xalign(0.0f);
    m_model.add_css_class("dim-label");
    m_model.set_xalign(0.0f);
    m_model.set_ellipsize(Pango::EllipsizeMode::END);
    m_titles.append(m_title);
    m_titles.append(m_model);
    m_titles.set_hexpand(true);
    m_value.add_css_class("tile-value");
    m_value.set_valign(Gtk::Align::CENTER);
    m_value.set_text("--");
    m_head.append(m_icon);
    m_head.append(m_titles);
    m_head.append(m_value);

    m_detail.set_xalign(0.0f);
    m_detail.add_css_class("tile-detail");
    m_load.set_xalign(0.0f);
    m_load.add_css_class("tile-detail");
    m_graph.set_content_height(150);
    m_graph.set_scale(widgets::Graph::Scale::Percent);

    m_panel.append(m_head);
    m_panel.append(m_detail);
    m_panel.append(m_load);
    m_panel.append(m_graph);

    m_page.append(m_panel);
    m_page.append(m_clusters);
    set_child(m_page);
}

CpuPage::~CpuPage() { registry::remove(this); }

void CpuPage::rebuild(const core::Snapshot& s) {
    while (auto* c = m_clusters.get_first_child()) m_clusters.remove(*c);
    m_cards.clear();
    m_cluster_heads.clear();
    for (std::size_t i = 0; i < s.core_busy.size(); ++i) {
        const int id = i < s.core_ids.size() ? s.core_ids[i] : int(i);
        m_cards.push_back(std::make_unique<CoreCard>("cpu.core." + std::to_string(id)));
        m_cards.back()->name.set_text("cpu" + std::to_string(id));
    }
    for (const auto& cl : s.clusters) {
        auto head = Gtk::make_managed<Gtk::Label>();
        head->add_css_class("section-title");
        head->set_xalign(0.0f);
        m_cluster_heads.push_back(head);
        auto flow = Gtk::make_managed<Gtk::FlowBox>();
        flow->set_selection_mode(Gtk::SelectionMode::NONE);
        flow->set_homogeneous(true);
        flow->set_min_children_per_line(2);
        flow->set_max_children_per_line(8);
        flow->set_row_spacing(10);
        flow->set_column_spacing(10);
        for (int idx : cl.cores)
            if (idx >= 0 && std::size_t(idx) < m_cards.size()) {
                m_cards[std::size_t(idx)]->box.set_size_request(150, -1);
                flow->append(m_cards[std::size_t(idx)]->box);
            }
        m_clusters.append(*head);
        m_clusters.append(*flow);
    }
}

void CpuPage::paint(const core::Snapshot& s) {
    namespace fmt = core::fmt;
    // Rebuild only when the layout changed.
    std::string layout = std::to_string(s.core_busy.size()) + "|";
    for (const auto& cl : s.clusters) {
        layout += cl.name + ":";
        for (int c : cl.cores) layout += std::to_string(c) + ",";
    }
    if (layout != m_layout) { rebuild(s); m_layout = layout; }

    const bool r = s.rates_valid;
    set(m_model, s.cpu_model.empty() ? std::string("Processor") : s.cpu_model);
    set(m_value, r ? fmt::pct(s.cpu.busy) : "--");

    double sum = 0; int n = 0;
    for (double m : s.core_mhz) if (m > 0) { sum += m; ++n; }
    std::string d = std::to_string(s.n_cpus) + " cores";
    if (n) d += " · " + ghz(sum / n) + " average";
    if (r) d += " · user " + fmt::pct(s.cpu.user) + " · system " + fmt::pct(s.cpu.system) +
                " · iowait " + fmt::pct(s.cpu.iowait);
    set(m_detail, d);
    set(m_load, "Load " + two(s.load.one) + "  " + two(s.load.five) + "  " + two(s.load.fifteen) +
                " · " + std::to_string(s.load.running) + " running of " +
                std::to_string(s.load.total) + " tasks");
    const auto* ring = s.find("cpu.total");
    m_graph.set_series(hist(s, "cpu.total"), hist(s, "cpu.system"), ring ? ring->capacity() : 300);

    for (std::size_t i = 0; i < m_cluster_heads.size() && i < s.clusters.size(); ++i) {
        const auto& cl = s.clusters[i];
        std::string h = cl.name + " · " + std::to_string(cl.cores.size()) +
                        (cl.cores.size() == 1 ? " core" : " cores");
        if (cl.max_khz) h += " · up to " + ghz(cl.max_khz / 1000.0);
        set(*m_cluster_heads[i], h);
    }
    for (std::size_t i = 0; i < m_cards.size(); ++i) {
        auto& c = *m_cards[i];
        set(c.pct, r && i < s.core_busy.size() ? fmt::pct(s.core_busy[i]) : "--");
        set(c.mhz, i < s.core_mhz.size() ? ghz(s.core_mhz[i]) : "--");
        const std::string id = "cpu.core." + std::to_string(i);
        const auto* cr = s.find(id);
        c.graph.set_series(hist(s, id), {}, cr ? cr->capacity() : 300);
    }
}

}  // namespace trails::pages

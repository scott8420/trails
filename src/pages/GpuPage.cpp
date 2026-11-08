#include "pages/GpuPage.hpp"
#include "Registry.hpp"
#include "core/Format.hpp"

namespace trails::pages {

namespace fmt = core::fmt;

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
void dim_left(Gtk::Label& l) {
    l.set_xalign(0.0f);
    l.set_wrap(true);
}
}  // namespace

GpuPage::GpuPage() : m_graph("gpu.graph") {
    set_name("page.gpu");
    registry::add("page.gpu", this);
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_page.add_css_class("page");
    m_page.add_css_class("tab-gpu");

    m_panel.add_css_class("panel");
    m_icon.set_from_icon_name("trails-hero-gpu");
    m_icon.set_pixel_size(56);
    m_icon.add_css_class("tile-icon");
    m_title.set_text("GPU");
    m_title.add_css_class("panel-title");
    m_title.set_xalign(0.0f);
    m_sub.add_css_class("dim-label");
    m_sub.set_xalign(0.0f);
    m_sub.set_wrap(true);
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
    m_graph.set_scale(widgets::Graph::Scale::Percent);
    m_graph.set_tooltip_text("The busiest GPU engine");
    dim_left(m_note);
    m_note.add_css_class("dim-label");
    m_panel.append(m_head);
    m_panel.append(m_graph);
    m_panel.append(m_note);
    m_page.append(m_panel);
    m_page.append(m_cards);
    set_child(m_page);
}

GpuPage::~GpuPage() { registry::remove(this); }

void GpuPage::rebuild(const core::Snapshot& s, const std::string& layout) {
    while (auto* c = m_cards.get_first_child()) m_cards.remove(*c);
    m_store.clear();
    for (const auto& d : s.drm) {
        auto c = std::make_unique<Card>("gpu." + d.name);
        c->name = d.name;
        c->panel.add_css_class("panel");
        c->title.add_css_class("section-title");
        c->title.set_xalign(0.0f);
        c->sub.add_css_class("dim-label");
        c->sub.set_xalign(0.0f);
        for (Gtk::Label* l : {&c->busy_l, &c->vram_l, &c->engines, &c->displays}) dim_left(*l);
        c->busy.add_css_class("graph");
        c->vram.add_css_class("graph");
        c->panel.append(c->title);
        c->panel.append(c->sub);
        if (d.busy >= 0) { c->panel.append(c->busy_l); c->panel.append(c->busy); }
        if (d.vram_total) { c->panel.append(c->vram_l); c->panel.append(c->vram); }
        c->panel.append(c->engines);
        c->panel.append(c->displays);
        m_cards.append(c->panel);
        m_store.push_back(std::move(c));
    }
    m_layout = layout;
}

void GpuPage::paint(const core::Snapshot& s) {
    std::string layout;
    for (const auto& d : s.drm) layout += d.name + (d.busy >= 0 ? "b" : "") + (d.vram_total ? "v" : "") + ";";
    if (layout != m_layout) rebuild(s, layout);

    std::string drivers;
    for (const auto& d : s.drm) {
        if (d.render.empty()) continue;
        if (!drivers.empty()) drivers += ", ";
        drivers += (d.driver.empty() ? "?" : d.driver) + " (" + d.name + ", " + d.render + ")";
    }
    set(m_sub, s.drm.empty() ? "No DRM devices found" : drivers.empty() ? "No render node" : "Render: " + drivers);
    if (s.gpu_busy >= 0) {
        set(m_value, fmt::pct(s.gpu_busy));
        set(m_note, "Busy = the busiest engine over the last interval. Engine time comes from DRM "
                    "fdinfo of the processes Trails can read (yours).");
    } else {
        set(m_value, "--");
        set(m_note, "GPU busy time is not reported here: no sysfs busy counter, and no drm-engine "
                    "time in the fdinfo of processes Trails can read. Nothing is drawn rather than a "
                    "flat zero that would look like an idle GPU.");
    }
    m_graph.set_series(hist(s, "gpu.busy"), {}, cap(s, "gpu.busy"));

    for (auto& c : m_store) {
        const core::probe::DrmCard* d = nullptr;
        for (const auto& x : s.drm) if (x.name == c->name) d = &x;
        if (!d) continue;
        set(c->title, d->name + "  ·  " + (d->driver.empty() ? std::string("unknown driver") : d->driver));
        set(c->sub, d->render.empty() ? "Display only (no render node)" : "Render node " + d->render);
        if (d->busy >= 0) {
            set(c->busy_l, "Busy " + std::to_string(d->busy) + "%");
            c->busy.set_parts({{d->busy / 100.0, 1.0}});
        }
        if (d->vram_total) {
            set(c->vram_l, "VRAM " + fmt::bytes(d->vram_used) + " of " + fmt::bytes(d->vram_total));
            c->vram.set_parts({{double(d->vram_used) / double(d->vram_total), 1.0}});
        }
        std::string e;
        if (!d->render.empty())
            for (const auto& g : s.gpu_engines)
                if (g.driver == d->driver)
                    e += (e.empty() ? "Engines:  " : "   ·   ") + g.engine + " " + fmt::pct(g.busy_pct);
        set(c->engines, e);
        c->engines.set_visible(!e.empty());
        std::string disp;
        for (const auto& k : d->connectors) {
            if (!disp.empty()) disp += "\n";
            disp += k.name + "  ·  " + k.status;
            if (k.status == "connected" && !k.mode.empty()) disp += "  ·  " + k.mode;
            if (k.status == "connected" && !k.enabled) disp += "  ·  off";
        }
        set(c->displays, disp.empty() ? "" : "Displays\n" + disp);
        c->displays.set_visible(!disp.empty());
    }
}

}  // namespace trails::pages

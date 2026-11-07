#include "pages/MemoryPage.hpp"
#include "Registry.hpp"
#include "core/Format.hpp"

#include <algorithm>
#include <cstdio>

namespace trails::pages {

namespace {
void set(Gtk::Label& l, const std::string& t) {
    if (std::string(l.get_text()) != t) l.set_text(t);
}
std::vector<float> hist(const core::Snapshot& s, const char* id) {
    const auto* r = s.find(id);
    return r ? r->to_vector() : std::vector<float>{};
}
double frac(std::uint64_t a, std::uint64_t b) { return b ? double(a) / double(b) : 0.0; }
std::string ratio(double v) {
    char b[16];
    std::snprintf(b, sizeof b, "%.1fx", v);
    return b;
}
void section(Gtk::Label& l, const char* text) {
    l.set_text(text);
    l.add_css_class("section-title");
    l.set_xalign(0.0f);
}
}  // namespace

MemoryPage::MemoryPage() : m_bar("memory.bar", 16), m_graph("memory.graph") {
    set_name("page.memory");
    registry::add("page.memory", this);
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

    m_page.add_css_class("page");
    m_page.add_css_class("tab-memory");

    // Header panel.
    m_panel.add_css_class("panel");
    m_icon.set_from_icon_name("trails-tab-memory-symbolic");
    m_icon.set_pixel_size(48);
    m_icon.add_css_class("tile-icon");
    m_title.set_text("Memory");
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
    m_bar.add_css_class("graph");   // takes the tab colour like the graphs
    m_graph.set_content_height(140);
    m_graph.set_scale(widgets::Graph::Scale::Percent);
    m_panel.append(m_head);
    m_panel.append(m_bar);
    m_panel.append(m_graph);
    m_page.append(m_panel);

    // Breakdown.
    section(m_break_title, "Breakdown");
    m_break.set_column_spacing(24);
    m_break.set_row_spacing(6);
    const char* keys[] = {"Used", "Available", "Cached", "Buffers", "Shared", "Free"};
    for (int i = 0; i < 6; ++i) {
        auto& kv = m_kv[std::size_t(i)];
        kv.key.set_text(keys[i]);
        kv.key.add_css_class("dim-label");
        kv.key.set_xalign(0.0f);
        kv.val.add_css_class("kv-value");
        kv.val.set_xalign(0.0f);
        m_break.attach(kv.key, (i % 3) * 2, i / 3);
        m_break.attach(kv.val, (i % 3) * 2 + 1, i / 3);
    }
    m_page.append(m_break_title);
    m_page.append(m_break);

    // Swap.
    section(m_swap_title, "Swap");
    m_page.append(m_swap_title);
    m_page.append(m_swaps);

    // Top memory.
    section(m_top_title, "Top memory");
    m_top.set_column_spacing(24);
    m_top.set_row_spacing(4);
    const char* heads[] = {"Process", "PID", "Resident", "of RAM"};
    for (int c = 0; c < 4; ++c) {
        auto h = Gtk::make_managed<Gtk::Label>(heads[c]);
        h->add_css_class("dim-label");
        h->set_xalign(c == 0 ? 0.0f : 1.0f);
        m_top.attach(*h, c, 0);
    }
    for (int r = 0; r < kTop; ++r)
        for (int c = 0; c < 4; ++c) {
            auto& l = m_rows[std::size_t(r)][std::size_t(c)];
            l.set_xalign(c == 0 ? 0.0f : 1.0f);
            if (c == 0) { l.set_hexpand(true); l.set_ellipsize(Pango::EllipsizeMode::END); }
            if (c == 2) l.add_css_class("kv-value");
            m_top.attach(l, c, r + 1);
        }
    m_page.append(m_top_title);
    m_page.append(m_top);

    set_child(m_page);
}

MemoryPage::~MemoryPage() { registry::remove(this); }

void MemoryPage::paint(const core::Snapshot& s) {
    namespace fmt = core::fmt;
    const auto& m = s.mem;
    const std::uint64_t cache = m.cached + m.buffers;

    set(m_value, fmt::bytes(m.used()));
    set(m_sub, "of " + fmt::bytes(m.total) + " · " + fmt::pct(100.0 * frac(m.used(), m.total)) +
               " used · " + fmt::bytes(m.available) + " available");
    // used | cache (whatever of it isn't counted in used) | the rest
    const double fu = frac(m.used(), m.total);
    const double fc = std::min(frac(cache, m.total), 1.0 - fu);
    m_bar.set_parts({{fu, 1.0}, {fc, 0.40}});
    const auto* ring = s.find("mem.used_pct");
    m_graph.set_series(hist(s, "mem.used_pct"), hist(s, "mem.cache_pct"), ring ? ring->capacity() : 300);

    const std::uint64_t vals[] = {m.used(), m.available, m.cached, m.buffers, m.shmem, m.free};
    for (int i = 0; i < 6; ++i) set(m_kv[std::size_t(i)].val, fmt::bytes(vals[i]));

    // Swap: rebuild rows only when the device list changes.
    std::string layout;
    for (const auto& sw : s.swaps) layout += sw.name + "|";
    for (const auto& z : s.zram) layout += z.name + "#";
    if (layout != m_swap_layout || m_swap_labels.empty()) {
        while (auto* c = m_swaps.get_first_child()) m_swaps.remove(*c);
        m_swap_labels.clear();
        m_swap_bars.clear();
        m_bar_store.clear();
        const std::size_t rows = std::max<std::size_t>(1, s.swaps.size()) + s.zram.size();
        for (std::size_t i = 0; i < rows; ++i) {
            auto l = Gtk::make_managed<Gtk::Label>();
            l->set_xalign(0.0f);
            l->set_wrap(true);
            m_swaps.append(*l);
            m_swap_labels.push_back(l);
            if (i < s.swaps.size()) {
                m_bar_store.push_back(std::make_unique<widgets::Bar>(
                    "memory.swap." + std::to_string(i), 10));
                m_bar_store.back()->add_css_class("graph");
                m_swaps.append(*m_bar_store.back());
                m_swap_bars.push_back(m_bar_store.back().get());
            }
        }
        m_swap_layout = layout;
    }
    std::size_t li = 0;
    if (s.swaps.empty()) {
        set(*m_swap_labels[li++], "No swap configured.");
    }
    for (std::size_t i = 0; i < s.swaps.size(); ++i) {
        const auto& sw = s.swaps[i];
        set(*m_swap_labels[li++], sw.name + " · " + sw.type + " · " + fmt::bytes(sw.used) +
                                  " of " + fmt::bytes(sw.size) + " · priority " +
                                  std::to_string(sw.priority));
        m_swap_bars[i]->set_parts({{frac(sw.used, sw.size), 1.0}});
    }
    for (const auto& z : s.zram) {
        std::string t = z.name + " (compressed RAM) · " + fmt::bytes(z.orig) + " stored in " +
                        fmt::bytes(z.compr);
        if (z.compr) t += " (" + ratio(double(z.orig) / double(z.compr)) + ")";
        t += " · costs " + fmt::bytes(z.mem_used) + " of RAM · capacity " + fmt::bytes(z.disksize);
        set(*m_swap_labels[li++], t);
    }

    // Top memory: the ten largest resident processes.
    std::vector<const core::ProcRow*> by;
    by.reserve(s.procs.size());
    for (const auto& p : s.procs) if (p.rss_bytes) by.push_back(&p);
    const std::size_t k = std::min<std::size_t>(kTop, by.size());
    std::partial_sort(by.begin(), by.begin() + std::ptrdiff_t(k), by.end(),
                      [](auto* a, auto* b) { return a->rss_bytes > b->rss_bytes; });
    for (int r = 0; r < kTop; ++r) {
        auto& row = m_rows[std::size_t(r)];
        if (std::size_t(r) < k) {
            const auto* p = by[std::size_t(r)];
            set(row[0], p->name);
            row[0].set_tooltip_text(p->cmdline.empty() ? p->name : p->cmdline);
            set(row[1], std::to_string(p->pid));
            set(row[2], fmt::bytes(p->rss_bytes));
            set(row[3], fmt::pct(100.0 * frac(p->rss_bytes, m.total)));
        } else {
            for (auto& l : row) set(l, "");
        }
    }
}

}  // namespace trails::pages

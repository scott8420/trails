#include "pages/DisksPage.hpp"
#include "Registry.hpp"
#include "core/Format.hpp"

#include <algorithm>

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
double frac(std::uint64_t a, std::uint64_t b) { return b ? double(a) / double(b) : 0.0; }

// The card a filesystem goes on: the first of its disks that has a card.
std::string card_of(const core::FsView& f, const core::Snapshot& s) {
    for (const auto& d : f.disks)
        for (const auto& i : s.disk_info)
            if (i.name == d) return d;
    return {};
}
const core::DiskRate* rate_of(const core::Snapshot& s, const std::string& name) {
    for (const auto& d : s.disks) if (d.name == name) return &d;
    return nullptr;
}
}  // namespace

DisksPage::DisksPage() : m_graph("disks.graph") {
    set_name("page.disks");
    registry::add("page.disks", this);
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_page.add_css_class("page");
    m_page.add_css_class("tab-disks");

    m_panel.add_css_class("panel");
    m_icon.set_from_icon_name("trails-tab-disks-symbolic");
    m_icon.set_pixel_size(48);
    m_icon.add_css_class("tile-icon");
    m_title.set_text("Disks");
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
    m_graph.set_content_height(120);
    m_graph.set_scale(widgets::Graph::Scale::Auto, 1024.0 * 1024.0);   // 1 MiB/s floor
    m_graph.set_tooltip_text("Read (filled) and write (line), all disks");
    m_panel.append(m_head);
    m_panel.append(m_graph);
    m_page.append(m_panel);
    m_page.append(m_cards);
    set_child(m_page);
}

DisksPage::~DisksPage() { registry::remove(this); }

void DisksPage::rebuild(const core::Snapshot& s, const std::string& layout) {
    while (auto* c = m_cards.get_first_child()) m_cards.remove(*c);
    m_card_store.clear();
    m_fs_rows.clear();

    auto make = [&](const std::string& disk) {
        auto c = std::make_unique<Card>("disks." + (disk.empty() ? std::string("other") : disk));
        c->disk = disk;
        c->panel.add_css_class("panel");
        c->title.add_css_class("section-title");
        c->title.set_xalign(0.0f);
        c->sub.add_css_class("dim-label");
        c->sub.set_xalign(0.0f);
        c->sub.set_ellipsize(Pango::EllipsizeMode::END);
        c->titles.append(c->title);
        c->titles.append(c->sub);
        c->titles.set_hexpand(true);
        c->value.add_css_class("tile-value");
        c->value.set_valign(Gtk::Align::CENTER);
        c->head.append(c->titles);
        if (!disk.empty()) c->head.append(c->value);
        c->panel.append(c->head);
        if (!disk.empty()) {
            c->graph.set_content_height(70);
            c->graph.set_scale(widgets::Graph::Scale::Auto, 1024.0 * 1024.0);
            c->graph.set_tooltip_text("Read (filled) and write (line)");
            c->rates.set_xalign(0.0f);
            c->rates.add_css_class("dim-label");
            c->panel.append(c->graph);
            c->panel.append(c->rates);
        }
        c->panel.append(c->fs);
        m_cards.append(c->panel);
        m_card_store.push_back(std::move(c));
        return m_card_store.back().get();
    };
    for (const auto& d : s.disk_info) make(d.name);
    Card* other = nullptr;

    std::size_t n = 0;
    for (const auto& f : s.filesystems) {
        const std::string home = card_of(f, s);
        Card* c = nullptr;
        for (auto& k : m_card_store) if (!home.empty() && k->disk == home) c = k.get();
        if (!c) c = other ? other : (other = make(""));
        auto r = std::make_unique<FsRow>("disks.fs." + std::to_string(n++));
        r->text.set_xalign(0.0f);
        r->text.set_ellipsize(Pango::EllipsizeMode::END);
        r->sub.set_xalign(0.0f);
        r->sub.add_css_class("dim-label");
        r->sub.set_wrap(true);
        r->sub.set_wrap_mode(Pango::WrapMode::WORD_CHAR);
        r->bar.add_css_class("graph");   // takes the tab colour
        r->box.append(r->text);
        r->box.append(r->bar);
        r->box.append(r->sub);
        c->fs.append(r->box);
        m_fs_rows.push_back(r.get());
        c->rows.push_back(std::move(r));
    }
    m_layout = layout;
}

void DisksPage::paint(const core::Snapshot& s) {
    // Layout: which cards, which filesystem rows on which card.
    std::string layout;
    for (const auto& d : s.disk_info) layout += d.name + ";";
    layout += "|";
    for (const auto& f : s.filesystems) layout += f.point + "@" + f.dev + ">" + card_of(f, s) + ";";
    if (layout != m_layout) rebuild(s, layout);

    // Header.
    set(m_value, fmt::rate(s.disk_read_bps + s.disk_write_bps));
    set(m_sub, std::to_string(s.disk_info.size()) + (s.disk_info.size() == 1 ? " disk · " : " disks · ") +
               std::to_string(s.filesystems.size()) + " filesystems · read " + fmt::rate(s.disk_read_bps) +
               " · write " + fmt::rate(s.disk_write_bps));
    m_graph.set_series(hist(s, "disk.read"), hist(s, "disk.write"), cap(s, "disk.read"));

    // Disk cards.
    for (auto& cp : m_card_store) {
        Card& c = *cp;
        if (c.disk.empty()) {
            set(c.title, "Other filesystems");
            set(c.sub, "Not on a disk listed above");
            continue;
        }
        const auto info = std::find_if(s.disk_info.begin(), s.disk_info.end(),
                                       [&](const auto& i) { return i.name == c.disk; });
        if (info == s.disk_info.end()) continue;
        set(c.title, info->name);
        std::string sub = info->model.empty() ? std::string() : info->model + " · ";
        sub += fmt::bytes(info->size_bytes) + " · " + (info->rotational ? "spinning disk" : "solid state");
        if (info->removable) sub += " · removable";
        sub += " · " + std::to_string(info->parts.size()) + (info->parts.size() == 1 ? " partition" : " partitions");
        set(c.sub, sub);
        const core::DiskRate* r = rate_of(s, c.disk);
        const double busy = r ? r->busy_pct : 0.0;
        set(c.value, fmt::pct(busy) + " busy");
        set(c.rates, "Read " + fmt::rate(r ? r->read_bps : 0) + " · Write " + fmt::rate(r ? r->write_bps : 0));
        const std::string id = "disk." + c.disk;
        c.graph.set_series(hist(s, id + ".read"), hist(s, id + ".write"), cap(s, id + ".read"));
    }

    // Filesystem rows: rebuild() kept one per filesystem, same order.
    for (std::size_t i = 0; i < s.filesystems.size(); ++i) {
        if (i >= m_fs_rows.size()) break;
        FsRow* r = m_fs_rows[i];
        const auto& f = s.filesystems[i];
        const auto& u = f.usage;
        std::string text = f.point + "  ·  " + f.fstype;
        if (u.size) text += "  ·  " + fmt::bytes(u.used) + " of " + fmt::bytes(u.size) + " used (" +
                            fmt::pct(100.0 * frac(u.used, u.size)) + ")  ·  " + fmt::bytes(u.avail) + " free";
        if (f.read_only) text += "  ·  read-only";
        set(r->text, text);
        r->bar.set_parts({{frac(u.used, u.size), 1.0}});
        std::string sub = f.source;
        if (!f.subvols.empty()) {
            sub += "  ·  subvolumes ";
            for (std::size_t k = 0; k < f.subvols.size(); ++k)
                sub += (k ? ",  " : "") + f.subvols[k].first + " → " + f.subvols[k].second;
        }
        if (!f.others.empty()) {
            sub += "  ·  also at ";
            for (std::size_t k = 0; k < f.others.size(); ++k) sub += (k ? ", " : "") + f.others[k];
        }
        set(r->sub, sub);
    }
}

}  // namespace trails::pages

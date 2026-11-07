#include "pages/ServicesPage.hpp"
#include "Registry.hpp"
#include "Systemd.hpp"

#include <gtk/gtk.h>
#include <gtkmm/signallistitemfactory.h>

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace trails::pages {

UnitItem::UnitItem() : Glib::ObjectBase(typeid(UnitItem)), Glib::Object() {}

namespace {
UnitItem* from_gobj(gconstpointer p) {
    return p ? dynamic_cast<UnitItem*>(Glib::ObjectBase::_get_current_wrapper(
                   static_cast<GObject*>(const_cast<gpointer>(p))))
             : nullptr;
}
using Cmp = std::function<int(const UnitItem&, const UnitItem&)>;
// gtkmm 4.10 / 4.14 don't wrap GtkCustomSorter (as on the Processes tab).
Glib::RefPtr<Gtk::Sorter> make_sorter(Cmp cmp) {
    auto* fn = new Cmp(std::move(cmp));
    GtkCustomSorter* s = gtk_custom_sorter_new(
        [](gconstpointer a, gconstpointer b, gpointer d) -> int {
            const UnitItem* x = from_gobj(a);
            const UnitItem* y = from_gobj(b);
            if (!x || !y) return 0;
            int r = (*static_cast<Cmp*>(d))(*x, *y);
            if (r == 0) r = x->row.name.compare(y->row.name);
            return r < 0 ? GTK_ORDERING_SMALLER : r > 0 ? GTK_ORDERING_LARGER : GTK_ORDERING_EQUAL;
        },
        fn, [](gpointer d) { delete static_cast<Cmp*>(d); });
    return Glib::wrap(GTK_SORTER(s));
}
void set(Gtk::Label& l, const std::string& t) {
    if (std::string(l.get_text()) != t) l.set_text(t);
}
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// failed first, then running / active, then the rest (the State column's order)
int state_rank(const core::UnitRow& r) {
    if (r.active == "failed") return 0;
    if (r.active == "active" || r.active == "activating" || r.active == "reloading") return 1;
    return 2;
}
std::string state_text(const core::UnitRow& r) {
    return r.sub.empty() || r.sub == r.active ? r.active : r.active + " (" + r.sub + ")";
}
}  // namespace

ServicesPage::ServicesPage() : Gtk::Box(Gtk::Orientation::VERTICAL, 12) {
    set_name("page.services");
    registry::add("page.services", this);
    add_css_class("page");
    add_css_class("tab-services");
    build_header();
    build_view();
    build_detail();
    m_paned.set_start_child(m_scroll);
    m_paned.set_end_child(m_detail);
    m_paned.set_resize_start_child(true);
    m_paned.set_resize_end_child(false);
    m_paned.set_shrink_end_child(false);
    m_paned.set_vexpand(true);
    append(m_header);
    append(m_paned);
}

ServicesPage::~ServicesPage() { registry::remove(this); }

core::UnitScope ServicesPage::scope() const {
    return m_user_btn.get_active() ? core::UnitScope::User : core::UnitScope::System;
}

void ServicesPage::build_header() {
    m_icon.set_from_icon_name("trails-tab-services-symbolic");
    m_icon.set_pixel_size(40);
    m_icon.add_css_class("tile-icon");
    m_title.set_text("Services");
    m_title.add_css_class("panel-title");
    m_title.set_xalign(0.0f);
    m_count.add_css_class("dim-label");
    m_count.set_xalign(0.0f);
    m_count.set_ellipsize(Pango::EllipsizeMode::END);   // an error line must not widen the window
    m_count.set_width_chars(12);
    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    titles->set_hexpand(true);
    titles->set_valign(Gtk::Align::CENTER);
    titles->append(m_title);
    titles->append(m_count);

    m_sys_btn.set_label("System");
    m_user_btn.set_label("User");
    m_user_btn.set_group(m_sys_btn);
    m_sys_btn.set_active(true);
    m_sys_btn.signal_toggled().connect([this] {
        // The other scope: a fresh list (the names mean different units).
        m_store->remove_all();
        m_items.clear();
        m_selected.clear();
        paint_detail();
        refresh();
    });
    auto seg = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    seg->add_css_class("linked");
    seg->set_valign(Gtk::Align::CENTER);
    seg->append(m_sys_btn);
    seg->append(m_user_btn);

    m_search.set_placeholder_text("Filter: name or description");
    m_search.set_size_request(220, -1);
    m_search.set_valign(Gtk::Align::CENTER);
    m_search.signal_search_changed().connect([this] { refilter(); });
    m_all_types.set_label("All types");
    m_all_types.set_tooltip_text("Show timers, sockets, mounts, targets ... too (services only when off)");
    m_all_types.set_valign(Gtk::Align::CENTER);
    m_all_types.signal_toggled().connect([this] { refilter(); });

    m_header.append(m_icon);
    m_header.append(*titles);
    m_header.append(*seg);
    m_header.append(m_search);
    m_header.append(m_all_types);
}

void ServicesPage::add_column(const char* title, Fmt f, Cmp cmp, bool expand, const char* id) {
    m_fmts.push_back(std::make_unique<Fmt>(std::move(f)));
    const Fmt* fp = m_fmts.back().get();
    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& li) {
        auto* l = Gtk::make_managed<Gtk::Label>();
        l->set_xalign(0.0f);
        l->set_ellipsize(Pango::EllipsizeMode::END);
        li->set_child(*l);
    });
    factory->signal_bind().connect([this, fp](const Glib::RefPtr<Gtk::ListItem>& li) {
        auto item = std::dynamic_pointer_cast<UnitItem>(li->get_item());
        auto* l = dynamic_cast<Gtk::Label*>(li->get_child());
        if (!item || !l) return;
        l->set_text((*fp)(*item));
        if (item->row.active == "failed") l->add_css_class("error");
        else l->remove_css_class("error");
        m_bound[l] = Bound{item, fp};
    });
    factory->signal_unbind().connect([this](const Glib::RefPtr<Gtk::ListItem>& li) {
        if (auto* l = dynamic_cast<Gtk::Label*>(li->get_child())) m_bound.erase(l);
    });
    auto col = Gtk::ColumnViewColumn::create(title, factory);
    col->set_sorter(make_sorter(std::move(cmp)));
    col->set_expand(expand);
    col->set_resizable(true);
    col->set_id(id);
    m_view.append_column(col);
    if (std::string(id) == "state") m_state_col = col;
}

void ServicesPage::build_view() {
    m_store = Gio::ListStore<UnitItem>::create();
    add_column("Unit", [](const UnitItem& u) { return u.row.name; },
               [](const UnitItem& a, const UnitItem& b) { return a.row.name.compare(b.row.name); }, false, "unit");
    add_column("State", [](const UnitItem& u) { return state_text(u.row); },
               [](const UnitItem& a, const UnitItem& b) {
                   const int r = state_rank(a.row) - state_rank(b.row);
                   return r ? r : a.row.sub.compare(b.row.sub);
               }, false, "state");
    add_column("Enabled", [](const UnitItem& u) { return u.row.file_state; },
               [](const UnitItem& a, const UnitItem& b) { return a.row.file_state.compare(b.row.file_state); }, false,
               "enabled");
    add_column("Description", [](const UnitItem& u) { return u.row.description; },
               [](const UnitItem& a, const UnitItem& b) { return a.row.description.compare(b.row.description); }, true,
               "description");

    m_filter = Glib::wrap(GTK_FILTER(gtk_custom_filter_new(
        [](gpointer item, gpointer d) -> gboolean {
            auto* self = static_cast<ServicesPage*>(d);
            const UnitItem* u = from_gobj(item);
            if (!u) return false;
            if (!self->m_all_types.get_active() && core::unit_type(u->row.name) != "service") return false;
            const std::string q = lower(self->m_search.get_text());
            if (q.empty()) return true;
            return lower(u->row.name).find(q) != std::string::npos ||
                   lower(u->row.description).find(q) != std::string::npos;
        },
        this, nullptr)));
    m_filtered = Gtk::FilterListModel::create(m_store, m_filter);
    m_sorted = Gtk::SortListModel::create(m_filtered, m_view.get_sorter());
    m_sel = Gtk::SingleSelection::create(m_sorted);
    m_sel->set_autoselect(false);
    m_sel->set_can_unselect(true);
    m_sel->property_selected().signal_changed().connect([this] {
        if (m_restoring) return;
        auto* u = dynamic_cast<UnitItem*>(m_sel->get_selected_item().get());
        const std::string name = u ? u->row.name : std::string();
        if (name != m_selected) {
            m_selected = name;
            set_status("", false);
            paint_detail();
            reload_log();
            if (m_on_pick) m_on_pick();
        }
    });
    m_view.set_model(m_sel);
    m_view.add_css_class("data-table");
    m_view.sort_by_column(m_state_col, Gtk::SortType::ASCENDING);   // failed first
    m_scroll.set_child(m_view);
    m_scroll.set_vexpand(true);
    m_scroll.set_size_request(-1, 220);
    m_scroll.add_css_class("panel-flat");
}

void ServicesPage::build_detail() {
    m_detail.add_css_class("panel");
    m_detail.set_margin_top(10);
    m_d_title.add_css_class("section-title");
    m_d_title.set_xalign(0.0f);
    for (auto* l : {&m_d_desc, &m_d_state}) { l->set_xalign(0.0f); l->set_wrap(true); }
    m_d_state.add_css_class("dim-label");
    struct B { const char* label; const char* action; const char* tip; bool danger; };
    static const B kButtons[] = {
        {"Start",   "win.unit-start",   "Start it now (asks first; not undoable)", false},
        {"Stop",    "win.unit-stop",    "Stop it now (asks first; not undoable)", true},
        {"Restart", "win.unit-restart", "Stop and start it (asks first; not undoable)", false},
        {"Enable",  "win.unit-enable",  "Start it at boot / login. Ctrl+Z undoes.", false},
        {"Disable", "win.unit-disable", "Don't start it at boot / login. Ctrl+Z undoes.", false},
    };
    for (const auto& b : kButtons) {
        auto* btn = Gtk::make_managed<Gtk::Button>(b.label);
        btn->set_action_name(b.action);
        btn->set_tooltip_text(b.tip);
        if (b.danger) btn->add_css_class("destructive-action");
        m_acts.append(*btn);
    }
    m_status.set_xalign(0.0f);
    m_status.set_hexpand(true);
    m_status.set_margin_start(12);
    m_status.set_ellipsize(Pango::EllipsizeMode::END);
    m_acts.append(m_status);
    m_log.set_editable(false);
    m_log.set_cursor_visible(false);
    m_log.set_monospace(true);
    m_log.set_wrap_mode(Gtk::WrapMode::CHAR);
    m_log_scroll.set_child(m_log);
    m_log_scroll.set_size_request(-1, 160);
    m_log_scroll.set_vexpand(true);
    m_detail.append(m_d_title);
    m_detail.append(m_d_desc);
    m_detail.append(m_d_state);
    m_detail.append(m_acts);
    m_detail.append(m_log_scroll);
    paint_detail();
}

void ServicesPage::shown() { refresh(); }

void ServicesPage::tick() {
    if (++m_ticks % 3 == 0) refresh();   // every ~3 s while visible
}

void ServicesPage::refresh() {
    if (!m_sd || m_waiting) return;
    m_waiting = true;
    const core::UnitScope sc = scope();
    m_sd->list(sc, [this, sc](std::vector<core::UnitRow> rows, std::string err) {
        m_waiting = false;
        if (sc != scope()) return;   // switched while waiting: that list is the other scope's
        apply(std::move(rows), err);
    });
}

void ServicesPage::apply(std::vector<core::UnitRow> rows, const std::string& err) {
    if (!err.empty() && rows.empty()) {
        set(m_count, err);
        return;
    }
    std::unordered_set<std::string> alive;
    std::vector<Glib::RefPtr<UnitItem>> fresh;
    int running = 0, failed = 0, services = 0;
    for (auto& r : rows) {
        alive.insert(r.name);
        if (core::unit_type(r.name) == "service") {
            ++services;
            if (r.active == "active") ++running;
        }
        if (r.active == "failed") ++failed;
        auto& it = m_items[r.name];
        if (!it) { it = UnitItem::create(); fresh.push_back(it); }
        it->row = std::move(r);
    }
    std::erase_if(m_items, [&](const auto& kv) { return !alive.count(kv.first); });
    for (guint i = m_store->get_n_items(); i-- > 0;)
        if (!alive.count(m_store->get_item(i)->row.name)) m_store->remove(i);
    if (!fresh.empty()) m_store->splice(m_store->get_n_items(), 0, fresh);
    for (auto& [l, b] : m_bound) {
        const std::string t = (*b.fmt)(*b.item);
        if (std::string(l->get_text()) != t) l->set_text(t);
        if (b.item->row.active == "failed") l->add_css_class("error");
        else l->remove_css_class("error");
    }
    m_view.get_sorter()->changed(Gtk::Sorter::Change::DIFFERENT);
    restore_selection();
    set(m_count, std::to_string(services) + " services · " + std::to_string(running) + " running · " +
                 std::to_string(failed) + " failed · " + std::to_string(m_items.size()) + " units");
    paint_detail();
}

void ServicesPage::refilter() {
    gtk_filter_changed(m_filter->gobj(), GTK_FILTER_CHANGE_DIFFERENT);
    restore_selection();
}

void ServicesPage::restore_selection() {
    const guint n = m_sel->get_n_items();
    const guint cur = m_sel->get_selected();
    if (cur < n)
        if (auto* u = dynamic_cast<UnitItem*>(m_sel->get_object(cur).get()); u && u->row.name == m_selected) return;
    m_restoring = true;
    bool found = false;
    if (!m_selected.empty())
        for (guint i = 0; i < n; ++i)
            if (auto* u = dynamic_cast<UnitItem*>(m_sel->get_object(i).get()); u && u->row.name == m_selected) {
                m_sel->set_selected(i);
                found = true;
                break;
            }
    if (!found) m_sel->unselect_all();
    m_restoring = false;
}

std::optional<ServicesPage::Picked> ServicesPage::picked() const {
    auto it = m_items.find(m_selected);
    if (m_selected.empty() || it == m_items.end()) return std::nullopt;
    return Picked{core::UnitKey{scope(), m_selected}, it->second->row};
}

void ServicesPage::paint_detail() {
    const auto p = picked();
    if (!p) {
        set(m_d_title, "Select a unit");
        set(m_d_desc, "");
        set(m_d_state, "");
        return;
    }
    const auto& r = p->row;
    set(m_d_title, r.name + (scope() == core::UnitScope::User ? "  ·  user" : "  ·  system"));
    set(m_d_desc, r.description.empty() ? "(no description)" : r.description);
    set(m_d_state, "State: " + state_text(r) + "    Enabled: " + (r.file_state.empty() ? "--" : r.file_state) +
                       (r.load.empty() ? "    (not loaded)" : "    Load: " + r.load));
}

void ServicesPage::reload_log() {
    const auto p = picked();
    if (!p || !m_sd) { m_log.get_buffer()->set_text(""); return; }
    const std::string want = p->key.name;
    m_sd->journal(p->key, 40, [this, want](std::string text) {
        if (want != m_selected) return;   // the pick moved on
        m_log.get_buffer()->set_text(text.empty() ? "(no log lines)" : text);
        auto end = m_log.get_buffer()->end();
        m_log.scroll_to(end);
    });
}

void ServicesPage::set_status(const std::string& text, bool error) {
    set(m_status, text);
    if (error) m_status.add_css_class("error");
    else m_status.remove_css_class("error");
}

}  // namespace trails::pages

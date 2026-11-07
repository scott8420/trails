#include "pages/ProcessesPage.hpp"
#include "Registry.hpp"
#include "core/Format.hpp"
#include "core/ProcView.hpp"

#include <gtk/gtk.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/treeexpander.h>
#include <gtkmm/treelistrow.h>

#include <algorithm>
#include <unordered_set>

namespace trails::pages {

namespace pv = core::procview;
namespace fmt = core::fmt;

ProcItem::ProcItem() : Glib::ObjectBase(typeid(ProcItem)), Glib::Object() {
    children = Gio::ListStore<ProcItem>::create();
}

namespace {

ProcItem* from_gobj(gconstpointer p) {
    return p ? dynamic_cast<ProcItem*>(Glib::ObjectBase::_get_current_wrapper(
                   static_cast<GObject*>(const_cast<gpointer>(p))))
             : nullptr;
}

// gtkmm 4.14 doesn't wrap GtkCustomSorter / GtkCustomFilter /
// GtkTreeListRowSorter, so these build them through the C API and hand back
// ordinary gtkmm RefPtrs (Glib::wrap takes the reference).
using Cmp = std::function<int(const ProcItem&, const ProcItem&)>;
Glib::RefPtr<Gtk::Sorter> make_sorter(Cmp cmp) {
    auto* fn = new Cmp(std::move(cmp));
    GtkCustomSorter* s = gtk_custom_sorter_new(
        [](gconstpointer a, gconstpointer b, gpointer d) -> int {
            const ProcItem* x = from_gobj(a);
            const ProcItem* y = from_gobj(b);
            if (!x || !y) return 0;
            int r = (*static_cast<Cmp*>(d))(*x, *y);
            if (r == 0) r = (x->row.pid > y->row.pid) - (x->row.pid < y->row.pid);   // stable
            return r < 0 ? GTK_ORDERING_SMALLER : r > 0 ? GTK_ORDERING_LARGER : GTK_ORDERING_EQUAL;
        },
        fn, [](gpointer d) { delete static_cast<Cmp*>(d); });
    return Glib::wrap(GTK_SORTER(s));
}

template <class T> int cmp3(const T& a, const T& b) { return (a > b) - (a < b); }

void set(Gtk::Label& l, const std::string& t) {
    if (std::string(l.get_text()) != t) l.set_text(t);
}

}  // namespace

ProcItem* ProcessesPage::item_of(const Glib::RefPtr<Glib::ObjectBase>& obj) {
    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(obj);
    if (!row) return nullptr;
    return dynamic_cast<ProcItem*>(row->get_item().get());
}

ProcessesPage::ProcessesPage() : Gtk::Box(Gtk::Orientation::VERTICAL, 12) {
    set_name("page.processes");
    registry::add("page.processes", this);
    add_css_class("page");
    add_css_class("tab-processes");
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

ProcessesPage::~ProcessesPage() { registry::remove(this); }

bool ProcessesPage::tree_mode() const { return m_tree_btn.get_active(); }

void ProcessesPage::build_header() {
    m_icon.set_from_icon_name("trails-tab-processes-symbolic");
    m_icon.set_pixel_size(40);
    m_icon.add_css_class("tile-icon");
    m_title.set_text("Processes");
    m_title.add_css_class("panel-title");
    m_title.set_xalign(0.0f);
    m_count.add_css_class("dim-label");
    m_count.set_xalign(0.0f);

    m_search.set_placeholder_text("Filter: name, command, user, PID");
    m_search.set_size_request(220, -1);
    m_search.set_valign(Gtk::Align::CENTER);
    m_search.signal_search_changed().connect([this] {
        const bool want_tree = tree_mode() && std::string(m_search.get_text()).empty();
        if (want_tree != m_built_tree_mode) rebuild_tree();
        refilter();
    });

    m_tree_btn.set_label("Tree");
    m_flat_btn.set_label("Flat");
    m_flat_btn.set_group(m_tree_btn);
    m_tree_btn.set_active(true);
    m_tree_btn.signal_toggled().connect([this] { if (m_sel) rebuild_tree(); });
    auto seg = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    seg->add_css_class("linked");
    seg->set_valign(Gtk::Align::CENTER);
    seg->append(m_tree_btn);
    seg->append(m_flat_btn);

    m_kthreads.set_label("Kernel");
    m_kthreads.set_tooltip_text("Show kernel threads");
    m_kthreads.set_valign(Gtk::Align::CENTER);
    m_kthreads.signal_toggled().connect([this] { refilter(); });

    auto titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    titles->set_hexpand(true);
    titles->set_valign(Gtk::Align::CENTER);
    titles->append(m_title);
    titles->append(m_count);
    m_header.append(m_icon);
    m_header.append(*titles);
    m_header.append(m_search);
    m_header.append(*seg);
    m_header.append(m_kthreads);
}

void ProcessesPage::add_column(const char* title, Fmt f, Cmp cmp, float xalign, bool expand,
                               const char* id, bool tree_column) {
    m_fmts.push_back(std::make_unique<Fmt>(std::move(f)));
    const Fmt* fp = m_fmts.back().get();

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([xalign, tree_column](const Glib::RefPtr<Gtk::ListItem>& li) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_xalign(xalign);
        if (tree_column) {
            label->set_ellipsize(Pango::EllipsizeMode::END);
            auto* ex = Gtk::make_managed<Gtk::TreeExpander>();
            ex->set_child(*label);
            li->set_child(*ex);
        } else {
            label->add_css_class("numeric");
            li->set_child(*label);
        }
    });
    factory->signal_bind().connect([this, fp, tree_column](const Glib::RefPtr<Gtk::ListItem>& li) {
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(li->get_item());
        if (!row) return;
        Gtk::Label* label = nullptr;
        if (tree_column) {
            auto* ex = dynamic_cast<Gtk::TreeExpander*>(li->get_child());
            if (!ex) return;
            ex->set_list_row(row);
            ex->set_indent_for_icon(true);
            label = dynamic_cast<Gtk::Label*>(ex->get_child());
            if (auto it = std::dynamic_pointer_cast<ProcItem>(row->get_item()))
                ex->set_hide_expander(!m_built_tree_mode || it->children->get_n_items() == 0);
        } else {
            label = dynamic_cast<Gtk::Label*>(li->get_child());
        }
        auto item = std::dynamic_pointer_cast<ProcItem>(row->get_item());
        if (!label || !item) return;
        label->set_text((*fp)(*item));
        m_bound[label] = Bound{item, fp, tree_column};
    });
    factory->signal_unbind().connect([this, tree_column](const Glib::RefPtr<Gtk::ListItem>& li) {
        Gtk::Widget* w = li->get_child();
        if (tree_column)
            if (auto* ex = dynamic_cast<Gtk::TreeExpander*>(w)) w = ex->get_child();
        if (auto* label = dynamic_cast<Gtk::Label*>(w)) m_bound.erase(label);
    });

    auto col = Gtk::ColumnViewColumn::create(title, factory);
    col->set_sorter(make_sorter(std::move(cmp)));
    col->set_expand(expand);
    col->set_resizable(true);
    col->set_id(id);
    m_view.append_column(col);
    if (std::string(id) == "cpu") m_cpu_col = col;
}

void ProcessesPage::build_view() {
    m_roots = Gio::ListStore<ProcItem>::create();
    m_all   = Gio::ListStore<ProcItem>::create();

    add_column("Name", [](const ProcItem& p) { return p.row.name; },
               [](const ProcItem& a, const ProcItem& b) { return a.row.name.compare(b.row.name); },
               0.0f, true, "name", true);
    add_column("PID", [](const ProcItem& p) { return std::to_string(p.row.pid); },
               [](const ProcItem& a, const ProcItem& b) { return cmp3(a.row.pid, b.row.pid); },
               1.0f, false, "pid");
    add_column("User", [](const ProcItem& p) { return p.user; },
               [](const ProcItem& a, const ProcItem& b) { return a.user.compare(b.user); },
               0.0f, false, "user");
    add_column("CPU", [](const ProcItem& p) { return fmt::pct(p.row.cpu_pct); },
               [](const ProcItem& a, const ProcItem& b) { return cmp3(a.row.cpu_pct, b.row.cpu_pct); },
               1.0f, false, "cpu");
    add_column("Memory", [](const ProcItem& p) { return p.row.rss_bytes ? fmt::bytes(p.row.rss_bytes) : std::string(); },
               [](const ProcItem& a, const ProcItem& b) { return cmp3(a.row.rss_bytes, b.row.rss_bytes); },
               1.0f, false, "memory");
    add_column("Threads", [](const ProcItem& p) { return std::to_string(p.row.threads); },
               [](const ProcItem& a, const ProcItem& b) { return cmp3(a.row.threads, b.row.threads); },
               1.0f, false, "threads");
    add_column("Nice", [](const ProcItem& p) { return std::to_string(p.row.nice); },
               [](const ProcItem& a, const ProcItem& b) { return cmp3(a.row.nice, b.row.nice); },
               1.0f, false, "nice");
    add_column("State", [](const ProcItem& p) { return std::string(pv::state_name(p.row.state)); },
               [](const ProcItem& a, const ProcItem& b) { return cmp3(a.row.state, b.row.state); },
               0.0f, false, "state");

    // Filter: kernel threads (toggle) + the typed query. Runs on tree rows.
    m_filter = Glib::wrap(GTK_FILTER(gtk_custom_filter_new(
        [](gpointer item, gpointer d) -> gboolean {
            auto* self = static_cast<ProcessesPage*>(d);
            auto* it = static_cast<GObject*>(gtk_tree_list_row_get_item(GTK_TREE_LIST_ROW(item)));
            const ProcItem* p = from_gobj(it);
            bool ok = p != nullptr;
            if (ok && p->row.kernel_thread && !self->m_kthreads.get_active()) ok = false;
            if (ok) ok = pv::matches(p->row, p->user, std::string(self->m_search.get_text()));
            if (it) g_object_unref(it);
            return ok;
        },
        this, nullptr)));

    m_built_tree_mode = true;
    m_tree = Gtk::TreeListModel::create(
        m_roots,
        [this](const Glib::RefPtr<Glib::ObjectBase>& o) -> Glib::RefPtr<Gio::ListModel> {
            auto* p = dynamic_cast<ProcItem*>(o.get());
            // ALWAYS the store in tree mode, even empty: GTK fixes a row's
            // expandability when the row is created, and a process gains its
            // first child later. Leaves just hide the arrow (refresh_cells).
            if (!m_built_tree_mode || !p) return {};
            return p->children;
        },
        false, true);
    m_filtered = Gtk::FilterListModel::create(m_tree, m_filter);
    // Sort siblings by the clicked column, keep the tree shape: the row sorter
    // takes (and owns) a reference to the view's sorter.
    m_row_sorter = Glib::wrap(GTK_SORTER(gtk_tree_list_row_sorter_new(
        GTK_SORTER(g_object_ref(m_view.get_sorter()->gobj())))));
    m_sorted = Gtk::SortListModel::create(m_filtered, m_row_sorter);
    m_sel = Gtk::SingleSelection::create(m_sorted);
    m_sel->set_autoselect(false);
    m_sel->set_can_unselect(true);
    m_sel->property_selected().signal_changed().connect([this] {
        if (m_restoring) return;
        const ProcItem* p = item_of(m_sel->get_selected_item());
        m_selected_pid = p ? p->row.pid : -1;
        paint_detail();
    });
    m_view.set_model(m_sel);
    m_view.add_css_class("data-table");
    m_view.set_show_column_separators(false);
    m_view.set_reorderable(false);
    m_view.sort_by_column(m_cpu_col, Gtk::SortType::DESCENDING);

    m_scroll.set_child(m_view);
    m_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    m_scroll.set_size_request(-1, 240);
    m_scroll.add_css_class("panel-flat");
}

void ProcessesPage::build_detail() {
    m_detail.add_css_class("panel");
    m_detail.set_margin_top(10);
    m_d_title.add_css_class("section-title");
    m_d_title.set_xalign(0.0f);
    m_d_cmd.set_xalign(0.0f);
    m_d_cmd.set_wrap(true);
    m_d_cmd.set_wrap_mode(Pango::WrapMode::WORD_CHAR);
    m_d_cmd.set_selectable(true);
    m_d_cmd.add_css_class("monospace");
    m_d_grid.set_column_spacing(20);
    m_d_grid.set_row_spacing(4);
    const char* keys[] = {"User", "PID", "Parent", "State", "Nice", "Threads", "CPU", "Resident", "Open files"};
    for (int i = 0; i < 9; ++i) {
        auto& kv = m_d_kv[std::size_t(i)];
        kv[0].set_text(keys[i]);
        kv[0].add_css_class("dim-label");
        kv[0].set_xalign(0.0f);
        kv[1].add_css_class("kv-value");
        kv[1].set_xalign(0.0f);
        m_d_grid.attach(kv[0], (i % 3) * 2, i / 3);
        m_d_grid.attach(kv[1], (i % 3) * 2 + 1, i / 3);
    }
    m_detail.append(m_d_title);
    m_detail.append(m_d_cmd);
    m_detail.append(m_d_grid);
    paint_detail();
}

void ProcessesPage::rebuild_tree() {
    m_built_tree_mode = tree_mode() && std::string(m_search.get_text()).empty();
    m_tree = Gtk::TreeListModel::create(
        m_built_tree_mode ? m_roots : m_all,
        [this](const Glib::RefPtr<Glib::ObjectBase>& o) -> Glib::RefPtr<Gio::ListModel> {
            auto* p = dynamic_cast<ProcItem*>(o.get());
            // ALWAYS the store in tree mode, even empty: GTK fixes a row's
            // expandability when the row is created, and a process gains its
            // first child later. Leaves just hide the arrow (refresh_cells).
            if (!m_built_tree_mode || !p) return {};
            return p->children;
        },
        false, true);
    m_filtered->set_model(m_tree);
    restore_selection();
}

void ProcessesPage::refilter() {
    gtk_filter_changed(m_filter->gobj(), GTK_FILTER_CHANGE_DIFFERENT);   // protected in gtkmm
    restore_selection();
}

void ProcessesPage::refresh_cells() {
    for (auto& [label, b] : m_bound) {
        const std::string t = (*b.fmt)(*b.item);
        if (std::string(label->get_text()) != t) label->set_text(t);
        if (b.tree)   // a process gained or lost its last child: show / hide the arrow
            if (auto* ex = dynamic_cast<Gtk::TreeExpander*>(label->get_parent())) {
                const bool hide = !m_built_tree_mode || b.item->children->get_n_items() == 0;
                if (ex->get_hide_expander() != hide) ex->set_hide_expander(hide);
            }
    }
}

void ProcessesPage::restore_selection() {
    if (!m_sel) return;
    const guint n = m_sel->get_n_items();
    const guint cur = m_sel->get_selected();
    if (m_selected_pid < 0) {
        if (cur != GTK_INVALID_LIST_POSITION) {
            m_restoring = true;
            m_sel->unselect_all();
            m_restoring = false;
        }
        return;
    }
    if (cur < n) {
        const ProcItem* p = item_of(m_sel->get_object(cur));
        if (p && p->row.pid == m_selected_pid) return;
    }
    m_restoring = true;
    bool found = false;
    for (guint i = 0; i < n; ++i) {
        const ProcItem* p = item_of(m_sel->get_object(i));
        if (p && p->row.pid == m_selected_pid) { m_sel->set_selected(i); found = true; break; }
    }
    if (!found) m_sel->unselect_all();   // filtered out or exited; the pid is kept
    m_restoring = false;
}

void ProcessesPage::paint(const core::Snapshot& s) {
    // 1. Items: update in place, create new.
    std::unordered_set<int> alive;
    alive.reserve(s.procs.size());
    std::vector<Glib::RefPtr<ProcItem>> fresh;
    int shown = 0;
    for (const auto& r : s.procs) {
        alive.insert(r.pid);
        auto& it = m_items[r.pid];
        if (!it) { it = ProcItem::create(); fresh.push_back(it); }
        it->row = r;
        it->user = pv::user_name(r.uid);
        if (!r.kernel_thread) ++shown;
    }
    std::erase_if(m_items, [&](const auto& kv) { return !alive.count(kv.first); });

    // 2. The flat store: drop the dead, append the new.
    for (guint i = m_all->get_n_items(); i-- > 0;)
        if (!alive.count(m_all->get_item(i)->row.pid)) m_all->remove(i);
    if (!fresh.empty()) m_all->splice(m_all->get_n_items(), 0, fresh);

    // 3. The tree: splice only stores whose membership changed.
    const auto ix = pv::build_index(s.procs);
    auto sync = [&](const Glib::RefPtr<Gio::ListStore<ProcItem>>& store, const std::vector<int>& want) {
        bool same = store->get_n_items() == want.size();
        for (guint i = 0; same && i < want.size(); ++i)
            same = store->get_item(i)->row.pid == want[i];
        if (same) return;
        std::vector<Glib::RefPtr<ProcItem>> v;
        v.reserve(want.size());
        for (int pid : want) v.push_back(m_items[pid]);
        store->splice(0, store->get_n_items(), v);
    };
    static const std::vector<int> none;
    sync(m_roots, ix.roots);
    for (auto& [pid, item] : m_items) {
        auto c = ix.children.find(pid);
        sync(item->children, c == ix.children.end() ? none : c->second);
    }

    // 4. Values moved: re-sort, refresh what's on screen, keep the selection.
    m_row_sorter->changed(Gtk::Sorter::Change::DIFFERENT);
    refresh_cells();
    restore_selection();
    set(m_count, std::to_string(shown) + " processes · " + std::to_string(s.procs.size() - shown) +
                 " kernel threads");
    paint_detail();
}

void ProcessesPage::paint_detail() {
    auto it = m_selected_pid >= 0 ? m_items.find(m_selected_pid) : m_items.end();
    if (it == m_items.end()) {
        set(m_d_title, m_selected_pid >= 0 ? "Process " + std::to_string(m_selected_pid) + " has exited"
                                           : "Select a process");
        set(m_d_cmd, "");
        for (auto& kv : m_d_kv) set(kv[1], "");
        return;
    }
    const ProcItem& p = *it->second;
    const auto& r = p.row;
    set(m_d_title, r.name + "  ·  " + std::to_string(r.pid));
    set(m_d_cmd, r.cmdline.empty() ? (r.kernel_thread ? "[kernel thread]" : "[no command line]") : r.cmdline);
    std::string files = "--";
    int nf = 0, ns = 0;
    if (core::probe::count_fds(m_root, r.pid, nf, ns))
        files = std::to_string(nf) + (ns ? " (" + std::to_string(ns) + " sockets)" : "");
    std::string parent = std::to_string(r.ppid);
    if (auto pp = m_items.find(r.ppid); pp != m_items.end()) parent += " " + pp->second->row.name;
    const std::string vals[] = {p.user, std::to_string(r.pid), parent, pv::state_name(r.state),
                                std::to_string(r.nice), std::to_string(r.threads), fmt::pct(r.cpu_pct),
                                fmt::bytes(r.rss_bytes), files};
    for (int i = 0; i < 9; ++i) set(m_d_kv[std::size_t(i)][1], vals[i]);
}

}  // namespace trails::pages

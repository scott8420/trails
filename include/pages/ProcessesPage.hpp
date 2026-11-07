#pragma once
#include "core/Sampler.hpp"

#include <giomm/liststore.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/popovermenu.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/columnview.h>
#include <gtkmm/filterlistmodel.h>
#include <gtkmm/grid.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/singleselection.h>
#include <gtkmm/sortlistmodel.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/treelistmodel.h>

#include <array>
#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// pages::ProcessesPage -- the Processes tab (s005).
//
// Model: one ProcItem (a GObject) per live pid, kept across ticks and updated
// IN PLACE -- rows are never rebuilt, so scroll position, expansion and
// selection survive the 1 Hz refresh. Two stores feed one TreeListModel:
// m_roots (tree mode: children hang off each item's own store) and m_all
// (flat mode, and whenever a filter is typed -- matches show flat).
//
//   TreeListModel -> FilterListModel (query, kernel threads)
//                 -> SortListModel (TreeListRowSorter over the ColumnView's
//                    sorter: siblings sort, the tree shape holds)
//                 -> SingleSelection -> ColumnView
//
// Every tick: update items, add / drop pids, fix children stores whose
// membership changed, poke the sorter (values moved), refresh the bound
// cells, re-select the selected pid if it moved, repaint the detail pane.
//
// GTK-free logic (tree index, filter test, state words, users, fd counts)
// is core::procview -- the selftest covers it.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::pages {

class ProcItem : public Glib::Object {
public:
    static Glib::RefPtr<ProcItem> create() { return Glib::make_refptr_for_instance(new ProcItem()); }
    core::ProcRow row;
    std::string   user;
    Glib::RefPtr<Gio::ListStore<ProcItem>> children;
protected:
    ProcItem();
};

class ProcessesPage : public Gtk::Box {
public:
    ProcessesPage();
    ~ProcessesPage() override;
    void paint(const core::Snapshot& s);
    void set_root(std::string root) { m_root = std::move(root); }   // where /proc is (fd counts)

    // s006 -- process actions. The page owns no verbs: its buttons, its
    // right-click menu and its keys all fire win.proc-* actions, and the Shell
    // (Shell_acts.cpp) runs them through the journal. The page answers "what
    // is selected" and shows how the last action went.
    struct Picked {
        int           pid = 0;
        std::uint64_t start = 0;
        std::string   name;
        char          state = '?';
        int           nice = 0;
        bool          kernel_thread = false;
    };
    std::optional<Picked> picked() const;
    void select_pid(int pid);
    void set_status(const std::string& text, bool error);
    void set_on_pick(std::function<void()> f) { m_on_pick = std::move(f); }

private:
    using Fmt = std::function<std::string(const ProcItem&)>;
    using Cmp = std::function<int(const ProcItem&, const ProcItem&)>;

    void build_header();
    void build_view();
    void build_detail();
    void add_column(const char* title, Fmt fmt, Cmp cmp, float xalign, bool expand,
                    const char* id, bool tree_column = false);
    void rebuild_tree();        // mode / query changed: new TreeListModel over the right store
    void refilter();
    void refresh_cells();
    void restore_selection();
    void paint_detail();
    void build_actions();       // s006: action row, right-click menu, Delete keys
    int  pid_at(double x, double y);   // the row under a point in m_view, or -1
    bool tree_mode() const;
    static ProcItem* item_of(const Glib::RefPtr<Glib::ObjectBase>& listitem_item);

    // Header.
    Gtk::Box         m_header{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Image       m_icon;
    Gtk::Label       m_title, m_count;
    Gtk::SearchEntry m_search;
    Gtk::ToggleButton m_tree_btn, m_flat_btn;
    Gtk::CheckButton m_kthreads;

    // List.
    Gtk::Paned          m_paned{Gtk::Orientation::VERTICAL};
    Gtk::ScrolledWindow m_scroll;
    Gtk::ColumnView     m_view;
    Glib::RefPtr<Gio::ListStore<ProcItem>> m_roots, m_all;
    Glib::RefPtr<Gtk::TreeListModel>   m_tree;
    Glib::RefPtr<Gtk::Filter>          m_filter;
    Glib::RefPtr<Gtk::FilterListModel> m_filtered;
    Glib::RefPtr<Gtk::Sorter>          m_row_sorter;
    Glib::RefPtr<Gtk::SortListModel>   m_sorted;
    Glib::RefPtr<Gtk::SingleSelection> m_sel;
    Glib::RefPtr<Gtk::ColumnViewColumn> m_cpu_col;
    std::unordered_map<int, Glib::RefPtr<ProcItem>> m_items;   // pid -> item

    struct Bound { Glib::RefPtr<ProcItem> item; const Fmt* fmt; bool tree = false; };
    std::unordered_map<Gtk::Label*, Bound> m_bound;   // cells on screen now
    std::vector<std::unique_ptr<Fmt>>      m_fmts;    // stable addresses for Bound::fmt

    std::string m_root = "/";
    int  m_selected_pid = -1;
    bool m_restoring = false;
    bool m_built_tree_mode = true;
    std::string m_built_query;

    // Detail.
    Gtk::Box   m_detail{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label m_d_title, m_d_cmd;
    Gtk::Grid  m_d_grid;
    std::array<std::array<Gtk::Label, 2>, 9> m_d_kv;

    // Actions (s006).
    Gtk::Box         m_acts{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Label       m_status;
    Gtk::PopoverMenu m_menu;
    std::function<void()> m_on_pick;
};

}  // namespace trails::pages

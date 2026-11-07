#pragma once
#include "core/Units.hpp"

#include <giomm/liststore.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/columnview.h>
#include <gtkmm/filterlistmodel.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/singleselection.h>
#include <gtkmm/sortlistmodel.h>
#include <gtkmm/textview.h>
#include <gtkmm/togglebutton.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// pages::ServicesPage -- the Services tab (s011).
//
//   header   System / User, filter, "All types" (services only by default),
//            counts (running, failed)
//   list     Unit, State, Enabled, Description -- sortable; failed units sort
//            first by default. One UnitItem per unit, updated IN PLACE on
//            each refresh, so scroll and selection hold (as on Processes).
//   detail   the unit, its states, Start / Stop / Restart (confirm, not
//            undoable) and Enable / Disable (undoable) as win.unit-* actions
//            the Shell runs; a status line; the unit's last 40 log lines.
// The page asks Systemd (the app's D-Bus side) for rows and logs; it never
// changes anything itself.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails { class Systemd; }

namespace trails::pages {

class UnitItem : public Glib::Object {
public:
    static Glib::RefPtr<UnitItem> create() { return Glib::make_refptr_for_instance(new UnitItem()); }
    core::UnitRow row;
protected:
    UnitItem();
};

class ServicesPage : public Gtk::Box {
public:
    ServicesPage();
    ~ServicesPage() override;

    void set_systemd(Systemd* s) { m_sd = s; }
    void shown();             // the tab came into view: refresh now
    void tick();              // each new snapshot while visible: refresh every few
    void refresh();           // ask for the list again (async)
    void reload_log();        // the picked unit's journal again

    struct Picked { core::UnitKey key; core::UnitRow row; };
    std::optional<Picked> picked() const;
    void set_status(const std::string& text, bool error);
    void set_on_pick(std::function<void()> f) { m_on_pick = std::move(f); }

private:
    using Fmt = std::function<std::string(const UnitItem&)>;
    using Cmp = std::function<int(const UnitItem&, const UnitItem&)>;
    void build_header();
    void build_view();
    void build_detail();
    void add_column(const char* title, Fmt fmt, Cmp cmp, bool expand, const char* id);
    void apply(std::vector<core::UnitRow> rows, const std::string& err);
    void refilter();
    void restore_selection();
    void paint_detail();
    core::UnitScope scope() const;

    Systemd* m_sd = nullptr;
    std::function<void()> m_on_pick;
    bool m_waiting = false;
    int  m_ticks = 0;

    Gtk::Box          m_header{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Image        m_icon;
    Gtk::Label        m_title, m_count;
    Gtk::ToggleButton m_sys_btn, m_user_btn;
    Gtk::SearchEntry  m_search;
    Gtk::CheckButton  m_all_types;

    Gtk::Paned          m_paned{Gtk::Orientation::VERTICAL};
    Gtk::ScrolledWindow m_scroll;
    Gtk::ColumnView     m_view;
    Glib::RefPtr<Gio::ListStore<UnitItem>> m_store;
    Glib::RefPtr<Gtk::Filter>          m_filter;
    Glib::RefPtr<Gtk::FilterListModel> m_filtered;
    Glib::RefPtr<Gtk::SortListModel>   m_sorted;
    Glib::RefPtr<Gtk::SingleSelection> m_sel;
    Glib::RefPtr<Gtk::ColumnViewColumn> m_state_col;
    std::unordered_map<std::string, Glib::RefPtr<UnitItem>> m_items;
    struct Bound { Glib::RefPtr<UnitItem> item; const Fmt* fmt; };
    std::unordered_map<Gtk::Label*, Bound> m_bound;
    std::vector<std::unique_ptr<Fmt>>      m_fmts;
    std::string m_selected;
    bool m_restoring = false;

    Gtk::Box   m_detail{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label m_d_title, m_d_desc, m_d_state, m_status;
    Gtk::Box   m_acts{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::ScrolledWindow m_log_scroll;
    Gtk::TextView       m_log;
};

}  // namespace trails::pages

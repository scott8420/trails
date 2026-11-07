#pragma once
#include "core/Trail.hpp"
#include "widgets/Timeline.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/grid.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stringlist.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// pages::TrailsPage -- the Trails tab (s010): walk a recording back.
//
//   header      which .trail (newest first; Open... for any other), what it
//               is (host, start, length, ticks), Export CSV..., Copy sqlite3
//   timeline    one metric filled + an optional second as a line, marks,
//               the cursor -- scrub, zoom, pan (widgets::Timeline)
//   at cursor   both metrics' values, and the recorded top processes then
//   marks       every marker; pick one to jump there. Mark at cursor /
//               Rename... / Delete fire win.trail-mark-* -- the Shell runs
//               them through the ONE journal (Shell_record.cpp), so Ctrl+Z
//               undoes them like anything else.
//
// The page owns the open core::Trail; the Shell's AppStore points at it.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::pages {

class TrailsPage : public Gtk::ScrolledWindow {
public:
    TrailsPage();
    ~TrailsPage() override;

    core::Trail& trail() { return m_trail; }
    // The tab came into view: refresh the file list; open the newest if none.
    void shown();
    // Each new snapshot while the tab is visible: a file still being
    // recorded grows, so reload it now and then.
    void tick();
    void refresh_marks();   // after a mark verb, undo or redo
    double cursor() const { return m_timeline.cursor(); }
    std::int64_t selected_mark() const { return m_sel_mark; }   // 0 = none
    void set_status(const std::string& text, bool error);
    void set_window(Gtk::Window* w) { m_window = w; }
    // The Shell says which file is being recorded right now ("" = none).
    void set_live_path(std::function<std::string()> f) { m_live = std::move(f); }

private:
    void build_header();
    void build_body();
    void open_path(const std::string& path);
    void refresh_files();
    void fill_metrics();
    void requery();          // the view moved / a metric changed: series again
    void paint_cursor();     // values + processes at the cursor
    void on_open_dialog();
    void on_export();
    std::string metric_a() const;
    std::string metric_b() const;

    core::Trail m_trail;
    Gtk::Window* m_window = nullptr;
    std::function<std::string()> m_live;
    int m_tick_count = 0;

    Gtk::Box   m_page{Gtk::Orientation::VERTICAL, 18};
    Gtk::Box   m_panel{Gtk::Orientation::VERTICAL, 10};
    Gtk::Box   m_head{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Image m_icon;
    Gtk::Box   m_titles{Gtk::Orientation::VERTICAL, 0};
    Gtk::Label m_title, m_sub, m_value, m_status, m_hint;
    Gtk::Box   m_files{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::DropDown m_file_dd;
    Glib::RefPtr<Gtk::StringList> m_file_model;
    std::vector<std::string> m_file_paths;
    Gtk::Button m_open_btn, m_export_btn, m_copy_btn;
    Gtk::Box   m_pick{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::DropDown m_a_dd, m_b_dd;
    Glib::RefPtr<Gtk::StringList> m_a_model, m_b_model;
    widgets::Timeline m_timeline;

    Gtk::Box   m_lower{Gtk::Orientation::HORIZONTAL, 18};
    Gtk::Box   m_at{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label m_at_title, m_at_a, m_at_b;
    Gtk::Grid  m_procs;
    std::array<std::array<Gtk::Label, 4>, 12> m_proc_rows;
    Gtk::Box   m_marks_box{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label m_marks_title;
    Gtk::ListBox m_marks;
    Gtk::Box   m_mark_btns{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Button m_add_btn, m_rename_btn, m_delete_btn;
    std::vector<core::TrailMark> m_mark_list;
    std::int64_t m_sel_mark = 0;
    bool m_syncing = false;
};

}  // namespace trails::pages

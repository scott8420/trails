#pragma once
#include "widgets/Widgets.hpp"
#include "widgets/Tile.hpp"
#include "pages/CpuPage.hpp"
#include "pages/MemoryPage.hpp"
#include "pages/ProcessesPage.hpp"
#include "pages/DisksPage.hpp"
#include "pages/NetworkPage.hpp"
#include "pages/SensorsPage.hpp"
#include "pages/GpuPage.hpp"
#include "pages/TrailsPage.hpp"
#include "core/Sampler.hpp"
#include "core/Tabs.hpp"
#include "core/ProcActs.hpp"
#include "core/AppStore.hpp"
#include "core/Recorder.hpp"

#include <gtkmm/applicationwindow.h>
#include <gtkmm/flowbox.h>
#include <giomm/menu.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Shell -- the app window, AND the split-class exemplar.
//
// This class is split across multiple TUs by CATEGORY OF CODE, not by feature
// (CANON: "vocabulary first, file layout second" + "the header is the index").
// Every method carries a trailing `// category: ...` tag naming the file its
// body lives in. Read the tag, open that file.
//
// Vocabulary
// ----------
//   Zones    -- construction of named regions: the window frame, header, the
//               tab rail + stack, the Overview tiles, placeholder pages, the
//               menu model.  File: src/Shell_zones.cpp
//   Bindings -- what's wired to what: Gio actions (win.tab(id) and friends).
//               File: src/Shell_bindings.cpp
//   Handlers -- slot bodies (on_*): what a user action actually does.
//               File: src/Shell_handlers.cpp
//   Live     -- the sampler -> screen path: start / stop the Sampler, the
//               frame tick, painting a Snapshot into the tiles (s003).
//               File: src/Shell_live.cpp
//   Acts     -- the verbs that CHANGE something (s006): process actions and
//               undo / redo, through the journal. File: src/Shell_acts.cpp
//   Record   -- the record button (s009): start / stop the Recorder, marks,
//               the header status. File: src/Shell_record.cpp
//   Helpers  -- reusable sync called from the others.  File: src/Shell_helpers.cpp
//   Glue     -- the ctor + build_ui() (the ordered pass). File: src/Shell.cpp
//
// Finding things
// --------------
//   "Where are the tabs built?"           -> Shell_zones.cpp  (build_rail / build_pages)
//   "Where does a tile get its numbers?"  -> Shell_live.cpp   (paint_overview)
//   "How often does the screen refresh?"  -> Shell_live.cpp   (kFrameMs)
//   "What does Alt+3 do?"                 -> core/Shortcuts.cpp -> win.tab::memory
//                                            -> Shell_handlers.cpp (on_tab)
//   "Where does the tab list come from?"  -> core/Tabs.cpp (one list for all of it)
//   "Where is the CPU / Memory tab?"      -> pages/CpuPage, pages/MemoryPage (s004)
//   "What does Terminate / Renice do?"    -> Shell_acts.cpp -> core/ProcActs (the verbs)
//   "Where does a recording go?"          -> Shell_record.cpp -> core/Recorder (SQLite)
//   "What runs as root?"                  -> src/helper/trails_helper.cpp, via Elevate.cpp
// ─────────────────────────────────────────────────────────────────────────────
namespace trails {

class AboutWindow;
class ShortcutsDialog;

class Shell : public Gtk::ApplicationWindow {
public:
    Shell();
    ~Shell() override;

    void build_ui();                             // category: glue

private:
    // ── zones ──────────────────────────────────────────────────────────────
    void build_shell();                          // category: zone: window + rail + stack
    void build_rail();                           // category: zone: the tab rail, from core::tab_registry()
    void build_pages();                          // category: zone: one stack page per tab
    Gtk::Widget& build_overview();               // category: zone: the Overview tiles
    Gtk::Widget& build_placeholder(const core::TabSpec& t);   // category: zone: "arrives in sNNN"
    Glib::RefPtr<Gio::Menu> build_menu();        // category: zone: hamburger model

    // ── bindings ───────────────────────────────────────────────────────────
    void bind_actions();                         // category: bindings: win.* actions

    // ── handlers ───────────────────────────────────────────────────────────
    void on_tab(const std::string& id);          // category: handler: Alt+N / a tile click / the rail
    void on_dump_registry();                     // category: handler: print the live widget tree
    void on_about();                             // category: handler: open the About window
    void on_shortcuts();                         // category: handler: the keyboard reference
    bool on_close();                             // category: handler: window closing -> stop sampling

    // ── live ───────────────────────────────────────────────────────────────
    void start_sampling();                       // category: live: Sampler + frame tick up
    void stop_sampling();                        // category: live: both down (idempotent)
    bool on_frame();                             // category: live: new snapshot? paint it
    void paint_overview(const core::Snapshot& s);   // category: live: tiles from a snapshot

    // ── acts (s006) ────────────────────────────────────────────────────────
    void bind_acts();                            // category: acts: win.proc-*, win.undo / redo
    void sync_acts();                            // category: acts: grey what can't apply now
    void on_proc_stop(bool stop);                // category: acts: Stop / Continue (undoable)
    void on_proc_renice();                       // category: acts: ask, then renice (undoable)
    void on_proc_affinity();                     // category: acts: ask, then pin (undoable)
    void on_proc_end(bool kill);                 // category: acts: confirm, then TERM / KILL
    void on_undo();                              // category: acts: Ctrl+Z
    void on_redo();                              // category: acts: Ctrl+Shift+Z
    void report(const std::string& done, const std::string& err);   // category: acts: status line + log

    // ── record (s009) ──────────────────────────────────────────────────────
    void on_record();                            // category: record: start / stop (Ctrl+R)
    void on_mark();                              // category: record: a mark now (Ctrl+M, undoable)
    void on_open_recordings();                   // category: record: the folder, in Files
    void on_copy_trail_query();                  // category: record: sqlite3 command on the clipboard
    void on_trail_mark_add();                    // category: record: a mark at the Trails cursor (undoable)
    void on_trail_mark_rename();                 // category: record: ask, then rename (undoable)
    void on_trail_mark_delete();                 // category: record: delete the picked mark (undoable)
    void paint_record();                         // category: record: button label, status, greying

    // ── helpers ────────────────────────────────────────────────────────────
    void select_rail(const std::string& id);     // category: helper: rail row follows the stack

    // Titlebar: brand logo (start) + hamburger (end).
    widgets::Button     m_logo_button;
    widgets::MenuButton m_menu_button;
    widgets::Button     m_rec_button;     // s009: "Record" / "Stop"
    widgets::Button     m_mark_button;    // s009: visible while recording
    Gtk::Label          m_rec_status;     // s009: "00:12:34 · 3 marks" or why it stopped
    widgets::Button     m_copy_button;    // s009: copy the sqlite3 command for this / the last .trail
    std::string         m_last_trail;     // the recording now, or the last one made this run
    int                 m_marks_made = 0;
    bool                m_rec_was_on = false;

    // Body: tab rail | stack.
    widgets::Box            m_body;        // horizontal
    widgets::ScrolledWindow m_rail_scroll;
    widgets::ListBox        m_rail;        // one row per tab; row name = tab id
    widgets::Stack          m_stack;       // one page per tab; page name = tab id
    bool                    m_syncing = false;   // rail <-> stack echo guard

    // Overview.
    widgets::ScrolledWindow m_overview_scroll;
    Gtk::FlowBox            m_tiles;
    widgets::Tile           m_tile_cpu, m_tile_mem, m_tile_disk, m_tile_net, m_tile_procs;

    // Built tabs (s004+): each page is its own class with paint(Snapshot).
    pages::CpuPage    m_cpu_page;
    pages::MemoryPage m_mem_page;
    pages::ProcessesPage m_proc_page;
    pages::DisksPage     m_disk_page;     // s007
    pages::NetworkPage   m_net_page;      // s007
    pages::SensorsPage   m_sensor_page;   // s008
    pages::GpuPage       m_gpu_page;      // s008
    pages::TrailsPage    m_trails_page;   // s010

    // Live.
    std::unique_ptr<core::Sampler> m_sampler;
    sigc::connection               m_frame;
    std::uint64_t                  m_painted_seq = 0;

    // Acts (s006, s009): the OS, the knob store over it, the recorder, the ONE
    // app store over both, the ONE journal over that.
    core::SysProcOS     m_proc_os;
    core::ProcKnobStore m_knobs{m_proc_os};
    core::Recorder      m_recorder;
    core::AppStore      m_app{m_knobs, &m_recorder};
    core::AppJournal    m_journal{m_app};
    std::map<std::string, Glib::RefPtr<Gio::SimpleAction>> m_act;

    std::unique_ptr<AboutWindow>     m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;
};

}  // namespace trails

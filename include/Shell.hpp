#pragma once
#include "widgets/Widgets.hpp"
#include "widgets/Tile.hpp"
#include "core/Sampler.hpp"
#include "core/Tabs.hpp"

#include <gtkmm/applicationwindow.h>
#include <gtkmm/flowbox.h>
#include <giomm/menu.h>

#include <cstdint>
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

    // ── helpers ────────────────────────────────────────────────────────────
    void select_rail(const std::string& id);     // category: helper: rail row follows the stack

    // Titlebar: brand logo (start) + hamburger (end).
    widgets::Button     m_logo_button;
    widgets::MenuButton m_menu_button;

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

    // Live.
    std::unique_ptr<core::Sampler> m_sampler;
    sigc::connection               m_frame;
    std::uint64_t                  m_painted_seq = 0;

    std::unique_ptr<AboutWindow>     m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;
};

}  // namespace trails

#include "Shell.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"

// Shell_handlers.cpp -- HANDLERS. Slot bodies: what a user action actually does.
// Each is small and single-concern; orchestration it shares lives in helpers.
//
// Nothing here writes anything, so nothing goes through the undo journal:
// switching tabs is navigation, not a choice that changes anything. The verbs
// that DO change something live in Shell_acts.cpp (s006) and go through
// Journal::run -- RULES "Undo from the first write".

namespace trails {

void Shell::on_tab(const std::string& id) {  // handler: Alt+N / a tile / the rail
    if (!core::find_tab(id)) {
        if (auto lg = log::get(log::Area::Shell)) lg->warn("tab '{}': no such tab", id);
        return;
    }
    m_stack.set_visible_child(id);
    select_rail(id);
    if (id == "trails") m_trails_page.shown();   // s010: newest recording, fresh file list
    if (id == "services") m_services_page.shown();   // s011: the unit list, now
    // Paint at once rather than waiting up to a frame for the next snapshot.
    m_painted_seq = 0;
    on_frame();
}

void Shell::on_dump_registry() {  // handler: print the live widget tree
    registry::dump();
}

void Shell::on_shortcuts() {  // handler: open the keyboard reference
    // Renders by walking core::shortcut_registry() -- the same list App wires
    // the accelerators from, so the two can't drift.
    if (!m_shortcuts) m_shortcuts = std::make_unique<ShortcutsDialog>();
    m_shortcuts->show(*this);
}

void Shell::on_about() {  // handler: open the About window (hide-on-close singleton)
    if (!m_about) m_about = std::make_unique<AboutWindow>(*this);
    m_about->present();
}

bool Shell::on_close() {  // handler: window closing
    // The sampler thread is joined HERE, while the window still exists,
    // rather than whenever the application gets round to deleting it.
    stop_sampling();
    m_recorder.stop();   // s009: close the .trail cleanly (stop marker, WAL folded in)
    return false;   // let it close
}

}  // namespace trails

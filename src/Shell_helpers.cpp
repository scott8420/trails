#include "Shell.hpp"

// Shell_helpers.cpp -- HELPERS. Reusable sync called from zones / bindings /
// handlers / live.

namespace trails {

void Shell::select_rail(const std::string& id) {  // helper: rail row follows the stack
    m_syncing = true;   // selecting a row must not re-enter on_tab
    for (auto* w = m_rail.get_first_child(); w; w = w->get_next_sibling()) {
        auto* row = dynamic_cast<Gtk::ListBoxRow*>(w);
        if (row && std::string(row->get_name()) == id) { m_rail.select_row(*row); break; }
    }
    m_syncing = false;
}

}  // namespace trails

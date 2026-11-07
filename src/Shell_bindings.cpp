#include "Shell.hpp"
#include "Log.hpp"

#include <glibmm/variant.h>

// Shell_bindings.cpp -- BINDINGS. What's wired to what: the win.* actions. No
// slot bodies here -- those are handlers; this file only wires. The keys for
// these actions are NOT here either: App::on_startup wires them from
// core::shortcut_registry(), the list the Shortcuts window renders from.

namespace trails {

void Shell::bind_actions() {  // bindings: win.* actions
    add_action("dump-registry", sigc::mem_fun(*this, &Shell::on_dump_registry));
    add_action("about",         sigc::mem_fun(*this, &Shell::on_about));
    add_action("shortcuts",     sigc::mem_fun(*this, &Shell::on_shortcuts));
    add_action("quit",          [this]() { close(); });

    // win.tab(<id>): ONE parameterised action for every tab, so Alt+N
    // ("win.tab::cpu"), a tile click and the rail all go through on_tab.
    add_action_with_parameter(
        "tab", Glib::Variant<Glib::ustring>::variant_type(),
        [this](const Glib::VariantBase& param) {
            on_tab(Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(param).get());
        });

    signal_close_request().connect(sigc::mem_fun(*this, &Shell::on_close), false);
}

}  // namespace trails

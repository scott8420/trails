#include "Shell.hpp"
#include "AboutWindow.hpp"      // complete type for ~unique_ptr<AboutWindow> in ~Shell()
#include "ShortcutsDialog.hpp"  // ditto -- same singleton shape, same dtor requirement
#include "Log.hpp"

// Shell.cpp -- GLUE. The ctor and the ordered build_ui() pass live here; every
// other method body lives in a category file (see the index in Shell.hpp).

namespace trails {

Shell::Shell()
    : m_logo_button("shell.logo_button"),
      m_menu_button("shell.menu_button"),
      m_body("shell.body", Gtk::Orientation::HORIZONTAL),
      m_rail_scroll("shell.rail_scroll"),
      m_rail("shell.rail"),
      m_stack("shell.stack"),
      m_overview_scroll("overview.scroll"),
      m_tile_cpu("overview.cpu", "CPU", "trails-tab-cpu-symbolic", "tab-cpu"),
      m_tile_mem("overview.memory", "Memory", "trails-tab-memory-symbolic", "tab-memory"),
      m_tile_disk("overview.disks", "Disks", "trails-tab-disks-symbolic", "tab-disks"),
      m_tile_net("overview.network", "Network", "trails-tab-network-symbolic", "tab-network"),
      m_tile_procs("overview.processes", "Processes", "trails-tab-processes-symbolic",
                   "tab-processes") {
    // m_cpu_page / m_mem_page default-construct (they name themselves).
    set_name("shell.window");
}

Shell::~Shell() { stop_sampling(); }

// The ordered pass: construct the shell, build the pages, bind actions, then
// start the sampler -- last, so the first frame finds every tile built.
void Shell::build_ui() {
    build_shell();     // zone
    build_pages();     // zone
    bind_actions();    // bindings
    start_sampling();  // live
    on_tab("overview");

    if (auto lg = log::get(log::Area::Shell)) lg->info("ready");
}

}  // namespace trails

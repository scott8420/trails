#pragma once
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core::tabs -- the ONE list of Trails' tabs. The rail, the stack pages, the
// per-tab colour classes, the bundled icons and the Alt+N shortcuts are all
// read from it, so adding a tab is one row here (plus its icon file). GTK-free
// so the selftest checks it: ids unique, every tab has an icon and a colour
// class, every tab is reachable from the keyboard.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::core {

struct TabSpec {
    std::string_view id;        // stack page name, action target: "cpu"
    std::string_view title;     // rail label: "CPU"
    std::string_view icon;      // bundled symbolic icon: "trails-tab-cpu-symbolic"
    std::string_view css;       // colour class: "tab-cpu" (trails.css)
    std::string_view arrives;   // "" = built; else the session that builds it ("s004")
    std::string_view blurb;     // one line under the placeholder heading
};

const std::vector<TabSpec>& tab_registry();
const TabSpec* find_tab(std::string_view id);

}  // namespace trails::core

#include "Registry.hpp"
#include "Log.hpp"

#include <gtkmm/root.h>
#include <gtkmm/widget.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace trails::registry {

namespace {
// name -> widget. A vector-of-pairs would do at this size, but the map keeps
// find() O(1) and mirrors what a bigger project reaches for.
std::unordered_map<std::string, Gtk::Widget*>& table() {
    static std::unordered_map<std::string, Gtk::Widget*> t;
    return t;
}
}  // namespace

void add(std::string_view name, Gtk::Widget* w) {
    auto& t = table();
    const std::string key(name);
    if (auto it = t.find(key); it != t.end() && it->second != w) {
        // Duplicate live name: the s191 self-rebuild-collision tripwire. Warn,
        // don't crash -- a seed should survive its own teaching mistakes.
        if (auto lg = log::get(log::Area::Registry))
            lg->warn("duplicate widget name '{}' -- overwriting the older entry", key);
    }
    t[key] = w;
    if (auto lg = log::get(log::Area::Registry))
        lg->trace("+ {} ({} live)", key, t.size());
}

void remove(Gtk::Widget* w) {
    auto& t = table();
    for (auto it = t.begin(); it != t.end(); ++it) {
        if (it->second == w) { t.erase(it); return; }
    }
}

Gtk::Widget* find(std::string_view name) {
    auto& t = table();
    auto it = t.find(std::string(name));
    return it == t.end() ? nullptr : it->second;
}

namespace {
// Log mode (3b, the Processes page overflow): each widget's width request --
// minimum / natural -- and what it was given, so "who widens the page" is a
// fact in the log, not a guess. Ctrl+D (win.dump-registry).
void dump_sizes(Gtk::Widget* w, int depth, const std::shared_ptr<spdlog::logger>& lg) {
    if (!w || depth > 9) return;
    int mn = 0, nat = 0, mb = 0, nb = 0;
    w->measure(Gtk::Orientation::HORIZONTAL, -1, mn, nat, mb, nb);
    std::string cls;
    for (const auto& c : w->get_css_classes()) cls += "." + std::string(c);
    double x = 0, y = 0;
    if (auto* root = dynamic_cast<Gtk::Widget*>(w->get_root())) w->translate_coordinates(*root, 0, 0, x, y);
    lg->info("{:>{}}{} {} min {} nat {} got {} at x {}", "", depth * 2, G_OBJECT_TYPE_NAME(w->gobj()), cls, mn, nat,
             w->get_width(), int(x));
    for (auto* c = w->get_first_child(); c; c = c->get_next_sibling()) dump_sizes(c, depth + 1, lg);
}
}  // namespace

void dump() {
    auto& t = table();
    std::vector<std::string> names;
    names.reserve(t.size());
    for (const auto& [k, _] : t) names.push_back(k);
    // Sorted so the dump reads the same run to run.
    std::sort(names.begin(), names.end());
    auto lg = log::get(log::Area::Registry);
    if (lg) lg->info("registry: {} live widget(s)", names.size());
    for (const auto& n : names)
        if (lg) lg->info("  {}", n);
    // The visible page's widget tree with sizes (pages register as "page.*").
    if (lg)
        for (const auto& n : names)
            if (n.rfind("page.", 0) == 0)
                if (auto* w = t[n]; w && w->get_mapped()) {
                    lg->info("sizes of {}:", n);
                    if (auto* root = dynamic_cast<Gtk::Widget*>(w->get_root())) dump_sizes(root, 0, lg);
                    break;
                }
}

std::size_t size() { return table().size(); }

}  // namespace trails::registry

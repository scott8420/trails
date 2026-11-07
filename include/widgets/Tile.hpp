#pragma once
#include "widgets/Graph.hpp"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>

#include <string>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
// widgets::Tile -- one Overview tile: big coloured icon + title, a large value,
// a sub-line, and a Graph. It is a Button so the whole tile is the click
// target (opens its tab) and keyboard-focusable for free. Carries the tab's
// colour class, so icon, value and graph all pick up the tab colour from CSS.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::widgets {

class Tile : public Gtk::Button {
public:
    // name: "overview.cpu"; icon: a bundled icon name; css: "tab-cpu".
    Tile(std::string_view name, std::string_view title, std::string_view icon,
         std::string_view css);
    ~Tile() override;

    void set_value(const std::string& text);    // the big line: "23%"
    void set_detail(const std::string& text);   // the small line under it
    Graph& graph() { return m_graph; }
    void   set_has_graph(bool on) { m_graph.set_visible(on); }

private:
    Gtk::Box   m_box{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box   m_head{Gtk::Orientation::HORIZONTAL, 10};
    Gtk::Image m_icon;
    Gtk::Label m_title;
    Gtk::Label m_value;
    Gtk::Label m_detail;
    Graph      m_graph;
};

}  // namespace trails::widgets

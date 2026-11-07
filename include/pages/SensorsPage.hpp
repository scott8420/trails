#pragma once
#include "core/Sampler.hpp"
#include "widgets/Bar.hpp"
#include "widgets/Graph.hpp"

#include <gtkmm/box.h>
#include <gtkmm/grid.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// pages::SensorsPage -- the Sensors & Power tab (s008).
//
//   header panel   the hottest working temperature, and its 5-minute graph
//   battery        charge (bar), state, draw in W, time to empty / full,
//                  energy, health vs design, cycles; draw graph. Adapters and
//                  USB-C sources as one line.
//   one card per chip NAME   (six tas2764 amps = one card, rows numbered)
//                  every sensor the chip offers: label, value, fan range, and
//                  a sparkline for temperatures, fans and power. Chips with
//                  no sensors (tps6598x) are left out.
// Everything is data-driven from /sys: no chip is special-cased beyond its
// title (probe::chip_title).
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::pages {

class SensorsPage : public Gtk::ScrolledWindow {
public:
    SensorsPage();
    ~SensorsPage() override;
    void paint(const core::Snapshot& s);

private:
    struct Row {
        std::string chip_id, key, kind, prefix;   // prefix: "2 · " when the card holds several chips
        Gtk::Label  name, value, extra;
        std::unique_ptr<widgets::Graph> spark;
    };
    struct Card {
        Gtk::Box   panel{Gtk::Orientation::VERTICAL, 8};
        Gtk::Label title;
        Gtk::Grid  grid;
        std::vector<std::unique_ptr<Row>> rows;
    };
    struct Batt {
        explicit Batt(const std::string& n) : bar(n + ".bar", 14), graph(n + ".graph") {}
        std::string    name;
        Gtk::Box       panel{Gtk::Orientation::VERTICAL, 8};
        Gtk::Box       head{Gtk::Orientation::HORIZONTAL, 12};
        Gtk::Box       titles{Gtk::Orientation::VERTICAL, 0};
        Gtk::Label     title, sub, value, line1, line2;
        widgets::Bar   bar;
        widgets::Graph graph;
    };
    void rebuild(const core::Snapshot& s, const std::string& layout);

    Gtk::Box       m_page{Gtk::Orientation::VERTICAL, 18};
    Gtk::Box       m_panel{Gtk::Orientation::VERTICAL, 8};
    Gtk::Box       m_head{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Image     m_icon;
    Gtk::Box       m_titles{Gtk::Orientation::VERTICAL, 0};
    Gtk::Label     m_title, m_sub, m_value, m_sources;
    widgets::Graph m_graph;

    Gtk::Box                           m_dyn{Gtk::Orientation::VERTICAL, 18};
    std::vector<std::unique_ptr<Batt>> m_batts;
    std::vector<std::unique_ptr<Card>> m_cards;
    std::string                        m_layout;
};

}  // namespace trails::pages

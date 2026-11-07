#pragma once
#include "core/Sampler.hpp"
#include "widgets/Bar.hpp"
#include "widgets/Graph.hpp"

#include <gtkmm/box.h>
#include <gtkmm/grid.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

#include <array>
#include <memory>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// pages::MemoryPage -- the Memory tab (s004).
//
//   header panel   used of total, a stacked bar (used | cache | free), and a
//                  graph: used % filled, cache % as the line
//   breakdown      used, available, cached, buffers, shared, free
//   swap           each device (used / size, bar); zram as stored -> compressed
//                  and what it costs in RAM
//   top memory     the ten largest resident processes
//
// "Used" = total - available (free(1), btop); see core::probe::MemInfo.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::pages {

class MemoryPage : public Gtk::ScrolledWindow {
public:
    MemoryPage();
    ~MemoryPage() override;
    void paint(const core::Snapshot& s);

private:
    static constexpr int kTop = 10;
    struct KV { Gtk::Label key, val; };

    Gtk::Box       m_page{Gtk::Orientation::VERTICAL, 18};
    Gtk::Box       m_panel{Gtk::Orientation::VERTICAL, 8};
    Gtk::Box       m_head{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Image     m_icon;
    Gtk::Box       m_titles{Gtk::Orientation::VERTICAL, 0};
    Gtk::Label     m_title, m_sub, m_value;
    widgets::Bar   m_bar;
    widgets::Graph m_graph;

    Gtk::Label             m_break_title;
    Gtk::Grid              m_break;
    std::array<KV, 6>      m_kv;

    Gtk::Label             m_swap_title;
    Gtk::Box               m_swaps{Gtk::Orientation::VERTICAL, 8};
    std::string            m_swap_layout;
    std::vector<Gtk::Label*>   m_swap_labels;
    std::vector<widgets::Bar*> m_swap_bars;
    std::vector<std::unique_ptr<widgets::Bar>> m_bar_store;

    Gtk::Label             m_top_title;
    Gtk::Grid              m_top;
    std::array<std::array<Gtk::Label, 4>, kTop> m_rows;   // name, pid, rss, % of RAM
};

}  // namespace trails::pages

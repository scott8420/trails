#pragma once
#include "core/Sampler.hpp"
#include "widgets/Graph.hpp"

#include <gtkmm/box.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// pages::CpuPage -- the CPU tab (s004).
//
//   header panel   model, total %, user / system / iowait, avg GHz, load, and a
//                  tall graph: total filled, system as the line
//   per cluster    "Performance · 8 cores · up to 3.23 GHz", then a card per
//                  core: cpuN, %, current GHz, a mini graph
//
// Clusters come from the Snapshot (core::probe::clusters_from -- grouped by
// max frequency, so Apple Silicon's P and E cores split on their own). The
// core cards are built on the first snapshot and rebuilt only if the core /
// cluster layout changes (hotplug); otherwise paint() only sets values.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::pages {

class CpuPage : public Gtk::ScrolledWindow {
public:
    CpuPage();
    ~CpuPage() override;
    void paint(const core::Snapshot& s);

private:
    struct CoreCard {
        Gtk::Box         box{Gtk::Orientation::VERTICAL, 2};
        Gtk::Box         head{Gtk::Orientation::HORIZONTAL, 6};
        Gtk::Label       name, pct, mhz;
        widgets::Graph   graph;
        explicit CoreCard(const std::string& id);
    };
    void rebuild(const core::Snapshot& s);

    Gtk::Box       m_page{Gtk::Orientation::VERTICAL, 18};
    Gtk::Box       m_panel{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box       m_head{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Image     m_icon;
    Gtk::Box       m_titles{Gtk::Orientation::VERTICAL, 0};
    Gtk::Label     m_title, m_model, m_value, m_detail, m_load;
    widgets::Graph m_graph;
    Gtk::Box       m_clusters{Gtk::Orientation::VERTICAL, 12};

    std::string                            m_layout;   // signature of the built layout
    std::vector<std::unique_ptr<CoreCard>> m_cards;    // index = core index in the snapshot
    std::vector<Gtk::Label*>               m_cluster_heads;
};

}  // namespace trails::pages

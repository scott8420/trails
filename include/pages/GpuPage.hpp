#pragma once
#include "core/Sampler.hpp"
#include "widgets/Bar.hpp"
#include "widgets/Graph.hpp"

#include <gtkmm/box.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// pages::GpuPage -- the GPU tab (s008).
//
//   header panel   busiest engine %, graph -- or a plain "not reported" line
//   one card per DRM card
//                  driver, render node; busy % + VRAM bars where sysfs has
//                  them (amdgpu); engine time from DRM fdinfo for this
//                  driver; displays (connectors: connected first, mode).
//
// Asahi's AGX exposes no busy counter in sysfs. If its driver prints
// drm-engine-* in fdinfo the engine rows fill in; if not, the tab says so
// rather than drawing a flat zero that would look like an idle GPU.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::pages {

class GpuPage : public Gtk::ScrolledWindow {
public:
    GpuPage();
    ~GpuPage() override;
    void paint(const core::Snapshot& s);

private:
    struct Card {
        explicit Card(const std::string& n) : busy(n + ".busy", 12), vram(n + ".vram", 12) {}
        std::string  name;
        Gtk::Box     panel{Gtk::Orientation::VERTICAL, 6};
        Gtk::Label   title, sub, busy_l, vram_l, engines, displays;
        widgets::Bar busy, vram;
    };
    void rebuild(const core::Snapshot& s, const std::string& layout);

    Gtk::Box       m_page{Gtk::Orientation::VERTICAL, 18};
    Gtk::Box       m_panel{Gtk::Orientation::VERTICAL, 8};
    Gtk::Box       m_head{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Image     m_icon;
    Gtk::Box       m_titles{Gtk::Orientation::VERTICAL, 0};
    Gtk::Label     m_title, m_sub, m_value, m_note;
    widgets::Graph m_graph;

    Gtk::Box                           m_cards{Gtk::Orientation::VERTICAL, 18};
    std::vector<std::unique_ptr<Card>> m_store;
    std::string                        m_layout;
};

}  // namespace trails::pages

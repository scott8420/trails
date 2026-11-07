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
// pages::DisksPage -- the Disks tab (s007).
//
//   header panel   total read / write, one graph (read filled, write line)
//   one card per disk
//                  model, size, SSD / HDD; busy %; read / write graph; and
//                  under it every FILESYSTEM that lives on it: mount point,
//                  type, used of size (bar), the device, and for btrfs the
//                  subvolumes (/root -> /, /home -> /home) -- one volume, one
//                  bar, however many places it is mounted.
//   Other filesystems   whatever didn't map to a listed disk.
//
// Cards are rebuilt only when the disk / filesystem list changes; every other
// tick just repaints the numbers.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::pages {

class DisksPage : public Gtk::ScrolledWindow {
public:
    DisksPage();
    ~DisksPage() override;
    void paint(const core::Snapshot& s);

private:
    struct FsRow {
        explicit FsRow(const std::string& name) : bar(name, 10) {}
        Gtk::Box     box{Gtk::Orientation::VERTICAL, 3};
        Gtk::Label   text, sub;
        widgets::Bar bar;
    };
    struct Card {
        explicit Card(const std::string& name) : graph(name + ".graph") {}
        Gtk::Box       panel{Gtk::Orientation::VERTICAL, 8};
        Gtk::Box       head{Gtk::Orientation::HORIZONTAL, 12};
        Gtk::Box       titles{Gtk::Orientation::VERTICAL, 0};
        Gtk::Label     title, sub, value, rates;
        widgets::Graph graph;
        Gtk::Box       fs{Gtk::Orientation::VERTICAL, 10};
        std::string    disk;   // "" = Other filesystems
        std::vector<std::unique_ptr<FsRow>> rows;
    };
    void rebuild(const core::Snapshot& s, const std::string& layout);

    Gtk::Box       m_page{Gtk::Orientation::VERTICAL, 18};
    Gtk::Box       m_panel{Gtk::Orientation::VERTICAL, 8};
    Gtk::Box       m_head{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Image     m_icon;
    Gtk::Box       m_titles{Gtk::Orientation::VERTICAL, 0};
    Gtk::Label     m_title, m_sub, m_value;
    widgets::Graph m_graph;

    Gtk::Box                           m_cards{Gtk::Orientation::VERTICAL, 18};
    std::vector<std::unique_ptr<Card>> m_card_store;
    std::vector<FsRow*>                m_fs_rows;     // one per Snapshot::filesystems entry
    std::string                        m_layout;
};

}  // namespace trails::pages

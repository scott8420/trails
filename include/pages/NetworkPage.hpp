#pragma once
#include "NetMgr.hpp"
#include "core/Sampler.hpp"
#include "widgets/Graph.hpp"

#include <gtkmm/box.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// pages::NetworkPage -- the Network tab (s007).
//
//   header panel   total down / up (loopback excluded), one graph
//   one card per interface
//                  kind (Wi-Fi, wired, virtual), state, Wi-Fi signal (dBm,
//                  from /proc/net/wireless) or link speed; down / up graph;
//                  totals since boot; addresses (IPv4, IPv6); MAC, MTU.
//   Order: physical links that are up, then down ones, then virtual
//   (docker0, veth, tun) that are UP -- a down virtual link is left out
//   (s014). Loopback only when there is nothing else.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::pages {

class NetworkPage : public Gtk::ScrolledWindow {
public:
    NetworkPage();
    ~NetworkPage() override;
    void paint(const core::Snapshot& s);

private:
    struct Card {
        explicit Card(const std::string& name) : graph(name + ".graph") {}
        Gtk::Box       panel{Gtk::Orientation::VERTICAL, 8};
        Gtk::Box       head{Gtk::Orientation::HORIZONTAL, 12};
        Gtk::Box       titles{Gtk::Orientation::VERTICAL, 0};
        Gtk::Label     title, sub, value, rates, addrs, hw;
        widgets::Graph graph;
        std::size_t    index = 0;   // into Snapshot::net / links
    };
    void rebuild(const core::Snapshot& s, const std::vector<std::size_t>& order, const std::string& layout);

    Gtk::Box       m_page{Gtk::Orientation::VERTICAL, 18};
    Gtk::Box       m_panel{Gtk::Orientation::VERTICAL, 8};
    Gtk::Box       m_head{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Image     m_icon;
    Gtk::Box       m_titles{Gtk::Orientation::VERTICAL, 0};
    Gtk::Label     m_title, m_sub, m_value;
    widgets::Graph m_graph;

    Gtk::Box                           m_cards{Gtk::Orientation::VERTICAL, 18};
    std::vector<std::unique_ptr<Card>> m_card_store;
    std::string                        m_layout;
    NetMgr                             m_nm;   // s015: SSID / band / bitrate for Wi-Fi
};

}  // namespace trails::pages

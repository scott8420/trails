#include "pages/NetworkPage.hpp"
#include "Registry.hpp"
#include "core/Format.hpp"

#include <cstdio>

namespace trails::pages {

namespace fmt = core::fmt;

namespace {
void set(Gtk::Label& l, const std::string& t) {
    if (std::string(l.get_text()) != t) l.set_text(t);
}
std::vector<float> hist(const core::Snapshot& s, const std::string& id) {
    const auto* r = s.find(id);
    return r ? r->to_vector() : std::vector<float>{};
}
std::size_t cap(const core::Snapshot& s, const std::string& id) {
    const auto* r = s.find(id);
    return r ? r->capacity() : 300;
}
std::string dbm(double v) {
    char b[24];
    std::snprintf(b, sizeof b, "%.0f dBm", v);
    return b;
}
// The words a person uses for a signal level (NetworkManager's rough bands).
const char* signal_word(double level) {
    return level >= -55 ? "excellent" : level >= -67 ? "good" : level >= -75 ? "fair" : "weak";
}
std::string kind(const core::LinkView& l) {
    if (l.link.wireless) return "Wi-Fi";
    if (l.link.virtual_if) return "virtual";
    return "wired";
}
bool up(const core::LinkView& l) { return l.link.operstate == "up" || l.link.operstate == "unknown"; }

// Physical up, physical down, virtual; loopback only if it's all there is.
std::vector<std::size_t> order_of(const core::Snapshot& s) {
    std::vector<std::size_t> a, b, c, lo;
    for (std::size_t i = 0; i < s.net.size() && i < s.links.size(); ++i) {
        const auto& l = s.links[i];
        if (s.net[i].loopback) lo.push_back(i);
        // s014: a virtual link that is down (ifb0, an idle docker0) says nothing.
        else if (l.link.virtual_if) { if (up(l)) c.push_back(i); }
        else if (l.link.operstate == "up") a.push_back(i);
        else b.push_back(i);
    }
    a.insert(a.end(), b.begin(), b.end());
    a.insert(a.end(), c.begin(), c.end());
    return a.empty() ? lo : a;
}
}  // namespace

NetworkPage::NetworkPage() : m_graph("network.graph") {
    set_name("page.network");
    registry::add("page.network", this);
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_page.add_css_class("page");
    m_page.add_css_class("tab-network");

    m_panel.add_css_class("panel");
    m_icon.set_from_icon_name("trails-hero-network");
    m_icon.set_pixel_size(56);
    m_icon.add_css_class("tile-icon");
    m_title.set_text("Network");
    m_title.add_css_class("panel-title");
    m_title.set_xalign(0.0f);
    m_sub.add_css_class("dim-label");
    m_sub.set_xalign(0.0f);
    m_titles.append(m_title);
    m_titles.append(m_sub);
    m_titles.set_hexpand(true);
    m_value.add_css_class("tile-value");
    m_value.set_valign(Gtk::Align::CENTER);
    m_value.set_text("--");
    m_head.append(m_icon);
    m_head.append(m_titles);
    m_head.append(m_value);
    m_graph.set_content_height(120);
    m_graph.set_scale(widgets::Graph::Scale::Auto, 64.0 * 1024.0);   // 64 KiB/s floor
    m_graph.set_tooltip_text("Download (filled) and upload (line), loopback excluded");
    m_panel.append(m_head);
    m_panel.append(m_graph);
    m_page.append(m_panel);
    m_page.append(m_cards);
    set_child(m_page);
}

NetworkPage::~NetworkPage() { registry::remove(this); }

void NetworkPage::rebuild(const core::Snapshot& s, const std::vector<std::size_t>& order,
                          const std::string& layout) {
    while (auto* c = m_cards.get_first_child()) m_cards.remove(*c);
    m_card_store.clear();
    for (std::size_t i : order) {
        auto c = std::make_unique<Card>("network." + s.net[i].name);
        c->index = i;
        c->panel.add_css_class("panel");
        c->title.add_css_class("section-title");
        c->title.set_xalign(0.0f);
        c->sub.add_css_class("dim-label");
        c->sub.set_xalign(0.0f);
        c->titles.append(c->title);
        c->titles.append(c->sub);
        c->titles.set_hexpand(true);
        c->value.add_css_class("tile-value");
        c->value.set_valign(Gtk::Align::CENTER);
        c->head.append(c->titles);
        c->head.append(c->value);
        c->graph.set_content_height(70);
        c->graph.set_scale(widgets::Graph::Scale::Auto, 64.0 * 1024.0);
        c->graph.set_tooltip_text("Download (filled) and upload (line)");
        for (Gtk::Label* l : {&c->rates, &c->addrs, &c->hw}) {
            l->set_xalign(0.0f);
            l->set_wrap(true);
            l->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
        }
        c->addrs.set_selectable(true);
        c->hw.add_css_class("dim-label");
        c->panel.append(c->head);
        c->panel.append(c->graph);
        c->panel.append(c->rates);
        c->panel.append(c->addrs);
        c->panel.append(c->hw);
        m_cards.append(c->panel);
        m_card_store.push_back(std::move(c));
    }
    m_layout = layout;
}

void NetworkPage::paint(const core::Snapshot& s) {
    const auto order = order_of(s);
    std::string layout;
    for (std::size_t i : order) layout += s.net[i].name + ";";
    if (layout != m_layout) rebuild(s, order, layout);

    set(m_value, fmt::rate(s.net_rx_bps));
    set(m_sub, "Down " + fmt::rate(s.net_rx_bps) + " · Up " + fmt::rate(s.net_tx_bps) + " · " +
               std::to_string(order.size()) + (order.size() == 1 ? " interface" : " interfaces"));
    m_graph.set_series(hist(s, "net.rx"), hist(s, "net.tx"), cap(s, "net.rx"));

    for (auto& cp : m_card_store) {
        Card& c = *cp;
        if (c.index >= s.net.size() || c.index >= s.links.size()) continue;
        const auto& n = s.net[c.index];
        const auto& l = s.links[c.index];
        set(c.title, n.name);
        std::string sub = kind(l) + " · " + (l.link.operstate.empty() ? "?" : l.link.operstate);
        if (l.has_signal)
            sub += " · signal " + dbm(l.level_dbm) + " (" + signal_word(l.level_dbm) + ")";
        else if (l.link.speed_mbps > 0)
            sub += " · " + (l.link.speed_mbps >= 1000 && l.link.speed_mbps % 1000 == 0
                                ? std::to_string(l.link.speed_mbps / 1000) + " Gb/s"
                                : std::to_string(l.link.speed_mbps) + " Mb/s");
        set(c.sub, sub);
        set(c.value, up(l) ? fmt::rate(n.rx_bps) : "--");
        set(c.rates, "Down " + fmt::rate(n.rx_bps) + " · Up " + fmt::rate(n.tx_bps) + "  ·  received " +
                     fmt::bytes(n.rx_total) + ", sent " + fmt::bytes(n.tx_total) + " since boot");
        std::string a;
        for (const auto& ad : l.addrs) {
            if (!a.empty()) a += "\n";
            a += (ad.v6 ? "IPv6  " : "IPv4  ") + ad.addr + "/" + std::to_string(ad.prefix);
        }
        set(c.addrs, a.empty() ? "No address" : a);
        std::string hw;
        if (!l.link.mac.empty()) hw = "MAC " + l.link.mac;
        if (l.link.mtu) hw += (hw.empty() ? "" : " · ") + std::string("MTU ") + std::to_string(l.link.mtu);
        set(c.hw, hw);
        c.graph.set_series(hist(s, "net." + n.name + ".rx"), hist(s, "net." + n.name + ".tx"),
                           cap(s, "net." + n.name + ".rx"));
    }
}

}  // namespace trails::pages

#include "widgets/Tile.hpp"
#include "Registry.hpp"

namespace trails::widgets {

Tile::Tile(std::string_view name, std::string_view title, std::string_view icon,
           std::string_view css)
    : m_graph(std::string(name) + ".graph") {
    set_name(std::string(name));
    registry::add(name, this);
    add_css_class("tile");
    add_css_class(std::string(css));
    set_hexpand(true);

    m_icon.set_from_icon_name(std::string(icon));
    m_icon.set_pixel_size(44);   // 3b: a Rouster hero orb (full colour)
    m_icon.add_css_class("tile-icon");
    m_title.set_text(std::string(title));
    m_title.add_css_class("tile-title");
    m_title.set_xalign(0.0f);
    m_head.append(m_icon);
    m_head.append(m_title);

    m_value.add_css_class("tile-value");
    m_value.set_xalign(0.0f);
    m_value.set_text("--");
    m_detail.add_css_class("tile-detail");
    m_detail.set_xalign(0.0f);
    m_detail.set_ellipsize(Pango::EllipsizeMode::END);

    m_box.append(m_head);
    m_box.append(m_value);
    m_box.append(m_detail);
    m_box.append(m_graph);
    set_child(m_box);
}

Tile::~Tile() { registry::remove(this); }

void Tile::set_value(const std::string& text) {
    if (std::string(m_value.get_text()) != text) m_value.set_text(text);
}
void Tile::set_detail(const std::string& text) {
    if (std::string(m_detail.get_text()) != text) m_detail.set_text(text);
}

}  // namespace trails::widgets

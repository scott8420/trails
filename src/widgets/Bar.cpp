#include "widgets/Bar.hpp"
#include "Registry.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace trails::widgets {

Bar::Bar(std::string_view name, int height) {
    set_name(std::string(name));
    registry::add(name, this);
    add_css_class("bar");
    set_content_height(height);
    set_hexpand(true);
    set_draw_func(sigc::mem_fun(*this, &Bar::draw));
}

Bar::~Bar() { registry::remove(this); }

void Bar::set_parts(std::vector<Part> parts) {
    m_parts = std::move(parts);
    queue_draw();
}

void Bar::draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    const Gdk::RGBA c = get_color();
    const double r = h / 2.0;
    // Rounded track, clipped so the parts inherit its ends.
    cr->begin_new_path();
    cr->arc(r, r, r, M_PI / 2, 3 * M_PI / 2);
    cr->arc(w - r, r, r, -M_PI / 2, M_PI / 2);
    cr->close_path();
    cr->clip_preserve();
    cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), 0.10);
    cr->fill();
    double x = 0;
    for (const auto& p : m_parts) {
        const double pw = std::clamp(p.fraction, 0.0, 1.0) * w;
        cr->rectangle(x, 0, pw, h);
        cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), p.alpha);
        cr->fill();
        x += pw;
    }
}

}  // namespace trails::widgets

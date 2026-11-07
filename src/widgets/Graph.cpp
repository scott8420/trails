#include "widgets/Graph.hpp"
#include "Registry.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace trails::widgets {

Graph::Graph(std::string_view name) {
    set_name(std::string(name));
    registry::add(name, this);
    add_css_class("graph");
    set_content_height(64);
    set_hexpand(true);
    set_draw_func(sigc::mem_fun(*this, &Graph::draw));
}

Graph::~Graph() { registry::remove(this); }

void Graph::set_series(std::vector<float> a, std::vector<float> b, std::size_t capacity) {
    m_a = std::move(a);
    m_b = std::move(b);
    m_capacity = std::max<std::size_t>(2, capacity);
    queue_draw();
}

namespace {
// 1, 2, 5 x 10^n at or above v: a top edge that reads as a round number.
double nice_ceiling(double v) {
    if (v <= 0) return 1;
    const double p = std::pow(10.0, std::floor(std::log10(v)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * p >= v) return m * p;
    return 10 * p;
}
}  // namespace

void Graph::draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    const Gdk::RGBA c = get_color();   // the skin's colour for this graph
    const double W = w, H = h;

    // Scale.
    if (m_scale == Scale::Percent) {
        m_top = 100.0;
    } else {
        double mx = 0;
        for (float v : m_a) mx = std::max(mx, double(v));
        for (float v : m_b) mx = std::max(mx, double(v));
        m_top = nice_ceiling(std::max(mx, m_floor));
    }

    // Quarter grid lines, faint.
    cr->set_line_width(1.0);
    cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), 0.10);
    for (int i = 1; i < 4; ++i) {
        const double y = std::round(H * i / 4.0) + 0.5;
        cr->move_to(0, y);
        cr->line_to(W, y);
    }
    cr->stroke();

    const double step = W / double(m_capacity - 1);
    auto px = [&](std::size_t i, std::size_t n) { return W - double(n - 1 - i) * step; };
    auto py = [&](float v) {
        const double f = std::clamp(double(v) / m_top, 0.0, 1.0);
        return H - 1.0 - f * (H - 2.0);
    };
    auto path = [&](const std::vector<float>& s) {
        const std::size_t n = std::min(s.size(), m_capacity);
        const std::size_t off = s.size() - n;
        for (std::size_t i = 0; i < n; ++i) {
            const double x = px(i, n), y = py(s[off + i]);
            if (i == 0) cr->move_to(x, y); else cr->line_to(x, y);
        }
        return n;
    };

    cr->set_line_join(Cairo::Context::LineJoin::ROUND);
    if (m_a.size() >= 2) {
        const std::size_t n = path(m_a);
        cr->line_to(W, H);
        cr->line_to(px(0, n), H);
        cr->close_path();
        cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), 0.22);
        cr->fill();
        path(m_a);
        cr->set_line_width(2.0);
        cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), 1.0);
        cr->stroke();
    }
    if (m_b.size() >= 2) {
        path(m_b);
        cr->set_line_width(1.5);
        cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), 0.55);
        cr->stroke();
    }
}

}  // namespace trails::widgets

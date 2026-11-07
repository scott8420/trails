#include "widgets/Timeline.hpp"
#include "Registry.hpp"

#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/gesturedrag.h>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace trails::widgets {

namespace {
constexpr double kTop = 20, kBottom = 20;   // room for mark text above, times below
constexpr double kMinView = 5;              // seconds: the deepest zoom

double nice_ceiling(double v) {
    if (v <= 0) return 1;
    const double p = std::pow(10.0, std::floor(std::log10(v)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * p >= v) return m * p;
    return 10 * p;
}
std::string clock(double t, bool secs) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    ::localtime_r(&tt, &tm);
    char b[16];
    std::strftime(b, sizeof b, secs ? "%H:%M:%S" : "%H:%M", &tm);
    return b;
}
}  // namespace

Timeline::Timeline(std::string_view name) {
    set_name(std::string(name));
    registry::add(name, this);
    add_css_class("graph");
    add_css_class("timeline");
    set_content_height(220);
    set_hexpand(true);
    set_focusable(true);
    set_draw_func(sigc::mem_fun(*this, &Timeline::draw));

    auto drag = Gtk::GestureDrag::create();
    drag->set_button(GDK_BUTTON_PRIMARY);
    drag->signal_drag_begin().connect([this](double x, double) { grab_focus(); scrub(x); });
    drag->signal_drag_update().connect([this, d = drag.get()](double dx, double) {
        double sx = 0, sy = 0;
        if (d->get_start_point(sx, sy)) scrub(sx + dx);
    });
    add_controller(drag);

    auto dbl = Gtk::GestureClick::create();
    dbl->signal_pressed().connect([this](int n, double, double) { if (n == 2) zoom_all(); });
    add_controller(dbl);

    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double) { m_pointer_x = x; });
    motion->signal_leave().connect([this] { m_pointer_x = -1; });
    add_controller(motion);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
    scroll->signal_scroll().connect(
        [this, s = scroll.get()](double dx, double dy) {
            const bool shift = (s->get_current_event_state() & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType{};
            if (shift || std::fabs(dx) > std::fabs(dy)) {
                pan(0.1 * (shift ? dy : dx));
            } else {
                const double around = m_pointer_x >= 0 ? t_at(m_pointer_x) : m_cursor;
                zoom(dy > 0 ? 1.25 : 0.8, around);
            }
            return true;
        },
        false);
    add_controller(scroll);

    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint key, guint, Gdk::ModifierType) {
            switch (key) {
                case GDK_KEY_Left:  m_sig_step.emit(-1); return true;
                case GDK_KEY_Right: m_sig_step.emit(+1); return true;
                case GDK_KEY_Home:  m_cursor = m_t0; queue_draw(); m_sig_cursor.emit(m_cursor); return true;
                case GDK_KEY_End:   m_cursor = m_t1; queue_draw(); m_sig_cursor.emit(m_cursor); return true;
                case GDK_KEY_plus: case GDK_KEY_equal: case GDK_KEY_KP_Add: zoom(0.5, m_cursor); return true;
                case GDK_KEY_minus: case GDK_KEY_KP_Subtract:               zoom(2.0, m_cursor); return true;
                default: return false;
            }
        },
        false);
    add_controller(keys);
}

Timeline::~Timeline() { registry::remove(this); }

void Timeline::set_span(double t0, double t1) {
    const bool fresh = m_from == 0 && m_to == 1;
    const bool was_all = std::fabs(m_from - m_t0) < 1e-6 && std::fabs(m_to - m_t1) < 1e-6;
    m_t0 = t0;
    m_t1 = std::max(t1, t0 + 1);
    if (fresh || was_all) { m_from = m_t0; m_to = m_t1; }   // a growing live file stays "all"
    set_view(m_from, m_to);
}

void Timeline::set_view(double from, double to) {
    const double span = m_t1 - m_t0;
    double w = std::clamp(to - from, std::min(kMinView, span), span);
    from = std::clamp(from, m_t0, m_t1 - w);
    if (from == m_from && from + w == m_to) { queue_draw(); return; }
    m_from = from;
    m_to = from + w;
    queue_draw();
    m_sig_view.emit();
}

void Timeline::zoom_all() { set_view(m_t0, m_t1); }

void Timeline::zoom(double factor, double around) {
    const double w = (m_to - m_from) * factor;
    const double f = (m_to > m_from) ? (around - m_from) / (m_to - m_from) : 0.5;
    set_view(around - w * f, around - w * f + w);
}

void Timeline::pan(double fraction) {
    const double d = (m_to - m_from) * fraction;
    set_view(m_from + d, m_to + d);
}

void Timeline::set_series(std::vector<core::TrailPoint> a, std::vector<core::TrailPoint> b) {
    m_a = std::move(a);
    m_b = std::move(b);
    queue_draw();
}

double Timeline::x_at(double t) const {
    const double w = get_width();
    return (m_to > m_from) ? (t - m_from) / (m_to - m_from) * w : 0;
}
double Timeline::t_at(double x) const {
    const double w = std::max(1, get_width());
    return m_from + std::clamp(x / w, 0.0, 1.0) * (m_to - m_from);
}

void Timeline::scrub(double x) {
    m_cursor = t_at(x);
    queue_draw();
    m_sig_cursor.emit(m_cursor);
}

void Timeline::draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    const Gdk::RGBA c = get_color();
    const double W = w, top = kTop, bottom = h - kBottom, H = bottom - top;
    auto rgba = [&](double a) { cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), a); };
    auto text = [&](const std::string& s, double x, double y, double alpha, bool right = false) {
        auto layout = create_pango_layout(s);
        int tw = 0, th = 0;
        layout->get_pixel_size(tw, th);
        if (right) x -= tw;
        x = std::clamp(x, 2.0, std::max(2.0, W - tw - 2));
        rgba(alpha);
        cr->move_to(x, y);
        layout->show_in_cairo_context(cr);
    };

    double mx = 0;
    for (const auto& p : m_a) mx = std::max(mx, double(p.v));
    for (const auto& p : m_b) mx = std::max(mx, double(p.v));
    const double ymax = m_fixed_top > 0 ? m_fixed_top : nice_ceiling(std::max(mx, 1e-9));

    // Grid + the top value.
    cr->set_line_width(1.0);
    rgba(0.10);
    for (int i = 0; i <= 4; ++i) {
        const double y = std::round(top + H * i / 4.0) + 0.5;
        cr->move_to(0, y);
        cr->line_to(W, y);
    }
    cr->stroke();
    if (m_fmt) text(m_fmt(ymax), 4, top + 2, 0.6);

    // Time labels: a round step giving ~6 labels.
    const double span = m_to - m_from;
    static const double steps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 10800, 21600, 43200, 86400};
    double step = steps[std::size(steps) - 1];
    for (double s : steps) if (span / s <= 7) { step = s; break; }
    rgba(0.12);
    for (double t = std::ceil(m_from / step) * step; t <= m_to; t += step) {
        const double x = std::round(x_at(t)) + 0.5;
        cr->move_to(x, top);
        cr->line_to(x, bottom);
        cr->stroke();
        text(clock(t, step < 60), x + 3, bottom + 2, 0.6);
    }

    auto px = [&](const core::TrailPoint& p) { return x_at(p.t); };
    auto py = [&](float v) { return bottom - std::clamp(double(v) / ymax, 0.0, 1.0) * H; };
    auto path = [&](const std::vector<core::TrailPoint>& s) {
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (i == 0) cr->move_to(px(s[i]), py(s[i].v));
            else cr->line_to(px(s[i]), py(s[i].v));
        }
    };
    cr->set_line_join(Cairo::Context::LineJoin::ROUND);
    if (m_a.size() >= 2) {
        path(m_a);
        cr->line_to(px(m_a.back()), bottom);
        cr->line_to(px(m_a.front()), bottom);
        cr->close_path();
        rgba(0.22);
        cr->fill();
        path(m_a);
        cr->set_line_width(2.0);
        rgba(1.0);
        cr->stroke();
    } else if (m_a.size() == 1) {
        cr->arc(px(m_a[0]), py(m_a[0].v), 3, 0, 2 * M_PI);
        rgba(1.0);
        cr->fill();
    }
    if (m_b.size() >= 2) {
        path(m_b);
        cr->set_line_width(1.5);
        rgba(0.55);
        cr->stroke();
    }

    // Marks: dashed verticals with their text on top.
    const std::vector<double> dash{4.0, 3.0};
    for (const auto& m : m_marks) {
        if (m.t < m_from || m.t > m_to) continue;
        const double x = std::round(x_at(m.t)) + 0.5;
        cr->set_dash(dash, 0);
        cr->set_line_width(m.selected ? 2.0 : 1.0);
        rgba(m.selected ? 0.95 : 0.6);
        cr->move_to(x, top - 2);
        cr->line_to(x, bottom);
        cr->stroke();
        cr->unset_dash();
        text(m.text, x + 3, 1, m.selected ? 1.0 : 0.75);
    }

    // The cursor.
    if (m_cursor >= m_from && m_cursor <= m_to) {
        const double x = std::round(x_at(m_cursor)) + 0.5;
        cr->set_line_width(2.0);
        cr->set_source_rgba(0.9, 0.2, 0.2, has_focus() ? 1.0 : 0.8);
        cr->move_to(x, top - 4);
        cr->line_to(x, bottom + 4);
        cr->stroke();
    }
}

}  // namespace trails::widgets

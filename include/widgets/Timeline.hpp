#pragma once
#include "core/Trail.hpp"

#include <gtkmm/drawingarea.h>
#include <sigc++/signal.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// widgets::Timeline -- the Trails tab's scrubbable, zoomable line (s010).
//
// It draws what it is given -- one metric filled, an optional second as a
// line, the marks as dashed verticals with their text, the cursor as a solid
// vertical, local times along the bottom -- over the VIEW (a window into the
// recording's span). It owns no data: when the view moves it says so
// (signal_view) and the page asks the Trail for that range at this width.
//
//   drag / click      scrub: move the cursor          (signal_cursor)
//   scroll            zoom around the pointer
//   Shift+scroll      pan
//   double-click      the whole recording again
//   Left / Right      one tick (signal_step), Home / End the ends
//   + / -             zoom around the cursor
// Colour is CSS `color`, like widgets::Graph -- the skin owns it.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::widgets {

class Timeline : public Gtk::DrawingArea {
public:
    struct Mark { double t = 0; std::string text; bool selected = false; };

    explicit Timeline(std::string_view name);
    ~Timeline() override;

    void set_span(double t0, double t1);   // the whole recording; the view is clamped into it
    void set_view(double from, double to);
    void zoom_all();
    void zoom(double factor, double around);   // factor < 1 zooms in
    void pan(double fraction);                 // of the view width; + = later
    double view_from() const { return m_from; }
    double view_to() const { return m_to; }

    void set_series(std::vector<core::TrailPoint> a, std::vector<core::TrailPoint> b);
    void set_top(double top) { m_fixed_top = top; queue_draw(); }   // 0 = auto (nice ceiling)
    void set_marks(std::vector<Mark> m) { m_marks = std::move(m); queue_draw(); }
    void set_cursor(double t) { m_cursor = t; queue_draw(); }
    double cursor() const { return m_cursor; }
    void set_value_format(std::function<std::string(double)> f) { m_fmt = std::move(f); queue_draw(); }

    sigc::signal<void(double)>& signal_cursor() { return m_sig_cursor; }   // user scrubbed to t
    sigc::signal<void()>&       signal_view()   { return m_sig_view; }     // zoom / pan
    sigc::signal<void(int)>&    signal_step()   { return m_sig_step; }     // -1 / +1 tick

private:
    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    double t_at(double x) const;
    double x_at(double t) const;
    void scrub(double x);

    double m_t0 = 0, m_t1 = 1, m_from = 0, m_to = 1, m_cursor = 0;
    double m_fixed_top = 0;
    double m_pointer_x = -1;
    std::vector<core::TrailPoint> m_a, m_b;
    std::vector<Mark> m_marks;
    std::function<std::string(double)> m_fmt;
    sigc::signal<void(double)> m_sig_cursor;
    sigc::signal<void()>       m_sig_view;
    sigc::signal<void(int)>    m_sig_step;
};

}  // namespace trails::widgets

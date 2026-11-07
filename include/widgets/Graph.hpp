#pragma once
#include <gtkmm/drawingarea.h>

#include <cstddef>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// widgets::Graph -- a Cairo sparkline over one or two history series.
//
// The data is a plain copy of a Snapshot ring (oldest -> newest), drawn
// RIGHT-ALIGNED against the ring's capacity: a fresh window fills in from the
// right edge, the way btop's graphs scroll, and 5 minutes of history always
// spans the same width.
//
// Colour is NOT a parameter: the graph draws in its own CSS `color`, so the
// skin owns it (.tab-cpu .graph { color: ... } in trails.css) and a light /
// dark switch repaints for free. Series A is a filled area + line, series B
// (rx / tx, read / write) a line in the same colour, lighter.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::widgets {

class Graph : public Gtk::DrawingArea {
public:
    enum class Scale { Percent,   // fixed 0..100
                       Auto };    // 0..max of the data, rounded up; never below `floor`

    explicit Graph(std::string_view name);
    ~Graph() override;

    void set_scale(Scale s, double floor = 1.0) { m_scale = s; m_floor = floor; }
    void set_series(std::vector<float> a, std::vector<float> b, std::size_t capacity);

    double top() const { return m_top; }   // the value the top edge stood for, last paint

private:
    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);

    std::vector<float> m_a, m_b;
    std::size_t        m_capacity = 300;
    Scale              m_scale = Scale::Percent;
    double             m_floor = 1.0;
    double             m_top = 100.0;
};

}  // namespace trails::widgets

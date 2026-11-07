#pragma once
#include <gtkmm/drawingarea.h>

#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// widgets::Bar -- a horizontal stacked bar: parts as fractions of the whole,
// each drawn in the widget's CSS `color` at its own opacity (used solid,
// cache lighter, free faint). Like Graph, the skin owns the colour.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::widgets {

class Bar : public Gtk::DrawingArea {
public:
    struct Part { double fraction; double alpha; };
    explicit Bar(std::string_view name, int height = 14);
    ~Bar() override;
    void set_parts(std::vector<Part> parts);

private:
    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    std::vector<Part> m_parts;
};

}  // namespace trails::widgets

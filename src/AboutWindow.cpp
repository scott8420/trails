#include "AboutWindow.hpp"
#include "Registry.hpp"
#include "core/SysInfo.hpp"

#include <giomm/resource.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/enums.h>
#include <gtkmm/grid.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/linkbutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stack.h>
#include <gtkmm/stackswitcher.h>
#include <gtkmm/textview.h>
#include <gtkmmconfig.h>

#include <unistd.h>

#ifndef TRAILS_VERSION
#define TRAILS_VERSION "dev"
#endif

namespace trails {

namespace {
constexpr const char* kSite = "https://github.com/scott8420/trails";

Gtk::Label* label(const std::string& markup, bool dim = false) {
    auto* l = Gtk::make_managed<Gtk::Label>();
    l->set_markup(markup);
    l->set_wrap(true);
    l->set_justify(Gtk::Justification::CENTER);
    l->set_max_width_chars(46);
    if (dim) l->add_css_class("dim-label");
    return l;
}
Gtk::Box* page_box() {
    auto* b = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    b->set_margin(24);
    return b;
}

// A two-column grid of "Label   value" rows, labels dim and right-aligned.
Gtk::Grid* rows_grid(const core::InfoRows& rows) {
    auto* g = Gtk::make_managed<Gtk::Grid>();
    g->set_row_spacing(6);
    g->set_column_spacing(14);
    g->set_halign(Gtk::Align::CENTER);
    int r = 0;
    for (const auto& [k, v] : rows) {
        auto* kl = Gtk::make_managed<Gtk::Label>(k);
        kl->add_css_class("dim-label");
        kl->set_xalign(1.0f);
        kl->set_valign(Gtk::Align::START);
        auto* vl = Gtk::make_managed<Gtk::Label>(v);
        vl->set_xalign(0.0f);
        vl->set_wrap(true);
        vl->set_max_width_chars(36);
        vl->set_selectable(true);
        g->attach(*kl, 0, r);
        g->attach(*vl, 1, r);
        ++r;
    }
    return g;
}

std::string license_text() {
    try {
        auto bytes = Gio::Resource::lookup_data_global("/io/github/scott8420/Trails/LICENSE");
        gsize n = 0;
        const auto* p = static_cast<const char*>(bytes->get_data(n));
        if (!p) return {};
        // Reflow: the file is hard-wrapped at ~78 columns; the view wraps
        // to its own width, so join each paragraph's lines into one.
        std::string in(p, n), out;
        std::size_t i = 0;
        while (i < in.size()) {
            std::size_t e = in.find('\n', i);
            if (e == std::string::npos) e = in.size();
            const std::string line = in.substr(i, e - i);
            if (line.empty()) out += "\n\n";
            else out += (out.empty() || out.back() == '\n' ? "" : " ") + line;
            i = e + 1;
        }
        while (out.find("\n\n\n") != std::string::npos) out.replace(out.find("\n\n\n"), 3, "\n\n");
        return out;
    } catch (const Glib::Error&) {
        return "MIT License -- see LICENSE in the source.";
    }
}
}  // namespace

AboutWindow::AboutWindow(Gtk::Window& parent) {
    set_name("shell.about");
    registry::add("shell.about", this);

    set_title("About Trails");
    set_modal(true);
    set_resizable(false);
    // s015c: width pinned on the stack, height left to GTK -- a fixed default
    // height came up 17 px short of the wrapped text on Scott's fonts
    // ("Trying to measure shell.about for height of 419, but it needs 436").
    set_transient_for(parent);
    set_hide_on_close(true);   // singleton: X hides, never destructs mid-session
    add_css_class("trails");   // the main window's skin
    add_css_class("page-overview");

    auto* stack = Gtk::make_managed<Gtk::Stack>();
    stack->set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    stack->set_transition_duration(120);
    stack->set_vexpand(true);
    stack->set_size_request(480, -1);

    // Tabs in the header, as GNOME apps do.
    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(true);
    auto* sw = Gtk::make_managed<Gtk::StackSwitcher>();
    sw->set_stack(*stack);
    hb->set_title_widget(*sw);
    set_titlebar(*hb);

    // ── About ────────────────────────────────────────────────────────────────
    {
        auto* p = page_box();
        p->set_valign(Gtk::Align::CENTER);
        auto* logo = Gtk::make_managed<Gtk::Image>();
        logo->set_from_icon_name("trails-hero-logo");   // the glass orb, full colour
        logo->set_pixel_size(112);
        p->append(*logo);
        p->append(*label("<span size='xx-large' weight='bold'>Trails</span>"));
        p->append(*label("Version " TRAILS_VERSION, true));
        auto* tag = label("A system monitor for GNOME. See what your machine is doing \u2014 "
                          "CPU, memory, disks, network, sensors, GPU, processes and services \u2014 "
                          "and record it, so the spike you missed is still there to find.");
        tag->set_margin_top(6);
        p->append(*tag);
        auto* link = Gtk::make_managed<Gtk::LinkButton>(kSite, "github.com/scott8420/trails");
        link->set_halign(Gtk::Align::CENTER);
        link->set_margin_top(4);
        p->append(*link);
        p->append(*label("© 2026 Scott · MIT License", true));
        stack->add(*p, "about", "About");
    }

    // ── Credits ──────────────────────────────────────────────────────────────
    {
        auto* p = page_box();
        p->set_valign(Gtk::Align::CENTER);
        p->append(*label("<b>Written by</b>"));
        p->append(*label("Scott, with Claude"));
        auto* b = label("<b>Built with</b>");
        b->set_margin_top(10);
        p->append(*b);
        p->append(*label("GTK 4 and gtkmm\nspdlog\nSQLite\nnlohmann/json"));
        auto* d = label("<b>Design</b>");
        d->set_margin_top(10);
        p->append(*d);
        p->append(*label("Skin after Rouster · started from the Cairn starter"));
        auto* t = label("<b>Thanks</b>");
        t->set_margin_top(10);
        p->append(*t);
        p->append(*label("btop, for showing what a monitor can be — and the kernel's /proc "
                         "and /sys, which tell all of it.", true));
        stack->add(*p, "credits", "Credits");
    }

    // ── License ──────────────────────────────────────────────────────────────
    {
        auto* p = page_box();
        auto* tv = Gtk::make_managed<Gtk::TextView>();
        tv->set_editable(false);
        tv->set_cursor_visible(false);
        tv->set_wrap_mode(Gtk::WrapMode::WORD);
        tv->set_monospace(true);
        tv->set_left_margin(8);
        tv->set_right_margin(8);
        tv->set_top_margin(8);
        tv->set_bottom_margin(8);
        tv->get_buffer()->set_text(license_text());
        auto* sc = Gtk::make_managed<Gtk::ScrolledWindow>();
        sc->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        sc->set_vexpand(true);
        sc->set_min_content_height(320);   // a fixed height: the text scrolls, the window doesn't grow
        sc->set_child(*tv);
        sc->add_css_class("panel");
        p->append(*sc);
        stack->add(*p, "license", "License");
    }

    // ── System ───────────────────────────────────────────────────────────────
    {
        auto* p = page_box();
        p->set_valign(Gtk::Align::CENTER);
        long n = ::sysconf(_SC_NPROCESSORS_ONLN);
        core::InfoRows rows{{"Trails", TRAILS_VERSION}};
        for (auto& r : core::system_info("/", int(n))) rows.push_back(std::move(r));
        rows.emplace_back("GTK", std::to_string(gtk_get_major_version()) + "." +
                                     std::to_string(gtk_get_minor_version()) + "." +
                                     std::to_string(gtk_get_micro_version()));
        rows.emplace_back("gtkmm", std::to_string(GTKMM_MAJOR_VERSION) + "." +
                                       std::to_string(GTKMM_MINOR_VERSION) + "." +
                                       std::to_string(GTKMM_MICRO_VERSION));
        p->append(*rows_grid(rows));
        auto* copy = Gtk::make_managed<Gtk::Button>("Copy");
        copy->set_tooltip_text("Copy these lines, for a bug report");
        copy->set_halign(Gtk::Align::CENTER);
        copy->set_margin_top(12);
        const std::string text = core::info_text(rows);
        copy->signal_clicked().connect([this, copy, text] {
            get_clipboard()->set_text(text);
            copy->set_label("Copied");
        });
        p->append(*copy);
        stack->add(*p, "system", "System");
    }

    set_child(*stack);
}

AboutWindow::~AboutWindow() {
    registry::remove(this);
}

}  // namespace trails

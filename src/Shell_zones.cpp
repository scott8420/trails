#include "Shell.hpp"
#include "Log.hpp"

#include <gtkmm/headerbar.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>

// Shell_zones.cpp -- ZONES. Construction of the named regions: the window frame
// and header, the tab rail + stack, the Overview tiles, the placeholder pages,
// and the menu model.

namespace trails {

void Shell::build_shell() {  // zone: window + rail + stack
    set_title("Trails");
    set_default_size(1100, 720);
    add_css_class("trails");

    auto header = Gtk::make_managed<Gtk::HeaderBar>();
    m_logo_button.set_icon_name("trails-logo-symbolic");
    m_logo_button.set_has_frame(false);
    m_logo_button.set_tooltip_text("About Trails");
    m_logo_button.set_action_name("win.about");
    header->pack_start(m_logo_button);

    m_menu_button.set_icon_name("open-menu-symbolic");
    m_menu_button.set_menu_model(build_menu());
    header->pack_end(m_menu_button);
    set_titlebar(*header);

    build_rail();
    m_stack.set_hexpand(true);
    m_stack.set_vexpand(true);
    m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_stack.set_transition_duration(120);
    m_stack.add_css_class("pages");

    m_body.append(m_rail_scroll);
    m_body.append(m_stack);
    set_child(m_body);
}

// The rail: a ListBox, not a StackSidebar, because each row carries a big
// coloured icon (StackSidebar shows text only). Rows are built from
// core::tab_registry() -- the same list the pages and the Alt+N keys read.
void Shell::build_rail() {  // zone: the tab rail
    m_rail.add_css_class("rail");
    m_rail.set_selection_mode(Gtk::SelectionMode::BROWSE);
    for (const auto& t : core::tab_registry()) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(std::string(t.id));
        row->add_css_class(std::string(t.css));
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        auto icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(std::string(t.icon));
        icon->set_pixel_size(28);
        icon->add_css_class("rail-icon");
        auto label = Gtk::make_managed<Gtk::Label>(std::string(t.title));
        label->set_xalign(0.0f);
        label->add_css_class("rail-label");
        box->append(*icon);
        box->append(*label);
        row->set_child(*box);
        m_rail.append(*row);
    }
    m_rail.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (m_syncing || !row) return;
        on_tab(std::string(row->get_name()));
    });
    m_rail_scroll.set_child(m_rail);
    m_rail_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_rail_scroll.set_size_request(210, -1);
    m_rail_scroll.add_css_class("rail-scroll");
}

void Shell::build_pages() {  // zone: one stack page per tab
    for (const auto& t : core::tab_registry()) {
        Gtk::Widget& page = (t.id == "overview") ? build_overview() : build_placeholder(t);
        m_stack.add(page, std::string(t.id), std::string(t.title));
    }
}

Gtk::Widget& Shell::build_overview() {  // zone: the Overview tiles
    m_tiles.set_selection_mode(Gtk::SelectionMode::NONE);
    m_tiles.set_homogeneous(true);
    m_tiles.set_min_children_per_line(1);
    m_tiles.set_max_children_per_line(3);
    m_tiles.set_row_spacing(16);
    m_tiles.set_column_spacing(16);
    m_tiles.set_valign(Gtk::Align::START);
    m_tiles.add_css_class("tiles");

    for (auto [tile, tab] : {std::pair{&m_tile_cpu, "cpu"}, std::pair{&m_tile_mem, "memory"},
                             std::pair{&m_tile_disk, "disks"}, std::pair{&m_tile_net, "network"},
                             std::pair{&m_tile_procs, "processes"}}) {
        tile->set_size_request(300, -1);
        const std::string id = tab;
        tile->signal_clicked().connect([this, id] { on_tab(id); });
        m_tiles.append(*tile);
    }
    m_tile_cpu.graph().set_scale(widgets::Graph::Scale::Percent);
    m_tile_mem.graph().set_scale(widgets::Graph::Scale::Percent);
    m_tile_disk.graph().set_scale(widgets::Graph::Scale::Auto, 1024.0 * 1024.0);   // 1 MiB/s floor
    m_tile_net.graph().set_scale(widgets::Graph::Scale::Auto, 64.0 * 1024.0);      // 64 KiB/s floor
    m_tile_procs.set_has_graph(false);

    auto page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    page->add_css_class("page");
    page->append(m_tiles);
    m_overview_scroll.set_child(*page);
    m_overview_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    return m_overview_scroll;
}

// A tab that isn't built yet still looks like itself: its big coloured icon,
// its name, what it will show, and which session brings it.
Gtk::Widget& Shell::build_placeholder(const core::TabSpec& t) {  // zone
    auto page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    page->set_name("page." + std::string(t.id));
    page->add_css_class("page");
    page->add_css_class("placeholder");
    page->add_css_class(std::string(t.css));
    page->set_valign(Gtk::Align::CENTER);
    page->set_halign(Gtk::Align::CENTER);

    auto icon = Gtk::make_managed<Gtk::Image>();
    icon->set_from_icon_name(std::string(t.icon));
    icon->set_pixel_size(112);
    icon->add_css_class("placeholder-icon");
    auto title = Gtk::make_managed<Gtk::Label>(std::string(t.title));
    title->add_css_class("placeholder-title");
    auto blurb = Gtk::make_managed<Gtk::Label>(std::string(t.blurb));
    blurb->set_wrap(true);
    blurb->set_max_width_chars(48);
    blurb->set_justify(Gtk::Justification::CENTER);
    auto when = Gtk::make_managed<Gtk::Label>("Arrives in " + std::string(t.arrives));
    when->add_css_class("dim-label");

    page->append(*icon);
    page->append(*title);
    page->append(*blurb);
    page->append(*when);
    return *page;
}

Glib::RefPtr<Gio::Menu> Shell::build_menu() {  // zone: hamburger model
    auto menu = Gio::Menu::create();

    auto help = Gio::Menu::create();
    help->append("Keyboard Shortcuts", "win.shortcuts");
    help->append("Dump registry", "win.dump-registry");
    menu->append_section(help);

    auto app = Gio::Menu::create();
    app->append("About Trails", "win.about");
    app->append("Quit", "win.quit");
    menu->append_section(app);
    return menu;
}

}  // namespace trails

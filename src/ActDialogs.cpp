#include "ActDialogs.hpp"

#include <gtkmm/alertdialog.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>

#include <unistd.h>

#include <algorithm>
#include <memory>

namespace trails::dialogs {

namespace {

std::string q(const std::string& name) { return "“" + name + "”"; }

// A modal window with a body and Cancel / OK in the header. Deletes itself
// once hidden. `ok` returns the button so the caller can grey it.
struct Modal {
    Gtk::Window* win;
    Gtk::Box*    body;
    Gtk::Button* ok;
};
Modal make_modal(Gtk::Window& parent, const std::string& title, const std::string& ok_label) {
    auto* w = new Gtk::Window();
    w->set_transient_for(parent);
    w->set_modal(true);
    w->set_hide_on_close(true);
    w->set_resizable(false);
    w->set_title(title);
    w->signal_hide().connect([w] { Glib::signal_idle().connect_once([w] { delete w; }); });

    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(false);
    auto* cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel->signal_clicked().connect([w] { w->close(); });
    auto* ok = Gtk::make_managed<Gtk::Button>(ok_label);
    ok->add_css_class("suggested-action");
    hb->pack_start(*cancel);
    hb->pack_end(*ok);
    w->set_titlebar(*hb);
    w->set_default_widget(*ok);

    // Escape cancels, Enter is OK -- caught before the focused widget (a
    // spin's entry would otherwise eat Enter on gtkmm < 4.14).
    auto keys = Gtk::EventControllerKey::create();
    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    keys->signal_key_pressed().connect(
        [w, ok](guint key, guint, Gdk::ModifierType) {
            if (key == GDK_KEY_Escape) { w->close(); return true; }
            if (key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) {
                if (ok->get_sensitive()) ok->activate();
                return true;
            }
            return false;
        }, false);
    w->add_controller(keys);

    auto* body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    body->set_margin(18);
    w->set_child(*body);
    return {w, body, ok};
}

}  // namespace

void ask_nice(Gtk::Window& parent, const std::string& name, int pid, int current,
              std::function<void(int)> done) {
    auto m = make_modal(parent, "Renice", "Renice");
    auto* head = Gtk::make_managed<Gtk::Label>();
    head->set_markup("<b>" + Glib::Markup::escape_text(q(name)) + "</b>  PID " + std::to_string(pid));
    head->set_xalign(0.0f);
    auto* spin = Gtk::make_managed<Gtk::SpinButton>();
    spin->set_range(-20, 19);
    spin->set_increments(1, 5);
    spin->set_digits(0);
    spin->set_value(current);
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    auto* lbl = Gtk::make_managed<Gtk::Label>("Nice");
    row->append(*lbl);
    row->append(*spin);
    auto* hint = Gtk::make_managed<Gtk::Label>(
        "Lower runs first (−20 highest, 19 lowest). Lowering it needs your\n"
        "password, even for your own process. Ctrl+Z puts it back.");
    hint->add_css_class("dim-label");
    hint->set_xalign(0.0f);
    m.body->append(*head);
    m.body->append(*row);
    m.body->append(*hint);
    m.ok->signal_clicked().connect([w = m.win, spin, done] {
        spin->update();
        const int v = static_cast<int>(spin->get_value());
        w->close();
        done(v);
    });
    m.win->present();
    spin->grab_focus();
}

void ask_cpus(Gtk::Window& parent, const std::string& name, int pid, const std::vector<int>& current,
              std::function<void(std::vector<int>)> done) {
    auto m = make_modal(parent, "CPU Affinity", "Apply");
    auto* head = Gtk::make_managed<Gtk::Label>();
    head->set_markup("<b>" + Glib::Markup::escape_text(q(name)) + "</b>  PID " + std::to_string(pid) +
                     "\nThe CPUs it may run on.");
    head->set_xalign(0.0f);
    m.body->append(*head);

    long n = ::sysconf(_SC_NPROCESSORS_CONF);
    if (n < 1) n = 1;
    auto* flow = Gtk::make_managed<Gtk::FlowBox>();
    flow->set_selection_mode(Gtk::SelectionMode::NONE);
    flow->set_max_children_per_line(8);
    flow->set_min_children_per_line(4);
    flow->set_column_spacing(6);
    auto checks = std::make_shared<std::vector<Gtk::CheckButton*>>();
    auto ok = m.ok;
    auto sync = [checks, ok] {
        bool any = false;
        for (auto* c : *checks) any = any || c->get_active();
        ok->set_sensitive(any);
    };
    for (long c = 0; c < n; ++c) {
        auto* cb = Gtk::make_managed<Gtk::CheckButton>("CPU " + std::to_string(c));
        cb->set_active(std::find(current.begin(), current.end(), int(c)) != current.end());
        cb->signal_toggled().connect(sync);
        checks->push_back(cb);
        flow->append(*cb);
    }
    m.body->append(*flow);

    auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto* all = Gtk::make_managed<Gtk::Button>("All");
    auto* none = Gtk::make_managed<Gtk::Button>("None");
    all->signal_clicked().connect([checks] { for (auto* c : *checks) c->set_active(true); });
    none->signal_clicked().connect([checks] { for (auto* c : *checks) c->set_active(false); });
    bar->append(*all);
    bar->append(*none);
    m.body->append(*bar);
    sync();

    m.ok->signal_clicked().connect([w = m.win, checks, done] {
        std::vector<int> cpus;
        for (std::size_t i = 0; i < checks->size(); ++i)
            if ((*checks)[i]->get_active()) cpus.push_back(int(i));
        w->close();
        if (!cpus.empty()) done(cpus);
    });
    m.win->present();
}

void ask_text(Gtk::Window& parent, const std::string& title, const std::string& prompt,
              const std::string& initial, std::function<void(std::string)> done) {
    auto m = make_modal(parent, title, "OK");
    auto* lbl = Gtk::make_managed<Gtk::Label>(prompt);
    lbl->set_xalign(0.0f);
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_text(initial);
    entry->set_width_chars(32);
    m.body->append(*lbl);
    m.body->append(*entry);
    m.ok->signal_clicked().connect([w = m.win, entry, done] {
        const std::string t = entry->get_text();
        w->close();
        done(t);
    });
    m.win->present();
    entry->grab_focus();
}

void confirm_end(Gtk::Window& parent, const std::string& name, int pid, bool kill,
                 std::function<void()> yes) {
    auto d = Gtk::AlertDialog::create((kill ? "Kill " : "Terminate ") + q(name) + "?");
    d->set_detail("PID " + std::to_string(pid) + ". " +
                  (kill ? "It stops at once, without saving anything."
                        : "It is asked to quit, and may save its work first.") +
                  " This cannot be undone.");
    d->set_buttons({"Cancel", kill ? "Kill" : "Terminate"});
    d->set_cancel_button(0);
    d->set_default_button(0);
    d->set_modal(true);
    d->choose(parent, [d, yes](Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            if (d->choose_finish(res) == 1) yes();
        } catch (const Glib::Error&) {}   // dismissed
    });
}

}  // namespace trails::dialogs

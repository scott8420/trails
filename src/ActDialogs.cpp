#include "ActDialogs.hpp"
#include "core/ProcActs.hpp"
#include "widgets/Searchable.hpp"

#include <gtkmm/alertdialog.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/grid.h>
#include <gtkmm/stringlist.h>
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
              const std::vector<core::procacts::CpuGroup>& groups,
              std::function<void(std::vector<int>)> done) {
    auto m = make_modal(parent, "CPU Affinity", "Apply");
    auto* head = Gtk::make_managed<Gtk::Label>();
    head->set_markup("<b>" + Glib::Markup::escape_text(q(name)) + "</b>  PID " + std::to_string(pid) +
                     "\nThe CPUs it may run on.");
    head->set_xalign(0.0f);
    m.body->append(*head);

    // (cpu number, its check), across every group.
    auto checks = std::make_shared<std::vector<std::pair<int, Gtk::CheckButton*>>>();
    auto ok = m.ok;
    auto sync = [checks, ok] {
        bool any = false;
        for (auto& [cpu, c] : *checks) any = any || c->get_active();
        ok->set_sensitive(any);
    };
    for (const auto& g : groups) {
        if (!g.name.empty()) {   // s015: "Performance  CPU 2-9   [Only these]"
            auto* gh = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            gh->set_margin_top(6);
            auto* gl = Gtk::make_managed<Gtk::Label>();
            gl->set_markup("<b>" + Glib::Markup::escape_text(g.name) + "</b>  <span alpha=\"60%\">CPU " +
                           core::procacts::cpus_to_text(g.cpus) + "</span>");
            gl->set_xalign(0.0f);
            gl->set_hexpand(true);
            gh->append(*gl);
            if (g.name != "Offline") {
                auto* only = Gtk::make_managed<Gtk::Button>("Only these");
                only->add_css_class("flat");
                only->set_tooltip_text("Tick the " + g.name + " CPUs and nothing else");
                only->signal_clicked().connect([checks, mine = g.cpus] {
                    for (auto& [cpu, c] : *checks)
                        c->set_active(std::find(mine.begin(), mine.end(), cpu) != mine.end());
                });
                gh->append(*only);
            }
            m.body->append(*gh);
        }
        auto* flow = Gtk::make_managed<Gtk::FlowBox>();
        flow->set_selection_mode(Gtk::SelectionMode::NONE);
        flow->set_max_children_per_line(8);
        flow->set_min_children_per_line(4);
        flow->set_column_spacing(6);
        for (int cpu : g.cpus) {
            auto* cb = Gtk::make_managed<Gtk::CheckButton>("CPU " + std::to_string(cpu));
            cb->set_active(std::find(current.begin(), current.end(), cpu) != current.end());
            cb->signal_toggled().connect(sync);
            checks->emplace_back(cpu, cb);
            flow->append(*cb);
        }
        m.body->append(*flow);
    }

    auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    bar->set_margin_top(6);
    auto* all = Gtk::make_managed<Gtk::Button>("All");
    auto* none = Gtk::make_managed<Gtk::Button>("None");
    all->signal_clicked().connect([checks] { for (auto& [cpu, c] : *checks) c->set_active(true); });
    none->signal_clicked().connect([checks] { for (auto& [cpu, c] : *checks) c->set_active(false); });
    bar->append(*all);
    bar->append(*none);
    m.body->append(*bar);
    sync();

    m.ok->signal_clicked().connect([w = m.win, checks, done] {
        std::vector<int> cpus;
        for (auto& [cpu, c] : *checks)
            if (c->get_active()) cpus.push_back(cpu);
        std::sort(cpus.begin(), cpus.end());
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

void ask_trigger(Gtk::Window& parent, const std::string& title, const core::Trigger& initial,
                 const std::vector<std::string>& metrics, std::function<void(core::Trigger)> done) {
    auto m = make_modal(parent, title, "Save");
    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(8);
    grid->set_column_spacing(12);
    int row = 0;
    auto label = [&](const char* t) {
        auto* l = Gtk::make_managed<Gtk::Label>(t);
        l->set_xalign(1.0f);
        l->add_css_class("dim-label");
        grid->attach(*l, 0, row);
    };

    label("Name");
    auto* name = Gtk::make_managed<Gtk::Entry>();
    name->set_text(initial.name);
    name->set_placeholder_text("CPU pegged");
    grid->attach(*name, 1, row++, 2);

    label("When");
    std::vector<Glib::ustring> ms;
    guint pick = 0;
    for (const auto& x : metrics) { if (x == initial.metric) pick = guint(ms.size()); ms.push_back(x); }
    if (std::find(metrics.begin(), metrics.end(), initial.metric) == metrics.end()) {
        pick = guint(ms.size());
        ms.push_back(initial.metric);
    }
    auto* metric = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(ms));
    metric->set_selected(pick);
    widgets::searchable(*metric);
    metric->set_hexpand(true);
    grid->attach(*metric, 1, row++, 2);

    label("is");
    auto* cmp = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create({"above", "below"}));
    cmp->set_selected(initial.above ? 0 : 1);
    auto* thr = Gtk::make_managed<Gtk::SpinButton>();
    thr->set_range(-1e12, 1e12);
    thr->set_increments(1, 10);
    thr->set_digits(2);
    thr->set_value(initial.threshold);
    thr->set_hexpand(true);
    grid->attach(*cmp, 1, row);
    grid->attach(*thr, 2, row++);

    label("for");
    auto* secs = Gtk::make_managed<Gtk::SpinButton>();
    secs->set_range(0, 86400);
    secs->set_increments(1, 10);
    secs->set_digits(0);
    secs->set_value(initial.seconds);
    auto* sl = Gtk::make_managed<Gtk::Label>("seconds (0 = at once)");
    sl->set_xalign(0.0f);
    sl->add_css_class("dim-label");
    grid->attach(*secs, 1, row);
    grid->attach(*sl, 2, row++);

    label("then");
    auto* rec = Gtk::make_managed<Gtk::CheckButton>("start recording");
    auto* mark = Gtk::make_managed<Gtk::CheckButton>("drop a mark");
    rec->set_active(initial.record);
    mark->set_active(initial.mark);
    grid->attach(*rec, 1, row);
    grid->attach(*mark, 2, row++);

    auto* on = Gtk::make_managed<Gtk::CheckButton>("On");
    on->set_active(initial.enabled);
    grid->attach(*on, 1, row++);

    auto* hint = Gtk::make_managed<Gtk::Label>(
        "Values are in the metric's own units: % for cpu.* and *_pct, bytes / s for\n"
        "rates, \u00b0C for temps, W for power. A trigger fires once, then again only\n"
        "after the condition clears. Triggers run while Trails is open.");
    hint->set_xalign(0.0f);
    hint->add_css_class("dim-label");
    m.body->append(*grid);
    m.body->append(*hint);

    auto ok = m.ok;
    auto sync = [ok, rec, mark] { ok->set_sensitive(rec->get_active() || mark->get_active()); };
    rec->signal_toggled().connect(sync);
    mark->signal_toggled().connect(sync);
    sync();

    m.ok->signal_clicked().connect([w = m.win, name, metric, cmp, thr, secs, rec, mark, on, ms, done] {
        thr->update();
        secs->update();
        core::Trigger t;
        t.name = name->get_text();
        const auto i = metric->get_selected();
        t.metric = i < ms.size() ? std::string(ms[i]) : std::string("cpu.total");
        t.above = cmp->get_selected() == 0;
        t.threshold = thr->get_value();
        t.seconds = int(secs->get_value());
        t.record = rec->get_active();
        t.mark = mark->get_active();
        t.enabled = on->get_active();
        w->close();
        done(t);
    });
    m.win->present();
    name->grab_focus();
}

void confirm(Gtk::Window& parent, const std::string& heading, const std::string& detail,
             const std::string& button, std::function<void()> yes) {
    auto d = Gtk::AlertDialog::create(heading);
    d->set_detail(detail);
    d->set_buttons({"Cancel", button});
    d->set_cancel_button(0);
    d->set_default_button(0);
    d->set_modal(true);
    d->choose(parent, [d, yes](Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            if (d->choose_finish(res) == 1) yes();
        } catch (const Glib::Error&) {}
    });
}

void confirm_end(Gtk::Window& parent, const std::string& name, int pid, bool kill,
                 std::function<void()> yes) {
    auto d = Gtk::AlertDialog::create((kill ? "Kill " : "Terminate ") + q(name) + "?");
    d->set_detail("PID " + std::to_string(pid) + ". " +
                  (kill ? "It stops at once, without saving anything."
                        : "It is asked to quit, and may save its work first.") +
                  " This cannot be undone." +
                  (core::procacts::session_critical(name)
                       ? std::string("\n\nIt is part of your desktop session: ending it may freeze the desktop or log you out.")
                       : std::string()));
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

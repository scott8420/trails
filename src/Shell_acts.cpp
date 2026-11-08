#include "Shell.hpp"
#include "ActDialogs.hpp"
#include "Elevate.hpp"
#include "Log.hpp"
#include <glibmm/main.h>

#include <unistd.h>

// Shell_acts.cpp -- ACTS (s006). The verbs that change something.
//
//   Stop / Continue / Renice / Affinity  -> core::procacts -> Journal::run
//                                           (a step; Ctrl+Z puts it back)
//   Terminate / Kill                     -> confirm dialog -> ProcKnobStore::end
//                                           (no step; nothing undoes it)
//
// Privilege is not this file's business: SysProcOS tries the syscall and, on
// EPERM, calls elevate::run_helper (pkexec trails-helper). While that waits on
// the password dialog the loop keeps spinning, so every handler here starts
// with the busy() check -- one action at a time.

namespace trails {

namespace {
std::string q(const std::string& n) { return "“" + n + "”"; }
}  // namespace

void Shell::bind_acts() {  // acts: win.proc-*, win.undo / redo
    m_proc_os.set_elevator(elevate::run_helper);
    // s012: the trigger book joins the store BEFORE the door is set, so its
    // put() is guarded too. Loading is not a user choice (not a step).
    m_triggers.set_path(core::TriggerBook::default_path());
    if (auto e = m_triggers.load(); !e.empty())
        if (auto lg = log::get(log::Area::Shell)) lg->warn("triggers: {}", e);
    m_app.set_triggers(&m_triggers);
    m_app.set_door([this] { return m_journal.open(); });
    m_journal.set_selection_source([this] {
        std::vector<core::Ref> v;
        if (auto p = m_proc_page.picked()) v.push_back(core::Ref::of(core::ProcKey{p->pid, p->start}));
        return v;
    });
    m_journal.set_on_change([this] { sync_acts(); });
    m_proc_page.set_on_pick([this] { sync_acts(); });

    auto add = [this](const char* name, auto fn) { m_act[name] = add_action(name, fn); };
    add("proc-stop",      [this] { on_proc_stop(true); });
    add("proc-continue",  [this] { on_proc_stop(false); });
    add("proc-renice",    [this] { on_proc_renice(); });
    add("proc-affinity",  [this] { on_proc_affinity(); });
    add("proc-terminate", [this] { on_proc_end(false); });
    add("proc-kill",      [this] { on_proc_end(true); });
    add("undo",           [this] { on_undo(); });
    add("redo",           [this] { on_redo(); });
    add("record",         [this] { on_record(); });            // s009, Shell_record.cpp
    add("mark",           [this] { on_mark(); });
    add("open-recordings",[this] { on_open_recordings(); });
    add("copy-trail-query", [this] { on_copy_trail_query(); });
    add("trail-mark-add",    [this] { on_trail_mark_add(); });      // s010, Shell_record.cpp
    add("trail-mark-rename", [this] { on_trail_mark_rename(); });
    add("trail-mark-delete", [this] { on_trail_mark_delete(); });
    m_app.set_trail(&m_trails_page.trail());
    // s011: systemd units.
    m_app.set_units(&m_units);
    m_services_page.set_systemd(&m_systemd);
    m_trails_page.set_triggers(&m_triggers, &m_trig_eval);
    m_trails_page.set_on_trigger_pick([this] { sync_acts(); });
    m_trails_page.refresh_triggers();
    add("trigger-add",    [this] { on_trigger_add(); });      // s012, Shell_record.cpp
    add("trigger-edit",   [this] { on_trigger_edit(); });
    add("trigger-remove", [this] { on_trigger_remove(); });
    add("trigger-toggle", [this] { on_trigger_toggle(); });
    m_services_page.set_on_pick([this] { sync_acts(); });
    add("unit-start",   [this] { on_unit_op(core::UnitOp::Start); });
    add("unit-stop",    [this] { on_unit_op(core::UnitOp::Stop); });
    add("unit-restart", [this] { on_unit_op(core::UnitOp::Restart); });
    add("unit-enable",  [this] { on_unit_enable(true); });
    add("unit-disable", [this] { on_unit_enable(false); });
    m_trails_page.set_window(this);
    m_trails_page.set_live_path([this] { return m_recorder.recording() ? m_recorder.path() : std::string(); });
    sync_acts();
}

void Shell::sync_acts() {  // acts: grey what can't apply now
    const auto p = m_proc_page.picked();
    // The live state, not the last snapshot's: a Stop shows at once.
    std::optional<core::ProcKnobs> k;
    if (p) k = m_proc_os.read({p->pid, p->start});
    const bool live = k.has_value();
    const bool user = live && !p->kernel_thread;
    const bool ends = user && p->pid > 1;
    auto en = [this](const char* name, bool on) {
        auto it = m_act.find(name);
        if (it != m_act.end() && it->second->get_enabled() != on) it->second->set_enabled(on);
    };
    // Never Stop ourselves: a stopped Trails can't run its own Continue.
    en("proc-stop",      user && !k->stopped && p->pid != static_cast<int>(::getpid()));
    en("proc-continue",  user && k->stopped);
    en("proc-renice",    live);
    en("proc-affinity",  live);
    en("proc-terminate", ends);
    en("proc-kill",      ends);
    en("undo",           m_journal.can_undo());
    en("redo",           m_journal.can_redo());
    en("mark",           m_recorder.recording());
    en("copy-trail-query", !m_last_trail.empty());
    {   // s011: from the list's state; a fresh refresh follows every action
        const auto u = m_services_page.picked();
        const bool act = u && (u->row.active == "active" || u->row.active == "activating" ||
                               u->row.active == "reloading");
        const bool idle = !Systemd::busy() && !elevate::busy();
        en("unit-start",   idle && u && !act && u->row.load != "masked");
        en("unit-stop",    idle && act);
        en("unit-restart", idle && act);
        en("unit-enable",  idle && u && u->row.file_state == "disabled");
        en("unit-disable", idle && u && u->row.file_state == "enabled");
    }
    en("trigger-edit",   m_trails_page.selected_trigger() != 0);
    en("trigger-remove", m_trails_page.selected_trigger() != 0);
    en("trigger-toggle", m_trails_page.selected_trigger() != 0);
    en("trail-mark-add",    m_trails_page.trail().is_open());
    en("trail-mark-rename", m_trails_page.trail().is_open() && m_trails_page.selected_mark() != 0);
    en("trail-mark-delete", m_trails_page.trail().is_open() && m_trails_page.selected_mark() != 0);
}

void Shell::report(const std::string& done, const std::string& err) {  // acts: status + log
    auto lg = log::get(log::Area::Shell);
    // The status line of the tab in view (s010: Trails, s011: Services).
    const std::string tab = m_stack.get_visible_child_name();
    if (tab == "trails" || tab == "services") {
        if (tab == "trails") m_trails_page.set_status(err.empty() ? done : err, !err.empty());
        else m_services_page.set_status(err.empty() ? done : err, !err.empty());
        if (lg) { if (err.empty()) lg->info("act: {}", done); else lg->warn("act: {} -- {}", done, err); }
        sync_acts();
        return;
    }
    if (err.empty()) {
        m_proc_page.set_status(done, false);
        if (lg) lg->info("act: {}", done);
    } else {
        m_proc_page.set_status(err, true);
        if (lg) lg->warn("act: {} -- {}", done, err);
    }
    sync_acts();
}

void Shell::on_proc_stop(bool stop) {  // acts: Stop / Continue
    if (elevate::busy()) return;
    const auto p = m_proc_page.picked();
    if (!p) return;
    const core::ProcKey key{p->pid, p->start};
    // s014: Stop on the desktop's own processes asks first.
    if (stop && core::procacts::session_critical(p->name)) {
        dialogs::confirm(*this, "Stop " + q(p->name) + "?",
                         p->name + " is part of your desktop session. Stopped, the desktop freezes -- Trails "
                         "too, if it runs there -- and Continue or Ctrl+Z may be out of reach. To recover, "
                         "switch to a text console (Ctrl+Alt+F3) and run: kill -CONT " + std::to_string(p->pid),
                         "Stop anyway", [this, key, name = p->name] {
                             report("Stopped " + q(name), core::procacts::stop(m_journal, m_app, key, name));
                         });
        return;
    }
    const std::string e = stop ? core::procacts::stop(m_journal, m_app, key, p->name)
                               : core::procacts::resume(m_journal, m_app, key, p->name);
    report((stop ? "Stopped " : "Continued ") + q(p->name), e);
}

void Shell::on_proc_renice() {  // acts: ask, then renice
    if (elevate::busy()) return;
    const auto p = m_proc_page.picked();
    if (!p) return;
    const core::ProcKey key{p->pid, p->start};
    const auto cur = m_proc_os.read(key);
    if (!cur) { report("", "The process has exited."); return; }
    dialogs::ask_nice(*this, p->name, p->pid, cur->nice, [this, key, name = p->name](int v) {
        if (elevate::busy()) return;
        const std::string e = core::procacts::renice(m_journal, m_app, key, name, v);
        report("Reniced " + q(name) + " to " + std::to_string(v), e);
    });
}

void Shell::on_proc_affinity() {  // acts: ask, then pin
    if (elevate::busy()) return;
    const auto p = m_proc_page.picked();
    if (!p) return;
    const core::ProcKey key{p->pid, p->start};
    const auto cur = m_proc_os.read(key);
    if (!cur) { report("", "The process has exited."); return; }
    // s015: group by P / E cluster when the sampler found them.
    long n = ::sysconf(_SC_NPROCESSORS_CONF);
    if (n < 1) n = 1;
    std::vector<core::procacts::CpuGroup> groups;
    if (auto snap = m_sampler ? m_sampler->snapshot() : nullptr)
        groups = core::procacts::cpu_groups(snap->clusters, snap->core_ids, int(n));
    else
        groups = core::procacts::cpu_groups({}, {}, int(n));
    dialogs::ask_cpus(*this, p->name, p->pid, cur->cpus, groups, [this, key, name = p->name](std::vector<int> cpus) {
        if (elevate::busy()) return;
        const std::string e = core::procacts::set_affinity(m_journal, m_app, key, name, cpus);
        report(q(name) + " may run on CPU " + core::procacts::cpus_to_text(cpus), e);
    });
}

void Shell::on_proc_end(bool kill) {  // acts: confirm, then TERM / KILL
    if (elevate::busy()) return;
    const auto p = m_proc_page.picked();
    if (!p || p->kernel_thread || p->pid <= 1) return;
    const core::ProcKey key{p->pid, p->start};
    dialogs::confirm_end(*this, p->name, p->pid, kill, [this, key, kill, name = p->name] {
        if (elevate::busy()) return;
        const std::string e = kill ? core::procacts::kill(m_app, key) : core::procacts::terminate(m_app, key);
        report((kill ? "Killed " : "Asked ") + q(name) + (kill ? "" : " to quit"), e);
    });
}

void Shell::on_undo() {  // acts: Ctrl+Z
    if (elevate::busy()) return;
    m_knobs.clear_error();
    m_app.clear_error();
    const auto r = m_journal.undo();
    if (!r.done) return;
    for (const auto& ref : r.selection)
        if (ref.kind == core::Ref::Kind::Proc) { m_proc_page.select_pid(ref.proc.pid); break; }
    m_trails_page.refresh_marks();
    m_services_page.refresh();
    m_trails_page.refresh_triggers();
    report("Undid " + r.label, !m_app.last_error().empty() ? m_app.last_error() : m_knobs.last_error());
}

void Shell::on_redo() {  // acts: Ctrl+Shift+Z
    if (elevate::busy()) return;
    m_knobs.clear_error();
    m_app.clear_error();
    const auto r = m_journal.redo();
    if (!r.done) return;
    for (const auto& ref : r.selection)
        if (ref.kind == core::Ref::Kind::Proc) { m_proc_page.select_pid(ref.proc.pid); break; }
    m_trails_page.refresh_marks();
    m_services_page.refresh();
    m_trails_page.refresh_triggers();
    report("Redid " + r.label, !m_app.last_error().empty() ? m_app.last_error() : m_knobs.last_error());
}

// ── s011: systemd units ─────────────────────────────────────────────────────

void Shell::on_unit_op(core::UnitOp op) {  // acts: confirm, then start / stop / restart
    if (Systemd::busy() || elevate::busy()) return;
    const auto u = m_services_page.picked();
    if (!u) return;
    const char* verb = op == core::UnitOp::Start ? "Start" : op == core::UnitOp::Stop ? "Stop" : "Restart";
    const std::string what = std::string(verb) + " " + q(u->key.name) + "?";
    std::string detail = u->row.description.empty() ? std::string() : u->row.description + ". ";
    detail += op == core::UnitOp::Start   ? "It starts now; whether it starts at boot is Enable / Disable."
            : op == core::UnitOp::Stop    ? "It stops now. This can't be undone -- Start runs it again as a new process."
                                          : "It stops and starts again now. This can't be undone.";
    if (u->key.scope == core::UnitScope::System) detail += " A system unit may ask for your password.";
    dialogs::confirm(*this, what, detail, verb, [this, key = u->key, op, verb = std::string(verb)] {
        const std::string e = op == core::UnitOp::Start ? core::units::start(m_app, key)
                            : op == core::UnitOp::Stop  ? core::units::stop(m_app, key)
                                                        : core::units::restart(m_app, key);
        report(verb + std::string(" sent to ") + q(key.name), e);
        // The job runs on; look again in a moment.
        Glib::signal_timeout().connect_once([this] { m_services_page.refresh(); m_services_page.reload_log(); }, 1200);
    });
}

void Shell::on_unit_enable(bool on) {  // acts: enable / disable (undoable)
    if (Systemd::busy() || elevate::busy()) return;
    const auto u = m_services_page.picked();
    if (!u) return;
    const std::string e = on ? core::units::enable(m_journal, m_app, u->key) : core::units::disable(m_journal, m_app, u->key);
    report((on ? "Enabled " : "Disabled ") + q(u->key.name) + " (Ctrl+Z undoes)", e);
    m_services_page.refresh();
}

}  // namespace trails

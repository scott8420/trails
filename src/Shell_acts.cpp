#include "Shell.hpp"
#include "ActDialogs.hpp"
#include "Elevate.hpp"
#include "Log.hpp"

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
}

void Shell::report(const std::string& done, const std::string& err) {  // acts: status + log
    auto lg = log::get(log::Area::Shell);
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
    dialogs::ask_cpus(*this, p->name, p->pid, cur->cpus, [this, key, name = p->name](std::vector<int> cpus) {
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
    const auto r = m_journal.undo();
    if (!r.done) return;
    for (const auto& ref : r.selection)
        if (ref.kind == core::Ref::Kind::Proc) { m_proc_page.select_pid(ref.proc.pid); break; }
    report("Undid " + r.label, m_knobs.last_error());
}

void Shell::on_redo() {  // acts: Ctrl+Shift+Z
    if (elevate::busy()) return;
    m_knobs.clear_error();
    const auto r = m_journal.redo();
    if (!r.done) return;
    for (const auto& ref : r.selection)
        if (ref.kind == core::Ref::Kind::Proc) { m_proc_page.select_pid(ref.proc.pid); break; }
    report("Redid " + r.label, m_knobs.last_error());
}

}  // namespace trails

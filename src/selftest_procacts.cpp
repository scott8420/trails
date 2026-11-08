// selftest -- s006 process actions: the knob store, the gate over every verb,
// pid reuse, refusals, and the real syscalls on a child we fork.
#include "core/ProcActs.hpp"
#include "core/AppStore.hpp"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>

namespace core = trails::core;
namespace pa = core::procacts;
using CheckFn = std::function<void(const std::string&, bool, const std::string&)>;
struct Check {
    const CheckFn& f;
    void operator()(const std::string& n, bool ok, const std::string& d = "") const { f(n, ok, d); }
};

namespace {

class FakeOS : public core::ProcOS {
public:
    struct P { std::uint64_t start; core::ProcKnobs k; };
    std::map<int, P> procs;
    bool deny = false;
    int  calls = 0;

    std::optional<core::ProcKnobs> read(const core::ProcKey& k) const override {
        auto it = procs.find(k.pid);
        if (it == procs.end() || it->second.start != k.start) return std::nullopt;
        return it->second.k;
    }
    core::ProcKnobs* at(const core::ProcKey& k) {
        auto it = procs.find(k.pid);
        return it == procs.end() || it->second.start != k.start ? nullptr : &it->second.k;
    }
    std::string set_nice(const core::ProcKey& k, int n) override {
        ++calls;
        if (deny) return "Not permitted.";
        auto* p = at(k); if (!p) return "The process has exited.";
        p->nice = n; return {};
    }
    std::string set_cpus(const core::ProcKey& k, const std::vector<int>& c) override {
        ++calls;
        if (deny) return "Not permitted.";
        auto* p = at(k); if (!p) return "The process has exited.";
        p->cpus = c; return {};
    }
    std::string signal(const core::ProcKey& k, int sig) override {
        ++calls;
        if (deny) return "Not permitted.";
        auto* p = at(k); if (!p) return "The process has exited.";
        if (sig == SIGSTOP) p->stopped = true;
        else if (sig == SIGCONT) p->stopped = false;
        else if (sig == SIGTERM || sig == SIGKILL) procs.erase(k.pid);
        return {};
    }
};

struct Fix {
    FakeOS             os;
    core::ProcKnobStore ks{os};
    core::AppStore      s{ks};
    core::ProcJournal   j{s};
    core::ProcKey       k{4242, 777};
    Fix() {
        os.procs[4242] = {777, {0, {0, 1, 2, 3}, false}};
        s.set_door([this] { return j.open(); });
    }
};

}  // namespace

void run_procacts_tests(const CheckFn& check_fn) {
    const Check check{check_fn};
    std::cout << "\n-- process actions (s006) --\n";

    {   // CPU lists.
        std::vector<int> v;
        check("cpus: to text", pa::cpus_to_text({5, 0, 1, 2, 3, 3}) == "0-3,5", pa::cpus_to_text({5, 0, 1, 2, 3}));
        check("cpus: single", pa::cpus_to_text({7}) == "7", "");
        check("cpus: parse", pa::cpus_from_text("0-3,5", v) && v == std::vector<int>{0, 1, 2, 3, 5}, "");
        check("cpus: round trip", pa::cpus_from_text(pa::cpus_to_text({1, 2, 4, 6, 7}), v) &&
                                      v == std::vector<int>{1, 2, 4, 6, 7}, "");
        bool junk = false;
        for (const char* bad : {"", "a", "1-", "3-1", "1,", ",1", "1,,2", "99999"})
            junk = junk || pa::cpus_from_text(bad, v);
        check("cpus: junk refused", !junk, "");
    }

    {   // s014: the session-critical guard.
        bool ok = true;
        for (const char* n : {"gnome-shell", "Xwayland", "Xorg", "mutter", "gdm-wayland-ses", "gnome-session-b",
                              "systemd", "systemd-journal", "dbus-broker", "dbus-daemon"})
            ok = ok && pa::session_critical(n);
        for (const char* n : {"firefox", "bash", "gnome-shell-cal", "sleep", "xdg-desktop-por", "trails"})
            ok = ok && !pa::session_critical(n);   // gnome-shell-cal(endar-server) is not the shell
        check("guard: the session's own processes, and not the rest", ok, "");
    }

    // THE GATE: every verb in the table, from a fresh fixture.
    for (const auto& v : pa::verbs()) {
        Fix f;
        if (v.prepare) { v.prepare(f.j, f.s, f.k); f.j.clear(); }
        const auto before = f.os.read(f.k);
        const std::string err = v.run(f.j, f.s, f.k);
        const auto after = f.os.read(f.k);
        const auto steps = f.j.size();
        check("gate/" + v.name + ": no error", err.empty(), err);
        check("gate/" + v.name + ": through a door", f.s.strays() == 0,
              std::to_string(f.s.strays()) + " stray write(s)");
        if (v.undoable) {
            check("gate/" + v.name + ": is a step", steps > 0 && after != before);
            for (std::size_t i = 0; i < steps; ++i) f.j.undo();
            check("gate/" + v.name + ": undo puts it back exactly", f.os.read(f.k) == before);
            for (std::size_t i = 0; i < steps; ++i) f.j.redo();
            check("gate/" + v.name + ": redo reproduces it", f.os.read(f.k) == after);
        } else {
            check("gate/" + v.name + ": is not a step (confirmed instead)", steps == 0);
            check("gate/" + v.name + ": the process is gone", !after.has_value());
        }
    }
    {   Fix f;
        f.s.put(core::Ref::of(f.k), core::Rec{core::ProcKnobs{5, {0}, false}});
        check("gate: a write around the door is a stray", f.s.strays() == 1, "");
    }

    {   // Labels name the process.
        Fix f;
        pa::renice(f.j, f.s, f.k, "firefox", 5);
        check("label: Renice “firefox”", f.j.undo_label() == "Renice “firefox”", f.j.undo_label());
        check("renice: applied", f.os.procs[4242].k.nice == 5, "");
        check("renice: same value again is no step", pa::renice(f.j, f.s, f.k, "firefox", 5).empty() &&
                                                     f.j.size() == 1, "");
        pa::renice(f.j, f.s, f.k, "firefox", 99);
        check("renice: clamped to 19", f.os.procs[4242].k.nice == 19, "");
    }
    {   // A reused pid: the step's start time no longer matches.
        Fix f;
        pa::renice(f.j, f.s, f.k, "a", 3);
        f.os.procs[4242] = {999, {0, {0, 1, 2, 3}, false}};   // exited; the pid was reused
        const auto r = f.j.undo();
        check("reuse: undo does not touch the stranger", f.os.procs[4242].k.nice == 0 && r.done, "");
        check("reuse: the store says why", f.ks.last_error() == "The process has exited.", f.ks.last_error());
        const std::string e = pa::stop(f.j, f.s, core::ProcKey{4242, 777}, "a");
        check("reuse: a verb on the old key refuses", !e.empty() && !f.os.procs[4242].k.stopped, e);
    }
    {   // Refused (password dismissed): an error, and no step.
        Fix f;
        f.os.deny = true;
        const std::string e = pa::renice(f.j, f.s, f.k, "a", -5);
        check("refused: error returned", e == "Not permitted.", e);
        check("refused: no step kept", f.j.size() == 0, std::to_string(f.j.size()));
        const std::string t = pa::terminate(f.s, f.k);
        check("refused: terminate reports it", t == "Not permitted." && f.os.read(f.k).has_value(), t);
    }
    {   // Affinity: empty refused, order normalised.
        Fix f;
        check("affinity: empty refused", !pa::set_affinity(f.j, f.s, f.k, "a", {}).empty() && f.j.size() == 0, "");
        pa::set_affinity(f.j, f.s, f.k, "a", {3, 1, 1});
        check("affinity: sorted, unique", f.os.procs[4242].k.cpus == std::vector<int>{1, 3}, "");
    }

    // ── the real thing: a child we fork ─────────────────────────────────────
    {
        const pid_t child = ::fork();
        if (child == 0) { for (;;) ::pause(); }
        std::uint64_t start = 0;
        for (int i = 0; i < 100 && !pa::read_start("/", child, start); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        int elevated = 0;
        core::SysProcOS os("/", [&](const std::vector<std::string>&) { ++elevated; return std::string("denied"); });
        core::ProcKnobStore ks(os);
        core::AppStore s(ks);
        core::ProcJournal j(s);
        s.set_door([&] { return j.open(); });
        const core::ProcKey k{child, start};

        const auto first = os.read(k);
        check("real: reads the child", first.has_value() && !first->cpus.empty() && !first->stopped, "");
        check("real: a wrong start time reads as gone", !os.read({child, start + 1}).has_value(), "");

        std::string e = pa::renice(j, s, k, "child", first ? first->nice + 5 : 5);
        check("real: renice up (no privilege needed)", e.empty() && os.read(k) && os.read(k)->nice == first->nice + 5, e);
        e = pa::stop(j, s, k, "child");
        check("real: stop", e.empty() && os.read(k) && os.read(k)->stopped, e);
        const bool multi = first && first->cpus.size() > 1;
        if (multi) {
            e = pa::set_affinity(j, s, k, "child", {first->cpus.front()});
            check("real: affinity to one CPU", e.empty() && os.read(k)->cpus == std::vector<int>{first->cpus.front()}, e);
            j.undo();
            check("real: undo affinity", os.read(k)->cpus == first->cpus, "");
        }
        j.undo();
        check("real: undo stop continues it", os.read(k) && !os.read(k)->stopped, "");
        j.undo();   // lowering nice: root does it directly, a user goes to the elevator
        if (::geteuid() == 0)
            check("real: undo renice (root, direct)", os.read(k)->nice == first->nice && elevated == 0, "");
        else
            check("real: undo renice asks the elevator", elevated == 1 && ks.last_error() == "denied", ks.last_error());

        e = pa::terminate(s, k);
        int status = 0;
        const pid_t w = ::waitpid(child, &status, 0);
        check("real: terminate", e.empty() && w == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM, e);
        check("real: gone after reaping", !os.read(k).has_value(), "");
        e = pa::renice(j, s, k, "child", 3);
        check("real: a verb on the dead refuses", e == "The process has exited.", e);
    }
}

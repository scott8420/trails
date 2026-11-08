#pragma once
// core::ProcActs -- process actions (s006). GTK-free; the selftest drives it.
//
// ── what is undoable, and what is not (Scott's s006 call) ──────────────────
//   renice, affinity, stop / continue: a process's KNOBS. They go through the
//     journal (the one door): a step records the knobs before and after, and
//     Ctrl+Z puts the before back. No per-verb inverse.
//   terminate, kill: the process is gone and nothing puts it back. These do
//     NOT go through the journal -- they confirm first (the UI's dialog names
//     the process) and go through the store's other door, end(). Still listed
//     in verbs(), flagged undoable = false, so the gate walks them too.
//
// ── identity: pid + start time ─────────────────────────────────────────────
// A pid is reused. A step held for ten minutes must not renice the stranger
// who got the number since, so the record id is (pid, starttime) -- field 22
// of /proc/<pid>/stat -- and get() answers "absent" when the start time no
// longer matches. The helper re-checks it as root, too.
//
// ── privilege ──────────────────────────────────────────────────────────────
// SysProcOS tries the syscall first (own processes: stop, continue, nice UP,
// affinity). On EPERM / EACCES it asks the Elevator -- the UI's pkexec of
// trails-helper -- to do the same thing as root. Lowering nice is privileged
// even on your own process, so UNDO of a renice may ask for the password.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Undo.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace trails::core {

struct ProcKey {
    int           pid = 0;
    std::uint64_t start = 0;   // clock ticks since boot (stat field 22)
    bool operator==(const ProcKey&) const = default;
};

// The knobs a step records. A value, compared whole.
struct ProcKnobs {
    int              nice = 0;      // -20 .. 19
    std::vector<int> cpus;          // allowed CPUs, ascending
    bool             stopped = false;
    bool operator==(const ProcKnobs&) const = default;
};

// What touches the kernel. Real: SysProcOS. Selftest: a fake.
class ProcOS {
public:
    virtual ~ProcOS() = default;
    virtual std::optional<ProcKnobs> read(const ProcKey& k) const = 0;   // nullopt: gone / reused
    // Each returns "" on success, else a sentence for the user.
    virtual std::string set_nice(const ProcKey& k, int nice) = 0;
    virtual std::string set_cpus(const ProcKey& k, const std::vector<int>& cpus) = 0;
    virtual std::string signal(const ProcKey& k, int sig) = 0;
};

// Runs trails-helper as root: argv after the helper's own path. Returns "" on
// success, else a sentence ("Authentication was dismissed").
using Elevator = std::function<std::string(const std::vector<std::string>& args)>;

class SysProcOS : public ProcOS {
public:
    explicit SysProcOS(std::string root = "/", Elevator up = {})
        : m_root(std::move(root)), m_up(std::move(up)) {}
    void set_elevator(Elevator up) { m_up = std::move(up); }

    std::optional<ProcKnobs> read(const ProcKey& k) const override;
    std::string set_nice(const ProcKey& k, int nice) override;
    std::string set_cpus(const ProcKey& k, const std::vector<int>& cpus) override;
    std::string signal(const ProcKey& k, int sig) override;

private:
    std::string elevate(const std::vector<std::string>& args, const std::string& direct_err);
    std::string m_root;
    Elevator    m_up;
};

// ── the store the journal writes through ───────────────────────────────────
class ProcKnobStore {
public:
    using Id    = ProcKey;
    using Value = ProcKnobs;

    explicit ProcKnobStore(ProcOS& os) : m_os(os) {}

    std::optional<ProcKnobs> get(const ProcKey& k) const { return m_os.read(k); }
    // Applies what differs: stopped, then nice, then cpus. A failure (the
    // process exited, the password was dismissed) is kept in last_error();
    // the journal then sees after == before and keeps no step.
    void put(const ProcKey& k, const std::optional<ProcKnobs>& v);

    // The other door: terminate / kill. Not a step -- nothing undoes it.
    std::string end(const ProcKey& k, int sig);

    const std::string& last_error() const { return m_err; }
    void clear_error() { m_err.clear(); }

    void set_door(std::function<bool()> open) { m_door = std::move(open); }
    int  strays() const { return m_strays; }

private:
    ProcOS&               m_os;
    std::string           m_err;
    std::function<bool()> m_door;
    int                   m_strays = 0;
};

// The verbs write through the app-wide store (core/AppStore.hpp, s009) so
// ONE journal holds every undoable step.
class AppStore;
using ProcJournal = Journal<AppStore>;

namespace procacts {

// "0-3,5" <-> {0,1,2,3,5}. parse returns false on junk.
std::string cpus_to_text(const std::vector<int>& cpus);
bool        cpus_from_text(const std::string& text, std::vector<int>& out);

// s014: a process your desktop session stands on (gnome-shell, Xwayland,
// Xorg, mutter, gdm*, gnome-session*, systemd*, dbus*). Stopping one freezes
// the desktop -- Trails too, if it runs there -- so the UI asks first, and
// warns before terminate / kill. `comm` is the kernel's 15-char name.
bool session_critical(const std::string& comm);

// Start time of a live pid ("" root = "/"); false if gone.
bool read_start(const std::string& root, int pid, std::uint64_t& start);

// The verbs. Each returns "" on success, else what went wrong. `name` labels
// the step: Renice “firefox”.
std::string renice(ProcJournal& j, AppStore& s, const ProcKey& k, const std::string& name, int nice);
std::string set_affinity(ProcJournal& j, AppStore& s, const ProcKey& k, const std::string& name,
                         const std::vector<int>& cpus);
std::string stop(ProcJournal& j, AppStore& s, const ProcKey& k, const std::string& name);
std::string resume(ProcJournal& j, AppStore& s, const ProcKey& k, const std::string& name);
std::string terminate(AppStore& s, const ProcKey& k);   // SIGTERM, confirmed by the UI
std::string kill(AppStore& s, const ProcKey& k);        // SIGKILL, confirmed by the UI

// The verb table the selftest walks. Add a verb here in the same change that
// adds its button (RULES "Undo from the first write").
struct Verb {
    std::string name;
    bool        undoable = true;
    std::function<std::string(ProcJournal&, AppStore&, const ProcKey&)> run;
    // Fixture set-up the verb needs first (continue: a stopped process).
    // The gate runs it, then clears the journal, then checks the verb alone.
    std::function<void(ProcJournal&, AppStore&, const ProcKey&)> prepare = {};
};
const std::vector<Verb>& verbs();

}  // namespace procacts
}  // namespace trails::core

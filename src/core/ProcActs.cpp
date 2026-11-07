// core::ProcActs -- see the header for the why.
#include "core/ProcActs.hpp"
#include "core/AppStore.hpp"
#include "core/probe/Probe.hpp"

#include <sched.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <memory>
#include <thread>

namespace trails::core {

namespace {

constexpr int kMaxCpus = 4096;

struct CpuSet {
    cpu_set_t*  set;
    std::size_t size;
    CpuSet() : set(CPU_ALLOC(kMaxCpus)), size(CPU_ALLOC_SIZE(kMaxCpus)) { CPU_ZERO_S(size, set); }
    ~CpuSet() { CPU_FREE(set); }
    CpuSet(const CpuSet&) = delete;
    CpuSet& operator=(const CpuSet&) = delete;
};

std::string errno_text(int e) {
    switch (e) {
        case ESRCH:  return "The process has exited.";
        case EPERM:
        case EACCES: return "Not permitted.";
        case EINVAL: return "The kernel refused the value.";
        default:     return std::strerror(e);
    }
}

const char* sig_word(int sig) {
    switch (sig) {
        case SIGTERM: return "TERM";
        case SIGKILL: return "KILL";
        case SIGSTOP: return "STOP";
        case SIGCONT: return "CONT";
        default:      return nullptr;
    }
}

}  // namespace

// ── SysProcOS ───────────────────────────────────────────────────────────────

std::optional<ProcKnobs> SysProcOS::read(const ProcKey& k) const {
    probe::ProcStat ps;
    if (!probe::read_proc(m_root, k.pid, 4096, ps) || ps.starttime != k.start) return std::nullopt;
    ProcKnobs v;
    v.nice    = static_cast<int>(ps.nice);
    v.stopped = ps.state == 'T';
    CpuSet cs;
    if (::sched_getaffinity(k.pid, cs.size, cs.set) == 0)
        for (int c = 0; c < kMaxCpus; ++c)
            if (CPU_ISSET_S(static_cast<std::size_t>(c), cs.size, cs.set)) v.cpus.push_back(c);
    return v;
}

std::string SysProcOS::elevate(const std::vector<std::string>& args, const std::string& direct_err) {
    if (!m_up) return direct_err + " The privileged helper is not available.";
    return m_up(args);
}

std::string SysProcOS::set_nice(const ProcKey& k, int nice) {
    if (!read(k)) return "The process has exited.";
    if (::setpriority(PRIO_PROCESS, static_cast<id_t>(k.pid), nice) == 0) return {};
    const int e = errno;
    if (e != EPERM && e != EACCES) return errno_text(e);
    return elevate({"nice", std::to_string(k.pid), std::to_string(k.start), std::to_string(nice)},
                   errno_text(e));
}

std::string SysProcOS::set_cpus(const ProcKey& k, const std::vector<int>& cpus) {
    if (cpus.empty()) return "A process needs at least one CPU.";
    if (!read(k)) return "The process has exited.";
    CpuSet cs;
    for (int c : cpus)
        if (c >= 0 && c < kMaxCpus) CPU_SET_S(static_cast<std::size_t>(c), cs.size, cs.set);
    if (::sched_setaffinity(k.pid, cs.size, cs.set) == 0) return {};
    const int e = errno;
    if (e != EPERM && e != EACCES) return errno_text(e);
    return elevate({"cpus", std::to_string(k.pid), std::to_string(k.start), procacts::cpus_to_text(cpus)},
                   errno_text(e));
}

std::string SysProcOS::signal(const ProcKey& k, int sig) {
    const char* word = sig_word(sig);
    if (!word) return "Unsupported signal.";
    if (!read(k)) return "The process has exited.";
    std::string err;
    if (::kill(k.pid, sig) != 0) {
        const int e = errno;
        if (e != EPERM && e != EACCES) return errno_text(e);
        err = elevate({"signal", std::to_string(k.pid), std::to_string(k.start), word}, errno_text(e));
        if (!err.empty()) return err;
    }
    // STOP / CONT land asynchronously; the journal reads the AFTER picture
    // right away, so wait (briefly) until /proc shows it.
    if (sig == SIGSTOP || sig == SIGCONT) {
        const bool want = sig == SIGSTOP;
        for (int i = 0; i < 100; ++i) {
            auto v = read(k);
            if (!v || v->stopped == want) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    return {};
}

// ── the store ───────────────────────────────────────────────────────────────

void ProcKnobStore::put(const ProcKey& k, const std::optional<ProcKnobs>& v) {
    if (m_door && !m_door()) ++m_strays;
    if (!v) return;   // processes are never created through the journal
    const auto cur = m_os.read(k);
    if (!cur) { m_err = "The process has exited."; return; }
    std::string e;
    if (v->stopped != cur->stopped) e = m_os.signal(k, v->stopped ? SIGSTOP : SIGCONT);
    if (e.empty() && v->nice != cur->nice) e = m_os.set_nice(k, v->nice);
    if (e.empty() && v->cpus != cur->cpus) e = m_os.set_cpus(k, v->cpus);
    if (!e.empty()) m_err = e;
}

std::string ProcKnobStore::end(const ProcKey& k, int sig) {
    m_err = m_os.signal(k, sig);
    return m_err;
}

// ── verbs ───────────────────────────────────────────────────────────────────
namespace procacts {

std::string cpus_to_text(const std::vector<int>& cpus) {
    std::vector<int> c = cpus;
    std::sort(c.begin(), c.end());
    c.erase(std::unique(c.begin(), c.end()), c.end());
    std::string out;
    for (std::size_t i = 0; i < c.size();) {
        std::size_t j = i;
        while (j + 1 < c.size() && c[j + 1] == c[j] + 1) ++j;
        if (!out.empty()) out += ',';
        out += std::to_string(c[i]);
        if (j > i) out += '-' + std::to_string(c[j]);
        i = j + 1;
    }
    return out;
}

bool cpus_from_text(const std::string& text, std::vector<int>& out) {
    out.clear();
    std::size_t i = 0;
    auto num = [&](int& n) {
        if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return false;
        long v = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            v = v * 10 + (text[i++] - '0');
            if (v >= kMaxCpus) return false;
        }
        n = static_cast<int>(v);
        return true;
    };
    while (i < text.size()) {
        int a = 0, b = 0;
        if (!num(a)) return false;
        b = a;
        if (i < text.size() && text[i] == '-') { ++i; if (!num(b) || b < a) return false; }
        for (int c = a; c <= b; ++c) out.push_back(c);
        if (i < text.size()) { if (text[i] != ',') return false; ++i; if (i == text.size()) return false; }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return !out.empty();
}

bool read_start(const std::string& root, int pid, std::uint64_t& start) {
    probe::ProcStat ps;
    if (!probe::read_proc(root.empty() ? "/" : root, pid, 4096, ps)) return false;
    start = ps.starttime;
    return true;
}

namespace {
// One knob change as one step. `edit` changes a copy of the current knobs.
template <class Edit>
std::string knob(ProcJournal& j, AppStore& app, const ProcKey& k, const std::string& label, Edit edit) {
    ProcKnobStore& s = app.knobs();
    s.clear_error();
    j.run(label, {Ref::of(k)}, [&] {
        auto cur = s.get(k);
        if (!cur) return;
        ProcKnobs v = *cur;
        edit(v);
        app.put(Ref::of(k), Rec{v});
    });
    if (!s.last_error().empty()) return s.last_error();
    if (!s.get(k)) return "The process has exited.";
    return {};
}
std::string q(const std::string& name) { return "“" + name + "”"; }
}  // namespace

std::string renice(ProcJournal& j, AppStore& s, const ProcKey& k, const std::string& name, int nice) {
    nice = std::clamp(nice, -20, 19);
    return knob(j, s, k, "Renice " + q(name), [nice](ProcKnobs& v) { v.nice = nice; });
}
std::string set_affinity(ProcJournal& j, AppStore& s, const ProcKey& k, const std::string& name,
                         const std::vector<int>& cpus) {
    if (cpus.empty()) return "A process needs at least one CPU.";
    std::vector<int> c = cpus;
    std::sort(c.begin(), c.end());
    c.erase(std::unique(c.begin(), c.end()), c.end());
    return knob(j, s, k, "CPU affinity " + q(name), [&c](ProcKnobs& v) { v.cpus = c; });
}
std::string stop(ProcJournal& j, AppStore& s, const ProcKey& k, const std::string& name) {
    return knob(j, s, k, "Stop " + q(name), [](ProcKnobs& v) { v.stopped = true; });
}
std::string resume(ProcJournal& j, AppStore& s, const ProcKey& k, const std::string& name) {
    return knob(j, s, k, "Continue " + q(name), [](ProcKnobs& v) { v.stopped = false; });
}
std::string terminate(AppStore& s, const ProcKey& k) { return s.knobs().end(k, SIGTERM); }
std::string kill(AppStore& s, const ProcKey& k) { return s.knobs().end(k, SIGKILL); }

const std::vector<Verb>& verbs() {
    static const std::vector<Verb> v = {
        {"renice", true, [](ProcJournal& j, AppStore& s, const ProcKey& k) {
             const auto c = s.knobs().get(k);
             return renice(j, s, k, "p", c ? (c->nice < 19 ? c->nice + 5 : 10) : 5);
         }},
        {"affinity", true, [](ProcJournal& j, AppStore& s, const ProcKey& k) {
             return set_affinity(j, s, k, "p", {0});
         }},
        {"stop", true, [](ProcJournal& j, AppStore& s, const ProcKey& k) { return stop(j, s, k, "p"); }},
        {"continue", true, [](ProcJournal& j, AppStore& s, const ProcKey& k) { return resume(j, s, k, "p"); },
         // A continue needs a stopped process.
         [](ProcJournal& j, AppStore& s, const ProcKey& k) { stop(j, s, k, "p"); }},
        {"terminate", false, [](ProcJournal&, AppStore& s, const ProcKey& k) { return terminate(s, k); }},
        {"kill", false, [](ProcJournal&, AppStore& s, const ProcKey& k) { return kill(s, k); }},
    };
    return v;
}

}  // namespace procacts
}  // namespace trails::core

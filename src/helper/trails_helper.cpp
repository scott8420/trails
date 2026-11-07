// trails-helper -- the ONE thing Trails runs as root (s006).
//
// Run by pkexec (polkit action io.github.scott8420.Trails.process), never by
// hand. It does exactly one process action and exits:
//
//   trails-helper nice   <pid> <starttime> <-20..19>
//   trails-helper cpus   <pid> <starttime> <cpu list, e.g. 0-3,6>
//   trails-helper signal <pid> <starttime> TERM|KILL|STOP|CONT
//
// The start time is the pid's identity: if the number now belongs to some
// other process, it refuses (exit 3). It will not touch pid 1, and it knows no
// other verbs or signals. Exit: 0 done, 2 bad arguments, 3 no such process,
// 4 the kernel refused. One line on stderr says why.
#include "core/ProcActs.hpp"

#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace pa = trails::core::procacts;

namespace {

int fail(int code, const std::string& msg) {
    std::fprintf(stderr, "trails-helper: %s\n", msg.c_str());
    return code;
}

bool to_long(const char* s, long long& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    errno = 0;
    out = std::strtoll(s, &end, 10);
    return errno == 0 && end && *end == '\0';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) return fail(2, "usage: trails-helper nice|cpus|signal PID STARTTIME VALUE");
    const std::string verb = argv[1];
    long long pid = 0, start = 0;
    if (!to_long(argv[2], pid) || pid <= 1 || pid > 0x3fffffff) return fail(2, "bad pid");
    if (!to_long(argv[3], start) || start < 0) return fail(2, "bad start time");

    std::uint64_t now = 0;
    if (!pa::read_start("/", static_cast<int>(pid), now) || now != static_cast<std::uint64_t>(start))
        return fail(3, "the process has exited");

    if (verb == "nice") {
        long long n = 0;
        if (!to_long(argv[4], n) || n < -20 || n > 19) return fail(2, "nice must be -20..19");
        if (::setpriority(PRIO_PROCESS, static_cast<id_t>(pid), static_cast<int>(n)) != 0)
            return fail(4, std::strerror(errno));
        return 0;
    }
    if (verb == "cpus") {
        std::vector<int> cpus;
        if (!pa::cpus_from_text(argv[4], cpus)) return fail(2, "bad cpu list");
        const int max = 4096;
        cpu_set_t* set = CPU_ALLOC(max);
        const std::size_t size = CPU_ALLOC_SIZE(max);
        CPU_ZERO_S(size, set);
        for (int c : cpus) CPU_SET_S(static_cast<std::size_t>(c), size, set);
        const int r = ::sched_setaffinity(static_cast<pid_t>(pid), size, set);
        const int e = errno;
        CPU_FREE(set);
        if (r != 0) return fail(4, std::strerror(e));
        return 0;
    }
    if (verb == "signal") {
        const std::string w = argv[4];
        int sig = w == "TERM" ? SIGTERM : w == "KILL" ? SIGKILL : w == "STOP" ? SIGSTOP
                : w == "CONT" ? SIGCONT : 0;
        if (!sig) return fail(2, "signal must be TERM, KILL, STOP or CONT");
        if (::kill(static_cast<pid_t>(pid), sig) != 0) return fail(4, std::strerror(errno));
        return 0;
    }
    return fail(2, "unknown verb '" + verb + "'");
}

// /proc/<pid>/stat + cmdline, and the pid walk.
#include "core/probe/Probe.hpp"
#include "Parse.hpp"

#include <dirent.h>
#include <sys/stat.h>

namespace trails::core::probe {
using namespace detail;

bool parse_proc_stat(std::string_view text, long page_size, ProcStat& out) {
    out = {};
    // "pid (comm) S ppid ..." -- comm is free text and may hold spaces and
    // parentheses, so it runs from the FIRST '(' to the LAST ')'.
    std::size_t lp = text.find('(');
    std::size_t rp = text.rfind(')');
    if (lp == std::string_view::npos || rp == std::string_view::npos || rp < lp) return false;
    std::string_view head = text.substr(0, lp);
    if (!next_num(head, out.pid)) return false;
    out.comm = std::string(text.substr(lp + 1, rp - lp - 1));

    std::string_view r = text.substr(rp + 1);
    // Fields numbered as proc(5): 3 state, 4 ppid, 9 flags, 14 utime, 15 stime,
    // 19 nice, 20 num_threads, 22 starttime, 24 rss.
    std::string_view st = next_tok(r);
    if (st.empty()) return false;
    out.state = st.front();
    std::uint64_t f[22] = {};   // f[k] = field (k + 4)
    std::int64_t nice = 0;
    for (int k = 0; k < 21; ++k) {
        std::string_view t = next_tok(r);
        if (t.empty()) return false;
        if (k == 15) { if (!to_num(t, nice)) return false; continue; }   // field 19, signed
        if (k == 0)  { if (!to_num(t, out.ppid)) return false; continue; }
        if (k == 1 || k == 2 || k == 3 || k == 4 || k == 12 || k == 13 || k == 14) {
            // pgrp, session, tty_nr, tpgid (-1), cutime, cstime, priority: signed, unused
            continue;
        }
        if (!to_num(t, f[k])) return false;
    }
    const std::uint64_t flags = f[5];
    out.utime     = f[10];
    out.stime     = f[11];
    out.nice      = nice;
    out.threads   = static_cast<int>(f[16]);
    out.starttime = f[18];
    out.rss_bytes = f[20] * static_cast<std::uint64_t>(page_size > 0 ? page_size : 4096);
    out.kernel_thread = (flags & 0x00200000u) != 0;   // PF_KTHREAD
    return true;
}

bool read_proc(const std::string& root, int pid, long page_size, ProcStat& out) {
    std::string dir = at(root, "proc/" + std::to_string(pid));
    std::string text;
    if (!slurp(dir + "/stat", text) || !parse_proc_stat(text, page_size, out)) {
        out = {};
        return false;   // raced with exit: normal, not an error
    }
    struct stat st{};
    if (::stat(dir.c_str(), &st) == 0) out.uid = st.st_uid;
    return true;
}

std::vector<int> list_pids(const std::string& root) {
    std::vector<int> pids;
    DIR* d = ::opendir(at(root, "proc").c_str());
    if (!d) return pids;
    while (dirent* e = ::readdir(d)) {
        std::string_view n = e->d_name;
        int pid = 0;
        if (to_num(n, pid) && pid > 0 && std::to_string(pid) == n) pids.push_back(pid);
    }
    ::closedir(d);
    return pids;
}

std::string read_cmdline(const std::string& root, int pid) {
    std::string text;
    if (!slurp(at(root, "proc/" + std::to_string(pid) + "/cmdline"), text)) return {};
    while (!text.empty() && text.back() == '\0') text.pop_back();
    for (char& c : text) if (c == '\0') c = ' ';
    return text;
}

}  // namespace trails::core::probe

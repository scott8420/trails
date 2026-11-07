#pragma once
#include "core/Sampler.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core::procview -- the GTK-free half of the Processes tab (s005): the tree
// shape, the filter test, state words, user names, open-file counts. The page
// only wires these into a ColumnView, so the selftest covers the logic.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::core::procview {

// Parent -> children by ppid. A process whose parent isn't in the table
// (pid 1, kthreadd, or a parent that exited) is a root. Children in pid order.
struct Index {
    std::vector<int>                              roots;
    std::unordered_map<int, std::vector<int>>     children;   // pid -> child pids
};
Index build_index(const std::vector<ProcRow>& procs);

// Case-insensitive substring match on name, cmdline, user, or the pid's digits.
// Empty query matches everything.
bool matches(const ProcRow& p, std::string_view user, std::string_view query);

// "R" -> "Running", "S" -> "Sleeping", "D" -> "Disk wait", ... ; unknown -> "?"
const char* state_name(char state);

// getpwuid, cached (users don't change under a running monitor often enough
// to matter; a miss caches the number as text).
const std::string& user_name(std::uint32_t uid);

}  // namespace trails::core::procview

namespace trails::core::probe {
// Entries in /proc/<pid>/fd, and how many are sockets. False if unreadable
// (another user's process without root) -- the UI shows "--", not 0.
bool count_fds(const std::string& root, int pid, int& files, int& sockets);
}  // namespace trails::core::probe

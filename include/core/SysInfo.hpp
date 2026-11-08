#pragma once
#include <string>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core::SysInfo -- the About window's System tab (s015c), GTK-free.
// What a bug report wants: OS, kernel, hardware, CPUs, memory, desktop,
// session. Read under `root` (fixture trees in the selftest) except uname
// and the environment, which are the live process's.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::core {

using InfoRows = std::vector<std::pair<std::string, std::string>>;

// os-release's PRETTY_NAME (etc/os-release, else usr/lib/os-release); "" if none.
std::string os_pretty_name(const std::string& root);

// The rows, in display order. Absent facts are left out, not shown as "?".
InfoRows system_info(const std::string& root, int n_cpus);

// "Label: value" lines, for the clipboard.
std::string info_text(const InfoRows& rows);

}  // namespace trails::core

#pragma once
// Elevate -- run trails-helper as root through pkexec (s006).
//
// The core's SysProcOS calls this (as its Elevator) when a syscall answers
// EPERM: another user's process, or lowering a nice value. It blocks the
// CALLER but not the window: it spins the main loop while pkexec waits on the
// polkit password dialog, so the screen keeps painting. busy() is true during
// that spin -- the action handlers refuse to start a second action inside it.
//
// The helper is found only at fixed root-owned paths (install.sh puts it
// there). A helper that is not owned by root, or is writable by anyone but
// root, is refused: pkexec running a user-writable binary is a root hole.
#include <string>
#include <vector>

namespace trails::elevate {

// The helper's path, or "" (with why) if none is installed safely.
std::string helper_path(std::string* why = nullptr);

// pkexec <helper> args... ; "" on success, else one sentence for the user.
std::string run_helper(const std::vector<std::string>& args);

bool busy();

}  // namespace trails::elevate

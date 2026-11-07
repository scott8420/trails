#pragma once
// ActDialogs -- the small modal questions process actions ask (s006).
// Each calls `done` only on OK / the confirming button; Cancel, Escape and
// closing the window call nothing. Windows delete themselves when hidden.
#include <gtkmm/window.h>

#include <functional>
#include <string>
#include <vector>

namespace trails::dialogs {

// Renice: a -20..19 spin starting at `current`.
void ask_nice(Gtk::Window& parent, const std::string& name, int pid, int current,
              std::function<void(int)> done);

// CPU affinity: one check per CPU, `current` ticked. OK needs at least one.
void ask_cpus(Gtk::Window& parent, const std::string& name, int pid, const std::vector<int>& current,
              std::function<void(std::vector<int>)> done);

// Terminate / kill: these cannot be undone, so they ask, naming the process.
// Cancel is the default button.
void confirm_end(Gtk::Window& parent, const std::string& name, int pid, bool kill,
                 std::function<void()> yes);

}  // namespace trails::dialogs

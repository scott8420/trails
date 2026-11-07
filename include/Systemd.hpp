#pragma once
// Systemd -- the Services tab's D-Bus side (s011). App layer (giomm).
//
// org.freedesktop.systemd1.Manager on the system bus (system units) and the
// session bus (your user units):
//   ListUnits + ListUnitFiles   -> core::merge_units, async (the list refreshes
//                                  without blocking the window)
//   GetUnitFileState            -> UnitBus::file_state (the journal's get)
//   EnableUnitFiles / DisableUnitFiles + Reload -> set_enabled
//   StartUnit / StopUnit / RestartUnit ("replace") -> control
// Calls that change something allow interactive authorization: systemd asks
// polkit, GNOME shows the password dialog, the window keeps painting (the
// call spins the main loop; busy() is true meanwhile).
// The unit's recent log comes from `journalctl -u` (async).
#include "core/Units.hpp"

#include <giomm/dbusconnection.h>

#include <functional>
#include <string>
#include <vector>

namespace trails {

class Systemd : public core::UnitBus {
public:
    using ListDone = std::function<void(std::vector<core::UnitRow> rows, std::string err)>;
    using TextDone = std::function<void(std::string text)>;

    void list(core::UnitScope scope, ListDone done);                  // async
    void journal(const core::UnitKey& k, int lines, TextDone done);   // async

    std::string file_state(const core::UnitKey& k, std::string* err = nullptr) override;
    std::string set_enabled(const core::UnitKey& k, bool on) override;
    std::string control(const core::UnitKey& k, core::UnitOp op) override;

    static bool busy();

private:
    Glib::RefPtr<Gio::DBus::Connection> bus(core::UnitScope s, std::string* err);
    // A call that may ask for a password: async + spin the loop until done.
    std::string interactive(core::UnitScope s, const char* method, const Glib::VariantContainerBase& args);
    Glib::RefPtr<Gio::DBus::Connection> m_system, m_session;
};

}  // namespace trails

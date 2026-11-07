#include "Systemd.hpp"
#include "Log.hpp"

#include <giomm/subprocess.h>
#include <glibmm/main.h>
#include <glibmm/variant.h>

#include <memory>

namespace trails {

namespace {
constexpr const char* kDest = "org.freedesktop.systemd1";
constexpr const char* kPath = "/org/freedesktop/systemd1";
constexpr const char* kMgr  = "org.freedesktop.systemd1.Manager";
bool g_busy = false;

std::string why(const Glib::Error& e) {
    const std::string m = e.what();
    if (m.find("AccessDenied") != std::string::npos || m.find("InteractiveAuthorizationRequired") != std::string::npos ||
        m.find("not authorized") != std::string::npos || m.find("Authorization") != std::string::npos)
        return "Not authorized (the password was dismissed or refused).";
    if (m.find("NoSuchUnit") != std::string::npos) return "No such unit.";
    // "GDBus.Error:org.freedesktop.systemd1.Foo: Unit x failed" -> the sentence after the name
    const auto colon = m.find(": ");
    return colon != std::string::npos && m.rfind("GDBus.Error:", 0) == 0 ? m.substr(colon + 2) : m;
}

Glib::VariantContainerBase tuple(std::vector<Glib::VariantBase> v) {
    return Glib::VariantContainerBase::create_tuple(v);
}
Glib::VariantBase str(const std::string& s) { return Glib::Variant<Glib::ustring>::create(s); }
Glib::VariantBase flag(bool b) { return Glib::Variant<bool>::create(b); }
}  // namespace

bool Systemd::busy() { return g_busy; }

Glib::RefPtr<Gio::DBus::Connection> Systemd::bus(core::UnitScope s, std::string* err) {
    auto& c = s == core::UnitScope::System ? m_system : m_session;
    if (!c) {
        try {
            c = Gio::DBus::Connection::get_sync(s == core::UnitScope::System ? Gio::DBus::BusType::SYSTEM
                                                                              : Gio::DBus::BusType::SESSION);
        } catch (const Glib::Error& e) {
            if (err) *err = std::string("D-Bus: ") + e.what();
        }
    }
    return c;
}

void Systemd::list(core::UnitScope scope, ListDone done) {
    std::string err;
    auto c = bus(scope, &err);
    if (!c) { done({}, err.empty() ? "systemd is not reachable." : err); return; }
    // Two calls, merged when both are back.
    struct State {
        std::vector<core::UnitRow> loaded;
        std::vector<std::pair<std::string, std::string>> files;
        int pending = 2;
        std::string err;
        ListDone done;
    };
    auto st = std::make_shared<State>();
    st->done = std::move(done);
    auto finish = [st] {
        if (--st->pending > 0) return;
        st->done(st->err.empty() ? core::merge_units(st->loaded, st->files) : std::vector<core::UnitRow>{}, st->err);
    };
    c->call(kPath, kMgr, "ListUnits", Glib::VariantContainerBase(),
            [c, st, finish](Glib::RefPtr<Gio::AsyncResult>& res) {
                try {
                    auto v = c->call_finish(res);
                    GVariant* arr = g_variant_get_child_value(v.gobj(), 0);
                    GVariantIter it;
                    g_variant_iter_init(&it, arr);
                    const char *name, *desc, *load, *active, *sub, *follow, *jtype;
                    const char *upath, *jpath;
                    guint32 jid;
                    while (g_variant_iter_next(&it, "(&s&s&s&s&s&s&ou&s&o)", &name, &desc, &load, &active, &sub,
                                               &follow, &upath, &jid, &jtype, &jpath))
                        st->loaded.push_back({name, desc, load, active, sub, ""});
                    g_variant_unref(arr);
                } catch (const Glib::Error& e) { st->err = why(e); }
                finish();
            },
            kDest);
    c->call(kPath, kMgr, "ListUnitFiles", Glib::VariantContainerBase(),
            [c, st, finish](Glib::RefPtr<Gio::AsyncResult>& res) {
                try {
                    auto v = c->call_finish(res);
                    GVariant* arr = g_variant_get_child_value(v.gobj(), 0);
                    GVariantIter it;
                    g_variant_iter_init(&it, arr);
                    const char *path, *state;
                    while (g_variant_iter_next(&it, "(&s&s)", &path, &state)) st->files.emplace_back(path, state);
                    g_variant_unref(arr);
                } catch (const Glib::Error& e) { if (st->err.empty()) st->err = why(e); }
                finish();
            },
            kDest);
}

std::string Systemd::file_state(const core::UnitKey& k, std::string* err) {
    auto c = bus(k.scope, err);
    if (!c) return {};
    try {
        auto v = c->call_sync(kPath, kMgr, "GetUnitFileState", tuple({str(k.name)}), kDest, 5000);
        Glib::Variant<Glib::ustring> s;
        v.get_child(s, 0);
        return s.get();
    } catch (const Glib::Error& e) {
        if (err) *err = why(e);
        return {};
    }
}

std::string Systemd::interactive(core::UnitScope s, const char* method, const Glib::VariantContainerBase& args) {
    std::string err;
    auto c = bus(s, &err);
    if (!c) return err.empty() ? "systemd is not reachable." : err;
    if (g_busy) return "Another action is waiting for authentication.";
    if (auto lg = log::get(log::Area::Shell)) lg->info("systemd: {} {}", method, std::string(args.print()));
    g_busy = true;
    bool done = false;
    std::string out;
    c->call(kPath, kMgr, method, args,
            [&, c](Glib::RefPtr<Gio::AsyncResult>& res) {
                try { c->call_finish(res); } catch (const Glib::Error& e) { out = why(e); }
                done = true;
            },
            kDest, 120000, Gio::DBus::CallFlags::ALLOW_INTERACTIVE_AUTHORIZATION);
    auto ctx = Glib::MainContext::get_default();
    while (!done) ctx->iteration(true);
    g_busy = false;
    if (!out.empty())
        if (auto lg = log::get(log::Area::Shell)) lg->warn("systemd: {}: {}", method, out);
    return out;
}

std::string Systemd::set_enabled(const core::UnitKey& k, bool on) {
    const auto files = Glib::Variant<std::vector<Glib::ustring>>::create({k.name});
    std::string e = on ? interactive(k.scope, "EnableUnitFiles", tuple({files, flag(false), flag(false)}))
                       : interactive(k.scope, "DisableUnitFiles", tuple({files, flag(false)}));
    if (!e.empty()) return e;
    // What `systemctl enable` does next: reload so the change is seen.
    return interactive(k.scope, "Reload", Glib::VariantContainerBase());
}

std::string Systemd::control(const core::UnitKey& k, core::UnitOp op) {
    const char* m = op == core::UnitOp::Start ? "StartUnit" : op == core::UnitOp::Stop ? "StopUnit" : "RestartUnit";
    return interactive(k.scope, m, tuple({str(k.name), str("replace")}));
}

void Systemd::journal(const core::UnitKey& k, int lines, TextDone done) {
    std::vector<std::string> argv{"journalctl"};
    if (k.scope == core::UnitScope::User) argv.push_back("--user");
    for (const std::string& a : {std::string("-u"), k.name, std::string("-n"), std::to_string(lines),
                                 std::string("--no-pager"), std::string("-o"), std::string("short-iso"),
                                 std::string("-q")})
        argv.push_back(a);
    Glib::RefPtr<Gio::Subprocess> p;
    try {
        p = Gio::Subprocess::create(argv, Gio::Subprocess::Flags::STDOUT_PIPE | Gio::Subprocess::Flags::STDERR_MERGE);
    } catch (const Glib::Error& e) {
        done(std::string("journalctl: ") + e.what());
        return;
    }
    p->communicate_utf8_async({}, [p, done](Glib::RefPtr<Gio::AsyncResult>& res) {
        std::string text;
        try { text = p->communicate_utf8_finish(res).first; } catch (const Glib::Error& e) { text = e.what(); }
        done(text);
    });
}

}  // namespace trails

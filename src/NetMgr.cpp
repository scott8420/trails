#include "NetMgr.hpp"
#include "Log.hpp"

#include <glibmm/main.h>
#include <glibmm/variant.h>

namespace trails {

namespace {
constexpr const char* kNM    = "org.freedesktop.NetworkManager";
constexpr const char* kProps = "org.freedesktop.DBus.Properties";
constexpr double      kEvery = 5.0;   // seconds between asks per interface

Glib::VariantContainerBase args(std::vector<Glib::VariantBase> v) {
    return Glib::VariantContainerBase::create_tuple(v);
}
Glib::VariantBase str(const std::string& s) { return Glib::Variant<Glib::ustring>::create(s); }

// a{sv} (child 0 of the reply) -> the value at `key`, or nullptr. Caller unrefs.
GVariant* prop(const Glib::VariantContainerBase& reply, const char* key) {
    GVariant* dict = g_variant_get_child_value(const_cast<GVariant*>(reply.gobj()), 0);
    GVariant* v = g_variant_lookup_value(dict, key, nullptr);
    g_variant_unref(dict);
    return v;
}
unsigned prop_u(const Glib::VariantContainerBase& r, const char* key) {
    unsigned out = 0;
    if (GVariant* v = prop(r, key)) {
        if (g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32)) out = g_variant_get_uint32(v);
        else if (g_variant_is_of_type(v, G_VARIANT_TYPE_BYTE)) out = g_variant_get_byte(v);
        g_variant_unref(v);
    }
    return out;
}
std::string prop_o(const Glib::VariantContainerBase& r, const char* key) {
    std::string out;
    if (GVariant* v = prop(r, key)) {
        if (g_variant_is_of_type(v, G_VARIANT_TYPE_OBJECT_PATH)) out = g_variant_get_string(v, nullptr);
        g_variant_unref(v);
    }
    return out;
}
std::vector<unsigned char> prop_ay(const Glib::VariantContainerBase& r, const char* key) {
    std::vector<unsigned char> out;
    if (GVariant* v = prop(r, key)) {
        if (g_variant_is_of_type(v, G_VARIANT_TYPE_BYTESTRING)) {
            gsize n = 0;
            const auto* p = static_cast<const unsigned char*>(g_variant_get_fixed_array(v, &n, 1));
            if (p) out.assign(p, p + n);
        }
        g_variant_unref(v);
    }
    return out;
}
double mono() { return double(g_get_monotonic_time()) / 1e6; }
}  // namespace

std::optional<core::WifiInfo> NetMgr::get(const std::string& iface) const {
    auto it = m_st->info.find(iface);
    if (it == m_st->info.end()) return std::nullopt;
    return it->second;
}

void NetMgr::poll(const std::string& iface) {
    if (m_dead) return;
    auto& st = *m_st;
    if (st.inflight[iface]) return;
    const double now = mono();
    if (auto a = st.asked.find(iface); a != st.asked.end() && now - a->second < kEvery) return;
    if (!m_bus) {
        try {
            m_bus = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::SYSTEM);
        } catch (const Glib::Error& e) {
            m_dead = true;
            if (auto lg = log::get(log::Area::Io)) lg->info("wifi: no system bus ({})", e.what());
            return;
        }
    }
    st.asked[iface] = now;
    st.inflight[iface] = true;

    auto c = m_bus;
    std::weak_ptr<State> wk = m_st;
    // Any failure ends the chain quietly: the tab keeps its signal-only line.
    auto fail = [wk, iface](const std::string& why) {
        if (auto s = wk.lock()) { s->inflight[iface] = false; s->info.erase(iface); }
        if (auto lg = log::get(log::Area::Io)) lg->debug("wifi {}: {}", iface, why);
    };

    c->call("/org/freedesktop/NetworkManager", kNM, "GetDeviceByIpIface", args({str(iface)}),
            [c, wk, iface, fail](Glib::RefPtr<Gio::AsyncResult>& res) {
                std::string dev;
                try {
                    auto v = c->call_finish(res);
                    GVariant* o = g_variant_get_child_value(v.gobj(), 0);
                    dev = g_variant_get_string(o, nullptr);
                    g_variant_unref(o);
                } catch (const Glib::Error& e) { fail(e.what()); return; }
                if (wk.expired()) return;
                c->call(dev, kProps, "GetAll", args({str("org.freedesktop.NetworkManager.Device.Wireless")}),
                        [c, wk, iface, fail](Glib::RefPtr<Gio::AsyncResult>& res2) {
                            core::WifiInfo w;
                            std::string ap;
                            try {
                                auto v = c->call_finish(res2);
                                w.bitrate_kbps = prop_u(v, "Bitrate");
                                ap = prop_o(v, "ActiveAccessPoint");
                            } catch (const Glib::Error& e) { fail(e.what()); return; }
                            if (ap.empty() || ap == "/") {   // not associated
                                if (auto s = wk.lock()) { s->info[iface] = w; s->inflight[iface] = false; }
                                return;
                            }
                            c->call(ap, kProps, "GetAll", args({str("org.freedesktop.NetworkManager.AccessPoint")}),
                                    [c, wk, iface, fail, w](Glib::RefPtr<Gio::AsyncResult>& res3) mutable {
                                        try {
                                            auto v = c->call_finish(res3);
                                            w.ssid = core::ssid_text(prop_ay(v, "Ssid"));
                                            w.freq_mhz = prop_u(v, "Frequency");
                                            w.strength = int(prop_u(v, "Strength"));
                                            w.associated = true;
                                        } catch (const Glib::Error& e) { fail(e.what()); return; }
                                        if (auto s = wk.lock()) { s->info[iface] = w; s->inflight[iface] = false; }
                                    },
                                    kNM, 3000);
                        },
                        kNM, 3000);
            },
            kNM, 3000);
}

}  // namespace trails

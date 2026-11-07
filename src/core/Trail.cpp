#include "core/Trail.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace trails::core {

namespace {
std::atomic<std::int64_t> g_serial{0};

struct Stmt {
    sqlite3_stmt* s = nullptr;
    Stmt(sqlite3* db, const char* sql) { if (db) sqlite3_prepare_v2(db, sql, -1, &s, nullptr); }
    ~Stmt() { sqlite3_finalize(s); }
    bool ok() const { return s != nullptr; }
    bool row() { return s && sqlite3_step(s) == SQLITE_ROW; }
    bool done() { return s && sqlite3_step(s) == SQLITE_DONE; }
    Stmt& bind(int i, double v) { if (s) sqlite3_bind_double(s, i, v); return *this; }
    Stmt& bind(int i, std::int64_t v) { if (s) sqlite3_bind_int64(s, i, v); return *this; }
    Stmt& bind(int i, const std::string& v) { if (s) sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT); return *this; }
    double      d(int c) const { return sqlite3_column_double(s, c); }
    std::int64_t i(int c) const { return sqlite3_column_int64(s, c); }
    bool        null(int c) const { return sqlite3_column_type(s, c) == SQLITE_NULL; }
    std::string t(int c) const {
        auto* p = sqlite3_column_text(s, c);
        return p ? reinterpret_cast<const char*>(p) : "";
    }
};

std::string local_time(double t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    ::localtime_r(&tt, &tm);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tm);
    return b;
}
}  // namespace

Trail::~Trail() { close(); }

void Trail::close() {
    if (m_db) sqlite3_close(m_db);
    m_db = nullptr;
    m_path.clear();
    m_meta.clear();
    m_ids.clear();
    m_names.clear();
    m_t0 = m_t1 = 0;
    m_ticks = 0;
}

std::string Trail::open(const std::string& path) {
    close();
    if (!std::filesystem::exists(path)) return path + " does not exist.";
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        db = nullptr;
        if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            const std::string why = db ? sqlite3_errmsg(db) : "out of memory";
            sqlite3_close(db);
            return "Could not open " + path + ": " + why;
        }
    }
    sqlite3_busy_timeout(db, 2000);   // the recorder may hold the write lock for a tick
    m_db = db;
    {
        Stmt q(m_db, "SELECT value FROM meta WHERE key = 'format'");
        if (!q.row() || q.t(0) != "trails-recording") {
            close();
            return path + " is not a Trails recording.";
        }
    }
    m_path = path;
    m_serial = ++g_serial;
    reload();
    return {};
}

void Trail::reload() {
    if (!m_db) return;
    m_meta.clear();
    for (Stmt q(m_db, "SELECT key, value FROM meta"); q.row();) m_meta[q.t(0)] = q.t(1);
    m_ids.clear();
    m_names.clear();
    for (Stmt q(m_db, "SELECT id, name FROM metric ORDER BY name"); q.row();) {
        m_ids[q.t(1)] = q.i(0);
        m_names.push_back(q.t(1));
    }
    Stmt q(m_db, "SELECT min(t), max(t), count(DISTINCT t) FROM sample");
    if (q.row() && !q.null(0)) {
        m_t0 = q.d(0);
        m_t1 = q.d(1);
        m_ticks = std::size_t(q.i(2));
    } else {   // no ticks yet: the span the meta claims
        auto num = [&](const char* k) {
            auto it = m_meta.find(k);
            return it == m_meta.end() ? 0.0 : std::atof(it->second.c_str());
        };
        m_t0 = num("start_unix");
        m_t1 = std::max(m_t0, num("end_unix"));
        m_ticks = 0;
    }
}

std::vector<TrailPoint> Trail::series(const std::string& name, double from, double to, int buckets) const {
    std::vector<TrailPoint> pts;
    auto it = m_ids.find(name);
    if (!m_db || it == m_ids.end() || to < from) return pts;
    Stmt q(m_db, "SELECT t, value FROM sample WHERE metric = ? AND t >= ? AND t <= ? ORDER BY t");
    q.bind(1, it->second).bind(2, from).bind(3, to);
    while (q.row()) pts.push_back({q.d(0), float(q.d(1))});
    if (buckets < 1 || int(pts.size()) <= buckets) return pts;
    // Max per bucket, kept at the time of the max (a spike stays where it was).
    std::vector<TrailPoint> out;
    out.reserve(std::size_t(buckets));
    const double w = (to - from) / buckets;
    std::size_t i = 0;
    for (int b = 0; b < buckets && i < pts.size(); ++b) {
        const double end = (b == buckets - 1) ? to + 1 : from + w * (b + 1);
        if (pts[i].t >= end) continue;
        TrailPoint best = pts[i];
        for (; i < pts.size() && pts[i].t < end; ++i)
            if (pts[i].v > best.v) best = pts[i];
        out.push_back(best);
    }
    return out;
}

namespace {
// The tick nearest t in `table` (optionally for one metric).
std::optional<double> nearest(sqlite3* db, const char* table, double t, std::int64_t metric = -1) {
    const std::string w = metric >= 0 ? " AND metric = ?2" : "";
    const std::string a = std::string("SELECT t FROM ") + table + " WHERE t <= ?1" + w + " ORDER BY t DESC LIMIT 1";
    const std::string b = std::string("SELECT t FROM ") + table + " WHERE t >= ?1" + w + " ORDER BY t ASC LIMIT 1";
    std::optional<double> lo, hi;
    {
        Stmt q(db, a.c_str());
        q.bind(1, t);
        if (metric >= 0) q.bind(2, metric);
        if (q.row()) lo = q.d(0);
    }
    {
        Stmt q(db, b.c_str());
        q.bind(1, t);
        if (metric >= 0) q.bind(2, metric);
        if (q.row()) hi = q.d(0);
    }
    if (!lo) return hi;
    if (!hi) return lo;
    return (t - *lo <= *hi - t) ? lo : hi;
}
}  // namespace

std::optional<float> Trail::value_at(const std::string& name, double t) const {
    auto it = m_ids.find(name);
    if (!m_db || it == m_ids.end()) return std::nullopt;
    const auto tick = nearest(m_db, "sample", t, it->second);
    if (!tick) return std::nullopt;
    Stmt q(m_db, "SELECT value FROM sample WHERE metric = ? AND t = ?");
    q.bind(1, it->second).bind(2, *tick);
    if (!q.row()) return std::nullopt;
    return float(q.d(0));
}

std::vector<TrailProc> Trail::procs_at(double t, double* tick) const {
    std::vector<TrailProc> out;
    if (!m_db) return out;
    const auto at = nearest(m_db, "proc_sample", t);
    if (!at) return out;
    if (tick) *tick = *at;
    Stmt q(m_db, "SELECT pid, name, cpu, rss FROM proc_sample WHERE t = ? ORDER BY cpu DESC, rss DESC");
    q.bind(1, *at);
    while (q.row()) out.push_back({int(q.i(0)), q.t(1), q.d(2), std::uint64_t(q.i(3))});
    return out;
}

double Trail::tick_before(double t) const {
    Stmt q(m_db, "SELECT max(t) FROM sample WHERE t < ?");
    q.bind(1, t - 1e-6);
    return q.row() && !q.null(0) ? q.d(0) : t;
}
double Trail::tick_after(double t) const {
    Stmt q(m_db, "SELECT min(t) FROM sample WHERE t > ?");
    q.bind(1, t + 1e-6);
    return q.row() && !q.null(0) ? q.d(0) : t;
}

std::vector<TrailMark> Trail::marks() const {
    std::vector<TrailMark> out;
    for (Stmt q(m_db, "SELECT id, t, kind, text FROM marker ORDER BY t, id"); q.row();)
        out.push_back({q.i(0), Marker{q.d(1), q.t(2), q.t(3)}});
    return out;
}

std::optional<Marker> Trail::get_mark(std::int64_t id) const {
    Stmt q(m_db, "SELECT t, kind, text FROM marker WHERE id = ?");
    q.bind(1, id);
    if (!q.row()) return std::nullopt;
    return Marker{q.d(0), q.t(1), q.t(2)};
}

std::string Trail::put_mark(std::int64_t id, const std::optional<Marker>& m) {
    if (!m_db) return "No recording is open.";
    bool ok;
    if (m) {
        Stmt q(m_db, "INSERT OR REPLACE INTO marker(id, t, kind, text) VALUES(?, ?, ?, ?)");
        q.bind(1, id).bind(2, m->t).bind(3, m->kind).bind(4, m->text);
        ok = q.done();
    } else {
        Stmt q(m_db, "DELETE FROM marker WHERE id = ?");
        q.bind(1, id);
        ok = q.done();
    }
    return ok ? std::string() : std::string("Could not change the mark: ") + sqlite3_errmsg(m_db);
}

std::int64_t Trail::fresh_mark_id() const {
    Stmt q(m_db, "SELECT coalesce(max(id), 0) + 1 FROM marker");
    return q.row() ? q.i(0) : 1;
}

long Trail::export_csv(const std::string& out, double from, double to, std::string* err) const {
    if (!m_db) { if (err) *err = "No recording is open."; return -1; }
    std::ofstream f(out, std::ios::trunc);
    if (!f) { if (err) *err = "Could not write " + out; return -1; }
    std::map<std::string, std::size_t> col;
    for (std::size_t i = 0; i < m_names.size(); ++i) col[m_names[i]] = i;
    f << "time_local,unix";
    for (const auto& n : m_names) f << ',' << n;
    f << '\n';
    Stmt q(m_db, "SELECT s.t, m.name, s.value FROM sample s JOIN metric m ON m.id = s.metric "
                 "WHERE s.t >= ? AND s.t <= ? ORDER BY s.t");
    q.bind(1, from).bind(2, to);
    long rows = 0;
    double cur = NAN;
    std::vector<std::string> cells(m_names.size());
    auto flush = [&] {
        if (std::isnan(cur)) return;
        char ts[32];
        std::snprintf(ts, sizeof ts, "%.3f", cur);
        f << local_time(cur) << ',' << ts;
        for (auto& c : cells) { f << ',' << c; c.clear(); }
        f << '\n';
        ++rows;
    };
    while (q.row()) {
        const double t = q.d(0);
        if (t != cur) { flush(); cur = t; }
        auto it = col.find(q.t(1));
        if (it == col.end()) continue;
        char v[32];
        std::snprintf(v, sizeof v, "%.6g", q.d(2));
        cells[it->second] = v;
    }
    flush();
    if (!f) { if (err) *err = "Write failed: " + out; return -1; }
    return rows;
}

std::vector<std::string> Trail::list(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".trail") out.push_back(e.path().string());
    std::sort(out.begin(), out.end(), std::greater<>());
    return out;
}

}  // namespace trails::core

#include "core/Recorder.hpp"

#include <sqlite3.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <set>

namespace trails::core {

namespace {
constexpr const char* kSchema = R"SQL(
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
CREATE TABLE IF NOT EXISTS meta   (key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS metric (id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL);
CREATE TABLE IF NOT EXISTS sample (t REAL NOT NULL, metric INTEGER NOT NULL, value REAL NOT NULL);
CREATE TABLE IF NOT EXISTS proc_sample (t REAL NOT NULL, pid INTEGER NOT NULL, name TEXT,
                                        cpu REAL, rss INTEGER, io REAL);
CREATE TABLE IF NOT EXISTS marker (id INTEGER PRIMARY KEY, t REAL NOT NULL, kind TEXT NOT NULL, text TEXT);
CREATE INDEX IF NOT EXISTS sample_t ON sample(t);
CREATE INDEX IF NOT EXISTS proc_sample_t ON proc_sample(t);
CREATE VIEW IF NOT EXISTS samples AS
    SELECT s.t AS t, m.name AS name, s.value AS value FROM sample s JOIN metric m ON m.id = s.metric;
)SQL";

std::string iso_local(double t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    ::localtime_r(&tt, &tm);
    char b[40];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S%z", &tm);
    return b;
}
}  // namespace

Recorder::Recorder() = default;
Recorder::~Recorder() { stop(); }

double Recorder::wall_now() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

std::string Recorder::default_dir() {
    const char* x = std::getenv("XDG_DATA_HOME");
    std::string base = (x && *x) ? x : std::string(std::getenv("HOME") ? std::getenv("HOME") : "/tmp") + "/.local/share";
    return base + "/trails/recordings";
}

std::string Recorder::default_name(std::int64_t t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    ::localtime_r(&tt, &tm);
    char b[40];
    std::strftime(b, sizeof b, "%Y-%m-%d_%H-%M-%S.trail", &tm);
    return b;
}

std::string Recorder::query_command(const std::string& path, int limit) {
    std::string q = "'";
    for (char c : path) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
    q += "'";
    return "sqlite3 -header -column " + q +
           " \"SELECT datetime(t, 'unixepoch', 'localtime') AS time, name, round(value, 2) AS value"
           " FROM samples ORDER BY t, name LIMIT " + std::to_string(limit) + ";\"";
}

std::string Recorder::path() const { std::lock_guard l(m_mu); return m_path; }
std::string Recorder::error() const { std::lock_guard l(m_mu); return m_err; }

bool Recorder::exec(const char* sql) {
    char* msg = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &msg) == SQLITE_OK) return true;
    const std::string why = msg ? msg : "sqlite error";
    sqlite3_free(msg);
    fail(why);
    return false;
}

void Recorder::fail(const std::string& why) {
    std::lock_guard l(m_mu);
    if (m_err.empty()) m_err = why;
    m_on = false;
}

std::string Recorder::start(const std::string& path, const Meta& meta) {
    if (m_on) return "Already recording.";
    if (m_thread.joinable()) stop();
    {
        std::lock_guard l(m_mu);
        m_err.clear();
        m_q.clear();
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) return "Could not create " + std::filesystem::path(path).parent_path().string() + ": " + ec.message();
    if (std::filesystem::exists(path)) return path + " already exists.";
    if (sqlite3_open_v2(path.c_str(), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        const std::string why = m_db ? sqlite3_errmsg(m_db) : "out of memory";
        sqlite3_close(m_db);
        m_db = nullptr;
        return "Could not open " + path + ": " + why;
    }
    if (!exec(kSchema)) {
        const std::string why = error();
        sqlite3_close(m_db);
        m_db = nullptr;
        return "Could not write " + path + ": " + why;
    }
    m_t0 = wall_now();
    utsname u{};
    ::uname(&u);
    const std::pair<std::string, std::string> kv[] = {
        {"format", "trails-recording"}, {"format_version", "1"},
        {"host", meta.host},            {"kernel", meta.kernel.empty() ? u.release : meta.kernel},
        {"cpu_model", meta.cpu_model},  {"n_cpus", std::to_string(meta.n_cpus)},
        {"mem_total", std::to_string(meta.mem_total)},
        {"interval_ms", std::to_string(meta.interval_ms)},
        {"top_n", std::to_string(kTopN)},
        {"start", iso_local(m_t0)},     {"start_unix", std::to_string(m_t0)},
    };
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(m_db, "INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)", -1, &st, nullptr);
    for (const auto& [k, v] : kv) {
        sqlite3_bind_text(st, 1, k.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, v.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    write_mark(0, Marker{m_t0, "start", ""});

    {
        std::lock_guard l(m_mu);
        m_path = path;
    }
    m_metric.clear();
    m_marks.clear();
    ++m_serial;
    m_next_mark = 1;
    m_ticks = 0;
    m_on = true;
    m_thread = std::thread([this] { run(); });
    return {};
}

void Recorder::stop() {
    if (!m_thread.joinable()) return;
    {
        std::lock_guard l(m_mu);
        Job j;
        j.kind = Job::Kind::Stop;
        j.t = wall_now();
        m_q.push_back(std::move(j));
    }
    m_on = false;
    m_cv.notify_all();
    m_thread.join();
    m_marks.clear();
}

void Recorder::push(std::shared_ptr<const Snapshot> s) {
    if (!m_on || !s) return;
    {
        std::lock_guard l(m_mu);
        Job j;
        j.kind = Job::Kind::Tick;
        j.t = wall_now();
        j.snap = std::move(s);
        m_q.push_back(std::move(j));
    }
    m_cv.notify_one();
}

void Recorder::run() {
    for (;;) {
        Job j;
        {
            std::unique_lock l(m_mu);
            m_cv.wait(l, [&] { return !m_q.empty(); });
            j = std::move(m_q.front());
            m_q.pop_front();
        }
        if (j.kind == Job::Kind::Stop) {
            if (!m_db) return;   // a failure already closed it
            write_mark(0, Marker{j.t, "stop", ""});
            const std::string end = std::to_string(j.t);
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(m_db, "INSERT OR REPLACE INTO meta(key, value) VALUES('end_unix', ?)", -1, &st,
                                   nullptr) == SQLITE_OK) {
                sqlite3_bind_text(st, 1, end.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(st);
            }
            sqlite3_finalize(st);
            sqlite3_close(m_db);   // checkpoints the WAL; -wal / -shm go away
            m_db = nullptr;
            return;
        }
        if (!m_db) continue;
        bool ok = true;
        if (j.kind == Job::Kind::Tick) ok = write_tick(j);
        else if (j.kind == Job::Kind::Mark) ok = write_mark(j.mark_id, j.mark);
        if (!ok) {
            // Keep draining until Stop, writing nothing more.
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }
}

std::int64_t Recorder::metric_id(const std::string& name) {
    if (auto it = m_metric.find(name); it != m_metric.end()) return it->second;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(m_db, "INSERT OR IGNORE INTO metric(name) VALUES(?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    std::int64_t id = 0;
    sqlite3_prepare_v2(m_db, "SELECT id FROM metric WHERE name = ?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) id = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    m_metric[name] = id;
    return id;
}

bool Recorder::write_tick(const Job& j) {
    if (!exec("BEGIN")) return false;
    sqlite3_stmt* st = nullptr;
    bool ok = sqlite3_prepare_v2(m_db, "INSERT INTO sample(t, metric, value) VALUES(?, ?, ?)", -1, &st, nullptr) ==
              SQLITE_OK;
    for (const auto& [name, v] : j.snap->now) {
        if (!ok) break;
        sqlite3_bind_double(st, 1, j.t);
        sqlite3_bind_int64(st, 2, metric_id(name));
        sqlite3_bind_double(st, 3, v);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);

    // Top processes: by CPU and by resident memory, once each.
    std::vector<const ProcRow*> by_cpu, by_rss;
    for (const auto& p : j.snap->procs) if (!p.kernel_thread || p.cpu_pct > 0) { by_cpu.push_back(&p); by_rss.push_back(&p); }
    const auto k_cpu = std::min<std::size_t>(kTopN, by_cpu.size());
    const auto k_rss = std::min<std::size_t>(kTopN, by_rss.size());
    std::partial_sort(by_cpu.begin(), by_cpu.begin() + std::ptrdiff_t(k_cpu), by_cpu.end(),
                      [](auto* a, auto* b) { return a->cpu_pct > b->cpu_pct; });
    std::partial_sort(by_rss.begin(), by_rss.begin() + std::ptrdiff_t(k_rss), by_rss.end(),
                      [](auto* a, auto* b) { return a->rss_bytes > b->rss_bytes; });
    std::set<int> seen;
    std::vector<const ProcRow*> top;
    for (std::size_t i = 0; i < k_cpu; ++i) if (seen.insert(by_cpu[i]->pid).second) top.push_back(by_cpu[i]);
    for (std::size_t i = 0; i < k_rss; ++i) if (seen.insert(by_rss[i]->pid).second) top.push_back(by_rss[i]);
    if (ok)
        ok = sqlite3_prepare_v2(m_db, "INSERT INTO proc_sample(t, pid, name, cpu, rss, io) VALUES(?, ?, ?, ?, ?, NULL)",
                                -1, &st, nullptr) == SQLITE_OK;
    for (const auto* p : top) {
        if (!ok) break;
        sqlite3_bind_double(st, 1, j.t);
        sqlite3_bind_int(st, 2, p->pid);
        sqlite3_bind_text(st, 3, p->name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(st, 4, p->cpu_pct);
        sqlite3_bind_int64(st, 5, static_cast<sqlite3_int64>(p->rss_bytes));
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_reset(st);
    }
    if (ok) sqlite3_finalize(st);
    if (!ok) {
        const std::string why = sqlite3_errmsg(m_db);
        sqlite3_finalize(st);
        exec("ROLLBACK");
        fail(why);
        return false;
    }
    if (!exec("COMMIT")) return false;
    ++m_ticks;
    return true;
}

bool Recorder::write_mark(std::int64_t id, const std::optional<Marker>& m) {
    sqlite3_stmt* st = nullptr;
    int rc;
    if (m) {
        rc = sqlite3_prepare_v2(m_db, id ? "INSERT OR REPLACE INTO marker(id, t, kind, text) VALUES(?4, ?1, ?2, ?3)"
                                         : "INSERT INTO marker(t, kind, text) VALUES(?1, ?2, ?3)",
                                -1, &st, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_double(st, 1, m->t);
            sqlite3_bind_text(st, 2, m->kind.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, m->text.c_str(), -1, SQLITE_TRANSIENT);
            if (id) sqlite3_bind_int64(st, 4, id);
        }
    } else {
        rc = sqlite3_prepare_v2(m_db, "DELETE FROM marker WHERE id = ?", -1, &st, nullptr);
        if (rc == SQLITE_OK) sqlite3_bind_int64(st, 1, id);
    }
    const bool ok = rc == SQLITE_OK && sqlite3_step(st) == SQLITE_DONE;
    if (!ok) fail(sqlite3_errmsg(m_db));
    sqlite3_finalize(st);
    return ok;
}

// ── marks ───────────────────────────────────────────────────────────────────
// The start / stop markers take SQLite's own ids (small); user marks take
// serial << 32 | n, so they never collide with those, and an id from an
// earlier recording names nothing in this one.

std::int64_t Recorder::fresh_mark_id() {
    if (!m_on) return 0;
    return (m_serial << 32) | m_next_mark++;
}

std::optional<Marker> Recorder::get_mark(std::int64_t id) const {
    auto it = m_marks.find(id);
    if (it == m_marks.end()) return std::nullopt;
    return it->second;
}

void Recorder::put_mark(std::int64_t id, const std::optional<Marker>& m) {
    if (!m_on || (id >> 32) != m_serial) return;   // another recording's mark: nothing to touch
    if (m) m_marks[id] = *m;
    else m_marks.erase(id);
    {
        std::lock_guard l(m_mu);
        Job j;
        j.kind = Job::Kind::Mark;
        j.mark_id = id;
        j.mark = m;
        m_q.push_back(std::move(j));
    }
    m_cv.notify_one();
}

}  // namespace trails::core

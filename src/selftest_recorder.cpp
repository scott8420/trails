// selftest -- s009 the recorder: a real .trail file written from synthetic
// snapshots, read back with SQLite; marks through the ONE journal (undo,
// redo, a mark after stop); refusals; the sampler's listener.
#include "core/AppStore.hpp"
#include "core/Recorder.hpp"
#include "core/Sampler.hpp"

#include <sqlite3.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

namespace core = trails::core;
namespace fs = std::filesystem;
using CheckFn = std::function<void(const std::string&, bool, const std::string&)>;

namespace {
long long q1(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* st = nullptr;
    long long v = -1;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        v = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return v;
}
std::string qs(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* st = nullptr;
    std::string v;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        if (auto* t = sqlite3_column_text(st, 0)) v = reinterpret_cast<const char*>(t);
    sqlite3_finalize(st);
    return v;
}
std::shared_ptr<core::Snapshot> snap(int n) {
    auto s = std::make_shared<core::Snapshot>();
    s->seq = std::uint64_t(n);
    s->now["cpu.total"] = 10.0f * float(n);
    s->now["mem.used_pct"] = 50.0f;
    if (n == 2) s->now["net.wlan0.rx"] = 1234.0f;   // a metric that shows up late
    for (int p = 1; p <= 40; ++p) {
        core::ProcRow r;
        r.pid = p;
        r.name = "p" + std::to_string(p);
        r.cpu_pct = p == 40 ? 99.0 : double(p) / 10.0;
        r.rss_bytes = std::uint64_t(p == 1 ? 1 << 30 : p * 1000);
        s->procs.push_back(r);
    }
    return s;
}
}  // namespace

void run_recorder_tests(const CheckFn& check_fn) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_fn(n, ok, d); };
    std::cout << "\n-- recorder (s009) --\n";

    const fs::path dir = fs::temp_directory_path() /
                         ("trails_rec_" + std::to_string(::getpid()) + "_" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string path = (dir / "sub" / core::Recorder::default_name(1762555212)).string();
    check("name: dated .trail", core::Recorder::default_name(1762555212).size() == 25 &&
                                    core::Recorder::default_name(1762555212).ends_with(".trail"),
          core::Recorder::default_name(1762555212));

    {   // The copyable command: the path single-quoted (a quote inside survives).
        const std::string c = core::Recorder::query_command("/home/s/My Trails/it's.trail", 10);
        check("query: path quoted for the shell", c.find("'/home/s/My Trails/it'\\''s.trail'") != std::string::npos, c);
        check("query: reads the samples view, local times, limit",
              c.rfind("sqlite3 -header -column ", 0) == 0 && c.find("FROM samples") != std::string::npos &&
                  c.find("'localtime'") != std::string::npos && c.ends_with("LIMIT 10;\""), c);
    }

    // The one journal over the app store: no process OS needed for marks.
    struct NoOS : core::ProcOS {
        std::optional<core::ProcKnobs> read(const core::ProcKey&) const override { return std::nullopt; }
        std::string set_nice(const core::ProcKey&, int) override { return "no"; }
        std::string set_cpus(const core::ProcKey&, const std::vector<int>&) override { return "no"; }
        std::string signal(const core::ProcKey&, int) override { return "no"; }
    } os;
    core::ProcKnobStore ks(os);
    core::Recorder rec;
    core::AppStore app(ks, &rec);
    core::AppJournal j(app);
    app.set_door([&] { return j.open(); });

    std::string err;
    check("mark: refused when not recording", core::marks::add(j, app, "x", &err) == 0 && !err.empty() && j.size() == 0, err);

    core::Recorder::Meta meta;
    meta.host = "m1";
    meta.cpu_model = "Apple M1 Pro";
    meta.n_cpus = 10;
    meta.mem_total = 16ull << 30;
    const std::string e = rec.start(path, meta);
    check("start: creates dirs + file", e.empty() && rec.recording() && fs::exists(path), e);
    check("start: refused while recording", !rec.start(path, meta).empty());

    rec.push(snap(1));
    const auto m1 = core::marks::add(j, app, "kettle on");
    check("mark: one step, labelled", m1 != 0 && j.size() == 1 && j.undo_label() == "Mark “kettle on”", j.undo_label());
    check("mark: through the door", app.strays() == 0);
    check("mark: readable at once (mirror)", rec.get_mark(m1) && rec.get_mark(m1)->text == "kettle on");
    j.undo();
    check("mark: undo removes it", !rec.get_mark(m1));
    j.redo();
    check("mark: redo brings it back, same id", rec.get_mark(m1).has_value());
    {   // s015: a named mark keeps the moment of the keypress
        const double pressed = core::Recorder::wall_now() - 4.0;   // the dialog took 4 s
        const auto m2 = core::marks::add_at(j, app, pressed, "fans spun up");
        check("mark: add_at keeps the keypress time", m2 && rec.get_mark(m2) &&
              rec.get_mark(m2)->t == pressed && rec.get_mark(m2)->text == "fans spun up", "");
        j.undo();
        check("mark: add_at is one undoable step", m2 && !rec.get_mark(m2), "");
    }
    rec.push(snap(2));
    core::marks::add(j, app, "spike");
    rec.push(snap(3));
    rec.stop();
    check("stop: not recording", !rec.recording() && rec.error().empty(), rec.error());
    check("stop: one file (no -wal / -shm left)", !fs::exists(path + "-wal") && !fs::exists(path + "-shm"));
    j.undo();   // the "spike" mark, from a recording that has ended
    check("mark: undo after stop touches nothing", !rec.recording());
    check("start: refused on an existing file", !rec.start(path, meta).empty());

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        check("read back: opens", false, sqlite3_errmsg(db));
    } else {
        check("meta: format + host + cpu", qs(db, "SELECT value FROM meta WHERE key='format'") == "trails-recording" &&
                                               qs(db, "SELECT value FROM meta WHERE key='host'") == "m1" &&
                                               qs(db, "SELECT value FROM meta WHERE key='n_cpus'") == "10");
        check("meta: start and end", !qs(db, "SELECT value FROM meta WHERE key='start_unix'").empty() &&
                                         !qs(db, "SELECT value FROM meta WHERE key='end_unix'").empty());
        check("metric: three names", q1(db, "SELECT count(*) FROM metric") == 3, std::to_string(q1(db, "SELECT count(*) FROM metric")));
        check("sample: 2 + 3 + 2 rows", q1(db, "SELECT count(*) FROM sample") == 7, std::to_string(q1(db, "SELECT count(*) FROM sample")));
        check("samples view: names joined", q1(db, "SELECT count(*) FROM samples WHERE name='cpu.total'") == 3);
        check("samples view: the values", q1(db, "SELECT CAST(max(value) AS INTEGER) FROM samples WHERE name='cpu.total'") == 30);
        check("ticks: three, time ordered", q1(db, "SELECT count(DISTINCT t) FROM sample") == 3);
        // 15 by CPU + 15 by memory, deduped: pid 40 (CPU) and pid 1 (memory) both in.
        const long long per = q1(db, "SELECT count(*) FROM proc_sample") / 3;
        check("proc: top by CPU and memory, deduped, <= 30 a tick", per > 15 && per <= 30, std::to_string(per));
        check("proc: the CPU hog and the memory hog", q1(db, "SELECT count(*) FROM proc_sample WHERE pid=40") == 3 &&
                                                         q1(db, "SELECT count(*) FROM proc_sample WHERE pid=1") == 3);
        check("marker: start, kettle on, spike, stop",
              qs(db, "SELECT group_concat(kind || ':' || text, ',') FROM (SELECT * FROM marker ORDER BY t, id)") ==
                  "start:,mark:kettle on,mark:spike,stop:",
              qs(db, "SELECT group_concat(kind || ':' || text, ',') FROM (SELECT * FROM marker ORDER BY t, id)"));
        sqlite3_close(db);
    }

    // A failure stops the recording and says why: the directory is a file.
    {
        core::Recorder r2;
        const std::string bad = (dir / "sub" / core::Recorder::default_name(1762555212) / "x.trail").string();
        check("start: a path under a file is refused", !r2.start(bad, meta).empty() && !r2.recording());
    }

    // The sampler's listener hears every tick, with this tick's values.
    {
        core::SamplerConfig cfg;
        core::Sampler smp(cfg);
        int heard = 0;
        std::size_t vals = 0;
        smp.set_listener([&](std::shared_ptr<const core::Snapshot> s) { ++heard; vals = s->now.size(); });
        smp.tick();
        smp.tick();
        check("listener: hears each tick", heard == 2, std::to_string(heard));
        check("listener: second tick carries values", vals > 0, std::to_string(vals));
        smp.set_listener({});
        smp.tick();
        check("listener: cleared", heard == 2);
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

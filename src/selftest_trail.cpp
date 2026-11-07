// selftest -- s010 reading a .trail back: a 120-tick recording made by the
// real Recorder (one CPU spike at tick 77), then the Trail reader: span,
// metrics, max-per-bucket downsampling keeps the spike, values and procs at
// a moment, tick stepping, marks through the ONE journal (add / rename /
// delete, undo / redo, a stale file), CSV export, the folder listing.
#include "core/AppStore.hpp"
#include "core/Recorder.hpp"
#include "core/Trail.hpp"

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace core = trails::core;
namespace fs = std::filesystem;
using CheckFn = std::function<void(const std::string&, bool, const std::string&)>;

void run_trail_tests(const CheckFn& check_fn) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_fn(n, ok, d); };
    std::cout << "\n-- trail reader (s010) --\n";

    const fs::path dir = fs::temp_directory_path() /
                         ("trails_tr_" + std::to_string(::getpid()) + "_" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const std::string a = (dir / "2026-11-07_10-00-00.trail").string();
    const std::string b = (dir / "2026-11-07_11-00-00.trail").string();
    {
        core::Recorder rec;
        core::Recorder::Meta meta;
        meta.host = "m1";
        check("fixture: records", rec.start(a, meta).empty());
        for (int i = 0; i < 120; ++i) {
            auto s = std::make_shared<core::Snapshot>();
            s->now["cpu.total"] = i == 77 ? 99.0f : 5.0f;
            s->now["mem.used_pct"] = 40.0f + float(i) * 0.1f;
            if (i >= 60) s->now["net.wlan0.rx"] = 1000.0f;   // arrives halfway
            core::ProcRow p;
            p.pid = 100 + (i == 77 ? 1 : 0);
            p.name = i == 77 ? "hog" : "idle";
            p.cpu_pct = i == 77 ? 95.0 : 1.0;
            p.rss_bytes = 4096;
            s->procs.push_back(p);
            core::ProcRow q = p;
            q.pid = 200; q.name = "other"; q.cpu_pct = 0.5;
            s->procs.push_back(q);
            rec.push(s);
            std::this_thread::sleep_for(std::chrono::microseconds(200));   // distinct times
        }
        rec.stop();
    }
    fs::copy_file(a, b);
    std::ofstream(dir / "notes.txt") << "not a trail\n";
    std::ofstream(dir / "fake.trail") << "not sqlite\n";

    const auto files = core::Trail::list(dir.string());
    check("list: .trail files, newest first", files.size() == 3 && files[0].ends_with("fake.trail") &&
                                                  files[1] == b && files[2] == a);

    core::Trail tr;
    check("open: a non-recording is refused", !tr.open((dir / "fake.trail").string()).empty() && !tr.is_open());
    check("open: missing file", !tr.open((dir / "nope.trail").string()).empty());
    const std::string e = tr.open(a);
    check("open: reads", e.empty() && tr.is_open(), e);
    check("open: meta", tr.meta().count("host") && tr.meta().at("host") == "m1");
    check("span: 120 ticks, t0 < t1", tr.ticks() == 120 && tr.t0() < tr.t1(), std::to_string(tr.ticks()));
    check("metrics: three, sorted", tr.metrics() == std::vector<std::string>{"cpu.total", "mem.used_pct", "net.wlan0.rx"});

    const auto all = tr.series("cpu.total", tr.t0(), tr.t1(), 1000);
    check("series: fewer points than buckets -> every sample", all.size() == 120, std::to_string(all.size()));
    const auto ten = tr.series("cpu.total", tr.t0(), tr.t1(), 10);
    float top = 0;
    double spike_t = 0;
    for (const auto& p : ten) if (p.v > top) { top = p.v; spike_t = p.t; }
    check("series: 10 buckets, the spike survives (max, not mean)", ten.size() <= 10 && top == 99.0f,
          std::to_string(ten.size()) + " / " + std::to_string(top));
    check("series: the spike keeps its own time", all[77].t == spike_t);
    check("series: a late metric starts late", tr.series("net.wlan0.rx", tr.t0(), tr.t1(), 1000).size() == 60);
    check("series: unknown metric -> nothing", tr.series("nope", tr.t0(), tr.t1(), 10).empty());

    check("value_at: the spike", tr.value_at("cpu.total", spike_t).value_or(0) == 99.0f);
    check("value_at: before a metric existed -> nearest, not nothing",
          tr.value_at("net.wlan0.rx", tr.t0()).has_value());
    double tick = 0;
    const auto ps = tr.procs_at(spike_t + 1e-7, &tick);
    check("procs_at: the hog at the spike, busiest first", ps.size() == 2 && ps[0].name == "hog" && tick == spike_t);
    check("step: after / before", tr.tick_after(all[10].t) == all[11].t && tr.tick_before(all[10].t) == all[9].t);
    check("step: stops at the ends", tr.tick_before(tr.t0()) == tr.t0() && tr.tick_after(tr.t1()) == tr.t1());

    // Marks through the one journal.
    struct NoOS : core::ProcOS {
        std::optional<core::ProcKnobs> read(const core::ProcKey&) const override { return std::nullopt; }
        std::string set_nice(const core::ProcKey&, int) override { return "no"; }
        std::string set_cpus(const core::ProcKey&, const std::vector<int>&) override { return "no"; }
        std::string signal(const core::ProcKey&, int) override { return "no"; }
    } os;
    core::ProcKnobStore ks(os);
    core::AppStore app(ks);
    core::AppJournal j(app);
    app.set_door([&] { return j.open(); });
    app.set_trail(&tr);

    check("marks: start + stop", tr.marks().size() == 2 && tr.marks().front().m.kind == "start" &&
                                     tr.marks().back().m.kind == "stop");
    std::string err;
    const auto id = core::trailmarks::add(j, app, spike_t, "spike", &err);
    check("mark add: one step, in time order", id && j.size() == 1 && tr.marks().size() == 3 &&
                                                   tr.marks()[1].m.text == "spike", err);
    check("mark add: through the door", app.strays() == 0);
    check("mark rename", core::trailmarks::rename(j, app, id, "the hog").empty() && tr.get_mark(id)->text == "the hog" &&
                             j.undo_label() == "Rename mark “the hog”");
    check("mark delete", core::trailmarks::remove(j, app, id).empty() && !tr.get_mark(id));
    j.undo();
    check("undo delete: back, renamed", tr.get_mark(id) && tr.get_mark(id)->text == "the hog");
    j.undo();
    check("undo rename", tr.get_mark(id)->text == "spike");
    j.undo();
    check("undo add: gone", !tr.get_mark(id) && tr.marks().size() == 2);
    j.redo();
    check("redo add: same id, same time", tr.get_mark(id) && tr.get_mark(id)->t == spike_t);
    check("mark: the start marker can be deleted and undone too",
          core::trailmarks::remove(j, app, tr.marks().front().id).empty() && tr.marks().size() == 2);
    j.undo();
    check("mark: start back", tr.marks().size() == 3);

    // CSV over the second half.
    const std::string csv = (dir / "out.csv").string();
    const long rows = tr.export_csv(csv, all[60].t, tr.t1(), &err);
    check("csv: 60 rows", rows == 60, std::to_string(rows) + " " + err);
    {
        std::ifstream f(csv);
        std::string head, first;
        std::getline(f, head);
        std::getline(f, first);
        check("csv: header", head == "time_local,unix,cpu.total,mem.used_pct,net.wlan0.rx", head);
        int commas = 0;
        for (char c : first) commas += c == ',';
        check("csv: every column in a row", commas == 4 && first.ends_with(",1000"), first);
    }

    // A step from a file that has since been closed / reopened touches nothing.
    const auto stale = j.size();
    tr.open(b);
    app.clear_error();
    j.undo();   // the start-mark restore belongs to the first open of `a`
    check("stale: undo across a reopen does nothing to the new file", tr.marks().size() == 2 && stale > 0);
    check("stale: says why", app.last_error() == "That recording is no longer open.", app.last_error());

    tr.close();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

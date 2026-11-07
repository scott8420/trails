// selftest -- s012 triggers: the book through the ONE journal (the gate,
// undo / redo), JSON save / load (atomic, bad file), and the evaluator
// (holds for N s, fires once, re-arms on clear, below, off, missing metric).
#include "core/AppStore.hpp"
#include "core/Triggers.hpp"

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

namespace core = trails::core;
namespace fs = std::filesystem;
using CheckFn = std::function<void(const std::string&, bool, const std::string&)>;

namespace {
struct NoOS : core::ProcOS {
    std::optional<core::ProcKnobs> read(const core::ProcKey&) const override { return std::nullopt; }
    std::string set_nice(const core::ProcKey&, int) override { return "no"; }
    std::string set_cpus(const core::ProcKey&, const std::vector<int>&) override { return "no"; }
    std::string signal(const core::ProcKey&, int) override { return "no"; }
};
core::Snapshot snap(std::initializer_list<std::pair<const std::string, float>> v) {
    core::Snapshot s;
    s.now = v;
    return s;
}
}  // namespace

void run_trigger_tests(const CheckFn& check_fn) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_fn(n, ok, d); };
    std::cout << "\n-- triggers (s012) --\n";

    const fs::path dir = fs::temp_directory_path() /
                         ("trails_trg_" + std::to_string(::getpid()) + "_" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string path = (dir / "cfg" / "triggers.json").string();

    check("describe", core::describe(core::Trigger{"x", "cpu.total", true, 90, 30, true, true, true}) ==
                          "cpu.total above 90 for 30 s → record + mark",
          core::describe(core::Trigger{"x", "cpu.total", true, 90, 30, true, true, true}));

    // THE GATE over the trigger verbs: each from a book with one rule.
    for (const auto& v : core::triggers::verbs()) {
        NoOS os;
        core::ProcKnobStore ks(os);
        core::TriggerBook book;
        core::AppStore app(ks);
        app.set_triggers(&book);
        core::AppJournal j(app);
        app.set_door([&] { return j.open(); });
        // The fixture rule is LOADED (not a user choice): written with no door.
        book.set_door({});
        book.put(1, core::Trigger{"busy", "cpu.total", true, 90, 30, true, true, true});
        book.set_door([&] { return j.open(); });
        auto snapshot = [&] {
            std::string s;
            for (auto id : book.ids()) s += std::to_string(id) + ":" + core::describe(*book.get(id)) +
                                            (book.get(id)->enabled ? "" : " off") + ";";
            return s;
        };
        const auto before = snapshot();
        v.run(j, app, 1);
        const auto after = snapshot();
        check("gate/trigger " + v.name + ": through the one door", app.strays() == 0);
        check("gate/trigger " + v.name + ": is a step", j.size() == 1 && after != before);
        j.undo();
        check("gate/trigger " + v.name + ": undo puts it back exactly", snapshot() == before, snapshot());
        j.redo();
        check("gate/trigger " + v.name + ": redo reproduces it", snapshot() == after);
    }
    {
        core::TriggerBook book;
        book.put(1, core::Trigger{});
        book.set_door([] { return false; });
        book.put(2, core::Trigger{});
        check("gate/trigger: a write around the door is a stray", book.strays() == 1);
    }

    // Save / load.
    {
        NoOS os;
        core::ProcKnobStore ks(os);
        core::TriggerBook book;
        book.set_path(path);
        core::AppStore app(ks);
        app.set_triggers(&book);
        core::AppJournal j(app);
        app.set_door([&] { return j.open(); });
        const auto a = core::triggers::add(j, app, core::Trigger{"CPU pegged", "cpu.total", true, 90, 30, true, true, true});
        const auto b = core::triggers::add(j, app, core::Trigger{"Cold", "temp.max", false, 20.5, 0, false, true, false});
        check("save: written on every change, no temp left", fs::exists(path) && !fs::exists(path + ".tmp"));
        core::TriggerBook again;
        again.set_path(path);
        const auto e = again.load();
        check("load: round trip", e.empty() && again.ids() == std::vector<std::int64_t>{a, b} &&
                                      *again.get(a) == *book.get(a) && *again.get(b) == *book.get(b), e);
        j.undo();   // remove "Cold" again -> saved
        core::TriggerBook third;
        third.set_path(path);
        third.load();
        check("save: an undo is saved too", third.ids().size() == 1);
        core::TriggerBook missing;
        missing.set_path((dir / "nope.json").string());
        check("load: no file = empty book", missing.load().empty() && missing.ids().empty());
        std::ofstream(dir / "bad.json") << "{ not json";
        core::TriggerBook bad;
        bad.set_path((dir / "bad.json").string());
        check("load: a bad file says so, book empty", !bad.load().empty() && bad.ids().empty());
    }

    // The evaluator.
    {
        core::TriggerBook book;
        book.put(1, core::Trigger{"busy", "cpu.total", true, 90, 3, true, true, true});
        book.put(2, core::Trigger{"cold", "temp.max", false, 20, 0, false, true, true});
        book.put(3, core::Trigger{"off", "cpu.total", true, 10, 0, false, true, false});
        core::TriggerEval ev;
        auto ids = [](const std::vector<core::TriggerFire>& f) {
            std::string s;
            for (const auto& x : f) s += std::to_string(x.id);
            return s;
        };
        check("eval: below the line, nothing", ids(ev.step(book, snap({{"cpu.total", 50}, {"temp.max", 40}}), 0)) == "");
        check("eval: above, holding", ids(ev.step(book, snap({{"cpu.total", 95}, {"temp.max", 40}}), 1)) == "" &&
                                          ev.status(1).state == core::TriggerEval::State::Holding);
        check("eval: still holding at 2 s", ids(ev.step(book, snap({{"cpu.total", 95}, {"temp.max", 40}}), 3)) == "");
        check("eval: fires at 3 s held", ids(ev.step(book, snap({{"cpu.total", 96}, {"temp.max", 40}}), 4)) == "1");
        check("eval: fires once", ids(ev.step(book, snap({{"cpu.total", 99}, {"temp.max", 40}}), 9)) == "" &&
                                      ev.status(1).state == core::TriggerEval::State::Fired);
        check("eval: a dip re-arms", ids(ev.step(book, snap({{"cpu.total", 20}, {"temp.max", 40}}), 10)) == "" &&
                                         ev.status(1).state == core::TriggerEval::State::Armed);
        ev.step(book, snap({{"cpu.total", 95}}), 11);
        check("eval: and the hold starts over", ids(ev.step(book, snap({{"cpu.total", 95}}), 13)) == "" &&
                                                    ids(ev.step(book, snap({{"cpu.total", 95}}), 14)) == "1");
        check("eval: below + 0 s fires at once", ids(ev.step(book, snap({{"temp.max", 15}}), 20)) == "2");
        check("eval: a missing metric is not holding", ids(ev.step(book, snap({}), 21)) == "" &&
                                                           ev.status(2).state == core::TriggerEval::State::Armed);
        check("eval: a turned-off rule never fires", ev.status(3).state == core::TriggerEval::State::Off);
        book.put(1, std::nullopt);
        ev.step(book, snap({}), 22);
        check("eval: a removed rule is forgotten", ev.status(1).state == core::TriggerEval::State::Armed);
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

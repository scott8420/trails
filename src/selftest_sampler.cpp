// trails_selftest -- the sampler core (s002).
//
// Fixture trees, not the live machine: each test writes a tiny <tmp>/proc +
// <tmp>/sys, points a reader or the Sampler at it with a fake clock, rewrites
// it as "frame 2", ticks again, and checks numbers worked out by hand in the
// comments. A last block runs against the real "/" for sanity ranges only, and
// one drives the thread to prove start / stop / interval change are prompt.

#include "core/Sampler.hpp"
#include "core/probe/Probe.hpp"
#include "core/series/Ring.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
namespace core = trails::core;
namespace probe = trails::core::probe;

using Check = std::function<void(const std::string&, bool, const std::string&)>;

namespace {

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
std::string num(double v) { return std::to_string(v); }

void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}

// A /proc/<pid>/stat line with the fields the parser reads; the rest are
// plausible fillers (tpgid -1 and a negative priority keep the signed paths honest).
std::string stat_line(int pid, const std::string& comm, char state, int ppid,
                      unsigned long flags, unsigned long utime, unsigned long stime,
                      long nice, int threads, unsigned long starttime, unsigned long rss) {
    return std::to_string(pid) + " (" + comm + ") " + state + " " + std::to_string(ppid) +
           " 1 1 0 -1 " + std::to_string(flags) + " 10 0 0 0 " + std::to_string(utime) + " " +
           std::to_string(stime) + " 0 0 " + std::to_string(20 + nice) + " " +
           std::to_string(nice) + " " + std::to_string(threads) + " 0 " +
           std::to_string(starttime) + " 123456 " + std::to_string(rss) +
           " 18446744073709551615 1 1 0 0 0 0 0 0 0 0 0 0 17 1 0 0 0 0 0\n";
}

struct Fixture {
    fs::path root;
    Fixture() {
        root = fs::temp_directory_path() /
               ("trails_fx_" + std::to_string(::getpid()) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
    }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    std::string str() const { return root.string(); }
    void file(const std::string& rel, const std::string& text) const { put(root / rel, text); }
    void rm(const std::string& rel) const { std::error_code ec; fs::remove_all(root / rel, ec); }
};

// ── Frame 1 / frame 2 of a small machine. dt between them = 2.0 s. ────────────
void write_common(const Fixture& fx) {
    fx.file("proc/meminfo",
            "MemTotal:       16000000 kB\n"
            "MemFree:         2000000 kB\n"
            "MemAvailable:   10000000 kB\n"
            "Buffers:          100000 kB\n"
            "Cached:          5000000 kB\n"
            "SReclaimable:     300000 kB\n"
            "Shmem:            200000 kB\n"
            "SwapTotal:       8000000 kB\n"
            "SwapFree:        6000000 kB\n");
    fx.file("proc/loadavg", "1.50 0.75 0.25 3/412 9999\n");
    fs::create_directories(fx.root / "sys/block/nvme0n1");
    fs::create_directories(fx.root / "sys/block/loop0");
}

void write_frame1(const Fixture& fx) {
    // total 1000, idle+iowait 850
    fx.file("proc/stat",
            "cpu  100 0 50 800 50 0 0 0 0 0\n"
            "cpu0 50 0 25 400 25 0 0 0 0 0\n"
            "cpu1 50 0 25 400 25 0 0 0 0 0\n"
            "intr 1 2 3\nctxt 5000\nbtime 1\nprocesses 10\nprocs_running 2\nprocs_blocked 0\n");
    fx.file("proc/net/dev",
            "Inter-|   Receive                                                |  Transmit\n"
            " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
            "    lo:    1000      10    0    0    0     0          0         0     1000      10    0    0    0     0       0          0\n"
            "  eth0:   10000     100    0    0    0     0          0         0     5000      50    0    0    0     0       0          0\n"
            " wlan0:100 1 0 0 0 0 0 0 200 2 0 0 0 0 0 0\n");
    fx.file("proc/diskstats",
            " 259       0 nvme0n1 100 0 2000 0 50 0 4000 0 0 500 0 0 0 0 0 0 0\n"
            " 259       1 nvme0n1p1 90 0 1800 0 40 0 3000 0 0 400 0 0 0 0 0 0 0\n"
            "   7       0 loop0 10 0 100 0 0 0 0 0 0 5 0 0 0 0 0 0 0\n");
    // pid 1: systemd; 7: kernel thread; 42: comm with spaces and parens;
    // 55: exits before frame 2; 99: pid that gets REUSED in frame 2.
    fx.file("proc/1/stat",  stat_line(1, "systemd", 'S', 0, 0x400100, 500, 200, 0, 1, 10, 3000));
    fx.file("proc/1/cmdline", std::string("/usr/lib/systemd/systemd\0--switched-root\0", 41));
    fx.file("proc/7/stat",  stat_line(7, "kworker/0:1", 'I', 2, 0x04208060, 0, 50, 0, 1, 20, 0));
    fx.file("proc/42/stat", stat_line(42, "tmux: server) x", 'S', 1, 0x400000, 100, 0, -5, 3, 1000, 10));
    fx.file("proc/42/cmdline", std::string("tmux\0", 5));
    fx.file("proc/55/stat", stat_line(55, "short", 'R', 1, 0, 1, 1, 0, 1, 1200, 5));
    fx.file("proc/99/stat", stat_line(99, "old", 'S', 1, 0, 50, 50, 0, 1, 500, 5));
    fx.file("proc/99/cmdline", std::string("old\0", 4));
    fx.file("proc/self/stat", "not a pid dir\n");   // non-numeric entries are skipped
}

void write_frame2(const Fixture& fx) {
    // delta total 1000; idle+iowait 1450 -> delta 600 -> busy 40%.
    // user +300 (30%), system +100 (10%), iowait +100 (10%).
    // cpu0 +300 user +100 sys, no idle -> 100%; cpu1 +500 idle +100 iowait -> 0%.
    fx.file("proc/stat",
            "cpu  400 0 150 1300 150 0 0 0 0 0\n"
            "cpu0 350 0 125 400 25 0 0 0 0 0\n"
            "cpu1 50 0 25 900 125 0 0 0 0 0\n"
            "ctxt 9000\nprocs_running 1\n");
    // eth0: +20000 rx, +4000 tx over 2 s -> 10000 / 2000 B/s. lo: +2000 each
    // (excluded from the machine total). wlan0 unchanged.
    fx.file("proc/net/dev",
            "Inter-|   Receive                                                |  Transmit\n"
            " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
            "    lo:    3000      30    0    0    0     0          0         0     3000      30    0    0    0     0       0          0\n"
            "  eth0:   30000     300    0    0    0     0          0         0     9000      90    0    0    0     0       0          0\n"
            " wlan0:100 1 0 0 0 0 0 0 200 2 0 0 0 0 0 0\n");
    // nvme0n1: +4096 sectors read, +2048 written, +1000 io_ms over 2 s
    //   -> 1048576 B/s read, 524288 B/s write, 50% busy.
    // loop0 reads a lot but must not count in the machine total.
    fx.file("proc/diskstats",
            " 259       0 nvme0n1 200 0 6096 0 80 0 6048 0 0 1500 0 0 0 0 0 0 0\n"
            " 259       1 nvme0n1p1 180 0 5800 0 70 0 5000 0 0 1300 0 0 0 0 0 0 0\n"
            "   7       0 loop0 1000 0 900000 0 0 0 0 0 0 900 0 0 0 0 0 0 0\n");
    fx.file("proc/1/stat",  stat_line(1, "systemd", 'S', 0, 0x400100, 500, 200, 0, 1, 10, 3000));
    fx.file("proc/7/stat",  stat_line(7, "kworker/0:1", 'I', 2, 0x04208060, 0, 50, 0, 1, 20, 0));
    // 42: +200 utime +100 stime = 300 of 1000 machine jiffies -> 30%.
    fx.file("proc/42/stat", stat_line(42, "tmux: server) x", 'S', 1, 0x400000, 300, 100, -5, 3, 1000, 10));
    fx.rm("proc/55");
    // 99 reused: new starttime, big ticks -- must read 0% (new identity), new cmdline.
    fx.file("proc/99/stat", stat_line(99, "new", 'R', 1, 0, 5000, 5000, 0, 2, 900, 5));
    fx.file("proc/99/cmdline", std::string("new\0--flag\0", 11));
}

const core::ProcRow* find_pid(const core::Snapshot& s, int pid) {
    for (const auto& p : s.procs) if (p.pid == pid) return &p;
    return nullptr;
}

}  // namespace

void run_sampler_tests(const Check& check) {
    // ── Ring ────────────────────────────────────────────────────────────────
    {
        core::Ring<int> r(3);
        check("ring: starts empty", r.empty() && r.capacity() == 3, "");
        r.push(1); r.push(2);
        check("ring: partial fill keeps order", r.size() == 2 && r[0] == 1 && r[1] == 2, "");
        r.push(3); r.push(4); r.push(5);
        check("ring: overwrites oldest when full",
              r.full() && r.size() == 3 && r.front() == 3 && r.back() == 5 &&
              r.to_vector() == std::vector<int>({3, 4, 5}), "");
        core::Ring<int> copy = r;
        r.push(6);
        check("ring: a copy is independent (snapshot safety)", copy.back() == 5 && r.back() == 6, "");
        r.clear();
        check("ring: clear empties, keeps capacity", r.empty() && r.capacity() == 3, "");
    }

    // ── Helpers + parsers in isolation ─────────────────────────────────────────
    {
        check("probe: at() joins with one slash",
              probe::at("/", "proc/stat") == "/proc/stat" &&
              probe::at("/tmp/x/", "/proc") == "/tmp/x/proc" &&
              probe::at("/tmp/x", "proc") == "/tmp/x/proc", probe::at("/tmp/x/", "/proc"));
        check("probe: delta never wraps backwards", probe::delta(10, 4) == 0 && probe::delta(4, 10) == 6, "");

        probe::ProcStat ps;
        bool ok = probe::parse_proc_stat(
            stat_line(42, "a (b) c)", 'S', 7, 0x00200040, 11, 22, -3, 4, 777, 10), 16384, ps);
        check("procstat: comm with spaces and parens parses", ok && ps.comm == "a (b) c)", ps.comm);
        check("procstat: fields after comm land right",
              ps.pid == 42 && ps.ppid == 7 && ps.state == 'S' && ps.utime == 11 &&
              ps.stime == 22 && ps.nice == -3 && ps.threads == 4 && ps.starttime == 777,
              "ppid=" + std::to_string(ps.ppid) + " nice=" + std::to_string(ps.nice) +
              " thr=" + std::to_string(ps.threads) + " start=" + std::to_string(ps.starttime));
        check("procstat: rss scales by the given page size (16 KiB, Asahi)",
              ps.rss_bytes == 10u * 16384u, std::to_string(ps.rss_bytes));
        check("procstat: PF_KTHREAD flag detected", ps.kernel_thread, "");
        check("procstat: truncated line rejected",
              !probe::parse_proc_stat("42 (x) S 1 2 3", 4096, ps), "");
        check("procstat: garbage rejected", !probe::parse_proc_stat("hello", 4096, ps), "");
    }

    // ── Readers against a fixture tree ─────────────────────────────────────────
    {
        Fixture fx;
        write_common(fx);
        write_frame1(fx);
        const std::string R = fx.str();

        probe::CpuStat c;
        check("stat: reads aggregate + cores",
              probe::read_stat(R, c) && c.all.total() == 1000 && c.cores.size() == 2 &&
              c.ctxt == 5000 && c.procs_running == 2, std::to_string(c.cores.size()));

        probe::MemInfo m;
        check("meminfo: kB -> bytes, used = total - available",
              probe::read_meminfo(R, m) && m.total == 16000000ull * 1024 &&
              m.used() == 6000000ull * 1024 && m.swap_used() == 2000000ull * 1024, "");

        probe::LoadAvg la;
        check("loadavg: three averages + running/total",
              probe::read_loadavg(R, la) && near(la.one, 1.5) && near(la.fifteen, 0.25) &&
              la.running == 3 && la.total == 412, num(la.one));

        std::vector<probe::NetIf> nets;
        bool ok = probe::read_netdev(R, nets);
        check("netdev: headers skipped, three interfaces", ok && nets.size() == 3,
              std::to_string(nets.size()));
        check("netdev: 'name:digits' with no space parses",
              nets.size() == 3 && nets[2].name == "wlan0" && nets[2].rx_bytes == 100 &&
              nets[2].tx_bytes == 200, nets.size() == 3 ? nets[2].name : "");

        std::vector<probe::DiskStat> ds;
        ok = probe::read_diskstats(R, ds);
        check("diskstats: three devices", ok && ds.size() == 3, std::to_string(ds.size()));
        check("diskstats: whole disk vs partition from sys/block",
              ds.size() == 3 && ds[0].whole_disk && !ds[1].whole_disk && ds[2].whole_disk, "");
        check("diskstats: loop / partitions stay out of the machine total",
              ds.size() == 3 && probe::disk_counts_in_total(ds[0]) &&
              !probe::disk_counts_in_total(ds[1]) && !probe::disk_counts_in_total(ds[2]), "");

        auto pids = probe::list_pids(R);
        check("pids: numeric dirs only", pids.size() == 5, std::to_string(pids.size()));
        check("cmdline: NULs become spaces, trailing NUL dropped",
              probe::read_cmdline(R, 1) == "/usr/lib/systemd/systemd --switched-root",
              probe::read_cmdline(R, 1));

        check("readers: missing source -> false, no throw",
              !probe::read_stat(R + "/nope", c) && !probe::read_meminfo(R + "/nope", m), "");
    }

    // ── Sampler: two frames, exact rates ───────────────────────────────────────
    {
        Fixture fx;
        write_common(fx);
        write_frame1(fx);
        double fake_t = 100.0;
        core::SamplerConfig cfg;
        cfg.root = fx.str();
        cfg.history = 2;
        cfg.page_size = 16384;
        cfg.clock = [&] { return fake_t; };
        core::Sampler sm(cfg);

        check("sampler: nothing published before the first tick", sm.snapshot() == nullptr, "");
        auto s1 = sm.tick();
        check("sampler: first tick publishes, no rates yet",
              s1 && sm.snapshot() == s1 && s1->seq == 1 && !s1->rates_valid &&
              s1->series.empty() && s1->n_cpus == 2 && s1->error.empty(), s1 ? s1->error : "null");
        check("sampler: first tick still carries counters",
              s1->mem.total == 16000000ull * 1024 && s1->procs.size() == 5, std::to_string(s1->procs.size()));

        write_frame2(fx);
        fake_t = 102.0;
        auto s2 = sm.tick();
        check("sampler: second tick has rates, dt = 2",
              s2->seq == 2 && s2->rates_valid && near(s2->dt, 2.0), num(s2->dt));
        check("sampler: s1 is untouched by the next tick (immutable)",
              s1->seq == 1 && !s1->rates_valid, "");
        check("cpu: busy 40 / user 30 / system 10 / iowait 10",
              near(s2->cpu.busy, 40) && near(s2->cpu.user, 30) &&
              near(s2->cpu.system, 10) && near(s2->cpu.iowait, 10),
              num(s2->cpu.busy) + " " + num(s2->cpu.user) + " " + num(s2->cpu.system));
        check("cpu: per core 100 / 0",
              s2->core_busy.size() == 2 && near(s2->core_busy[0], 100) && near(s2->core_busy[1], 0), "");

        double eth_rx = -1, eth_tx = -1, lo_rx = -1;
        for (const auto& n : s2->net) {
            if (n.name == "eth0") { eth_rx = n.rx_bps; eth_tx = n.tx_bps; }
            if (n.name == "lo")   lo_rx = n.rx_bps;
        }
        check("net: eth0 10000 rx / 2000 tx B/s", near(eth_rx, 10000) && near(eth_tx, 2000),
              num(eth_rx) + " " + num(eth_tx));
        check("net: machine total excludes loopback",
              near(lo_rx, 1000) && near(s2->net_rx_bps, 10000) && near(s2->net_tx_bps, 2000),
              num(s2->net_rx_bps));

        const core::DiskRate* nv = nullptr;
        for (const auto& d : s2->disks) if (d.name == "nvme0n1") nv = &d;
        check("disk: nvme0n1 1 MiB/s read, 512 KiB/s write, 50% busy",
              nv && near(nv->read_bps, 1048576) && near(nv->write_bps, 524288) && near(nv->busy_pct, 50),
              nv ? num(nv->read_bps) + " " + num(nv->busy_pct) : "missing");
        check("disk: machine total = real whole disks only (no loop, no partition)",
              near(s2->disk_read_bps, 1048576) && near(s2->disk_write_bps, 524288),
              num(s2->disk_read_bps));

        const core::ProcRow* p42 = find_pid(*s2, 42);
        check("proc: 300 of 1000 machine jiffies -> 30%", p42 && near(p42->cpu_pct, 30),
              p42 ? num(p42->cpu_pct) : "missing");
        check("proc: name, nice, threads, rss carried",
              p42 && p42->name == "tmux: server) x" && p42->nice == -5 && p42->threads == 3 &&
              p42->rss_bytes == 10u * 16384u && p42->cmdline == "tmux", "");
        check("proc: idle process reads 0%", find_pid(*s2, 1) && near(find_pid(*s2, 1)->cpu_pct, 0), "");
        check("proc: kernel thread flagged, no cmdline",
              find_pid(*s2, 7) && find_pid(*s2, 7)->kernel_thread && find_pid(*s2, 7)->cmdline.empty(), "");
        check("proc: exited process gone", find_pid(*s2, 55) == nullptr, "");
        const core::ProcRow* p99 = find_pid(*s2, 99);
        check("proc: reused pid is a new identity (0%, new cmdline)",
              p99 && near(p99->cpu_pct, 0) && p99->cmdline == "new --flag" && p99->name == "new",
              p99 ? p99->cmdline + " " + num(p99->cpu_pct) : "missing");
        check("proc: rows in pid order",
              s2->procs.size() == 4 && s2->procs.front().pid == 1 && s2->procs.back().pid == 99,
              std::to_string(s2->procs.size()));

        const auto* ct = s2->find("cpu.total");
        check("series: history starts at the second tick",
              ct && ct->size() == 1 && near(ct->back(), 40), ct ? std::to_string(ct->size()) : "missing");
        check("series: per-core, per-interface, per-disk ids present",
              s2->find("cpu.core.1") && s2->find("net.eth0.rx") && s2->find("disk.nvme0n1.busy") &&
              !s2->find("disk.nvme0n1p1.read"), "");

        // Frame 3 = frame 2 again, 1 s later, with eth0 unplugged.
        fx.file("proc/net/dev",
                "Inter-|\n face |\n    lo: 3000 30 0 0 0 0 0 0 3000 30 0 0 0 0 0 0\n");
        fake_t = 103.0;
        auto s3 = sm.tick();
        check("series: idle interval reads 0% busy",
              s3->find("cpu.total") && s3->find("cpu.total")->size() == 2 &&
              near(s3->find("cpu.total")->back(), 0), "");
        check("series: vanished interface keeps its window while still recent",
              s3->find("net.eth0.rx") != nullptr, "");
        fake_t = 104.0;
        auto s4 = sm.tick();
        check("series: ring capped at history (2)",
              s4->find("cpu.total") && s4->find("cpu.total")->size() == 2, "");
        check("series: vanished interface ages out after a full window",
              s4->find("net.eth0.rx") == nullptr && s4->find("net.lo.rx") != nullptr, "");

        // Clock stands still -> no division by zero, no history push.
        auto s5 = sm.tick();
        check("sampler: dt = 0 tick publishes without rates",
              !s5->rates_valid && s5->find("cpu.total")->size() == 2, "");
    }

    // ── Sampler on an empty root: errors reported, nothing thrown ──────────────
    {
        Fixture fx;
        core::SamplerConfig cfg;
        cfg.root = fx.str();
        core::Sampler sm(cfg);
        auto s = sm.tick();
        check("sampler: missing sources named in error, empty tables",
              s && s->error.find("proc/stat") != std::string::npos &&
              s->error.find("proc/meminfo") != std::string::npos && s->procs.empty() && s->n_cpus == 0,
              s ? s->error : "null");
    }

    // ── Live "/" -- ranges only ────────────────────────────────────────────────
    {
        core::Sampler sm;   // root "/", real clock
        sm.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        auto s = sm.tick();
        bool cores_ok = !s->core_busy.empty();
        for (double v : s->core_busy) cores_ok = cores_ok && v >= 0 && v <= 100;
        check("live: rates valid, cpu in 0..100, cores in 0..100",
              s->rates_valid && s->cpu.busy >= 0 && s->cpu.busy <= 100 && cores_ok,
              num(s->cpu.busy) + "% over " + std::to_string(s->n_cpus) + " cpus");
        check("live: memory read", s->mem.total > 0 && s->mem.used() <= s->mem.total,
              std::to_string(s->mem.total / (1024 * 1024)) + " MiB");
        const core::ProcRow* me = find_pid(*s, static_cast<int>(::getpid()));
        check("live: finds itself in the process table with its cmdline",
              me && me->cmdline.find("trails_selftest") != std::string::npos &&
              me->uid == ::getuid() && me->rss_bytes > 0,
              me ? me->cmdline : "self missing of " + std::to_string(s->procs.size()));
        check("live: all sources read", s->error.empty(), s->error);
    }

    // ── The thread: publishes on its own, stops promptly ──────────────────────
    {
        using namespace std::chrono;
        core::SamplerConfig cfg;
        cfg.interval = milliseconds(50);
        core::Sampler sm(cfg);
        sm.start();
        sm.start();   // idempotent
        const auto t0 = steady_clock::now();
        while ((!sm.snapshot() || sm.snapshot()->seq < 3) && steady_clock::now() - t0 < seconds(5))
            std::this_thread::sleep_for(milliseconds(10));
        const auto got = sm.snapshot() ? sm.snapshot()->seq : 0;
        check("thread: publishes snapshots on its own", got >= 3, std::to_string(got));

        sm.set_interval(seconds(30));
        std::this_thread::sleep_for(milliseconds(80));   // now parked in a 30 s wait
        const auto t1 = steady_clock::now();
        sm.stop();
        const auto ms = duration_cast<milliseconds>(steady_clock::now() - t1).count();
        check("thread: stop() is prompt mid-interval", !sm.running() && ms < 500,
              std::to_string(ms) + " ms");

        sm.set_interval(milliseconds(10));
        check("thread: interval floor is 50 ms", sm.interval() == milliseconds(50),
              std::to_string(sm.interval().count()));
    }
}

// ── s003: the tab list, value formatting, tabs <-> Alt+N ─────────────────────
#include "core/Format.hpp"
#include "core/Shortcuts.hpp"
#include "core/Tabs.hpp"

#include <set>

void run_shell_core_tests(const Check& check) {
    namespace fmt = trails::core::fmt;
    const auto& tabs = trails::core::tab_registry();

    {
        std::set<std::string_view> ids, css, icons;
        bool fields = true;
        for (const auto& t : tabs) {
            ids.insert(t.id); css.insert(t.css); icons.insert(t.icon);
            fields = fields && !t.title.empty() && !t.blurb.empty() &&
                     t.icon.ends_with("-symbolic") && t.css == "tab-" + std::string(t.id);
        }
        check("tabs: ten, ids / icons / colour classes unique",
              tabs.size() == 10 && ids.size() == 10 && css.size() == 10 && icons.size() == 10,
              std::to_string(tabs.size()));
        check("tabs: every tab has title, blurb, symbolic icon, tab-<id> class", fields, "");
        check("tabs: Overview first and built; unbuilt tabs name their session",
              tabs.front().id == "overview" && tabs.front().arrives.empty() &&
              std::all_of(tabs.begin() + 1, tabs.end(),
                          [](const auto& t) { return t.arrives.empty() || t.arrives.starts_with("s"); }), "");
        check("tabs: CPU and Memory are built (s004)",
              trails::core::find_tab("cpu")->arrives.empty() &&
              trails::core::find_tab("memory")->arrives.empty(), "");
        check("tabs: find_tab hits and misses",
              trails::core::find_tab("cpu") && trails::core::find_tab("cpu")->title == "CPU" &&
              !trails::core::find_tab("nope"), "");
    }
    {
        // Each tab is reachable by one Alt+N row, in rail order.
        std::vector<std::string> rows;
        for (const auto& s : trails::core::shortcut_registry())
            if (s.action.starts_with("win.tab::")) rows.push_back(s.action.substr(9));
        bool same = rows.size() == tabs.size();
        for (std::size_t i = 0; same && i < rows.size(); ++i) same = rows[i] == tabs[i].id;
        check("tabs: Alt+N shortcut rows match the tab list, in order", same,
              std::to_string(rows.size()) + " rows");
    }
    {
        check("fmt: bytes edges",
              fmt::bytes(0) == "0 B" && fmt::bytes(1023) == "1023 B" &&
              fmt::bytes(1024) == "1.00 KiB" && fmt::bytes(1536) == "1.50 KiB",
              fmt::bytes(1536));
        check("fmt: three significant figures",
              fmt::bytes(16000000ull * 1024) == "15.3 GiB" && fmt::bytes(300ull * 1024 * 1024) == "300 MiB",
              fmt::bytes(16000000ull * 1024) + " / " + fmt::bytes(300ull * 1024 * 1024));
        check("fmt: rounding to 1024 steps up a unit",
              fmt::bytes(1024ull * 1024 - 1) == "1.00 MiB", fmt::bytes(1024ull * 1024 - 1));
        check("fmt: rate", fmt::rate(0) == "0 B/s" && fmt::rate(1048576) == "1.00 MiB/s" &&
                           fmt::rate(-5) == "0 B/s", fmt::rate(1048576));
        check("fmt: pct", fmt::pct(0) == "0%" && fmt::pct(7.46) == "7.5%" && fmt::pct(42.4) == "42%" &&
                          fmt::pct(250) == "100%", fmt::pct(7.46));
    }
}

// ── s004: cpufreq, model, P / E clusters, swaps, zram ─────────────────────────
void run_cpu_mem_tests(const Check& check) {
    // An M1 Pro as Asahi shows it: cpu0-1 efficiency (2.064 GHz max), cpu2-9
    // performance (3.228 GHz max), 16 KiB pages, no "model name" in cpuinfo.
    Fixture fx;
    write_common(fx);
    std::string stat = "cpu  100 0 50 800 50 0 0 0 0 0\n";
    for (int c = 0; c < 10; ++c) stat += "cpu" + std::to_string(c) + " 10 0 5 80 5 0 0 0 0 0\n";
    fx.file("proc/stat", stat);
    for (int c = 0; c < 10; ++c) {
        const std::string d = "sys/devices/system/cpu/cpu" + std::to_string(c) + "/cpufreq/";
        const bool e = c < 2;
        fx.file(d + "cpuinfo_max_freq", e ? "2064000\n" : "3228000\n");
        fx.file(d + "scaling_cur_freq", e ? "600000\n" : "3000000\n");
    }
    fx.file("proc/cpuinfo", "processor\t: 0\nBogoMIPS\t: 48.00\nCPU implementer\t: 0x61\n");
    fx.file("proc/device-tree/model", std::string("Apple MacBook Pro (14-inch, M1 Pro, 2021)\0", 42));
    fx.file("proc/swaps",
            "Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n"
            "/dev/zram0                              partition\t8388604\t\t1048576\t\t100\n"
            "/var/swap/swapfile                      file\t\t4194300\t\t0\t\t-2\n");
    fx.file("sys/block/zram0/disksize", "8589934592\n");
    fx.file("sys/block/zram0/mm_stat", "1073741824 268435456 285212672 0 300000000 100 0 0 0\n");
    fx.file("sys/block/zram1/disksize", "0\n");   // allocated, unused: skipped
    const std::string R = fx.str();

    probe::CpuStat c;
    probe::read_stat(R, c);
    check("stat: core ids kept alongside cores", c.core_ids.size() == 10 && c.core_ids[9] == 9, "");
    {
        Fixture gap;
        gap.file("proc/stat", "cpu  1 0 1 1 0 0 0 0\ncpu0 1 0 1 1 0 0 0 0\ncpu2 1 0 1 1 0 0 0 0\n");
        probe::CpuStat g;
        probe::read_stat(gap.str(), g);
        check("stat: an offline core leaves a gap in ids, not a renumber",
              g.core_ids == std::vector<int>({0, 2}), "");
    }

    auto f = probe::read_cpufreq(R, c.core_ids);
    check("cpufreq: cur + max per core, kHz",
          f.size() == 10 && f[0].cur_khz == 600000 && f[0].max_khz == 2064000 &&
          f[9].cur_khz == 3000000 && f[9].max_khz == 3228000, "");
    check("cpufreq: absent (VM) -> zeros, no throw",
          probe::read_cpufreq(R + "/nope", {0, 1}).at(1).max_khz == 0, "");

    auto cl = probe::clusters_from(f);
    check("clusters: two max frequencies -> Performance (first) + Efficiency",
          cl.size() == 2 && cl[0].name == "Performance" && cl[0].cores.size() == 8 &&
          cl[0].max_khz == 3228000 && cl[1].name == "Efficiency" &&
          cl[1].cores == std::vector<int>({0, 1}),
          cl.empty() ? "none" : cl[0].name);
    {
        std::vector<probe::CpuFreq> same = {{0, 1, 3000}, {1, 1, 3000}};
        std::vector<probe::CpuFreq> none = {{0, 0, 0}, {1, 0, 0}};
        std::vector<probe::CpuFreq> three = {{0, 1, 1000}, {1, 1, 2000}, {2, 1, 3000}};
        check("clusters: one max -> 'All cores'; no data -> 'All cores' at 0; three -> numbered",
              probe::clusters_from(same).size() == 1 && probe::clusters_from(same)[0].name == "All cores" &&
              probe::clusters_from(none)[0].max_khz == 0 && probe::clusters_from(none)[0].cores.size() == 2 &&
              probe::clusters_from(three).size() == 3 && probe::clusters_from(three)[0].max_khz == 3000 &&
              probe::clusters_from(three)[0].name == "Cluster 3", "");
    }

    check("model: no 'model name' -> device-tree model, NUL trimmed",
          probe::read_cpu_model(R) == "Apple MacBook Pro (14-inch, M1 Pro, 2021)", probe::read_cpu_model(R));
    {
        Fixture x86;
        x86.file("proc/cpuinfo", "processor\t: 0\nmodel name\t: AMD Ryzen 7 5800X 8-Core Processor\n");
        check("model: x86 'model name' wins", probe::read_cpu_model(x86.str()) ==
              "AMD Ryzen 7 5800X 8-Core Processor", probe::read_cpu_model(x86.str()));
    }

    std::vector<probe::SwapDev> sw;
    check("swaps: header skipped, KiB -> bytes, priority signed",
          probe::read_swaps(R, sw) && sw.size() == 2 && sw[0].name == "/dev/zram0" &&
          sw[0].used == 1048576ull * 1024 && sw[0].priority == 100 && sw[1].type == "file" &&
          sw[1].priority == -2, std::to_string(sw.size()));
    auto z = probe::read_zram(R);
    check("zram: configured device read, unused one skipped",
          z.size() == 1 && z[0].name == "zram0" && z[0].disksize == 8589934592ull &&
          z[0].orig == 1073741824ull && z[0].compr == 268435456ull && z[0].mem_used == 285212672ull,
          std::to_string(z.size()));

    // Through the Sampler.
    double t = 10;
    core::SamplerConfig cfg;
    cfg.root = R;
    cfg.clock = [&] { return t; };
    core::Sampler sm(cfg);
    sm.tick();
    t = 11;
    auto s = sm.tick();
    check("snapshot: core ids, MHz, clusters, model carried",
          s->core_ids.size() == 10 && s->core_mhz.size() == 10 && near(s->core_mhz[0], 600) &&
          near(s->core_mhz[5], 3000) && s->clusters.size() == 2 &&
          s->cpu_model.find("M1 Pro") != std::string::npos, "");
    check("snapshot: swaps + zram carried", s->swaps.size() == 2 && s->zram.size() == 1, "");
    const auto* mhz = s->find("cpu.mhz");
    check("series: cpu.mhz is the mean of reporting cores (2x600 + 8x3000)/10 = 2520",
          mhz && near(mhz->back(), 2520, 1e-3), mhz ? num(mhz->back()) : "missing");
    check("series: mem.cache_pct (5.1 of 16 GB = 31.875%) and swap.used_pct (25%)",
          s->find("mem.cache_pct") && near(s->find("mem.cache_pct")->back(), 31.875, 1e-4) &&
          s->find("swap.used_pct") && near(s->find("swap.used_pct")->back(), 25, 1e-4), "");
}

// ── s005: process tree, filter, state words, users, fds ──────────────────────
#include "core/ProcView.hpp"

void run_procview_tests(const Check& check) {
    namespace pv = trails::core::procview;
    auto row = [](int pid, int ppid, const char* name, const char* cmd = "") {
        core::ProcRow r; r.pid = pid; r.ppid = ppid; r.name = name; r.cmdline = cmd; return r;
    };
    // 1 systemd -> 500 gnome-shell -> 900 firefox -> 901, 902 (content)
    // 2 kthreadd -> 3 kworker ; 777 orphan whose parent 600 exited.
    std::vector<core::ProcRow> procs = {
        row(1, 0, "systemd"), row(2, 0, "kthreadd"), row(3, 2, "kworker/0:0"),
        row(500, 1, "gnome-shell"), row(777, 600, "orphan"),
        row(900, 500, "firefox", "/usr/lib64/firefox/firefox --new-window"),
        row(901, 900, "Isolated Web Co"), row(902, 900, "Isolated Web Co")};
    auto ix = pv::build_index(procs);
    check("procview: roots = no parent in the table (incl. orphans)",
          ix.roots == std::vector<int>({1, 2, 777}), std::to_string(ix.roots.size()));
    check("procview: children by ppid, pid order",
          ix.children[900] == std::vector<int>({901, 902}) && ix.children[1] == std::vector<int>({500}) &&
          ix.children.count(901) == 0, "");
    {
        auto self = pv::build_index({row(5, 5, "loop")});
        check("procview: a self-parented row is a root, not its own child",
              self.roots == std::vector<int>({5}) && self.children.empty(), "");
    }

    const auto& ff = procs[5];
    check("procview: filter matches name / cmdline / user / pid, case-insensitive",
          pv::matches(ff, "scott", "FIRE") && pv::matches(ff, "scott", "new-window") &&
          pv::matches(ff, "scott", "Scott") && pv::matches(ff, "scott", "90") &&
          !pv::matches(ff, "scott", "chrome"), "");
    check("procview: empty / blank query matches all", pv::matches(ff, "", "") && pv::matches(ff, "", "   "), "");

    check("procview: state words",
          std::string(pv::state_name('R')) == "Running" && std::string(pv::state_name('D')) == "Disk wait" &&
          std::string(pv::state_name('Z')) == "Zombie" && std::string(pv::state_name('?')) == "?", "");
    check("procview: user name resolves (root is 0) and caches",
          pv::user_name(0) == "root" && &pv::user_name(0) == &pv::user_name(0), pv::user_name(0));
    check("procview: unknown uid falls back to the number", pv::user_name(4000000000u) == "4000000000",
          pv::user_name(4000000000u));

    {
        Fixture fx;
        fs::create_directories(fx.root / "proc/42/fd");
        fs::create_symlink("/home/scott/a.txt", fx.root / "proc/42/fd/0");
        fs::create_symlink("socket:[12345]", fx.root / "proc/42/fd/1");
        fs::create_symlink("socket:[12346]", fx.root / "proc/42/fd/2");
        fs::create_symlink("pipe:[999]", fx.root / "proc/42/fd/3");
        int files = -1, socks = -1;
        bool ok = probe::count_fds(fx.str(), 42, files, socks);
        check("fds: counts entries and sockets", ok && files == 4 && socks == 2,
              std::to_string(files) + "/" + std::to_string(socks));
        check("fds: unreadable -> false", !probe::count_fds(fx.str(), 43, files, socks), "");
    }
}

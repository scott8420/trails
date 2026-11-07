#pragma once
#include "core/probe/Probe.hpp"
#include "core/series/Ring.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core::Sampler -- owns the sampling thread, the interval, the history rings,
// and publishes an IMMUTABLE Snapshot each tick.
//
// The UI never locks and never waits: it calls snapshot() on its frame tick
// and gets a shared_ptr<const Snapshot> -- an atomic pointer swap. Whatever it
// holds stays valid and unchanging while it paints, however long that takes;
// the sampler has already moved on to building the next one.
//
// tick() is the whole sample, callable directly (the selftest drives it by
// hand against fixture trees with a fake clock); the thread just calls tick()
// every interval. Rates need two readings, so the FIRST snapshot has counters
// and no rates (rates_valid == false) and history starts with the second.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::core {

struct NetRate {
    std::string   name;
    double        rx_bps = 0, tx_bps = 0;      // bytes / second
    std::uint64_t rx_total = 0, tx_total = 0;  // bytes since boot
    bool          loopback = false;
};

struct DiskRate {
    std::string name;
    double      read_bps = 0, write_bps = 0;   // bytes / second
    double      busy_pct = 0;                  // share of the interval doing I/O
    bool        whole_disk = false;
    bool        in_total = false;
};

struct ProcRow {
    int           pid = 0, ppid = 0;
    std::uint32_t uid = 0;
    std::string   name;        // comm (15 chars max)
    std::string   cmdline;     // full argv, read once per process lifetime
    char          state = '?';
    double        cpu_pct = 0; // share of the WHOLE machine, 0..100 (x n_cpus for "one core = 100%")
    std::uint64_t rss_bytes = 0;
    int           threads = 0;
    std::int64_t  nice = 0;
    bool          kernel_thread = false;
};

struct Snapshot {
    std::uint64_t seq = 0;          // 1, 2, 3, ... per tick
    double        t = 0;            // seconds on the sampler's clock
    double        dt = 0;           // seconds since the previous tick (0 on the first)
    bool          rates_valid = false;

    int                 n_cpus = 0;
    probe::CpuPct       cpu;
    std::vector<double> core_busy;  // per core, 0..100
    probe::LoadAvg      load;
    probe::MemInfo      mem;

    std::vector<NetRate>  net;
    double                net_rx_bps = 0, net_tx_bps = 0;      // excludes loopback
    std::vector<DiskRate> disks;
    double                disk_read_bps = 0, disk_write_bps = 0; // real whole disks only

    std::vector<ProcRow> procs;     // pid order

    // History, oldest -> newest. Metric ids:
    //   cpu.total cpu.user cpu.system cpu.iowait cpu.core.<N>
    //   mem.used mem.used_pct swap.used load.1
    //   net.rx net.tx net.<if>.rx net.<if>.tx
    //   disk.read disk.write disk.<dev>.read disk.<dev>.write disk.<dev>.busy
    std::map<std::string, Ring<float>> series;

    std::string error;              // a source that failed this tick ("" = all read)

    const Ring<float>* find(const std::string& id) const {
        auto it = series.find(id);
        return it == series.end() ? nullptr : &it->second;
    }
};

struct SamplerConfig {
    std::string               root = "/";
    std::chrono::milliseconds interval{1000};
    std::size_t               history = 300;      // samples kept per metric (5 min at 1 Hz)
    long                      page_size = 0;      // 0 = ask the system (16 KiB on Asahi)
    std::function<double()>   clock;              // seconds; empty = steady_clock
};

class Sampler {
public:
    explicit Sampler(SamplerConfig cfg = {});
    ~Sampler();
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    void start();                 // idempotent; first tick happens immediately
    void stop();                  // joins; prompt even mid-interval
    bool running() const { return thread_.joinable(); }

    void set_interval(std::chrono::milliseconds ms);
    std::chrono::milliseconds interval() const {
        return std::chrono::milliseconds(interval_ms_.load());
    }

    // Latest published snapshot (never null after the first tick; null before).
    std::shared_ptr<const Snapshot> snapshot() const { return published_.load(); }

    // One synchronous sample: read, derive, publish, return what was published.
    std::shared_ptr<const Snapshot> tick();

private:
    struct ProcMemo {
        std::uint64_t starttime = 0, ticks = 0;
        std::string   cmdline;
        std::uint64_t seen = 0;   // tick seq last seen
    };
    struct SeriesSlot { Ring<float> ring; std::uint64_t updated = 0; };

    void loop(std::stop_token st);
    void push(const std::string& id, double v);
    double now() const;

    SamplerConfig cfg_;
    long          page_size_ = 4096;
    std::atomic<long long> interval_ms_;

    std::mutex tick_mu_;          // tick() from the thread and from a caller never overlap
    // --- previous raw readings (tick_mu_) ---
    std::uint64_t seq_ = 0;
    double        prev_t_ = 0;
    bool          have_prev_ = false;
    probe::CpuStat prev_cpu_;
    std::unordered_map<std::string, probe::NetIf>    prev_net_;
    std::unordered_map<std::string, probe::DiskStat> prev_disk_;
    std::unordered_map<int, ProcMemo>                procs_;
    std::map<std::string, SeriesSlot>                series_;

    std::atomic<std::shared_ptr<const Snapshot>> published_;

    std::mutex                  wait_mu_;
    std::condition_variable_any wait_cv_;
    bool                        wake_ = false;   // wait_mu_: interval changed
    std::jthread                thread_;
};

}  // namespace trails::core

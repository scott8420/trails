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

// One filesystem (s007): every mount of it folded together. A btrfs volume
// mounted as / (subvol=/root) and /home (subvol=/home) is ONE filesystem with
// two subvolumes -- one usage, one bar, not two bars that each claim the disk.
struct FsView {
    std::string              dev;        // "nvme0n1p6", "dm-0"
    std::string              source;     // "/dev/nvme0n1p6"
    std::string              fstype;
    std::vector<std::string> disks;      // the whole disks it lives on
    std::string              point;      // the primary mount point (shortest)
    std::vector<std::string> others;     // further mount points (bind mounts, non-btrfs)
    std::vector<std::pair<std::string, std::string>> subvols;   // btrfs: subvolume -> mount point
    probe::FsUsage           usage;
    bool                     read_only = false;
};
std::vector<FsView> group_filesystems(const std::string& root, const std::vector<probe::Mount>& mounts);

struct LinkView {                        // an interface's facts (s007), parallel to Snapshot::net
    probe::NetLink              link;
    bool                        has_signal = false;   // in proc/net/wireless
    double                      quality = 0, level_dbm = 0;
    std::vector<probe::IfAddr>  addrs;
};

struct GpuEngine {                       // s008: one DRM engine's share of the interval
    std::string driver, engine;          // "amdgpu", "gfx"
    double      busy_pct = 0;
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
    std::uint64_t starttime = 0;  // ticks since boot -- with pid, the process's identity (s006)
};

struct Snapshot {
    std::uint64_t seq = 0;          // 1, 2, 3, ... per tick
    double        t = 0;            // seconds on the sampler's clock
    double        dt = 0;           // seconds since the previous tick (0 on the first)
    bool          rates_valid = false;

    int                 n_cpus = 0;
    probe::CpuPct       cpu;
    std::vector<double> core_busy;  // per core, 0..100
    std::vector<int>    core_ids;   // the N of cpuN, parallel to core_busy
    std::vector<double> core_mhz;   // current frequency; 0 = not reported (VMs)
    std::vector<probe::Cluster> clusters;   // P / E groups (indices into core_*)
    std::string         cpu_model;  // "" if nothing says
    probe::LoadAvg      load;
    probe::MemInfo      mem;
    std::vector<probe::SwapDev> swaps;
    std::vector<probe::Zram>    zram;

    std::vector<NetRate>  net;
    double                net_rx_bps = 0, net_tx_bps = 0;      // excludes loopback
    std::vector<DiskRate> disks;
    double                disk_read_bps = 0, disk_write_bps = 0; // real whole disks only
    std::vector<probe::DiskInfo> disk_info;   // s007: the in_total whole disks, in diskstats order
    std::vector<FsView>          filesystems; // s007: storage filesystems, by primary mount point
    std::vector<LinkView>        links;       // s007: one per Snapshot::net entry, same order

    // s008 -- Sensors & Power, GPU.
    std::vector<probe::HwmonChip>   hwmon;       // every chip, sensorless ones too (the page hides them)
    std::vector<probe::PowerSupply> power;       // batteries, adapters, USB-C sources
    double                          temp_max = 0;   // hottest working temperature sensor, degC (0 = none)
    std::string                     temp_max_name;  // "Apple SMC · Heatsink 1"
    std::vector<probe::DrmCard>     drm;
    std::vector<GpuEngine>          gpu_engines;  // from fdinfo, rates (needs two ticks)
    double                          gpu_busy = -1;   // busiest engine / gpu_busy_percent; -1 = unknown

    std::vector<ProcRow> procs;     // pid order

    // History, oldest -> newest. Metric ids:
    //   cpu.total cpu.user cpu.system cpu.iowait cpu.core.<N>
    //   cpu.mhz (mean of cores that report)
    //   mem.used mem.used_pct mem.cache_pct swap.used swap.used_pct load.1
    //   net.rx net.tx net.<if>.rx net.<if>.tx
    //   disk.read disk.write disk.<dev>.read disk.<dev>.write disk.<dev>.busy
    //   procs.count (s014) temp.max gpu.busy battery.power hw.<hwmonN>.<key> (temp / fan / power sensors)
    std::map<std::string, Ring<float>> series;
    // s009: the values pushed THIS tick, by metric id. A metric whose source
    // went quiet keeps its ring (it ages out) but is absent here, so the
    // recorder never writes a stale value as if it were new.
    std::map<std::string, float> now;

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

    // s009: called on the sampling thread after each publish (the recorder
    // enqueues there). Keep it quick; {} clears it.
    using Listener = std::function<void(std::shared_ptr<const Snapshot>)>;
    void set_listener(Listener f) { std::lock_guard ll(listen_mu_); listener_ = std::move(f); }

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

    std::mutex tick_mu_;
    std::mutex listen_mu_;
    Listener   listener_;
    std::map<std::string, float> now_;          // tick() from the thread and from a caller never overlap
    // --- previous raw readings (tick_mu_) ---
    std::uint64_t seq_ = 0;
    double        prev_t_ = 0;
    bool          have_prev_ = false;
    probe::CpuStat prev_cpu_;
    std::unordered_map<std::string, probe::NetIf>    prev_net_;
    std::unordered_map<std::string, probe::DiskStat> prev_disk_;
    std::map<std::string, std::uint64_t>             prev_gpu_;   // "driver/engine" -> ns (s008)
    std::unordered_map<int, ProcMemo>                procs_;
    std::map<std::string, SeriesSlot>                series_;
    std::string                                      cpu_model_;
    bool                                             model_read_ = false;

    std::atomic<std::shared_ptr<const Snapshot>> published_;

    std::mutex                  wait_mu_;
    std::condition_variable_any wait_cv_;
    bool                        wake_ = false;   // wait_mu_: interval changed
    std::jthread                thread_;
};

}  // namespace trails::core

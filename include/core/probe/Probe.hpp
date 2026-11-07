#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core::probe -- the readers. Each parses ONE kernel source into a plain struct
// of raw counters. No rates, no history, no threads: that is the Sampler's job.
//
// Every reader takes a ROOT path ("/" in the app). The selftest writes fixture
// trees -- <tmp>/proc/stat, <tmp>/sys/block/... -- and points a reader at
// them, so each parser is checked against exact, known numbers with no live
// system involved. Missing / unreadable source -> the reader returns false and
// leaves `out` cleared; nothing throws.
// ─────────────────────────────────────────────────────────────────────────────
namespace trails::core::probe {

// root + "proc/stat" with exactly one slash between.
std::string at(const std::string& root, std::string_view rel);
// Whole file into `out` (procfs reports size 0, so read until EOF).
bool slurp(const std::string& path, std::string& out);

// Monotonic counters can reset (hotplug, driver reload, pid reuse): never let a
// backwards step become a huge unsigned delta.
inline std::uint64_t delta(std::uint64_t before, std::uint64_t after) {
    return after >= before ? after - before : 0;
}

// ── /proc/stat ───────────────────────────────────────────────────────────────
// Jiffies (USER_HZ). guest / guest_nice are already folded into user / nice by
// the kernel, so they are not summed again.
struct CpuTimes {
    std::uint64_t user = 0, nice = 0, system = 0, idle = 0,
                  iowait = 0, irq = 0, softirq = 0, steal = 0;
    std::uint64_t total() const {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }
    std::uint64_t idle_all() const { return idle + iowait; }
};
struct CpuStat {
    CpuTimes              all;
    std::vector<CpuTimes> cores;   // in /proc/stat order (offline cores are absent)
    std::vector<int>      core_ids;// the N of each "cpuN" line, parallel to cores
    std::uint64_t         ctxt = 0;
    std::uint64_t         procs_running = 0;
};
bool read_stat(const std::string& root, CpuStat& out);

// Percentages of the interval between two readings (0..100).
struct CpuPct { double busy = 0, user = 0, system = 0, iowait = 0; };
CpuPct cpu_pct(const CpuTimes& before, const CpuTimes& after);

// ── cpufreq + model + clusters (s004) ────────────────────────────────────────
// sys/devices/system/cpu/cpuN/cpufreq/{scaling_cur_freq,cpuinfo_max_freq}, kHz.
// Absent in VMs and some ARM boards: the entry stays 0 and the UI says so.
struct CpuFreq { int cpu = 0; std::uint32_t cur_khz = 0, max_khz = 0; };
std::vector<CpuFreq> read_cpufreq(const std::string& root, const std::vector<int>& cpus);
// "model name" from proc/cpuinfo (x86); else proc/device-tree/model (Asahi:
// "Apple MacBook Pro (14-inch, M1 Pro, 2021)"); else "".
std::string read_cpu_model(const std::string& root);
// Cores grouped by max frequency. Two groups -> "Efficiency" (slower) and
// "Performance"; one -> "All cores"; more -> "Cluster k". `cores` holds
// INDICES into the freq vector (= into CpuStat::cores). No freq data -> one
// group with max_khz 0.
struct Cluster { std::string name; std::uint32_t max_khz = 0; std::vector<int> cores; };
std::vector<Cluster> clusters_from(const std::vector<CpuFreq>& freq);

// ── /proc/loadavg ────────────────────────────────────────────────────────────
struct LoadAvg { double one = 0, five = 0, fifteen = 0; int running = 0, total = 0; };
bool read_loadavg(const std::string& root, LoadAvg& out);

// ── /proc/meminfo (bytes) ────────────────────────────────────────────────────
struct MemInfo {
    std::uint64_t total = 0, free = 0, available = 0, buffers = 0, cached = 0,
                  sreclaimable = 0, shmem = 0, swap_total = 0, swap_free = 0;
    // "Used" as free(1) and btop mean it now: what is not available.
    std::uint64_t used()      const { return total > available ? total - available : 0; }
    std::uint64_t swap_used() const { return swap_total > swap_free ? swap_total - swap_free : 0; }
};
bool read_meminfo(const std::string& root, MemInfo& out);

// ── proc/swaps + sys/block/zram* (s004) ───────────────────────────────────────
struct SwapDev {
    std::string   name, type;       // "/dev/zram0", "partition"
    std::uint64_t size = 0, used = 0;   // bytes
    int           priority = 0;
};
bool read_swaps(const std::string& root, std::vector<SwapDev>& out);
struct Zram {
    std::string   name;             // "zram0"
    std::uint64_t disksize = 0;     // configured capacity
    std::uint64_t orig = 0;         // data stored, uncompressed
    std::uint64_t compr = 0;        // ... compressed
    std::uint64_t mem_used = 0;     // RAM it actually costs
};
std::vector<Zram> read_zram(const std::string& root);   // only devices with a disksize

// ── /proc/net/dev ────────────────────────────────────────────────────────────
struct NetIf {
    std::string   name;
    std::uint64_t rx_bytes = 0, rx_packets = 0, rx_errs = 0,
                  tx_bytes = 0, tx_packets = 0, tx_errs = 0;
};
bool read_netdev(const std::string& root, std::vector<NetIf>& out);

// ── /proc/diskstats ──────────────────────────────────────────────────────────
// Sectors are always 512 bytes here regardless of the device's block size.
struct DiskStat {
    std::string   name;
    unsigned      major = 0, minor = 0;
    std::uint64_t reads = 0, sectors_read = 0, writes = 0, sectors_written = 0;
    std::uint64_t io_ms = 0;          // ms spent doing I/O -> busy %
    bool          whole_disk = false; // root/sys/block/<name> exists (not a partition)
};
bool read_diskstats(const std::string& root, std::vector<DiskStat>& out);
// Whole disks that would double-count in a machine total (loop, ram, dm-, md,
// zram sit on top of / beside real devices).
bool disk_counts_in_total(const DiskStat& d);

// ── /proc/<pid>/stat ─────────────────────────────────────────────────────────
struct ProcStat {
    int           pid = 0, ppid = 0;
    std::string   comm;               // kernel's 15-char name; may hold spaces / parens
    char          state = '?';
    std::uint64_t utime = 0, stime = 0;   // jiffies
    std::int64_t  nice = 0;
    int           threads = 0;
    std::uint64_t starttime = 0;      // jiffies after boot; pid + starttime = identity
    std::uint64_t rss_bytes = 0;
    std::uint32_t uid = 0;            // owner of /proc/<pid>
    bool          kernel_thread = false;
    std::uint64_t ticks() const { return utime + stime; }
};
// page_size: rss is in pages -- 16 KiB on Asahi, 4 KiB on most x86, so the
// caller passes it rather than the parser assuming.
bool parse_proc_stat(std::string_view text, long page_size, ProcStat& out);
bool read_proc(const std::string& root, int pid, long page_size, ProcStat& out);
std::vector<int> list_pids(const std::string& root);
// NUL-separated argv joined with spaces; empty for kernel threads / zombies.
std::string read_cmdline(const std::string& root, int pid);

}  // namespace trails::core::probe

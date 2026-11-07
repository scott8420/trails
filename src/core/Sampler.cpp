#include "core/Sampler.hpp"

#include <algorithm>
#include <unistd.h>

namespace trails::core {

Sampler::Sampler(SamplerConfig cfg)
    : cfg_(std::move(cfg)), interval_ms_(cfg_.interval.count()) {
    page_size_ = cfg_.page_size > 0 ? cfg_.page_size : ::sysconf(_SC_PAGESIZE);
    if (page_size_ <= 0) page_size_ = 4096;
    if (cfg_.history == 0) cfg_.history = 1;
    if (interval_ms_.load() < 50) interval_ms_ = 50;
}

Sampler::~Sampler() { stop(); }

double Sampler::now() const {
    if (cfg_.clock) return cfg_.clock();
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void Sampler::start() {
    if (thread_.joinable()) return;
    thread_ = std::jthread([this](std::stop_token st) { loop(st); });
}

void Sampler::stop() {
    if (!thread_.joinable()) return;
    thread_.request_stop();
    wait_cv_.notify_all();
    thread_.join();
    thread_ = {};
}

void Sampler::set_interval(std::chrono::milliseconds ms) {
    interval_ms_ = std::max<long long>(50, ms.count());
    { std::lock_guard lk(wait_mu_); wake_ = true; }
    wait_cv_.notify_all();   // the wait re-reads the interval
}

void Sampler::loop(std::stop_token st) {
    while (!st.stop_requested()) {
        try {
            tick();
        } catch (...) {
            // A reader must never take the thread down; the next tick retries.
        }
        // Wait out the interval. set_interval() wakes us and the remaining time
        // is recomputed, so a shorter interval ends the wait early and a longer
        // one extends it. stop() ends it at once.
        std::unique_lock lk(wait_mu_);
        const auto start = std::chrono::steady_clock::now();
        for (;;) {
            const auto left = interval() - std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now() - start);
            if (left <= std::chrono::milliseconds::zero()) break;
            wait_cv_.wait_for(lk, st, left, [&] { return wake_; });
            if (st.stop_requested()) return;
            wake_ = false;
        }
    }
}

void Sampler::push(const std::string& id, double v) {
    auto it = series_.find(id);
    if (it == series_.end())
        it = series_.emplace(id, SeriesSlot{Ring<float>(cfg_.history), 0}).first;
    it->second.ring.push(static_cast<float>(v));
    it->second.updated = seq_;
}

std::shared_ptr<const Snapshot> Sampler::tick() {
    std::lock_guard lk(tick_mu_);
    auto snap = std::make_shared<Snapshot>();
    Snapshot& s = *snap;
    s.seq = ++seq_;
    s.t = now();
    s.dt = have_prev_ ? s.t - prev_t_ : 0.0;
    s.rates_valid = have_prev_ && s.dt > 0;
    const double dt = s.rates_valid ? s.dt : 0.0;
    std::string err;
    auto fail = [&](const char* what) { if (!err.empty()) err += ", "; err += what; };

    // ── CPU ───────────────────────────────────────────────────────────────
    probe::CpuStat cpu;
    if (!probe::read_stat(cfg_.root, cpu)) fail("proc/stat");
    s.n_cpus = static_cast<int>(cpu.cores.size());
    const bool cpu_ok = have_prev_ && prev_cpu_.all.total() > 0 && cpu.all.total() > 0;
    if (cpu_ok) {
        s.cpu = probe::cpu_pct(prev_cpu_.all, cpu.all);
        s.core_busy.resize(cpu.cores.size(), 0.0);
        for (std::size_t i = 0; i < cpu.cores.size() && i < prev_cpu_.cores.size(); ++i)
            s.core_busy[i] = probe::cpu_pct(prev_cpu_.cores[i], cpu.cores[i]).busy;
    } else {
        s.core_busy.assign(cpu.cores.size(), 0.0);
    }
    s.core_ids = cpu.core_ids;
    {
        const auto freq = probe::read_cpufreq(cfg_.root, cpu.core_ids);
        s.core_mhz.reserve(freq.size());
        for (const auto& f : freq) s.core_mhz.push_back(f.cur_khz / 1000.0);
        s.clusters = probe::clusters_from(freq);
    }
    if (!model_read_) { cpu_model_ = probe::read_cpu_model(cfg_.root); model_read_ = true; }
    s.cpu_model = cpu_model_;
    const std::uint64_t cpu_total_d =
        cpu_ok ? probe::delta(prev_cpu_.all.total(), cpu.all.total()) : 0;

    // ── Load / memory ─────────────────────────────────────────────────────
    if (!probe::read_loadavg(cfg_.root, s.load)) fail("proc/loadavg");
    if (!probe::read_meminfo(cfg_.root, s.mem)) fail("proc/meminfo");
    probe::read_swaps(cfg_.root, s.swaps);   // absent = no swap: not an error
    s.zram = probe::read_zram(cfg_.root);

    // ── Network ───────────────────────────────────────────────────────────
    std::vector<probe::NetIf> nets;
    if (!probe::read_netdev(cfg_.root, nets)) fail("proc/net/dev");
    std::unordered_map<std::string, probe::NetIf> next_net;
    for (auto& n : nets) {
        NetRate r;
        r.name = n.name;
        r.rx_total = n.rx_bytes;
        r.tx_total = n.tx_bytes;
        r.loopback = (n.name == "lo");
        if (dt > 0) {
            if (auto p = prev_net_.find(n.name); p != prev_net_.end()) {
                r.rx_bps = static_cast<double>(probe::delta(p->second.rx_bytes, n.rx_bytes)) / dt;
                r.tx_bps = static_cast<double>(probe::delta(p->second.tx_bytes, n.tx_bytes)) / dt;
            }
        }
        if (!r.loopback) { s.net_rx_bps += r.rx_bps; s.net_tx_bps += r.tx_bps; }
        s.net.push_back(std::move(r));
        next_net.emplace(n.name, std::move(n));
    }

    // ── Disks ─────────────────────────────────────────────────────────────
    std::vector<probe::DiskStat> disks;
    if (!probe::read_diskstats(cfg_.root, disks)) fail("proc/diskstats");
    std::unordered_map<std::string, probe::DiskStat> next_disk;
    for (auto& d : disks) {
        DiskRate r;
        r.name = d.name;
        r.whole_disk = d.whole_disk;
        r.in_total = probe::disk_counts_in_total(d);
        if (dt > 0) {
            if (auto p = prev_disk_.find(d.name); p != prev_disk_.end()) {
                r.read_bps  = 512.0 * static_cast<double>(probe::delta(p->second.sectors_read, d.sectors_read)) / dt;
                r.write_bps = 512.0 * static_cast<double>(probe::delta(p->second.sectors_written, d.sectors_written)) / dt;
                r.busy_pct  = std::min(100.0, 100.0 * static_cast<double>(probe::delta(p->second.io_ms, d.io_ms)) / (dt * 1000.0));
            }
        }
        if (r.in_total) { s.disk_read_bps += r.read_bps; s.disk_write_bps += r.write_bps; }
        s.disks.push_back(std::move(r));
        next_disk.emplace(d.name, std::move(d));
    }

    // ── Storage + link facts (s007) ───────────────────────────────────────
    for (const auto& d : s.disks) {
        if (!d.in_total) continue;
        probe::DiskInfo info;
        if (probe::read_disk_info(cfg_.root, d.name, info)) s.disk_info.push_back(std::move(info));
    }
    {
        std::vector<probe::Mount> mounts;
        // No mountinfo (a stripped container, an old fixture): no filesystems, not an error.
        if (probe::read_mounts(cfg_.root, mounts)) s.filesystems = group_filesystems(cfg_.root, mounts);
    }
    {
        std::vector<probe::Wireless> wl;
        probe::read_wireless(cfg_.root, wl);   // absent without Wi-Fi: not an error
        std::vector<probe::IfAddr> addrs;
        if (cfg_.root.empty() || cfg_.root == "/") addrs = probe::read_addrs();
        for (const auto& n : s.net) {
            LinkView lv;
            probe::read_netlink(cfg_.root, n.name, lv.link);
            lv.link.name = n.name;
            for (const auto& w : wl)
                if (w.name == n.name) { lv.has_signal = true; lv.quality = w.link; lv.level_dbm = w.level_dbm; }
            for (const auto& a : addrs)
                if (a.name == n.name) lv.addrs.push_back(a);
            s.links.push_back(std::move(lv));
        }
    }

    // ── Processes ─────────────────────────────────────────────────────────
    std::vector<int> pids = probe::list_pids(cfg_.root);
    std::sort(pids.begin(), pids.end());
    s.procs.reserve(pids.size());
    for (int pid : pids) {
        probe::ProcStat ps;
        if (!probe::read_proc(cfg_.root, pid, page_size_, ps)) continue;   // exited mid-walk
        ProcRow row;
        row.pid = ps.pid; row.ppid = ps.ppid; row.uid = ps.uid;
        row.name = ps.comm; row.state = ps.state;
        row.rss_bytes = ps.rss_bytes; row.threads = ps.threads;
        row.nice = ps.nice; row.kernel_thread = ps.kernel_thread; row.starttime = ps.starttime;

        auto it = procs_.find(pid);
        const bool same = it != procs_.end() && it->second.starttime == ps.starttime;
        if (same) {
            if (cpu_total_d > 0)
                row.cpu_pct = 100.0 * static_cast<double>(probe::delta(it->second.ticks, ps.ticks()))
                              / static_cast<double>(cpu_total_d);
            if (row.cpu_pct > 100.0) row.cpu_pct = 100.0;
        } else {
            // New process, or a recycled pid: fresh identity, cmdline read once.
            ProcMemo m;
            m.starttime = ps.starttime;
            if (!ps.kernel_thread) m.cmdline = probe::read_cmdline(cfg_.root, pid);
            it = procs_.insert_or_assign(pid, std::move(m)).first;
        }
        it->second.ticks = ps.ticks();
        it->second.seen = s.seq;
        row.cmdline = it->second.cmdline;
        s.procs.push_back(std::move(row));
    }
    std::erase_if(procs_, [&](const auto& kv) { return kv.second.seen != s.seq; });

    // ── History (from the second tick: the first has no rates) ────────────
    if (s.rates_valid) {
        push("cpu.total", s.cpu.busy);
        push("cpu.user", s.cpu.user);
        push("cpu.system", s.cpu.system);
        push("cpu.iowait", s.cpu.iowait);
        for (std::size_t i = 0; i < s.core_busy.size(); ++i)
            push("cpu.core." + std::to_string(i), s.core_busy[i]);
        push("mem.used", static_cast<double>(s.mem.used()));
        push("mem.used_pct", s.mem.total ? 100.0 * static_cast<double>(s.mem.used()) /
                                               static_cast<double>(s.mem.total) : 0.0);
        push("mem.cache_pct", s.mem.total ? 100.0 * static_cast<double>(s.mem.cached + s.mem.buffers) /
                                                static_cast<double>(s.mem.total) : 0.0);
        push("swap.used", static_cast<double>(s.mem.swap_used()));
        push("swap.used_pct", s.mem.swap_total ? 100.0 * static_cast<double>(s.mem.swap_used()) /
                                                     static_cast<double>(s.mem.swap_total) : 0.0);
        {
            double sum = 0; int n = 0;
            for (double m : s.core_mhz) if (m > 0) { sum += m; ++n; }
            if (n) push("cpu.mhz", sum / n);
        }
        push("load.1", s.load.one);
        push("net.rx", s.net_rx_bps);
        push("net.tx", s.net_tx_bps);
        for (const auto& n : s.net) {
            push("net." + n.name + ".rx", n.rx_bps);
            push("net." + n.name + ".tx", n.tx_bps);
        }
        push("disk.read", s.disk_read_bps);
        push("disk.write", s.disk_write_bps);
        for (const auto& d : s.disks) {
            if (!d.whole_disk) continue;   // partitions: on demand later, not 300 rings each
            push("disk." + d.name + ".read", d.read_bps);
            push("disk." + d.name + ".write", d.write_bps);
            push("disk." + d.name + ".busy", d.busy_pct);
        }
        // A metric whose source vanished (unplugged disk, a container's veth)
        // ages out once its whole window is stale.
        std::erase_if(series_, [&](const auto& kv) {
            return s.seq - kv.second.updated >= cfg_.history;
        });
    }
    for (const auto& [id, slot] : series_) s.series.emplace(id, slot.ring);

    s.error = std::move(err);
    prev_cpu_ = std::move(cpu);
    prev_net_ = std::move(next_net);
    prev_disk_ = std::move(next_disk);
    prev_t_ = s.t;
    have_prev_ = true;

    std::shared_ptr<const Snapshot> out = std::move(snap);
    published_.store(out);
    return out;
}


std::vector<FsView> group_filesystems(const std::string& root, const std::vector<probe::Mount>& mounts) {
    std::vector<FsView> out;
    std::vector<std::pair<unsigned, unsigned>> keys;   // parallel to out: major:minor
    for (const auto& m : mounts) {
        if (!probe::is_storage_mount(m)) continue;
        const std::pair<unsigned, unsigned> key{m.major, m.minor};
        std::size_t i = 0;
        while (i < keys.size() && keys[i] != key) ++i;
        if (i == keys.size()) {
            keys.push_back(key);
            FsView f;
            f.source = m.source;
            f.dev    = probe::dev_of_source(root, m.source);
            f.fstype = m.fstype;
            f.disks  = probe::disks_under(root, f.dev);
            f.point  = m.point;
            f.read_only = m.read_only;
            out.push_back(std::move(f));
        } else if (m.point.size() < out[i].point.size()) {
            out[i].others.push_back(out[i].point);
            out[i].point = m.point;
            out[i].read_only = m.read_only;
        } else {
            out[i].others.push_back(m.point);
        }
        if (m.fstype == "btrfs")
            out[i].subvols.emplace_back(m.subvol.empty() ? m.root : m.subvol, m.point);
    }
    for (auto& f : out) {
        if (f.fstype == "btrfs") {   // the subvolume list says it better than "others"
            f.others.clear();
            std::sort(f.subvols.begin(), f.subvols.end(),
                      [](const auto& a, const auto& b) { return a.second < b.second; });
        }
        std::sort(f.others.begin(), f.others.end());
        probe::fs_usage(root, f.point, f.usage);
    }
    std::sort(out.begin(), out.end(), [](const FsView& a, const FsView& b) { return a.point < b.point; });
    return out;
}

}  // namespace trails::core

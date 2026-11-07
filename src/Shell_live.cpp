#include "Shell.hpp"
#include "Log.hpp"
#include "core/Format.hpp"

#include <glibmm/main.h>

#include <cstdio>
#include <cstdlib>

// Shell_live.cpp -- LIVE. The sampler -> screen path (s003).
//
// The Sampler runs on its own thread and publishes an immutable Snapshot once
// a second. The UI polls snapshot() on a short timer (an atomic pointer load,
// no lock) and paints only when the sequence number moved -- so the main loop
// never waits on /proc, and a slow paint can never stall sampling.

namespace trails {

namespace {
constexpr unsigned kFrameMs = 250;   // poll 4x per sample: a new snapshot shows within 1/4 s

std::vector<float> hist(const core::Snapshot& s, const char* id) {
    const auto* r = s.find(id);
    return r ? r->to_vector() : std::vector<float>{};
}
std::size_t cap(const core::Snapshot& s, const char* id) {
    const auto* r = s.find(id);
    return r ? r->capacity() : 300;
}
std::string two(double v) {
    char b[16];
    std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}
}  // namespace

void Shell::start_sampling() {  // live: Sampler + frame tick up
    if (m_sampler) return;
    // TRAILS_ROOT (dev aid): sample a fixture tree instead of "/" -- e.g. an
    // Asahi-shaped /proc + /sys to see P / E clusters on an x86 box.
    core::SamplerConfig cfg;   // "/", 1 s, 300 samples
    if (const char* r = std::getenv("TRAILS_ROOT"); r && *r) cfg.root = r;
    m_sampler = std::make_unique<core::Sampler>(cfg);
    m_proc_page.set_root(cfg.root);
    // s009: every published tick goes to the recorder (it ignores them
    // unless recording) -- on the sampler thread, so recording never
    // depends on which tab is showing or whether the window is drawn.
    m_sampler->set_listener([this](std::shared_ptr<const core::Snapshot> s) { m_recorder.push(std::move(s)); });
    m_sampler->start();
    m_frame = Glib::signal_timeout().connect(sigc::mem_fun(*this, &Shell::on_frame), kFrameMs);
    if (auto lg = log::get(log::Area::Shell))
        lg->info("sampling {} every {} ms", cfg.root, m_sampler->interval().count());
}

void Shell::stop_sampling() {  // live: both down (idempotent)
    m_frame.disconnect();
    if (m_sampler) {
        m_sampler->stop();
        m_sampler.reset();
        if (auto lg = log::get(log::Area::Shell)) lg->info("sampling stopped");
    }
}

bool Shell::on_frame() {  // live: new snapshot? paint it
    if (!m_sampler) return false;
    auto snap = m_sampler->snapshot();
    if (!snap || snap->seq == m_painted_seq) return true;
    m_painted_seq = snap->seq;
    if (!snap->error.empty())
        if (auto lg = log::get(log::Area::Io)) lg->warn("sample {}: unread {}", snap->seq, snap->error);
    // Only the visible page is painted; the others catch up when shown.
    const std::string tab = m_stack.get_visible_child_name();
    if      (tab == "overview") paint_overview(*snap);
    else if (tab == "cpu")      m_cpu_page.paint(*snap);
    else if (tab == "memory")   m_mem_page.paint(*snap);
    else if (tab == "processes") { m_proc_page.paint(*snap); sync_acts(); }
    else if (tab == "disks")    m_disk_page.paint(*snap);
    else if (tab == "network")  m_net_page.paint(*snap);
    else if (tab == "sensors")  m_sensor_page.paint(*snap);
    else if (tab == "gpu")      m_gpu_page.paint(*snap);
    paint_record();   // s009: elapsed time, or why a recording stopped
    return true;
}

void Shell::paint_overview(const core::Snapshot& s) {  // live: tiles from a snapshot
    namespace fmt = core::fmt;
    const bool r = s.rates_valid;

    // CPU
    m_tile_cpu.set_value(r ? fmt::pct(s.cpu.busy) : "--");
    m_tile_cpu.set_detail(std::to_string(s.n_cpus) + " cores · load " + two(s.load.one) +
                          "  " + two(s.load.five) + "  " + two(s.load.fifteen));
    m_tile_cpu.graph().set_series(hist(s, "cpu.total"), {}, cap(s, "cpu.total"));

    // Memory
    m_tile_mem.set_value(fmt::bytes(s.mem.used()));
    std::string md = "of " + fmt::bytes(s.mem.total) + " · cache " +
                     fmt::bytes(s.mem.cached + s.mem.buffers);
    if (s.mem.swap_total) md += " · swap " + fmt::bytes(s.mem.swap_used());
    m_tile_mem.set_detail(md);
    m_tile_mem.graph().set_series(hist(s, "mem.used_pct"), {}, cap(s, "mem.used_pct"));

    // Disks: read filled, write as the line.
    m_tile_disk.set_value(r ? fmt::rate(s.disk_read_bps + s.disk_write_bps) : "--");
    m_tile_disk.set_detail("read " + fmt::rate(s.disk_read_bps) +
                           " · write " + fmt::rate(s.disk_write_bps));
    m_tile_disk.graph().set_series(hist(s, "disk.read"), hist(s, "disk.write"), cap(s, "disk.read"));

    // Network: down filled, up as the line.
    m_tile_net.set_value(r ? fmt::rate(s.net_rx_bps + s.net_tx_bps) : "--");
    m_tile_net.set_detail("down " + fmt::rate(s.net_rx_bps) +
                          " · up " + fmt::rate(s.net_tx_bps));
    m_tile_net.graph().set_series(hist(s, "net.rx"), hist(s, "net.tx"), cap(s, "net.rx"));

    // Processes: count, threads, the busiest.
    long threads = 0;
    const core::ProcRow* top = nullptr;
    for (const auto& p : s.procs) {
        threads += p.threads;
        if (!top || p.cpu_pct > top->cpu_pct) top = &p;
    }
    m_tile_procs.set_value(std::to_string(s.procs.size()));
    std::string pd = std::to_string(threads) + " threads";
    if (r && top) pd += " · busiest " + top->name + " " + fmt::pct(top->cpu_pct);
    m_tile_procs.set_detail(pd);
}

}  // namespace trails

#pragma once
// core::Recorder -- the record button's engine (s009). GTK-free.
//
// One .trail file (SQLite) per recording, as ARCHITECTURE decided:
//
//   meta(key, value)                        host, kernel, cpu, interval, start, end ...
//   metric(id, name)                        "cpu.total", "net.wlan0.rx", ...
//   sample(t, metric, value)                every series value measured that tick
//   proc_sample(t, pid, name, cpu, rss, io) the top processes by CPU and by memory
//   marker(id, t, kind, text)               start, stop, and the user's marks
//   VIEW samples(t, name, value)            the friendly join
//
// t is UNIX time in seconds (REAL) -- a recording lines up with the clock,
// the journal and anything else from that afternoon.
//
// Threading: push() is called on the SAMPLER thread (Sampler::set_listener)
// and only enqueues; a writer thread owns the database and writes each tick
// in one transaction. The UI never touches SQLite. WAL mode while open; the
// -wal / -shm files fold back in at close, leaving one file to keep or send.
//
// Marks are records the undo journal can see: get_mark / put_mark keep an
// in-memory mirror (so get() never waits on the disk) and queue the write.
// A mark id carries its recording's serial, so an undo step from an earlier
// recording finds nothing and does nothing.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Sampler.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct sqlite3;

namespace trails::core {

struct Marker {
    double      t = 0;       // unix seconds
    std::string kind;        // "start", "stop", "mark"
    std::string text;
    bool operator==(const Marker&) const = default;
};

class Recorder {
public:
    struct Meta {
        std::string   host, kernel, cpu_model;
        int           n_cpus = 0, interval_ms = 1000;
        std::uint64_t mem_total = 0;
    };
    static constexpr int kTopN = 15;   // per tick: top 15 by CPU, union top 15 by memory

    Recorder();
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Opens (creates) `path`, writes the schema, meta and a "start" marker,
    // starts the writer. "" on success, else why. Refused while recording.
    std::string start(const std::string& path, const Meta& meta);
    // Writes the "stop" marker and meta end, drains the queue, closes. Idempotent.
    void stop();

    bool recording() const { return m_on.load(); }
    // Any thread. Ignored when not recording.
    void push(std::shared_ptr<const Snapshot> s);

    std::uint64_t ticks()   const { return m_ticks.load(); }   // ticks written
    double        started() const { return m_t0; }             // unix seconds
    std::string   path()    const;
    // A writer failure (disk full, the file vanished) stops the recording
    // itself; the reason stays here until the next start().
    std::string   error()   const;

    // ── marks: the journal's store side ────────────────────────────────────
    std::optional<Marker> get_mark(std::int64_t id) const;
    void                  put_mark(std::int64_t id, const std::optional<Marker>& m);
    std::int64_t          fresh_mark_id();   // 0 when not recording

    // $XDG_DATA_HOME/trails/recordings (else ~/.local/share/...)
    static std::string default_dir();
    // "2026-11-07_16-40-12.trail" in local time
    static std::string default_name(std::int64_t unix_seconds);
    static double      wall_now();
    // A ready-to-paste shell command that reads `path` (single-quoted, so
    // spaces and quotes in the path survive): the samples with local times.
    static std::string query_command(const std::string& path, int limit = 50);

private:
    struct Job {
        enum class Kind { Tick, Mark, Stop } kind = Kind::Tick;
        double                          t = 0;
        std::shared_ptr<const Snapshot> snap;
        std::int64_t                    mark_id = 0;
        std::optional<Marker>           mark;
    };
    void run();
    bool write_tick(const Job& j);
    bool write_mark(std::int64_t id, const std::optional<Marker>& m);
    bool exec(const char* sql);
    void fail(const std::string& why);
    std::int64_t metric_id(const std::string& name);

    sqlite3*                    m_db = nullptr;
    std::thread                 m_thread;
    mutable std::mutex          m_mu;
    std::condition_variable     m_cv;
    std::deque<Job>             m_q;
    std::atomic<bool>           m_on{false};
    std::atomic<std::uint64_t>  m_ticks{0};
    double                      m_t0 = 0;
    std::string                 m_path, m_err;
    std::map<std::string, std::int64_t> m_metric;   // writer thread only

    // marks mirror (UI thread reads / writes; the queue carries them on)
    std::map<std::int64_t, Marker> m_marks;
    std::int64_t                   m_serial = 0;   // recordings started this run
    std::int64_t                   m_next_mark = 1;
};

}  // namespace trails::core

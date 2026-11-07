#pragma once
// core::Trail -- reading a .trail back (s010). GTK-free.
//
// The Trails tab opens one recording (the newest by default) and asks it:
//   * what's in it      -- meta, the time span, the metric names
//   * a metric's line   -- series(): downsampled to the pixels on screen by
//                          taking each bucket's MAXIMUM, so a one-second spike
//                          in a four-hour recording still shows (a bloodhound
//                          that averages loses the scent)
//   * who was busy      -- procs_at(t): the recorded top processes at the
//                          tick nearest t
//   * the marks         -- list, and get / put by id for the undo journal
//                          (add, rename, delete are steps -- core/AppStore)
//   * a spreadsheet     -- export_csv(): one row per tick, one column per
//                          metric, over a time range
//
// A file being recorded right now opens fine (WAL lets a reader in); reload()
// picks up what was written since.
// ─────────────────────────────────────────────────────────────────────────────
#include "core/Recorder.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

namespace trails::core {

struct TrailPoint { double t = 0; float v = 0; };
struct TrailProc  { int pid = 0; std::string name; double cpu = 0; std::uint64_t rss = 0; };
struct TrailMark  { std::int64_t id = 0; Marker m; };

class Trail {
public:
    Trail() = default;
    ~Trail();
    Trail(const Trail&) = delete;
    Trail& operator=(const Trail&) = delete;

    std::string open(const std::string& path);   // "" on success, else why
    void        close();
    bool        is_open() const { return m_db != nullptr; }
    void        reload();                        // span, metrics, marks again (a live file grew)
    const std::string& path() const { return m_path; }
    // Changes every open() -- the journal's records name the file they belong to.
    std::int64_t serial() const { return m_serial; }

    const std::map<std::string, std::string>& meta() const { return m_meta; }
    double t0() const { return m_t0; }
    double t1() const { return m_t1; }
    std::size_t ticks() const { return m_ticks; }
    const std::vector<std::string>& metrics() const { return m_names; }   // sorted

    // Up to `buckets` points over [from, to]: each the max of its bucket, at
    // the time of that max. Fewer points than buckets: every sample, as is.
    std::vector<TrailPoint> series(const std::string& name, double from, double to, int buckets) const;
    // The value at the tick nearest t (nullopt: metric absent then).
    std::optional<float> value_at(const std::string& name, double t) const;
    // The recorded top processes at the tick nearest t, busiest CPU first.
    std::vector<TrailProc> procs_at(double t, double* tick = nullptr) const;
    // Nearest recorded tick times around t (for stepping with the arrows).
    double tick_before(double t) const;
    double tick_after(double t) const;

    std::vector<TrailMark> marks() const;    // by time
    std::optional<Marker>  get_mark(std::int64_t id) const;
    // Insert / replace (value) or delete (nullopt) a marker row by id.
    // "" on success. A new user mark takes fresh_mark_id().
    std::string            put_mark(std::int64_t id, const std::optional<Marker>& m);
    std::int64_t           fresh_mark_id() const;

    // time_local, unix, then every metric (sorted) -- one row per tick in
    // [from, to]. Empty cell = not measured that tick. Returns rows written,
    // -1 on failure (and `err`).
    long export_csv(const std::string& out, double from, double to, std::string* err = nullptr) const;

    // The .trail files in `dir`, newest first (by name = by start time).
    static std::vector<std::string> list(const std::string& dir);

private:
    sqlite3*                           m_db = nullptr;
    std::string                        m_path;
    std::int64_t                       m_serial = 0;
    std::map<std::string, std::string> m_meta;
    std::map<std::string, std::int64_t> m_ids;   // metric name -> id
    std::vector<std::string>           m_names;
    double                             m_t0 = 0, m_t1 = 0;
    std::size_t                        m_ticks = 0;
};

}  // namespace trails::core

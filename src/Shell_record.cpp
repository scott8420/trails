#include "Shell.hpp"
#include "Log.hpp"

#include <giomm/appinfo.h>
#include <giomm/file.h>
#include <gdkmm/clipboard.h>

#include <unistd.h>

#include <cstdio>
#include <filesystem>

// Shell_record.cpp -- RECORD (s009). The record button and its marks.
//
//   Ctrl+R / the button -> on_record -> core::Recorder::start / stop
//   Ctrl+M / Mark       -> on_mark   -> core::marks::add through the ONE journal
//
// The sampler's listener (Shell_live.cpp) feeds every tick to the recorder on
// the sampler thread; this file only starts, stops, marks and paints the
// header. Starting and stopping is not an undo step (time is not put back),
// and asks nothing: a recording is cheap and a stop is its own end marker.

namespace trails {

namespace {
std::string clock_text(double s) {
    const long t = long(s);
    char b[32];
    std::snprintf(b, sizeof b, "%02ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
    return b;
}
}  // namespace

void Shell::on_record() {  // record: start / stop
    auto lg = log::get(log::Area::Shell);
    if (m_recorder.recording()) {
        const std::string path = m_recorder.path();
        m_recorder.stop();
        m_rec_was_on = false;
        if (lg) lg->info("record: stopped {} ({} ticks)", path, m_recorder.ticks());
        m_rec_status.set_text("Saved " + std::filesystem::path(path).filename().string());
        m_rec_status.set_tooltip_text(path);
        m_rec_status.remove_css_class("error");
        paint_record();
        sync_acts();
        return;
    }
    core::Recorder::Meta meta;
    char host[256] = {};
    if (::gethostname(host, sizeof host - 1) == 0) meta.host = host;
    if (auto s = m_sampler ? m_sampler->snapshot() : nullptr) {
        meta.cpu_model = s->cpu_model;
        meta.n_cpus = s->n_cpus;
        meta.mem_total = s->mem.total;
    }
    if (m_sampler) meta.interval_ms = int(m_sampler->interval().count());
    const double now = core::Recorder::wall_now();
    const std::string path = core::Recorder::default_dir() + "/" + core::Recorder::default_name(std::int64_t(now));
    const std::string err = m_recorder.start(path, meta);
    if (!err.empty()) {
        if (lg) lg->warn("record: {}", err);
        m_rec_status.set_text("Could not record");
        m_rec_status.set_tooltip_text(err);
        m_rec_status.add_css_class("error");
        return;
    }
    m_marks_made = 0;
    m_rec_was_on = true;
    m_last_trail = path;
    m_rec_status.remove_css_class("error");
    m_rec_status.set_tooltip_text(path);
    if (lg) lg->info("record: started {}", path);
    paint_record();
    sync_acts();
}

void Shell::on_mark() {  // record: a mark now (undoable)
    if (!m_recorder.recording()) return;
    std::string err;
    const std::string text = "Mark " + std::to_string(m_marks_made + 1);
    if (core::marks::add(m_journal, m_app, text, &err)) {
        ++m_marks_made;
        if (auto lg = log::get(log::Area::Shell)) lg->info("record: {}", text);
    }
    paint_record();
}

void Shell::on_open_recordings() {  // record: the folder, in Files
    const std::string dir = core::Recorder::default_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    try {
        Gio::AppInfo::launch_default_for_uri(Gio::File::create_for_path(dir)->get_uri());
    } catch (const Glib::Error& e) {
        if (auto lg = log::get(log::Area::Shell)) lg->warn("open recordings: {}", e.what());
    }
}

void Shell::on_copy_trail_query() {  // record: sqlite3 command on the clipboard
    if (m_last_trail.empty()) return;
    // Works while recording too: WAL lets a reader in beside the writer.
    const std::string cmd = core::Recorder::query_command(m_last_trail);
    get_clipboard()->set_text(cmd);
    m_rec_status.set_text("Copied the sqlite3 command");
    m_rec_status.remove_css_class("error");
    if (auto lg = log::get(log::Area::Shell)) lg->info("record: copied {}", cmd);
}

void Shell::paint_record() {  // record: button, status, greying
    const bool on = m_recorder.recording();
    if (m_rec_was_on && !on) {   // the writer stopped itself (disk full, file gone)
        m_rec_was_on = false;
        const std::string why = m_recorder.error();
        m_rec_status.set_text("Recording stopped: " + (why.empty() ? std::string("unknown error") : why));
        m_rec_status.add_css_class("error");
        if (auto lg = log::get(log::Area::Shell)) lg->warn("record: stopped itself: {}", why);
        m_recorder.stop();   // join the writer
        sync_acts();
    }
    const std::string label = on ? "■  Stop" : "●  Record";
    if (std::string(m_rec_button.get_label()) != label) {
        m_rec_button.set_label(label);
        m_rec_button.set_tooltip_text(on ? "Stop recording (Ctrl+R)" : "Record everything to a .trail file (Ctrl+R)");
        if (on) m_rec_button.add_css_class("recording");
        else m_rec_button.remove_css_class("recording");
    }
    m_mark_button.set_visible(on);
    if (!m_last_trail.empty() && !m_copy_button.get_visible()) {
        m_copy_button.set_visible(true);
        sync_acts();   // the action follows
    }
    m_copy_button.set_tooltip_text(m_last_trail.empty() ? "" :
        "Copy:  " + core::Recorder::query_command(m_last_trail) + "   (Ctrl+Shift+C)");
    if (on) {
        std::string t = clock_text(core::Recorder::wall_now() - m_recorder.started());
        if (m_marks_made) t += " · " + std::to_string(m_marks_made) + (m_marks_made == 1 ? " mark" : " marks");
        if (std::string(m_rec_status.get_text()) != t) m_rec_status.set_text(t);
    }
}

}  // namespace trails

#include "pages/TrailsPage.hpp"
#include "Registry.hpp"
#include "core/Format.hpp"
#include "core/Recorder.hpp"
#include "widgets/Searchable.hpp"

#include <gdkmm/clipboard.h>
#include <giomm/file.h>
#include <giomm/liststore.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/window.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace trails::pages {

namespace fmt = core::fmt;

namespace {
void set(Gtk::Label& l, const std::string& t) {
    if (std::string(l.get_text()) != t) l.set_text(t);
}
bool ends(const std::string& s, const char* e) { return s.ends_with(e); }
bool is_pct(const std::string& n) {
    return n.rfind("cpu.", 0) == 0 ? n != "cpu.mhz" : (ends(n, "_pct") || ends(n, ".busy"));
}
std::string num(double v, int d, const char* unit) {
    char b[48];
    std::snprintf(b, sizeof b, "%.*f%s", d, v, unit);
    return b;
}
// How a recorded metric reads, from its name (the sampler's naming).
std::string metric_text(const std::string& n, double v) {
    if (is_pct(n)) return fmt::pct(v);
    if (ends(n, ".rx") || ends(n, ".tx") || ends(n, ".read") || ends(n, ".write")) return fmt::rate(v);
    if (n == "mem.used" || n == "swap.used") return fmt::bytes(std::uint64_t(std::max(0.0, v)));
    if (n == "cpu.mhz") return num(v, 0, " MHz");
    if (n == "temp.max" || n.find(".temp") != std::string::npos) return num(v, 1, " °C");
    if (n.find(".fan") != std::string::npos) return num(v, 0, " RPM");
    if (n.find(".power") != std::string::npos || n == "battery.power") return num(v, 2, " W");
    return num(v, 2, "");
}
std::string clock(double t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    ::localtime_r(&tt, &tm);
    char b[32];
    std::strftime(b, sizeof b, "%H:%M:%S", &tm);
    return b;
}
std::string day_clock(double t) {
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    ::localtime_r(&tt, &tm);
    char b[40];
    std::strftime(b, sizeof b, "%a %b %e, %H:%M:%S", &tm);
    return b;
}
std::string length(double s) {
    const long t = long(s + 0.5);
    char b[32];
    if (t >= 3600) std::snprintf(b, sizeof b, "%ld h %02ld m", t / 3600, (t / 60) % 60);
    else std::snprintf(b, sizeof b, "%ld m %02ld s", t / 60, t % 60);
    return b;
}
}  // namespace

TrailsPage::TrailsPage() : m_timeline("trails.timeline") {
    set_name("page.trails");
    registry::add("page.trails", this);
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_page.add_css_class("page");
    m_page.add_css_class("tab-trails");
    build_header();
    build_body();
    set_child(m_page);
}

TrailsPage::~TrailsPage() { registry::remove(this); }

void TrailsPage::build_header() {
    m_panel.add_css_class("panel");
    m_icon.set_from_icon_name("trails-hero-trails");
    m_icon.set_pixel_size(56);
    m_icon.add_css_class("tile-icon");
    m_title.set_text("Trails");
    m_title.add_css_class("panel-title");
    m_title.set_xalign(0.0f);
    m_sub.add_css_class("dim-label");
    m_sub.set_xalign(0.0f);
    m_sub.set_ellipsize(Pango::EllipsizeMode::END);
    m_titles.append(m_title);
    m_titles.append(m_sub);
    m_titles.set_hexpand(true);
    m_value.add_css_class("tile-value");
    m_value.set_valign(Gtk::Align::CENTER);
    m_value.set_text("--");
    m_head.append(m_icon);
    m_head.append(m_titles);
    m_head.append(m_value);

    // Which recording.
    m_file_model = Gtk::StringList::create({});
    m_file_dd.set_model(m_file_model);
    m_file_dd.set_hexpand(true);
    m_file_dd.property_selected().signal_changed().connect([this] {
        if (m_syncing) return;
        const auto i = m_file_dd.get_selected();
        if (i < m_file_paths.size() && m_file_paths[i] != m_trail.path()) open_path(m_file_paths[i]);
    });
    m_open_btn.set_label("Open…");
    m_open_btn.set_tooltip_text("Open a .trail from anywhere");
    m_open_btn.signal_clicked().connect([this] { on_open_dialog(); });
    m_export_btn.set_label("Export CSV…");
    m_export_btn.set_tooltip_text("Every metric, one row per second, over the range in view");
    m_export_btn.signal_clicked().connect([this] { on_export(); });
    m_copy_btn.set_icon_name("edit-copy-symbolic");
    m_copy_btn.set_tooltip_text("Copy a sqlite3 command that reads this recording");
    m_copy_btn.signal_clicked().connect([this] {
        if (!m_trail.is_open()) return;
        get_clipboard()->set_text(core::Recorder::query_command(m_trail.path()));
        set_status("Copied the sqlite3 command", false);
    });
    m_files.append(m_file_dd);
    m_files.append(m_open_btn);
    m_files.append(m_export_btn);
    m_files.append(m_copy_btn);

    // Which metrics.
    m_a_model = Gtk::StringList::create({});
    m_b_model = Gtk::StringList::create({});
    m_a_dd.set_model(m_a_model);
    m_b_dd.set_model(m_b_model);
    m_a_dd.set_hexpand(true);
    m_b_dd.set_hexpand(true);
    widgets::searchable(m_a_dd);   // s014: type to find one of ~70 metrics
    widgets::searchable(m_b_dd);
    for (auto* dd : {&m_a_dd, &m_b_dd})
        dd->property_selected().signal_changed().connect([this] { if (!m_syncing) { requery(); paint_cursor(); } });
    auto* show = Gtk::make_managed<Gtk::Label>("Show");
    auto* with = Gtk::make_managed<Gtk::Label>("with");
    show->add_css_class("dim-label");
    with->add_css_class("dim-label");
    m_pick.append(*show);
    m_pick.append(m_a_dd);
    m_pick.append(*with);
    m_pick.append(m_b_dd);

    m_timeline.signal_view().connect([this] { requery(); });
    m_timeline.signal_cursor().connect([this](double) { paint_cursor(); });
    m_timeline.signal_step().connect([this](int d) {
        if (!m_trail.is_open()) return;
        const double t = d < 0 ? m_trail.tick_before(m_timeline.cursor()) : m_trail.tick_after(m_timeline.cursor());
        m_timeline.set_cursor(t);
        if (t < m_timeline.view_from() || t > m_timeline.view_to()) {
            const double w = m_timeline.view_to() - m_timeline.view_from();
            m_timeline.set_view(t - w / 2, t + w / 2);
        }
        paint_cursor();
    });
    m_hint.set_text("Drag to scrub · scroll to zoom · Shift+scroll to pan · double-click for all · "
                    "← → one second · Home / End");
    m_hint.add_css_class("dim-label");
    m_hint.set_xalign(0.0f);
    m_status.set_xalign(0.0f);
    m_status.set_ellipsize(Pango::EllipsizeMode::END);

    m_panel.append(m_head);
    m_panel.append(m_files);
    m_panel.append(m_pick);
    m_panel.append(m_timeline);
    m_panel.append(m_hint);
    m_panel.append(m_status);
    m_page.append(m_panel);
}

void TrailsPage::build_body() {
    // At the cursor.
    m_at.add_css_class("panel");
    m_at.set_hexpand(true);
    m_at_title.add_css_class("section-title");
    m_at_title.set_xalign(0.0f);
    for (auto* l : {&m_at_a, &m_at_b}) { l->set_xalign(0.0f); l->add_css_class("kv-value"); }
    m_procs.set_column_spacing(18);
    m_procs.set_row_spacing(3);
    const char* heads[] = {"Process", "PID", "CPU", "Memory"};
    for (int c = 0; c < 4; ++c) {
        auto* h = Gtk::make_managed<Gtk::Label>(heads[c]);
        h->add_css_class("dim-label");
        h->set_xalign(c == 0 ? 0.0f : 1.0f);
        m_procs.attach(*h, c, 0);
    }
    for (int r = 0; r < 12; ++r)
        for (int c = 0; c < 4; ++c) {
            auto& l = m_proc_rows[std::size_t(r)][std::size_t(c)];
            l.set_xalign(c == 0 ? 0.0f : 1.0f);
            if (c == 0) { l.set_hexpand(true); l.set_ellipsize(Pango::EllipsizeMode::END); }
            m_procs.attach(l, c, r + 1);
        }
    m_at.append(m_at_title);
    m_at.append(m_at_a);
    m_at.append(m_at_b);
    m_at.append(m_procs);

    // Marks.
    m_marks_box.add_css_class("panel");
    m_marks_box.set_size_request(320, -1);
    m_marks_title.set_text("Marks");
    m_marks_title.add_css_class("section-title");
    m_marks_title.set_xalign(0.0f);
    m_marks.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_marks.add_css_class("boxed-list");
    m_marks.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (m_syncing) return;
        m_sel_mark = 0;
        if (row) {
            const auto i = std::size_t(row->get_index());
            if (i < m_mark_list.size()) {
                m_sel_mark = m_mark_list[i].id;
                const double t = m_mark_list[i].m.t;
                m_timeline.set_cursor(t);
                if (t < m_timeline.view_from() || t > m_timeline.view_to()) {
                    const double w = m_timeline.view_to() - m_timeline.view_from();
                    m_timeline.set_view(t - w / 2, t + w / 2);
                }
                paint_cursor();
            }
        }
        std::vector<widgets::Timeline::Mark> ms;
        for (const auto& m : m_mark_list)
            ms.push_back({m.m.t, m.m.text.empty() ? m.m.kind : m.m.text, m.id == m_sel_mark});
        m_timeline.set_marks(std::move(ms));
        m_rename_btn.set_sensitive(m_sel_mark != 0);
        m_delete_btn.set_sensitive(m_sel_mark != 0);
    });
    m_add_btn.set_label("Mark at cursor");
    m_add_btn.set_action_name("win.trail-mark-add");
    m_rename_btn.set_label("Rename…");
    m_rename_btn.set_action_name("win.trail-mark-rename");
    m_delete_btn.set_label("Delete");
    m_delete_btn.set_action_name("win.trail-mark-delete");
    m_mark_btns.append(m_add_btn);
    m_mark_btns.append(m_rename_btn);
    m_mark_btns.append(m_delete_btn);
    m_marks_box.append(m_marks_title);
    m_marks_box.append(m_marks);
    m_marks_box.append(m_mark_btns);

    m_lower.append(m_at);
    m_lower.append(m_marks_box);
    m_page.append(m_lower);

    // Triggers (s012): start the trail before you notice.
    m_trig_box.add_css_class("panel");
    m_trig_title.set_text("Triggers");
    m_trig_title.add_css_class("section-title");
    m_trig_title.set_xalign(0.0f);
    m_trig_hint.set_text("Start a recording or drop a mark when a condition holds -- e.g. cpu.total above 90 "
                         "for 30 s. Triggers run while Trails is open.");
    m_trig_hint.add_css_class("dim-label");
    m_trig_hint.set_xalign(0.0f);
    m_trig_hint.set_wrap(true);
    m_trig_list.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_trig_list.add_css_class("boxed-list");
    m_trig_list.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (m_syncing) return;
        const auto i = row ? std::size_t(row->get_index()) : m_trig_ids.size();
        m_sel_trigger = i < m_trig_ids.size() ? m_trig_ids[i] : 0;
        if (m_on_trigger_pick) m_on_trigger_pick();
    });
    struct B { const char* label; const char* action; };
    for (const B& b : {B{"Add\u2026", "win.trigger-add"}, B{"Edit\u2026", "win.trigger-edit"},
                       B{"On / Off", "win.trigger-toggle"}, B{"Remove", "win.trigger-remove"}}) {
        auto* btn = Gtk::make_managed<Gtk::Button>(b.label);
        btn->set_action_name(b.action);
        m_trig_btns.append(*btn);
    }
    m_trig_box.append(m_trig_title);
    m_trig_box.append(m_trig_hint);
    m_trig_box.append(m_trig_list);
    m_trig_box.append(m_trig_btns);
    m_page.append(m_trig_box);
}

void TrailsPage::refresh_triggers() {
    if (!m_book) return;
    m_syncing = true;
    while (auto* r = m_trig_list.get_row_at_index(0)) m_trig_list.remove(*r);
    m_trig_ids = m_book->ids();
    m_trig_status.clear();
    for (auto id : m_trig_ids) {
        const auto t = *m_book->get(id);
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        row->set_margin(6);
        auto* name = Gtk::make_managed<Gtk::Label>(t.name.empty() ? "(unnamed)" : t.name);
        name->add_css_class("kv-value");
        name->set_xalign(0.0f);
        auto* what = Gtk::make_managed<Gtk::Label>(core::describe(t));
        what->set_xalign(0.0f);
        what->set_hexpand(true);
        what->set_ellipsize(Pango::EllipsizeMode::END);
        auto* st = Gtk::make_managed<Gtk::Label>();
        st->add_css_class("dim-label");
        row->append(*name);
        row->append(*what);
        row->append(*st);
        if (!t.enabled) row->set_opacity(0.5);
        m_trig_list.append(*row);
        m_trig_status.push_back(st);
    }
    bool found = false;
    for (std::size_t i = 0; i < m_trig_ids.size(); ++i)
        if (m_trig_ids[i] == m_sel_trigger) { m_trig_list.select_row(*m_trig_list.get_row_at_index(int(i))); found = true; }
    if (!found) m_sel_trigger = 0;
    m_trig_list.set_visible(!m_trig_ids.empty());
    m_syncing = false;
    paint_triggers();
    if (m_on_trigger_pick) m_on_trigger_pick();
}

void TrailsPage::paint_triggers() {
    if (!m_eval || !m_book) return;
    for (std::size_t i = 0; i < m_trig_ids.size() && i < m_trig_status.size(); ++i) {
        const auto r = m_book->get(m_trig_ids[i]);
        const auto st = m_eval->status(m_trig_ids[i]);
        std::string w;
        if (!r || !r->enabled) w = "off";
        else switch (st.state) {
            case core::TriggerEval::State::Off:     w = "off"; break;
            case core::TriggerEval::State::Armed:   w = "watching"; break;
            case core::TriggerEval::State::Holding: w = "holding " + std::to_string(int(st.held)) + " / " +
                                                        std::to_string(r->seconds) + " s"; break;
            case core::TriggerEval::State::Fired:   w = "fired \u2014 waiting to clear"; break;
        }
        set(*m_trig_status[i], w);
    }
}

void TrailsPage::set_status(const std::string& text, bool error) {
    set(m_status, text);
    if (error) m_status.add_css_class("error");
    else m_status.remove_css_class("error");
}

void TrailsPage::refresh_files() {
    m_file_paths = core::Trail::list(core::Recorder::default_dir());
    if (m_trail.is_open() &&
        std::find(m_file_paths.begin(), m_file_paths.end(), m_trail.path()) == m_file_paths.end())
        m_file_paths.insert(m_file_paths.begin(), m_trail.path());   // opened from elsewhere
    std::vector<Glib::ustring> names;
    const std::string live = m_live ? m_live() : std::string();
    for (const auto& p : m_file_paths)
        names.push_back(std::filesystem::path(p).filename().string() + (p == live ? "  (recording)" : ""));
    m_syncing = true;
    m_file_model->splice(0, m_file_model->get_n_items(), names);
    for (std::size_t i = 0; i < m_file_paths.size(); ++i)
        if (m_file_paths[i] == m_trail.path()) m_file_dd.set_selected(guint(i));
    m_syncing = false;
}

void TrailsPage::shown() {
    refresh_files();
    if (!m_trail.is_open()) {
        if (!m_file_paths.empty()) open_path(m_file_paths.front());
        else set(m_sub, "No recordings yet — press ● Record (Ctrl+R) to make one.");
    }
}

void TrailsPage::open_path(const std::string& path) {
    const std::string e = m_trail.open(path);
    if (!e.empty()) {
        set_status(e, true);
        return;
    }
    set_status("", false);
    refresh_files();
    fill_metrics();
    m_timeline.set_span(m_trail.t0(), m_trail.t1());
    m_timeline.zoom_all();
    m_timeline.set_cursor(m_trail.t0());
    refresh_marks();
    requery();
    paint_cursor();
}

void TrailsPage::fill_metrics() {
    const std::string keep_a = metric_a(), keep_b = metric_b();
    std::vector<Glib::ustring> a, b{"—"};
    for (const auto& n : m_trail.metrics()) { a.push_back(n); b.push_back(n); }
    m_syncing = true;
    m_a_model->splice(0, m_a_model->get_n_items(), a);
    m_b_model->splice(0, m_b_model->get_n_items(), b);
    guint ia = 0, ib = 0;
    const std::string want_a = keep_a.empty() ? "cpu.total" : keep_a;
    for (guint i = 0; i < a.size(); ++i) if (std::string(a[i]) == want_a) ia = i;
    for (guint i = 0; i < b.size(); ++i) if (std::string(b[i]) == keep_b) ib = i;
    m_a_dd.set_selected(ia);
    m_b_dd.set_selected(ib);
    m_syncing = false;
}

std::string TrailsPage::metric_a() const {
    const auto i = m_a_dd.get_selected();
    return i < m_a_model->get_n_items() ? std::string(m_a_model->get_string(i)) : std::string();
}
std::string TrailsPage::metric_b() const {
    const auto i = m_b_dd.get_selected();
    return (i > 0 && i < m_b_model->get_n_items()) ? std::string(m_b_model->get_string(i)) : std::string();
}

void TrailsPage::requery() {
    if (!m_trail.is_open()) return;
    const int px = std::max(100, m_timeline.get_width());
    const std::string a = metric_a(), b = metric_b();
    const double f = m_timeline.view_from(), t = m_timeline.view_to();
    m_timeline.set_series(m_trail.series(a, f, t, px), b.empty() ? std::vector<core::TrailPoint>{}
                                                                 : m_trail.series(b, f, t, px));
    m_timeline.set_top(is_pct(a) && (b.empty() || is_pct(b)) ? 100.0 : 0.0);
    m_timeline.set_value_format([a](double v) { return metric_text(a, v); });

    const auto& m = m_trail.meta();
    auto get = [&](const char* k) { auto it = m.find(k); return it == m.end() ? std::string() : it->second; };
    const std::string live = m_live ? m_live() : std::string();
    std::string sub = get("host") + " · " + day_clock(m_trail.t0()) + " · " +
                      length(m_trail.t1() - m_trail.t0()) + " · " + std::to_string(m_trail.ticks()) + " ticks";
    if (m_trail.path() == live) sub += " · still recording";
    set(m_sub, sub);
}

void TrailsPage::paint_cursor() {
    if (!m_trail.is_open()) return;
    const double c = m_timeline.cursor();
    double tick = c;
    const auto procs = m_trail.procs_at(c, &tick);
    set(m_value, clock(c));
    set(m_at_title, "At " + clock(tick));
    const std::string a = metric_a(), b = metric_b();
    auto va = m_trail.value_at(a, c);
    set(m_at_a, a + "   " + (va ? metric_text(a, *va) : std::string("--")));
    if (!b.empty()) {
        auto vb = m_trail.value_at(b, c);
        set(m_at_b, b + "   " + (vb ? metric_text(b, *vb) : std::string("--")));
    }
    m_at_b.set_visible(!b.empty());
    for (std::size_t r = 0; r < m_proc_rows.size(); ++r) {
        auto& row = m_proc_rows[r];
        if (r < procs.size()) {
            set(row[0], procs[r].name);
            set(row[1], std::to_string(procs[r].pid));
            set(row[2], fmt::pct(procs[r].cpu));
            set(row[3], fmt::bytes(procs[r].rss));
        } else {
            for (auto& l : row) set(l, "");
        }
    }
}

void TrailsPage::refresh_marks() {
    if (!m_trail.is_open()) return;
    m_mark_list = m_trail.marks();
    m_syncing = true;
    while (auto* r = m_marks.get_row_at_index(0)) m_marks.remove(*r);
    for (const auto& m : m_mark_list) {
        auto* l = Gtk::make_managed<Gtk::Label>(clock(m.m.t) + "   " + (m.m.text.empty() ? m.m.kind : m.m.text));
        l->set_xalign(0.0f);
        l->set_margin(4);
        m_marks.append(*l);
    }
    // Reselect by id (the list was rebuilt), quietly: no jump.
    bool found = false;
    for (std::size_t i = 0; i < m_mark_list.size(); ++i)
        if (m_mark_list[i].id == m_sel_mark) {
            m_marks.select_row(*m_marks.get_row_at_index(int(i)));
            found = true;
        }
    if (!found) {
        m_sel_mark = 0;
        m_marks.unselect_all();
    }
    m_syncing = false;
    std::vector<widgets::Timeline::Mark> ms;
    for (const auto& m : m_mark_list)
        ms.push_back({m.m.t, m.m.text.empty() ? m.m.kind : m.m.text, m.id == m_sel_mark});
    m_timeline.set_marks(std::move(ms));
    m_rename_btn.set_sensitive(m_sel_mark != 0);
    m_delete_btn.set_sensitive(m_sel_mark != 0);
}

void TrailsPage::tick() {
    // A file still being recorded grows: reload every few seconds.
    if (!m_trail.is_open() || !m_live || m_live() != m_trail.path()) return;
    if (++m_tick_count % 3 != 0) return;
    m_trail.reload();
    m_timeline.set_span(m_trail.t0(), m_trail.t1());
    if (m_trail.metrics().size() != std::size_t(m_a_model->get_n_items())) fill_metrics();
    requery();
    refresh_marks();   // a trigger's (or Ctrl+M's) mark shows up too
}

void TrailsPage::on_open_dialog() {
    if (!m_window) return;
    auto d = Gtk::FileDialog::create();
    d->set_title("Open a recording");
    d->set_initial_folder(Gio::File::create_for_path(core::Recorder::default_dir()));
    auto filter = Gtk::FileFilter::create();
    filter->set_name("Trails recordings");
    filter->add_pattern("*.trail");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    d->set_filters(filters);
    d->open(*m_window, [this, d](Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            if (auto f = d->open_finish(res)) open_path(f->get_path());
        } catch (const Glib::Error&) {}   // cancelled
    });
}

void TrailsPage::on_export() {
    if (!m_window || !m_trail.is_open()) return;
    auto d = Gtk::FileDialog::create();
    d->set_title("Export CSV");
    const std::string stem = std::filesystem::path(m_trail.path()).stem().string();
    d->set_initial_name(stem + ".csv");
    d->save(*m_window, [this, d](Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            auto f = d->save_finish(res);
            if (!f) return;
            std::string err;
            const long rows = m_trail.export_csv(f->get_path(), m_timeline.view_from(), m_timeline.view_to(), &err);
            if (rows < 0) set_status(err, true);
            else set_status("Exported " + std::to_string(rows) + " rows to " + f->get_path(), false);
        } catch (const Glib::Error&) {}
    });
}

}  // namespace trails::pages

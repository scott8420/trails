#include "Elevate.hpp"
#include "Log.hpp"

#include <giomm/subprocess.h>
#include <glibmm/main.h>

#include <sys/stat.h>

namespace trails::elevate {

namespace {
bool g_busy = false;

constexpr const char* kPaths[] = {
    "/usr/local/libexec/trails/trails-helper",
    "/usr/libexec/trails/trails-helper",
};

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    const std::string tag = "trails-helper: ";
    if (s.rfind(tag, 0) == 0) s.erase(0, tag.size());
    return s;
}
}  // namespace

bool busy() { return g_busy; }

std::string helper_path(std::string* why) {
    for (const char* p : kPaths) {
        struct stat st{};
        if (::stat(p, &st) != 0) continue;
        if (st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH)) || !(st.st_mode & S_IXUSR)) {
            if (why) *why = std::string(p) + " is not a root-owned executable; refusing it.";
            return {};
        }
        return p;
    }
    if (why) *why = "The privileged helper is not installed (run install.sh).";
    return {};
}

std::string run_helper(const std::vector<std::string>& args) {
    std::string why;
    const std::string helper = helper_path(&why);
    if (helper.empty()) return "Not permitted. " + why;
    if (g_busy) return "Another action is waiting for authentication.";

    std::vector<std::string> argv{"pkexec", helper};
    argv.insert(argv.end(), args.begin(), args.end());
    auto lg = log::get(log::Area::Shell);
    if (lg) {
        std::string line;
        for (const auto& a : args) line += " " + a;
        lg->info("elevate: trails-helper{}", line);
    }

    Glib::RefPtr<Gio::Subprocess> proc;
    try {
        proc = Gio::Subprocess::create(argv, Gio::Subprocess::Flags::STDERR_PIPE |
                                                 Gio::Subprocess::Flags::STDOUT_SILENCE);
    } catch (const Glib::Error& e) {
        return std::string("Could not start pkexec: ") + e.what();
    }

    // Spin the loop until pkexec returns: the polkit dialog belongs to the
    // shell, and our window keeps painting meanwhile.
    g_busy = true;
    bool done = false;
    std::string err_text;
    proc->communicate_utf8_async({}, [&](Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            auto out = proc->communicate_utf8_finish(res);
            err_text = out.second;
        } catch (const Glib::Error&) {}
        done = true;
    });
    auto ctx = Glib::MainContext::get_default();
    while (!done) ctx->iteration(true);
    g_busy = false;

    if (!proc->get_if_exited()) return "The helper did not finish.";
    const int code = proc->get_exit_status();
    if (lg) lg->info("elevate: exit {} {}", code, trim(err_text));
    switch (code) {
        case 0:   return {};
        case 126: return "Authentication was dismissed.";
        case 127: return "Not authorized.";
        case 3:   return "The process has exited.";
        default: {
            std::string t = trim(err_text);
            return t.empty() ? "The helper failed (exit " + std::to_string(code) + ")." : "Refused: " + t + ".";
        }
    }
}

}  // namespace trails::elevate

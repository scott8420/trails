#include "core/SysInfo.hpp"
#include "core/Format.hpp"
#include "core/probe/Probe.hpp"

#include <sys/utsname.h>

#include <cstdlib>
#include <fstream>

namespace trails::core {

namespace {
std::string at(const std::string& root, const char* rel) {
    std::string r = root.empty() ? "/" : root;
    if (r.back() != '/') r += '/';
    return r + rel;
}
std::string env(const char* k) {
    const char* v = std::getenv(k);
    return v ? v : "";
}
}  // namespace

std::string os_pretty_name(const std::string& root) {
    for (const char* rel : {"etc/os-release", "usr/lib/os-release"}) {
        std::ifstream f(at(root, rel));
        if (!f) continue;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
            std::string v = line.substr(12);
            if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
                v = v.substr(1, v.size() - 2);
            return v;
        }
    }
    return {};
}

InfoRows system_info(const std::string& root, int n_cpus) {
    InfoRows out;
    auto add = [&](const char* k, const std::string& v) { if (!v.empty()) out.emplace_back(k, v); };
    add("OS", os_pretty_name(root));
    struct utsname u{};
    if (::uname(&u) == 0) add("Kernel", std::string(u.release) + " (" + u.machine + ")");
    add("Hardware", probe::read_cpu_model(root));
    if (n_cpus > 0) add("CPUs", std::to_string(n_cpus));
    probe::MemInfo m;
    if (probe::read_meminfo(root, m) && m.total > 0) add("Memory", fmt::bytes(m.total));
    add("Desktop", env("XDG_CURRENT_DESKTOP"));
    add("Session", env("XDG_SESSION_TYPE"));
    return out;
}

std::string info_text(const InfoRows& rows) {
    std::string s;
    for (const auto& [k, v] : rows) s += k + ": " + v + "\n";
    return s;
}

}  // namespace trails::core

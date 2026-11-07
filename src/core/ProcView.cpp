#include "core/ProcView.hpp"

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <mutex>
#include <pwd.h>
#include <unistd.h>
#include <unordered_set>

namespace trails::core::procview {

Index build_index(const std::vector<ProcRow>& procs) {
    Index ix;
    std::unordered_set<int> present;
    present.reserve(procs.size());
    for (const auto& p : procs) present.insert(p.pid);
    for (const auto& p : procs) {   // procs arrive in pid order, so children do too
        if (p.ppid != p.pid && present.count(p.ppid)) ix.children[p.ppid].push_back(p.pid);
        else ix.roots.push_back(p.pid);
    }
    return ix;
}

namespace {
bool icontains(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) {
                              return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
                          });
    return it != hay.end();
}
}  // namespace

bool matches(const ProcRow& p, std::string_view user, std::string_view q) {
    while (!q.empty() && q.front() == ' ') q.remove_prefix(1);
    while (!q.empty() && q.back() == ' ') q.remove_suffix(1);
    if (q.empty()) return true;
    return icontains(p.name, q) || icontains(p.cmdline, q) || icontains(user, q) ||
           icontains(std::to_string(p.pid), q);
}

const char* state_name(char s) {
    switch (s) {
        case 'R': return "Running";
        case 'S': return "Sleeping";
        case 'D': return "Disk wait";
        case 'Z': return "Zombie";
        case 'T': return "Stopped";
        case 't': return "Traced";
        case 'I': return "Idle";
        case 'X': case 'x': return "Dead";
        case 'W': return "Paging";
        case 'P': return "Parked";
        default:  return "?";
    }
}

const std::string& user_name(std::uint32_t uid) {
    static std::mutex mu;
    static std::unordered_map<std::uint32_t, std::string> cache;
    std::lock_guard lk(mu);
    auto it = cache.find(uid);
    if (it != cache.end()) return it->second;
    std::string name;
    passwd pw{}, *res = nullptr;
    std::vector<char> buf(16384);
    if (::getpwuid_r(uid, &pw, buf.data(), buf.size(), &res) == 0 && res && res->pw_name)
        name = res->pw_name;
    else
        name = std::to_string(uid);
    return cache.emplace(uid, std::move(name)).first->second;
}

}  // namespace trails::core::procview

namespace trails::core::probe {

bool count_fds(const std::string& root, int pid, int& files, int& sockets) {
    files = sockets = 0;
    const std::string dir = at(root, "proc/" + std::to_string(pid) + "/fd");
    DIR* d = ::opendir(dir.c_str());
    if (!d) return false;
    char link[256];
    while (dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        ++files;
        const std::string p = dir + "/" + e->d_name;
        const ssize_t n = ::readlink(p.c_str(), link, sizeof link - 1);
        if (n > 0) {
            link[n] = '\0';
            if (std::string_view(link, std::size_t(n)).starts_with("socket:")) ++sockets;
        }
    }
    ::closedir(d);
    return true;
}

}  // namespace trails::core::probe

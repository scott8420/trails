#pragma once
// Internal to the probe readers: a whitespace tokenizer over a string_view and
// a line splitter. No allocation, no iostreams -- the process table reads a few
// hundred files a second.
#include <charconv>
#include <cstdint>
#include <string_view>

namespace trails::core::probe::detail {

inline bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Next whitespace-delimited token, advancing `s`. Empty view at end.
inline std::string_view next_tok(std::string_view& s) {
    std::size_t i = 0;
    while (i < s.size() && is_ws(s[i])) ++i;
    std::size_t j = i;
    while (j < s.size() && !is_ws(s[j])) ++j;
    std::string_view t = s.substr(i, j - i);
    s.remove_prefix(j);
    return t;
}

template <class N>
inline bool to_num(std::string_view t, N& out) {
    if (t.empty()) return false;
    auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), out);
    return ec == std::errc{};
}

template <class N>
inline bool next_num(std::string_view& s, N& out) { return to_num(next_tok(s), out); }

// Next line (without '\n'), advancing `s`. Returns false at end.
inline bool next_line(std::string_view& s, std::string_view& line) {
    if (s.empty()) return false;
    std::size_t nl = s.find('\n');
    if (nl == std::string_view::npos) { line = s; s = {}; }
    else { line = s.substr(0, nl); s.remove_prefix(nl + 1); }
    return true;
}

}  // namespace trails::core::probe::detail

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace gbarecomp {

namespace detail {
inline bool ci_equal(const char* a, const char* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b)))
            return false;
        ++a; ++b;
    }
    return *a == *b;
}
}  // namespace detail

// Minimal INI section scan used by keybinds.ini and config.ini. Calls fn(key,
// value) for each assignment inside `section`. Pure file/std library code; no
// SDL dependency, so it remains usable on any backend.
template <typename Fn>
inline void ini_scan_section(const char* path, const char* section, Fn fn) {
    if (!path || !section) return;
    std::FILE* f = std::fopen(path, "r");
    if (!f) return;
    char line[512];
    bool in_section = false;
    while (std::fgets(line, sizeof(line), f)) {
        char* s = line;
        while (*s == ' ' || *s == '\t') ++s;
        std::size_t n = std::strlen(s);
        while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
            s[--n] = '\0';
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            char* close = std::strchr(s, ']');
            if (close) *close = '\0';
            in_section = detail::ci_equal(s + 1, section);
            continue;
        }
        if (!in_section) continue;
        char* eq = std::strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = s;
        char* val = eq + 1;
        std::size_t kl = std::strlen(key);
        while (kl && (key[kl-1] == ' ' || key[kl-1] == '\t')) key[--kl] = '\0';
        while (*val == ' ' || *val == '\t') ++val;
        char* hash = std::strchr(val, '#');
        if (hash) *hash = '\0';
        std::size_t vl = std::strlen(val);
        while (vl && (val[vl-1] == ' ' || val[vl-1] == '\t')) val[--vl] = '\0';
        fn(key, val);
    }
    std::fclose(f);
}

}  // namespace gbarecomp
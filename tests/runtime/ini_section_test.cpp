#include "ini_section.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

int main() {
    const char* path = "ini_section_test.ini";
    {
        std::ofstream f(path);
        f << "[Touch]\n";
        f << "pad_visible = 1 # comment\n";
        f << "toggle_x = 0.25\n";
        f << "[Other]\n";
        f << "ignored = value\n";
        f << "  \n";
        f << "not_an_assignment\n";
    }

    int count = 0;
    std::vector<std::pair<std::string, std::string>> seen;
    gbarecomp::ini_scan_section(path, "touch", [&](const char* key, const char* val) {
        ++count;
        seen.emplace_back(key, val);
    });

    assert(count == 2);
    assert(seen[0].first == "pad_visible" && seen[0].second == "1");
    assert(seen[1].first == "toggle_x" && seen[1].second == "0.25");

    std::remove(path);
    std::printf("ini_section tests: PASS\n");
    return 0;
}
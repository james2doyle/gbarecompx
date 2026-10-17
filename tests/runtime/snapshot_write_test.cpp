// snapshot_write_test.cpp — the atomic state-file write (temp + rename).
//
// Device incident (2026-10-16): the phone's slot 1 held a state file
// PUSHED BY ADB days earlier — owned by `shell`, mode r-- for the app's
// group — and the menu save failed with "snapshot: cannot open for
// write". The pre-fix writer opened the TARGET for trunc, which needs the
// file itself; the app owned only the directory. The atomic replace
// (write "<path>.tmp", fsync, rename over the target — the mod_state
// sidecar pattern) needs only the directory, so a foreign-owned target
// self-heals, and a crash mid-write can no longer leave a half-written
// slot (the suspend state must survive process kills, hence the fsync).
//
// RED demonstration, inline and literal: the test first reproduces the
// pre-fix mechanism (opening a foreign-owned target for trunc fails),
// then requires snapshot_write_file to succeed where that failed. Pure
// file I/O — no bus/PPU context, runs everywhere.

#include "snapshot.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using gbarecomp::debug::snapshot_write_file;

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace {

const char* kTmpRoots[] = {std::getenv("TMPDIR"), "/tmp/opencode", "/tmp"};

std::string make_scratch_dir() {
    for (const char* root : kTmpRoots) {
        if (!root || !*root) continue;
        std::string base = root;
        while (!base.empty() && base.back() == '/') base.pop_back();
        std::string tmpl = base + "/snapshot_write_test_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        char* d = ::mkdtemp(buf.data());
        if (d) return d;
    }
    std::fprintf(stderr, "snapshot_write_test: mkdtemp failed\n");
    std::exit(1);
}

std::string read_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    const std::string dir = make_scratch_dir();
    const std::string path = dir + "/ffta_usa.state1";

    // A foreign-owned, group-readonly target — the device's shape (the
    // test user plays both roles: owner-without-write == app-without-write).
    {
        std::ofstream f(path, std::ios::binary);
        f << "old state pushed by adb";
    }
    ::chmod(path.c_str(), 0444);

    // RED demonstration, pre-fix mechanism pinned: opening the TARGET for
    // trunc fails — this is exactly what the old save_state did.
    {
        std::ofstream probe(path, std::ios::binary | std::ios::trunc);
        CHECK(!probe.is_open());
    }

    // The contract: the atomic replace succeeds where the direct open
    // could not, the content lands intact, and no temp is left behind.
    const std::string blob = "new snapshot bytes";
    std::string err;
    CHECK(snapshot_write_file(path.c_str(), blob.data(),
                              blob.size(), &err));
    CHECK(read_all(path) == blob);
    std::FILE* probe = std::fopen((path + ".tmp").c_str(), "rb");
    CHECK(probe == nullptr);  // the temporary was consumed by the rename

    // Honesty: an unwritable DIRECTORY still fails, with an error.
    const std::string ro_dir = dir + "/ro";
    ::mkdir(ro_dir.c_str(), 0555);
    err.clear();
    const std::string ro_path = ro_dir + "/ffta_usa.state2";
    CHECK(!snapshot_write_file(ro_path.c_str(), blob.data(),
                               blob.size(), &err));
    CHECK(!err.empty());
    ::chmod(ro_dir.c_str(), 0755);  // cleanup can remove it

    std::printf("snapshot_write_test: PASS (atomic replace over a "
                "foreign-owned target; unwritable dir still errors)\n");
    return 0;
}

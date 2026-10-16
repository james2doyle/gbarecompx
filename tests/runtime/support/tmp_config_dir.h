// tmp_config_dir.h — scratch config.ini dir for persistence tests.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <cerrno>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace gbarecomp::testing {

// RAII scratch dir containing a config.ini. path() is the dir (load/save
// helpers take the dir, mirroring load_config).
class TmpConfigDir {
public:
    TmpConfigDir() {
        // Machine-local scratch roots differ (/tmp/opencode exists only where
        // the FFTA harness pre-created it -- the CI runners do not), so try
        // known-good roots and fall back to TMPDIR. mkdtemp needs the parent
        // to exist; create it if missing rather than failing the whole test.
        const char* roots[] = {std::getenv("TMPDIR"), "/tmp/opencode", "/tmp"};
        for (const char* root : roots) {
            if (!root || !*root) continue;
            std::string base = root;
            while (!base.empty() && base.back() == '/') base.pop_back();
            std::string tmpl = base + "/menu_settings_test_XXXXXX";
            std::vector<char> buf(tmpl.begin(), tmpl.end());
            buf.push_back('\0');
            char* d = ::mkdtemp(buf.data());
            if (d) {
                dir_ = d;
                return;
            }
            // Parent missing? Create it (one level is enough for /tmp/X) and
            // retry the same template once.
            if (::mkdir(base.c_str(), 0755) == 0 || errno == EEXIST) {
                std::vector<char> buf2(tmpl.begin(), tmpl.end());
                buf2.push_back('\0');
                d = ::mkdtemp(buf2.data());
                if (d) {
                    dir_ = d;
                    return;
                }
            }
        }
        std::fprintf(stderr, "TmpConfigDir: mkdtemp failed (TMPDIR=%s)\n",
                     std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "(null)");
        std::exit(1);
    }
    ~TmpConfigDir() {
        std::remove(config_path().c_str());
        ::rmdir(dir_.c_str());
    }
    TmpConfigDir(const TmpConfigDir&) = delete;
    TmpConfigDir& operator=(const TmpConfigDir&) = delete;

    const std::string& path() const { return dir_; }
    std::string config_path() const { return dir_ + "/config.ini"; }

    void write_config(const std::string& body) {
        std::ofstream f(config_path(), std::ios::trunc);
        f << body;
    }
    std::string read_config() const {
        std::ifstream f(config_path());
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
    bool contains(const std::string& needle) const {
        return read_config().find(needle) != std::string::npos;
    }
    int count_occurrences(const std::string& needle) const {
        int n = 0;
        std::string hay = read_config();
        std::string::size_type at = 0;
        while ((at = hay.find(needle, at)) != std::string::npos) {
            ++n;
            at += needle.size();
        }
        return n;
    }

private:
    std::string dir_;
};

}  // namespace gbarecomp::testing

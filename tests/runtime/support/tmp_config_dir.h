// tmp_config_dir.h — scratch config.ini dir for persistence tests.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace gbarecomp::testing {

// RAII scratch dir containing a config.ini. path() is the dir (load/save
// helpers take the dir, mirroring load_config).
class TmpConfigDir {
public:
    TmpConfigDir() {
        char tmpl[] = "/tmp/opencode/menu_settings_test_XXXXXX";
        char* d = ::mkdtemp(tmpl);
        if (!d) {
            std::fprintf(stderr, "TmpConfigDir: mkdtemp failed\n");
            std::exit(1);
        }
        dir_ = d;
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

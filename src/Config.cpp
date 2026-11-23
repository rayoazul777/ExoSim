#include "Config.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <nlohmann/json.hpp>

namespace {
using json = nlohmann::json;

struct Reader {
    Config& cfg;
    void warn(const std::string& s) { cfg.warnings.push_back(s); }

    void unknownKeys(const json& obj, const std::string& where, std::initializer_list<const char*> known) {
        std::set<std::string> k(known.begin(), known.end());
        for (auto it = obj.begin(); it != obj.end(); ++it)
            if (!k.count(it.key())) warn("unknown key '" + where + it.key() + "' (ignored)");
    }
    void str(const json& o, const char* key, const std::string& path, std::string& dst) {
        if (!o.contains(key)) return;
        if (o[key].is_string())
            dst = o[key].get<std::string>();
        else
            warn("'" + path + "' must be a string (default kept)");
    }
    void boolean(const json& o, const char* key, const std::string& path, bool& dst) {
        if (!o.contains(key)) return;
        if (o[key].is_boolean())
            dst = o[key].get<bool>();
        else
            warn("'" + path + "' must be true or false (default kept)");
    }
    void number(const json& o, const char* key, const std::string& path, double& dst, double lo, double hi) {
        if (!o.contains(key)) return;
        if (!o[key].is_number()) {
            warn("'" + path + "' must be a number (default kept)");
            return;
        }
        double v = o[key].get<double>();
        if (v < lo || v > hi) {
            warn("'" + path + "' out of range [" + std::to_string(lo) + ", " + std::to_string(hi) + "] (default kept)");
            return;
        }
        dst = v;
    }
    void integer(const json& o, const char* key, const std::string& path, int& dst, int lo, int hi) {
        double v = dst;
        number(o, key, path, v, lo, hi);
        dst = static_cast<int>(v);
    }
    void level(const json& o, const char* key, const std::string& path, Log::Level& dst) {
        std::string s;
        if (!o.contains(key)) return;
        str(o, key, path, s);
        if (!s.empty() && !Log::ParseLevel(s, dst)) warn("'" + path + "' must be debug, info, warn, error or off (default kept)");
    }
    const json* section(const json& root, const char* key) {
        if (!root.contains(key)) return nullptr;
        if (!root[key].is_object()) {
            warn(std::string("'") + key + "' must be an object (ignored)");
            return nullptr;
        }
        return &root[key];
    }
};
} // namespace

bool Config::Parse(const std::string& text, Config& out, std::string& error) {
    json root = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (root.is_discarded() || !root.is_object()) {
        error = "config is not a JSON object";
        return false;
    }
    Config cfg; // parse into a copy, commit only on success
    Reader r{cfg};
    r.unknownKeys(root, "", {"dataDir", "window", "simulation", "paths", "log"});
    r.str(root, "dataDir", "dataDir", cfg.dataDir);
    if (const json* w = r.section(root, "window")) {
        r.unknownKeys(*w, "window.", {"width", "height", "vsync"});
        r.integer(*w, "width", "window.width", cfg.windowWidth, 320, 16384);
        r.integer(*w, "height", "window.height", cfg.windowHeight, 240, 16384);
        r.boolean(*w, "vsync", "window.vsync", cfg.vsync);
    }
    if (const json* s = r.section(root, "simulation")) {
        r.unknownKeys(*s, "simulation.", {"speedDaysPerSec", "warpToEvents"});
        r.number(*s, "speedDaysPerSec", "simulation.speedDaysPerSec", cfg.speedDaysPerSec, 0.0, 200.0);
        r.boolean(*s, "warpToEvents", "simulation.warpToEvents", cfg.warpToEvents);
    }
    if (const json* p = r.section(root, "paths")) {
        r.unknownKeys(*p, "paths.", {"telemetry", "recordings"});
        r.str(*p, "telemetry", "paths.telemetry", cfg.telemetryDir);
        r.str(*p, "recordings", "paths.recordings", cfg.recordingsDir);
    }
    if (const json* l = r.section(root, "log")) {
        r.unknownKeys(*l, "log.", {"console", "file", "fileLevel"});
        r.level(*l, "console", "log.console", cfg.consoleLogLevel);
        r.str(*l, "file", "log.file", cfg.logFile);
        r.level(*l, "fileLevel", "log.fileLevel", cfg.fileLogLevel);
    }
    out = cfg;
    return true;
}

bool Config::Load(const std::string& path, Config& out, std::string& error) {
    if (!std::filesystem::exists(path)) return false;
    std::ifstream in(path);
    if (!in) {
        error = "cannot read " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    if (!Parse(ss.str(), out, error)) {
        error = path + ": " + error + " - using defaults";
        return false;
    }
    return true;
}

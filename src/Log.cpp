#include "Log.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>

namespace Log {
namespace {
std::mutex g_mutex;
Level g_consoleLevel = Level::Info;
Level g_fileLevel = Level::Debug;
std::ofstream g_file;

const char* levelName(Level l) {
    switch (l) {
        case Level::Debug: return "debug";
        case Level::Info: return "info";
        case Level::Warn: return "warn";
        case Level::Error: return "error";
        case Level::Off: return "off";
    }
    return "info";
}

void appendEscaped(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", c);
                    out += b;
                } else
                    out += static_cast<char>(c);
        }
    }
    out += '"';
}

std::string utcNow() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}
} // namespace

void SetConsoleLevel(Level level) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_consoleLevel = level;
}
void SetFileLevel(Level level) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_fileLevel = level;
}

bool OpenFile(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_mutex);
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    if (g_file.is_open()) g_file.close();
    g_file.open(path, std::ios::app);
    return g_file.is_open();
}

void CloseFile() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_file.is_open()) g_file.close();
}

bool ParseLevel(const std::string& n, Level& out) {
    if (n == "debug")
        out = Level::Debug;
    else if (n == "info")
        out = Level::Info;
    else if (n == "warn" || n == "warning")
        out = Level::Warn;
    else if (n == "error")
        out = Level::Error;
    else if (n == "off")
        out = Level::Off;
    else
        return false;
    return true;
}

std::string FormatJson(Level level, const char* component, const std::string& message, std::initializer_list<Field> fields,
                       const std::string& timestamp) {
    std::string line = "{\"t\":";
    appendEscaped(line, timestamp);
    line += ",\"level\":";
    appendEscaped(line, levelName(level));
    line += ",\"component\":";
    appendEscaped(line, component);
    line += ",\"msg\":";
    appendEscaped(line, message);
    for (const Field& f : fields) {
        line += ',';
        appendEscaped(line, f.key);
        line += ':';
        if (!f.isNumber) {
            appendEscaped(line, f.text);
            continue;
        }
        if (!std::isfinite(f.number)) {
            line += "null";
            continue;
        }
        char b[40];
        if (f.number == std::floor(f.number) && std::fabs(f.number) < 1e15)
            std::snprintf(b, sizeof(b), "%.0f", f.number);
        else
            std::snprintf(b, sizeof(b), "%.17g", f.number);
        line += b;
    }
    line += '}';
    return line;
}

void Write(Level level, const char* component, const std::string& message, std::initializer_list<Field> fields) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (level >= g_consoleLevel && g_consoleLevel != Level::Off) {
        std::ostream& os = level >= Level::Warn ? std::cerr : std::cout;
        os << '[' << component << "] ";
        if (level == Level::Warn) os << "WARNING: ";
        if (level == Level::Error) os << "ERROR: ";
        os << message << '\n';
    }
    if (g_file.is_open() && level >= g_fileLevel && g_fileLevel != Level::Off) {
        g_file << FormatJson(level, component, message, fields, utcNow()) << '\n';
        g_file.flush();
    }
}

} // namespace Log

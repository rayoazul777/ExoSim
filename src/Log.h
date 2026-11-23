#pragma once
#include <initializer_list>
#include <sstream>
#include <string>

// Structured logging. Every message has a level, a component and optional
// typed fields. It goes to the console in readable form
// ("[catalog] Grouped 3,598 planets ...") and, once a log file is opened, to
// a JSON-lines file - one object per line with a UTC timestamp - that tools
// can parse:
//   {"t":"2026-11-23T15:54:31.162Z","level":"info","component":"catalog",
//    "msg":"Grouped ...","planets":3598,"systems":2472}
namespace Log {

enum class Level { Debug = 0, Info, Warn, Error, Off };

struct Field {
    std::string key;
    std::string text;
    double number = 0.0;
    bool isNumber = true;
    Field(const char* k, double v) : key(k), number(v) {}
    Field(const char* k, int v) : key(k), number(v) {}
    Field(const char* k, std::string v) : key(k), text(std::move(v)), isNumber(false) {}
    Field(const char* k, const char* v) : key(k), text(v), isNumber(false) {}
};

void SetConsoleLevel(Level level);
void SetFileLevel(Level level);
// Appends to `path` (directories created). Returns false if it can't be opened.
bool OpenFile(const std::string& path);
void CloseFile();
bool ParseLevel(const std::string& name, Level& out);

void Write(Level level, const char* component, const std::string& message, std::initializer_list<Field> fields = {});
inline void Debug(const char* c, const std::string& m, std::initializer_list<Field> f = {}) { Write(Level::Debug, c, m, f); }
inline void Info(const char* c, const std::string& m, std::initializer_list<Field> f = {}) { Write(Level::Info, c, m, f); }
inline void Warn(const char* c, const std::string& m, std::initializer_list<Field> f = {}) { Write(Level::Warn, c, m, f); }
inline void Error(const char* c, const std::string& m, std::initializer_list<Field> f = {}) { Write(Level::Error, c, m, f); }

// Concatenates anything streamable: Log::Info("x", Log::Cat("Loaded ", n, " stars")).
template <typename... T> std::string Cat(const T&... parts) {
    std::ostringstream os;
    (os << ... << parts);
    return os.str();
}

// The JSON line Write() would emit (without trailing newline) - exposed for tests.
std::string FormatJson(Level level, const char* component, const std::string& message, std::initializer_list<Field> fields,
                       const std::string& timestamp);

} // namespace Log

#include "sim/Formatting.h"
#include "MissionPlanner.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace Format {

std::string Utc(double daysSinceJ2000) {
    double unixDays = daysSinceJ2000 + 10957.5;
    double secOfDay = (unixDays - std::floor(unixDays)) * 86400.0;
    int s = static_cast<int>(secOfDay);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %02d:%02d:%02d", MissionPlanner::DayToCalendar(daysSinceJ2000).c_str(), s / 3600,
                  (s / 60) % 60, s % 60);
    return buf;
}

std::string Duration(double seconds) {
    char buf[64];
    double a = std::fabs(seconds);
    if (a < 120.0)
        std::snprintf(buf, sizeof(buf), "%.1f s", seconds);
    else if (a < 7200.0)
        std::snprintf(buf, sizeof(buf), "%.1f min", seconds / 60.0);
    else if (a < 3.0 * 86400.0)
        std::snprintf(buf, sizeof(buf), "%.1f h", seconds / 3600.0);
    else
        std::snprintf(buf, sizeof(buf), "%.1f days", seconds / 86400.0);
    return buf;
}

std::string Warp(double simSecPerRealSec) {
    char buf[64];
    if (simSecPerRealSec < 59.5)
        std::snprintf(buf, sizeof(buf), "%.1fx real time", simSecPerRealSec);
    else if (simSecPerRealSec < 3600.0)
        std::snprintf(buf, sizeof(buf), "%.0fx (%.1f min/s)", simSecPerRealSec, simSecPerRealSec / 60.0);
    else if (simSecPerRealSec < 86400.0)
        std::snprintf(buf, sizeof(buf), "%.1f hours/s", simSecPerRealSec / 3600.0);
    else
        std::snprintf(buf, sizeof(buf), "%.2f days/s", simSecPerRealSec / 86400.0);
    return buf;
}

std::string SafeFileName(const std::string& s) {
    std::string out;
    for (char c : s) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return out;
}

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != haystack.end();
}

float HashSeed(const std::string& s) {
    float seed = 0.0f;
    for (char ch : s) seed += static_cast<float>(static_cast<unsigned char>(ch));
    return seed;
}

} // namespace Format

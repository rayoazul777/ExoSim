#pragma once
#include <string>

// Text formatting for the mission-operations UI. Pure functions (no GL or
// ImGui), unit-tested in ExoSimTests.
namespace Format {
// "2027-07-19 12:30:50" from days since J2000 (UTC, ignoring leap seconds).
std::string Utc(double daysSinceJ2000);
// "12.3 s" / "4.5 min" / "2.0 h" / "7.9 days", picking the unit by size.
std::string Duration(double seconds);
// Time-warp label from simulation seconds per real second.
std::string Warp(double simSecPerRealSec);
// Letters and digits kept, everything else replaced with '_'.
std::string SafeFileName(const std::string& s);
bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle);
// Deterministic per-name seed for procedural visuals (sum of byte values).
float HashSeed(const std::string& s);
} // namespace Format

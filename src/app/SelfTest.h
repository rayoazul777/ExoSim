#pragma once
#include <string>

// Headless check that a packaged build is complete and loads correctly.
// Runs before any window or GL context exists, so it works on CI machines
// with no GPU (the release workflow runs it on every package, from another
// working directory, to prove path resolution too).
int RunSelfTest(const std::string& version);

// Headless replay verification: loads data/, replays the recording and
// compares the final state hash with the one recorded. Exit code 0 = match.
int RunReplayVerify(const std::string& path, const std::string& dataDir = "data");

#pragma once
#include <filesystem>

// Directory containing the running executable, or an empty path if it
// can't be determined. ExoSim changes its working directory to this at
// startup so shaders/ and data/ load correctly however the exe is
// launched (double-click, shortcut, or from another folder's terminal).
std::filesystem::path ExecutableDirectory();

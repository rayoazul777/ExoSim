cmake_minimum_required(VERSION 3.20)
project(ExoSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies (fetched via vcpkg manifest mode - see vcpkg.json) ---
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(CURL REQUIRED)

add_executable(ExoSim
    src/main.cpp
    src/KeplerOrbit.cpp
    src/Dataset.cpp
    src/Renderer.cpp
    src/CrtPostProcess.cpp
    src/PlanetClassifier.cpp
    src/StarClassifier.cpp
    src/SolarSystemAppearance.cpp
    src/MissionPlanner.cpp
)

target_include_directories(ExoSim PRIVATE src)

target_link_libraries(ExoSim PRIVATE
    glfw
    glad::glad
    glm::glm
    imgui::imgui
    nlohmann_json::nlohmann_json
    OpenGL::GL
)

# Copy data + shader assets next to the built exe so relative paths work
add_custom_command(TARGET ExoSim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:ExoSim>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:ExoSim>/shaders
)

# --- Optional C++ data-fetch tool (see tools/fetch_data.cpp) ---
# Not required for ExoSim itself - data/fetch_exoplanets.py does the same
# job and ExoSim never invokes either one. This is here for anyone who'd
# rather keep the whole toolchain in C++.
add_executable(FetchData tools/fetch_data.cpp)
target_link_libraries(FetchData PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

# Run from the build output directory (so its default "data" relative
# path lands next to ExoSim.exe): FetchData.exe --out-dir data
add_custom_command(TARGET FetchData POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:FetchData>/data
)

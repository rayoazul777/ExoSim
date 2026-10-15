cmake_minimum_required(VERSION 3.21)
project(ExoSim VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# EXOSIM_BUILD_APP=OFF builds only the validation suite, which needs just
# GLM + nlohmann/json (no GPU, GLFW, ImGui or curl). CI uses this on Linux.
option(EXOSIM_BUILD_APP "Build ExoSim and FetchData (needs GLFW, ImGui, glad, OpenGL, curl)" ON)

# Portable Windows builds: with a static vcpkg triplet (x64-windows-static),
# also link the MSVC runtime statically, so the exe runs on a clean machine
# with no DLLs and no "Visual C++ Redistributable" install. The runtime
# setting MUST match the static libraries, or linking fails (LNK2038).
if(MSVC AND VCPKG_TARGET_TRIPLET MATCHES "-static$")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# --- Validation suite (no GPU needed) - see docs/VALIDATION.md ---
# Build + run:  ctest --output-on-failure   (ExoSimTests --markdown prints
# the results table used in the docs)
enable_testing()
add_executable(ExoSimTests
    tests/test_physics.cpp
    src/KeplerOrbit.cpp
    src/MissionPlanner.cpp
    src/Dataset.cpp
    src/PlanetClassifier.cpp
)
target_include_directories(ExoSimTests PRIVATE src)
target_link_libraries(ExoSimTests PRIVATE glm::glm nlohmann_json::nlohmann_json)
add_test(NAME physics_and_data_validation COMMAND ExoSimTests)

if(EXOSIM_BUILD_APP)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(glad CONFIG REQUIRED)
    find_package(CURL REQUIRED)

    add_executable(ExoSim
        src/main.cpp
        src/Platform.cpp
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
    target_compile_definitions(ExoSim PRIVATE EXOSIM_VERSION="${PROJECT_VERSION}")
    target_link_libraries(ExoSim PRIVATE
        glfw
        glad::glad
        glm::glm
        imgui::imgui
        nlohmann_json::nlohmann_json
        OpenGL::GL
    )

    # Copy data + shaders next to the built exe for running from the IDE.
    add_custom_command(TARGET ExoSim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:ExoSim>/data
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:ExoSim>/shaders
    )

    # Headless smoke test of a built tree (packaging runs it too).
    add_test(NAME app_selftest COMMAND ExoSim --selftest)

    # --- Optional C++ data-fetch tool (see tools/fetch_data.cpp) ---
    add_executable(FetchData tools/fetch_data.cpp)
    target_link_libraries(FetchData PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)
    add_custom_command(TARGET FetchData POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:FetchData>/data
    )

    # --- Release package layout (cmake --install / cpack) ---
    #   ExoSim-<ver>-win64/
    #     ExoSim.exe  FetchData.exe  README.txt  LICENSE  CHANGELOG.md  DATA_SOURCES.md
    #     shaders/  data/*.json
    install(TARGETS ExoSim FetchData RUNTIME DESTINATION .)
    install(DIRECTORY shaders/ DESTINATION shaders)
    install(DIRECTORY data/ DESTINATION data FILES_MATCHING PATTERN "*.json" PATTERN "FETCHED_AT.txt"
            PATTERN "__pycache__" EXCLUDE)
    install(FILES packaging/README.txt LICENSE CHANGELOG.md docs/DATA_SOURCES.md DESTINATION .)

    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_NAME ExoSim)
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
    if(WIN32)
        set(CPACK_SYSTEM_NAME win64)
    else()
        set(CPACK_SYSTEM_NAME ${CMAKE_SYSTEM_NAME})
    endif()
    set(CPACK_PACKAGE_FILE_NAME "ExoSim-${PROJECT_VERSION}-${CPACK_SYSTEM_NAME}")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    include(CPack)
endif()

#include "app/SelfTest.h"
#include "Dataset.h"
#include "MissionPlanner.h"
#include "sim/Replay.h"
#include "sim/SimulationState.h"

#include <cstdio>
#include <filesystem>
#include <vector>

int RunSelfTest(const std::string& version) {
    int failures = 0;
    auto report = [&](bool ok, bool required, const std::string& what) {
        std::printf("[%s] %s\n", ok ? "OK  " : (required ? "FAIL" : "INFO"), what.c_str());
        if (!ok && required) ++failures;
    };
    std::printf("ExoSim %s self-test\nWorking directory: %s\n", version.c_str(),
                std::filesystem::current_path().string().c_str());

    report(std::filesystem::exists("shaders/crt.frag"), true, "shaders/crt.frag present");

    std::vector<PlanetRecord> planets;
    bool loaded = Dataset::LoadFromFile("data/exoplanets.json", planets) && !planets.empty();
    report(loaded, true, "data/exoplanets.json loads: " + std::to_string(planets.size()) + " drawable planets");

    std::vector<PlanetRecord> dwarfs;
    Dataset::LoadDwarfPlanets("data/dwarf_planets.json", dwarfs);
    report(!dwarfs.empty(), false, "data/dwarf_planets.json: " + std::to_string(dwarfs.size()) + " dwarf planets");
    auto stars = Dataset::LoadStarCatalog("data/star_catalog.json");
    report(!stars.empty(), false, "data/star_catalog.json: " + std::to_string(stars.size()) + " stars");
    report(std::filesystem::exists("data/stellar_hosts.json"), false, "data/stellar_hosts.json present");
    report(std::filesystem::exists("data/double_stars.json"), false, "data/double_stars.json present");

    // End-to-end core check: plan the Mars mission from Sol's built-in data.
    auto sol = Dataset::DemoSolarSystem();
    const OrbitalElements* earth = nullptr;
    const OrbitalElements* mars = nullptr;
    for (const auto& p : sol) {
        if (p.name == "Earth") earth = &p.elements;
        if (p.name == "Mars") mars = &p.elements;
    }
    MissionPlan plan =
        (earth && mars) ? MissionPlanner::PlanHohmann(*earth, *mars, MissionPlanner::NowDaysSinceJ2000(), "Mars") : MissionPlan{};
    report(plan.valid, true,
           plan.valid ? "Mission planner: next Mars window " + MissionPlanner::DayToCalendar(plan.launchDay)
                      : std::string("Mission planner: no Mars window found"));

    std::printf("Self-test %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

int RunReplayVerify(const std::string& path, const std::string& dataDir) {
    ReplayFile file;
    std::string err;
    if (!ReplayFile::load(path, file, err)) {
        std::printf("Replay: cannot load %s: %s\n", path.c_str(), err.c_str());
        return 2;
    }
    if (!file.hasEnd) {
        std::printf("Replay: %s has no end record (recording not finished?)\n", path.c_str());
        return 2;
    }
    SimulationState sim(Catalog::Load(dataDir), file.startDay);
    if (file.planets != sim.planets().size() || file.systems != sim.systems().size())
        std::printf("Replay: WARNING catalog differs from the recording's (%llu/%llu planets/systems recorded, %zu/%zu now)\n",
                    static_cast<unsigned long long>(file.planets), static_cast<unsigned long long>(file.systems),
                    sim.planets().size(), sim.systems().size());
    ReplayPlayer player(file);
    if (!player.runToEnd(sim, err)) {
        std::printf("Replay: %s\n", err.c_str());
        return 1;
    }
    bool ok = sim.hash() == file.endHash && sim.clock().ticks == file.endTicks;
    std::printf("Replay: %zu commands, %llu ticks, final day %.9f (%s)\nRecorded hash %016llx, replayed %016llx: %s\n",
                file.commands.size(), static_cast<unsigned long long>(sim.clock().ticks), sim.day(),
                MissionPlanner::DayToCalendar(sim.day()).c_str(), static_cast<unsigned long long>(file.endHash),
                static_cast<unsigned long long>(sim.hash()), ok ? "MATCH" : "MISMATCH");
    return ok ? 0 : 1;
}

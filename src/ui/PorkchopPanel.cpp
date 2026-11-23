#include "ui/Panels.h"
#include "app/AppContext.h"
#include "GravityAssist.h"
#include "MissionPlanner.h"
#include "Porkchop.h"
#include "sim/Formatting.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// Perceptual ramp (viridis-like): low cost = bright yellow, high = deep blue.
ImU32 RampColor(double t) {
    static const float a[5][3] = {
        {0.99f, 0.91f, 0.14f}, {0.37f, 0.79f, 0.38f}, {0.13f, 0.57f, 0.55f}, {0.23f, 0.32f, 0.55f}, {0.27f, 0.00f, 0.33f}};
    t = std::clamp(t, 0.0, 1.0) * 4.0;
    int i = std::min(static_cast<int>(t), 3);
    float u = static_cast<float>(t - i);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(a[i][0] + (a[i + 1][0] - a[i][0]) * u, a[i][1] + (a[i + 1][1] - a[i][1]) * u,
                                                 a[i][2] + (a[i + 1][2] - a[i][2]) * u, 1.0f));
}

const char* kMetricNames[] = {"Launch energy C3 (km2/s2)", "Departure burn from LEO (km/s)", "Arrival v-infinity (km/s)",
                              "Total: departure burn + arrival v-inf (km/s)"};

void ComputeGrid(AppContext& ctx, int targetChoice) {
    SimulationState& sim = ctx.sim;
    const MissionTarget& mt = sim.missionTargets()[targetChoice];
    PorkchopView& pv = ctx.view.porkchop;
    const auto& planets = sim.planets();
    const OrbitalElements& earth = planets[sim.earthIndex()].elements;
    const OrbitalElements& target = planets[mt.planetIdx].elements;
    // Centre on the window that's open now (search from a quarter synodic
    // period back), so a window already in progress isn't cut in half.
    MissionPlan h0 = MissionPlanner::PlanHohmann(earth, target, sim.day(), planets[mt.planetIdx].name);
    MissionPlan h =
        h0.valid ? MissionPlanner::PlanHohmann(earth, target, sim.day() - 0.25 * h0.synodicPeriodDays, planets[mt.planetIdx].name)
                 : h0;
    if (!h.valid || h.launchDay + 0.3 * h.synodicPeriodDays < sim.day()) h = h0;
    if (!h.valid) {
        pv.grid = PorkchopGrid{};
        return;
    }
    const double halfWidth = std::clamp(0.3 * h.synodicPeriodDays, 30.0, 250.0);
    pv.grid = Porkchop::Compute(KeplerOrbit(earth), KeplerOrbit(target), h.launchDay - halfWidth, h.launchDay + halfWidth, 100,
                                0.45 * h.transferDays, 1.6 * h.transferDays, 70);
    pv.target = targetChoice;
    pv.hohmannLaunch = h.launchDay;
    pv.hohmannTof = h.transferDays;
}

// Marching squares on one grid cell: adds iso-line segments at `level`.
void ContourCell(ImDrawList* dl, const double v[4], const ImVec2 p[4], double level, ImU32 col) {
    // corners: 0=(i,j) 1=(i+1,j) 2=(i+1,j+1) 3=(i,j+1)
    ImVec2 pts[4];
    int n = 0;
    for (int e = 0; e < 4; ++e) {
        int a = e, b = (e + 1) % 4;
        if (!std::isfinite(v[a]) || !std::isfinite(v[b])) return;
        if ((v[a] < level) != (v[b] < level)) {
            float t = static_cast<float>((level - v[a]) / (v[b] - v[a]));
            pts[n++] = ImVec2(p[a].x + (p[b].x - p[a].x) * t, p[a].y + (p[b].y - p[a].y) * t);
        }
    }
    if (n >= 2) dl->AddLine(pts[0], pts[1], col, 1.0f);
    if (n == 4) dl->AddLine(pts[2], pts[3], col, 1.0f);
}

} // namespace

// Used by the --porkchop startup option.
void ComputePorkchopFor(AppContext& ctx) { ComputeGrid(ctx, ctx.sim.targetChoice()); }

void DrawPorkchop(AppContext& ctx) {
    PorkchopView& pv = ctx.view.porkchop;
    if (!pv.open) return;
    SimulationState& sim = ctx.sim;
    ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Porkchop Plot", &pv.open)) {
        ImGui::End();
        return;
    }

    const int choice = sim.targetChoice();
    const MissionTarget& mt = sim.missionTargets()[choice];
    ImGui::Text("Earth -> %s", mt.label.c_str());
    if (mt.isEarthMoon) {
        ImGui::TextDisabled("Earth-Moon transfers are geocentric - no heliocentric porkchop.");
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button(pv.target == choice && !pv.grid.cells.empty() ? "Recompute from sim date" : "Compute"))
        ComputeGrid(ctx, choice);
    ImGui::SetNextItemWidth(330);
    ImGui::Combo("Color", &pv.metric, kMetricNames, 4);
    if (pv.target != choice || pv.grid.cells.empty()) {
        ImGui::TextWrapped("Computes ~7,000 Lambert transfers around the next launch window: every launch date (x) and "
                           "time of flight (y), with each planet at its real position on those dates.");
        ImGui::End();
        return;
    }

    const PorkchopGrid& g = pv.grid;
    const PorkchopMetric metric = static_cast<PorkchopMetric>(pv.metric);
    // Color scale: from the minimum up to a cap well above it, so the valleys
    // (the useful region) get the color resolution.
    std::vector<double> vals;
    for (const auto& c : g.cells)
        if (c.ok && std::fabs(c.transferAngleDeg - 180.0) > 2.0) vals.push_back(PorkchopGrid::Value(c, metric));
    if (vals.empty()) {
        ImGui::TextDisabled("No Lambert solutions in this window.");
        ImGui::End();
        return;
    }
    std::sort(vals.begin(), vals.end());
    const double vMin = vals.front();
    const double vCap = vals[vals.size() / 2];

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float axisL = 52.0f, axisB = 34.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 plot0(origin.x + axisL, origin.y + 4.0f);
    const float W = std::max(200.0f, avail.x - axisL - 8.0f), H = std::max(160.0f, avail.y - axisB - 70.0f);
    const ImVec2 plot1(plot0.x + W, plot0.y + H);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cw = W / g.nLaunch, ch = H / g.nTof;
    auto cellPos = [&](double i, double j) {
        return ImVec2(plot0.x + static_cast<float>(i) * cw, plot1.y - static_cast<float>(j) * ch);
    };

    for (int j = 0; j < g.nTof; ++j)
        for (int i = 0; i < g.nLaunch; ++i) {
            const TransferPoint& c = g.at(i, j);
            double v = PorkchopGrid::Value(c, metric);
            ImU32 col = !std::isfinite(v) || std::fabs(c.transferAngleDeg - 180.0) <= 2.0
                            ? IM_COL32(30, 30, 34, 255)
                            : (v > vCap ? IM_COL32(40, 20, 55, 255) : RampColor((v - vMin) / std::max(vCap - vMin, 1e-9)));
            ImVec2 a = cellPos(i, j + 1), b = cellPos(i + 1, j);
            dl->AddRectFilled(a, ImVec2(b.x + 0.5f, b.y + 0.5f), col);
        }
    // Contours at 8 evenly spaced levels between the minimum and the cap.
    for (int k = 1; k <= 7; ++k) {
        double level = vMin + (vCap - vMin) * k / 8.0;
        for (int j = 0; j + 1 < g.nTof; ++j)
            for (int i = 0; i + 1 < g.nLaunch; ++i) {
                const TransferPoint* q[4] = {&g.at(i, j), &g.at(i + 1, j), &g.at(i + 1, j + 1), &g.at(i, j + 1)};
                double v[4];
                for (int n = 0; n < 4; ++n)
                    v[n] = std::fabs(q[n]->transferAngleDeg - 180.0) <= 2.0 ? NAN : PorkchopGrid::Value(*q[n], metric);
                ImVec2 p[4] = {cellPos(i + 0.5, j + 0.5), cellPos(i + 1.5, j + 0.5), cellPos(i + 1.5, j + 1.5),
                               cellPos(i + 0.5, j + 1.5)};
                ContourCell(dl, v, p, level, IM_COL32(255, 255, 255, 90));
            }
    }
    dl->AddRect(plot0, plot1, IM_COL32(150, 150, 160, 255));

    // Axes: launch date (x) and time of flight (y).
    char buf[64];
    for (int t = 0; t <= 4; ++t) {
        double day = g.launch0 + (g.nLaunch - 1) * g.launchStep * t / 4.0;
        float x = plot0.x + W * t / 4.0f;
        dl->AddLine(ImVec2(x, plot1.y), ImVec2(x, plot1.y + 4), IM_COL32(150, 150, 160, 255));
        std::string d = MissionPlanner::DayToCalendar(day);
        dl->AddText(ImVec2(x - 34.0f, plot1.y + 6), IM_COL32(200, 200, 210, 255), d.c_str());
        double tof = g.tof0 + (g.nTof - 1) * g.tofStep * t / 4.0;
        float y = plot1.y - H * t / 4.0f;
        std::snprintf(buf, sizeof(buf), "%.0f d", tof);
        dl->AddText(ImVec2(origin.x, y - 7), IM_COL32(200, 200, 210, 255), buf);
    }
    dl->AddText(ImVec2(plot0.x + W * 0.5f - 40, plot1.y + 20), IM_COL32(160, 160, 170, 255), "launch date");

    // Markers: the grid optimum, the Hohmann idealization, the active plan.
    auto markAt = [&](double launch, double tof) {
        return ImVec2(plot0.x + static_cast<float>((launch - g.launch0) / g.launchStep + 0.5) * cw,
                      plot1.y - static_cast<float>((tof - g.tof0) / g.tofStep + 0.5) * ch);
    };
    if (g.bestIndex >= 0) {
        const TransferPoint& b = g.cells[static_cast<size_t>(g.bestIndex)];
        dl->AddCircle(markAt(b.launchDay, b.tofDays), 6.0f, IM_COL32(60, 255, 120, 255), 16, 2.0f);
    }
    ImVec2 hm = markAt(pv.hohmannLaunch, pv.hohmannTof);
    dl->AddRect(ImVec2(hm.x - 4, hm.y - 4), ImVec2(hm.x + 4, hm.y + 4), IM_COL32(220, 220, 220, 255));
    if (sim.missionActive() && sim.activeTarget().body == mt.body) {
        ImVec2 m = markAt(sim.mission().launchDay, sim.mission().transferDays);
        dl->AddLine(ImVec2(m.x - 6, m.y - 6), ImVec2(m.x + 6, m.y + 6), IM_COL32(255, 160, 40, 255), 2.0f);
        dl->AddLine(ImVec2(m.x - 6, m.y + 6), ImVec2(m.x + 6, m.y - 6), IM_COL32(255, 160, 40, 255), 2.0f);
    }

    // Hover + click.
    ImGui::SetCursorScreenPos(plot0);
    ImGui::InvisibleButton("##porkchop", ImVec2(W, H));
    if (ImGui::IsItemHovered() && !ctx.screenshotMode) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        int i = std::clamp(static_cast<int>((mp.x - plot0.x) / cw), 0, g.nLaunch - 1);
        int j = std::clamp(static_cast<int>((plot1.y - mp.y) / ch), 0, g.nTof - 1);
        const TransferPoint& c = g.at(i, j);
        ImGui::BeginTooltip();
        ImGui::Text("Launch %s   Arrive %s", MissionPlanner::DayToCalendar(c.launchDay).c_str(),
                    MissionPlanner::DayToCalendar(c.launchDay + c.tofDays).c_str());
        ImGui::Text("Time of flight %.0f days   Transfer angle %.0f deg", c.tofDays, c.transferAngleDeg);
        if (c.ok) {
            ImGui::Text("C3 %.1f km2/s2   v-inf out %.2f km/s", c.c3, c.vInfDepartureKms);
            ImGui::Text("Burn from LEO %.2f km/s   v-inf at arrival %.2f km/s", c.dvFromLeoKms, c.vInfArrivalKms);
        }
        if (std::fabs(c.transferAngleDeg - 180.0) <= 2.0) ImGui::TextDisabled("Near 180 deg: transfer plane ill-defined");
        ImGui::TextDisabled("Click to fly this trajectory");
        ImGui::EndTooltip();
        if (ImGui::IsItemClicked() && c.ok) sim.apply(Command::Transfer(choice, c.launchDay, c.tofDays));
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, plot1.y + axisB));
    if (g.bestIndex >= 0) {
        const TransferPoint& b = g.cells[static_cast<size_t>(g.bestIndex)];
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Optimum (circle): launch %s, %.0f days, C3 %.1f, total %.2f km/s",
                           MissionPlanner::DayToCalendar(b.launchDay).c_str(), b.tofDays, b.c3, b.total());
    }
    ImGui::TextDisabled("Square: Hohmann idealization.  Orange X: the planned mission.  Gray: no solution / near 180 deg.");
    ImGui::TextDisabled("Two-body Lambert transfers between real planet positions; single revolution.");
    ImGui::End();
}

void DrawFlybyPlanner(AppContext& ctx) {
    if (!ctx.view.flybyOpen) return;
    SimulationState& sim = ctx.sim;
    ImGui::SetNextWindowSize(ImVec2(780, 470), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Gravity Assists", &ctx.view.flybyOpen)) {
        ImGui::End();
        return;
    }
    const auto& seqs = GravityAssist::Sequences();
    int& sel = ctx.view.flybySequence;
    if (ImGui::BeginCombo("Flyby chain", seqs[sel].name.c_str())) {
        for (int i = 0; i < static_cast<int>(seqs.size()); ++i)
            if (ImGui::Selectable(seqs[i].name.c_str(), i == sel)) sel = i;
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s", seqs[sel].note.c_str());
    ImGui::TextDisabled("Searches launches over the next %.0f years (trip capped at %.1f years).", seqs[sel].searchYears,
                        seqs[sel].maxYears);
    if (ImGui::Button("Find the best trajectory from the sim date")) sim.apply(Command::Flyby(sel, sim.day()));
    ImGui::SameLine();
    ImGui::TextDisabled("(may take a second)");

    const FlybyPlan* fp = sim.flyby();
    if (!fp) {
        ImGui::Separator();
        ImGui::TextWrapped("Patched conics: Lambert legs between the planets' real positions, joined at each flyby. "
                           "A flyby bends the spacecraft's path for free up to a limit set by the closest safe pass; "
                           "anything more needs a burn at closest approach, shown as 'powered dv'.");
        ImGui::End();
        return;
    }
    ImGui::Separator();
    ImGui::Text("%s", fp->sequenceName.c_str());
    ImGui::Text("Launch %s   C3 %.1f km2/s2   burn from LEO %.2f km/s",
                MissionPlanner::DayToCalendar(fp->encounterDays.front()).c_str(), fp->c3, fp->dvFromLeoKms);
    ImGui::Text("Arrive %s after %.1f years (direct Hohmann: %.1f years)",
                MissionPlanner::DayToCalendar(fp->encounterDays.back()).c_str(), fp->totalDays / 365.25,
                fp->directHohmannDays / 365.25);
    ImGui::Text("Arrival v-infinity %.2f km/s   total powered flyby dv %.0f m/s", fp->vInfArrivalKms,
                fp->totalPoweredDvKms * 1000.0);
    if (ImGui::BeginTable("flybys", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        for (const char* h :
             {"Body", "Closest approach", "Altitude km", "v-inf in/out km/s", "Turn deg (max)", "Powered dv", "SOI"})
            ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const FlybyEncounter& f : fp->flybys) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(f.body.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(MissionPlanner::DayToCalendar(f.day).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", f.periapsisAltitudeKm);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f / %.2f", f.vInfInKms, f.vInfOutKms);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f (%.1f)", f.turnAngleDeg, f.maxTurnDeg);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f m/s", f.poweredDvKms * 1000.0);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f Mkm, %.0f d", f.soiRadiusKm / 1e6, f.soiExitDay - f.soiEntryDay);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("The sequence of events in the Flight Director lists SOI entry/exit for each flyby.");
    ImGui::End();
}

void DrawSessionControls(AppContext& ctx) {
    SimulationState& sim = ctx.sim;
    if (ctx.replaying()) {
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(ctx.width) * 0.5f - 170.0f, 8.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Always);
        ImGui::Begin("Replay", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        bool done = ctx.replayPlayer->finished(sim);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), done ? "REPLAY COMPLETE" : "REPLAYING RECORDED SESSION");
        ImGui::ProgressBar(static_cast<float>(ctx.replayPlayer->progress(sim)));
        if (done) {
            bool match = ctx.replayFile->hasEnd && sim.hash() == ctx.replayFile->endHash;
            ImGui::TextColored(match ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               match ? "Final state matches the recording" : "Final state differs from the recording");
        }
        if (ImGui::Button("Take control")) {
            ctx.replayPlayer.reset();
            ctx.replayFile.reset();
        }
        ImGui::End();
        return;
    }
    if (ctx.view.mode != ViewMode::System) return;
    // Recording controls live at the bottom of the control panel's column.
    ImGui::Begin("ExoSim Control");
    ImGui::Separator();
    if (!sim.recording()) {
        if (ImGui::Button("Record session")) {
            ctx.recordingStartDay = sim.day();
            sim.startRecording();
            ctx.view.sessionStatus.clear();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(inputs only - replay with --replay)");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC  %zu commands, %llu ticks", sim.recorded().size(),
                           static_cast<unsigned long long>(sim.recordingTicks()));
        ImGui::SameLine();
        if (ImGui::Button("Stop & save")) {
            ReplayFile f = ReplayFile::FromRecording(sim, ctx.recordingStartDay, ctx.version);
            sim.stopRecording();
            char name[96];
            std::snprintf(name, sizeof(name), "/session-%s.jsonl", Format::SafeFileName(Format::Utc(sim.day())).c_str());
            std::string path = ctx.recordingsDir + name;
            ctx.view.sessionStatus = f.save(path) ? "Saved " + path : "Could not write " + path;
        }
    }
    if (!ctx.view.sessionStatus.empty()) ImGui::TextWrapped("%s", ctx.view.sessionStatus.c_str());
    ImGui::End();
}

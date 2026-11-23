#pragma once

struct AppContext;

// ImGui windows. Each reads the simulation through ctx.sim and changes it
// only via ctx.sim.apply(Command); view/camera changes go to ctx directly.
void DrawControlPanel(AppContext& ctx); // "ExoSim Control": time, sky, systems, planets
void DrawMissionPlanner(AppContext& ctx); // Sol only
void DrawFlightDirector(AppContext& ctx); // mission clock, sequence of events, telemetry
void DrawLandingTelemetry(AppContext& ctx); // Landing view
void DrawPlanetDetail(AppContext& ctx); // Detail view, planet
void DrawMoonDetail(AppContext& ctx); // Detail view, moon
void DrawPorkchop(AppContext& ctx); // launch/arrival transfer-cost map (Lambert grid)
void DrawFlybyPlanner(AppContext& ctx); // gravity-assist chains
void DrawSessionControls(AppContext& ctx); // record / replay status
void ComputePorkchopFor(AppContext& ctx); // fills the porkchop grid for the chosen target
void DrawGalaxyPanel(AppContext& ctx); // Milky Way map: layers, search, selected host
void OpenGalaxyMap(AppContext& ctx); // switch to the map, selecting the current system's host

#pragma once

struct AppContext;

// Planet or moon close-up: procedural surface, targeting grid, rings, and
// moons on their real (tilted, eccentric) orbits, distance-compressed.
void RenderDetailScene(AppContext& ctx);

// Chase camera on the descending lander (see LandingSimulator): surface,
// terrain grid, landing target, flown/remaining trajectory, vehicle glyph.
void RenderLandingScene(AppContext& ctx);

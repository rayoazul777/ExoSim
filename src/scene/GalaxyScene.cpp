#include "scene/GalaxyScene.h"
#include "app/AppContext.h"
#include "Renderer.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kSlotModel = 0, kSlotHosts = 1, kSlotLocal = 2;
constexpr std::uint32_t kModelSeed = 0x4D57u; // "MW": fixed, so every run draws the same Galaxy
constexpr int kModelPoints = 90000;

float smoothstep(double e0, double e1, double x) {
    double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

// World (pc) -> ImGui screen coordinates; false if behind the camera or off-screen by a margin.
bool project(const glm::dmat4& vp, const glm::dvec3& p, ImVec2& out) {
    glm::dvec4 c = vp * glm::dvec4(p, 1.0);
    if (c.w <= 1e-9) return false;
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    double nx = c.x / c.w, ny = c.y / c.w;
    if (std::fabs(nx) > 1.2 || std::fabs(ny) > 1.2) return false;
    out = ImVec2(static_cast<float>((nx * 0.5 + 0.5) * size.x), static_cast<float>((0.5 - ny * 0.5) * size.y));
    return true;
}

// "100 pc / 326 ly", "10 kpc / 33k ly" (1 pc = 3.26156 ly).
std::string distanceLabel(double pc) {
    char buf[64];
    const double ly = pc * 3.26156;
    if (pc >= 1000.0)
        std::snprintf(buf, sizeof buf, "%g kpc / %.0fk ly", pc / 1000.0, ly / 1000.0);
    else
        std::snprintf(buf, sizeof buf, "%g pc / %.0f ly", pc, ly);
    return buf;
}

void drawText(ImDrawList* dl, ImVec2 p, ImU32 col, const std::string& text) {
    dl->AddText(ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, 200), text.c_str());
    dl->AddText(p, col, text.c_str());
}
} // namespace

float GalaxyScene::ModelAlpha(double d) { return smoothstep(250.0, 2500.0, d); }
float GalaxyScene::LocalStarAlpha(double d) { return 1.0f - smoothstep(600.0, 5000.0, d); }

int GalaxyScene::hostForSystem(const std::string& hostName) const {
    for (size_t i = 0; i < m_hosts.size(); ++i)
        if (m_hosts[i].name == hostName) return static_cast<int>(i);
    return -1;
}

void GalaxyScene::ensureBuilt(AppContext& ctx) {
    if (m_built) return;
    m_built = true;
    Renderer& r = ctx.renderer;

    std::vector<glm::vec3> pos;
    std::vector<glm::vec4> col;
    std::vector<float> size;
    for (const auto& p : Galaxy::GenerateModel(kModelPoints, kModelSeed)) {
        pos.push_back(p.posPc);
        col.push_back(p.rgba);
        size.push_back(p.sizePx);
    }
    r.uploadPointCloud(kSlotModel, pos, col, size);

    m_hosts = Galaxy::LoadHosts(ctx.dataDir + "/exoplanets.json", ctx.dataDir + "/stellar_hosts.json", &m_hostStats);
    pos.clear();
    col.clear();
    size.clear();
    for (auto& h : m_hosts) {
        h.systemIndex = ctx.sim.catalog().findSystem(h.name);
        pos.emplace_back(h.posPc);
        glm::vec3 c = Galaxy::TeffToColor(h.teffK);
        col.emplace_back(glm::mix(c, glm::vec3(1.0f), 0.25f), 0.95f);
        size.push_back(3.0f + 1.0f * static_cast<float>(std::min(h.planets - 1, 4)));
    }
    r.uploadPointCloud(kSlotHosts, pos, col, size);

    pos.clear();
    col.clear();
    size.clear();
    for (const auto& s : Galaxy::LocalStars(ctx.sim.catalog().stars)) {
        pos.emplace_back(s.posPc);
        col.emplace_back(s.color, 0.8f);
        size.push_back(std::clamp(4.0f - 0.35f * static_cast<float>(s.vmag), 1.2f, 4.5f));
    }
    m_localCount = static_cast<int>(pos.size());
    r.uploadPointCloud(kSlotLocal, pos, col, size);

    for (const auto& arm : Galaxy::Arms()) {
        std::vector<glm::vec3> line;
        for (const auto& p : Galaxy::ArmCenterline(arm, arm.spur ? 24 : 160)) line.emplace_back(p);
        m_arms.push_back(std::move(line));
    }
}

void GalaxyScene::render(AppContext& ctx) {
    ensureBuilt(ctx);
    GalaxyView& g = ctx.view.galaxy;
    const double aspect = static_cast<double>(ctx.width) / std::max(ctx.height, 1);
    m_viewProj = g.viewProj(aspect);
    const glm::mat4 vp(m_viewProj);
    Renderer& r = ctx.renderer;

    const float modelA = g.showModel ? ModelAlpha(g.distance) : 0.0f;
    // Points stay a few pixels wide at every zoom; zoomed far out the cloud
    // is dense, so each point is dimmer.
    const float overview = smoothstep(8000.0, 60000.0, g.distance);
    r.renderPointCloud(kSlotModel, vp, 1.0f - 0.25f * overview, modelA * (1.0f - 0.45f * overview), true);

    if (g.showArms) {
        const float armA = smoothstep(600.0, 4000.0, g.distance) * 0.35f;
        for (size_t i = 0; i < m_arms.size(); ++i)
            r.renderLineStrip(vp, m_arms[i], i + 1 == m_arms.size() ? glm::vec3(0.9f, 0.8f, 0.5f) : glm::vec3(0.5f, 0.7f, 1.0f),
                              armA);
    }

    if (g.showRings) {
        for (double rad : {10.0, 100.0, 1000.0, 10000.0}) {
            if (rad < g.distance * 0.04 || rad > g.distance * 3.0) continue;
            std::vector<glm::vec3> ring;
            for (int k = 0; k <= 128; ++k) {
                double a = 2.0 * kPi * k / 128.0;
                ring.emplace_back(static_cast<float>(rad * std::cos(a)), static_cast<float>(rad * std::sin(a)), 0.0f);
            }
            r.renderLineStrip(vp, ring, glm::vec3(0.35f, 1.0f, 0.55f), 0.30f);
        }
    }

    if (g.showLocal) r.renderPointCloud(kSlotLocal, vp, 1.0f, LocalStarAlpha(g.distance) * 0.85f, true);
    if (g.showHosts) r.renderPointCloud(kSlotHosts, vp, g.distance > 20000.0 ? 0.8f : 1.0f, 1.0f, false);
}

void GalaxyScene::drawOverlay(AppContext& ctx) {
    GalaxyView& g = ctx.view.galaxy;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const glm::dmat4& vp = m_viewProj;
    ImVec2 s;

    // Hover / click picking: nearest host within 9 px of the mouse.
    g.hovered = -1;
    if (g.showHosts && !io.WantCaptureMouse && !ctx.screenshotMode) {
        double best = 9.0 * 9.0;
        for (size_t i = 0; i < m_hosts.size(); ++i) {
            if (!project(vp, m_hosts[i].posPc, s)) continue;
            double dx = s.x - io.MousePos.x, dy = s.y - io.MousePos.y;
            if (dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                g.hovered = static_cast<int>(i);
            }
        }
    }
    if (ctx.click.pending) g.selected = g.hovered;

    if (g.showLabels) {
        const ImU32 dim = IM_COL32(150, 200, 255, 170), green = IM_COL32(110, 255, 150, 200);
        if (g.showRings) {
            for (double rad : {10.0, 100.0, 1000.0, 10000.0}) {
                if (rad < g.distance * 0.04 || rad > g.distance * 3.0) continue;
                // Label the ring toward the anticenter-ish lower right, and mark l = 0/90/180/270 on the largest.
                if (project(vp, glm::dvec3(-rad * 0.7071, -rad * 0.7071, 0.0), s))
                    drawText(dl, ImVec2(s.x + 4, s.y), green, distanceLabel(rad));
            }
            double big = 0.0;
            for (double rad : {10.0, 100.0, 1000.0, 10000.0})
                if (rad >= g.distance * 0.04 && rad <= g.distance * 0.45) big = rad;
            if (big > 0.0) {
                const char* names[4] = {"l = 0", "l = 90", "l = 180", "l = 270"};
                for (int k = 0; k < 4; ++k) {
                    double a = k * kPi / 2.0;
                    if (project(vp, glm::dvec3(big * std::cos(a), big * std::sin(a), 0.0), s))
                        drawText(dl, ImVec2(s.x + 3, s.y - 14), green, names[k]);
                }
            }
        }
        if (g.showArms && g.distance > 1500.0) {
            const auto& arms = Galaxy::Arms();
            for (size_t i = 0; i < arms.size() && i < m_arms.size(); ++i) {
                const auto& line = m_arms[i];
                // Each name sits at its own azimuth offset from the Sun-center line
                // (radius grows by exp(offset tan p) along the arm), so they don't stack.
                static const double kOffsetRad[] = {2.2, -1.1, 0.9, -1.6};
                size_t at = line.size() / 2;
                if (!arms[i].spur) {
                    double want =
                        arms[i].radiusAtSunAzimuthPc * std::exp(kOffsetRad[i % 4] * std::tan(Galaxy::kPitchDeg * kPi / 180.0));
                    for (size_t k = 0; k < line.size(); ++k)
                        if (Galaxy::GalactocentricRadiusPc(glm::dvec3(line[k])) >= want) {
                            at = k;
                            break;
                        }
                }
                if (project(vp, glm::dvec3(line[at]), s)) drawText(dl, s, dim, arms[i].name);
            }
        }
        // The Sun and the Galactic Center.
        if (project(vp, glm::dvec3(0.0), s)) {
            dl->AddCircle(s, 6.0f, IM_COL32(255, 230, 120, 230), 16, 1.5f);
            drawText(dl, ImVec2(s.x + 8, s.y - 7), IM_COL32(255, 230, 120, 255), "Sun");
        }
        if (project(vp, glm::dvec3(Galaxy::kSunToCenterPc, 0.0, -Galaxy::kSunHeightPc), s)) {
            dl->AddCircle(s, 5.0f, IM_COL32(255, 160, 90, 220), 12, 1.5f);
            drawText(dl, ImVec2(s.x + 8, s.y - 7), IM_COL32(255, 180, 110, 230), "Galactic Center (Sgr A*)");
        }
        // Host names when zoomed in: brightest-first isn't meaningful here, so
        // label those nearest the screen center, skipping overlaps.
        if (g.showHosts && g.distance < 120.0) {
            std::vector<std::pair<float, int>> cand;
            const ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
            for (size_t i = 0; i < m_hosts.size(); ++i)
                if (project(vp, m_hosts[i].posPc, s))
                    cand.emplace_back((s.x - c.x) * (s.x - c.x) + (s.y - c.y) * (s.y - c.y), static_cast<int>(i));
            std::sort(cand.begin(), cand.end());
            std::vector<ImVec2> placed;
            for (const auto& [d2, i] : cand) {
                if (placed.size() >= 40) break;
                project(vp, m_hosts[static_cast<size_t>(i)].posPc, s);
                bool clash = false;
                for (const ImVec2& q : placed)
                    if (std::fabs(q.x - s.x) < 90.0f && std::fabs(q.y - s.y) < 14.0f) clash = true;
                if (clash) continue;
                placed.push_back(s);
                drawText(dl, ImVec2(s.x + 6, s.y - 6), IM_COL32(220, 220, 220, 200), m_hosts[static_cast<size_t>(i)].name);
            }
        }
    }

    auto mark = [&](int idx, ImU32 col, bool label) {
        if (idx < 0 || idx >= static_cast<int>(m_hosts.size())) return;
        const Galaxy::Host& h = m_hosts[static_cast<size_t>(idx)];
        if (!project(vp, h.posPc, s)) return;
        dl->AddCircle(s, 9.0f, col, 20, 1.5f);
        if (label) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s  %.1f pc  %d planet%s", h.name.c_str(), h.distPc, h.planets,
                          h.planets == 1 ? "" : "s");
            drawText(dl, ImVec2(s.x + 12, s.y - 7), col, buf);
        }
    };
    mark(g.selected, IM_COL32(80, 255, 140, 255), true);
    if (g.hovered != g.selected) mark(g.hovered, IM_COL32(255, 255, 255, 220), true);
}

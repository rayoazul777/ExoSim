#include "sky/SkyModel.h"
#include "sim/DisplaySpace.h"

#include <algorithm>
#include <cmath>

namespace Sky {

glm::vec3 BvToColor(double bv) {
    struct Anchor {
        float bv;
        glm::vec3 color;
    };
    static const Anchor anchors[] = {
        {-0.4f, glm::vec3(0.61f, 0.70f, 1.00f)}, // hot blue-white (O/B)
        {0.0f, glm::vec3(0.93f, 0.93f, 1.00f)}, // white (A)
        {0.3f, glm::vec3(1.00f, 0.96f, 0.87f)}, // yellow-white (F)
        {0.6f, glm::vec3(1.00f, 0.89f, 0.70f)}, // yellow, Sun-like (G)
        {1.0f, glm::vec3(1.00f, 0.75f, 0.48f)}, // orange (K)
        {1.5f, glm::vec3(1.00f, 0.58f, 0.32f)}, // orange-red (M)
        {2.0f, glm::vec3(1.00f, 0.45f, 0.22f)}, // deep red (cool M)
    };
    constexpr int n = static_cast<int>(sizeof(anchors) / sizeof(anchors[0]));
    float b = static_cast<float>(bv);
    if (b <= anchors[0].bv) return anchors[0].color;
    if (b >= anchors[n - 1].bv) return anchors[n - 1].color;
    for (int i = 0; i < n - 1; ++i) {
        if (b >= anchors[i].bv && b <= anchors[i + 1].bv) {
            float t = (b - anchors[i].bv) / (anchors[i + 1].bv - anchors[i].bv);
            return glm::mix(anchors[i].color, anchors[i + 1].color, t);
        }
    }
    return anchors[n - 1].color;
}

float VmagToPointSize(double vmag) {
    float size = 1.0f + static_cast<float>(6.5 - vmag) * 0.9f;
    return std::clamp(size, 1.0f, 7.0f);
}

float VmagToBrightness(double vmag) {
    float b = 1.5f - static_cast<float>(vmag) * 0.15f;
    return std::clamp(b, 0.35f, 1.6f);
}

SkyView Compute(const std::vector<StarCatalogEntry>& catalog, const StarSystem& sys, double nowDays, double epochOffsetYears) {
    SkyView out;
    glm::dvec3 observer(0.0);
    bool fromSun = sys.isHome || sys.distPc <= 0.0;
    if (!fromSun) observer = glm::dvec3(Display::RaDecToDirection(sys.hostRaDeg, sys.hostDecDeg)) * sys.distPc;

    out.directions.reserve(catalog.size());
    out.colors.reserve(catalog.size());
    out.sizes.reserve(catalog.size());
    // Hipparcos positions are at catalog epoch J1991.25.
    const double yearsSinceCatalogEpoch = 8.75 + nowDays / 365.25;
    const double dtYears = yearsSinceCatalogEpoch + epochOffsetYears;

    for (const auto& st : catalog) {
        double cosDec = std::cos(glm::radians(st.decDeg));
        double ra = st.raDeg + (cosDec > 1e-6 ? st.pmRaMasYr * dtYears / 3.6e6 / cosDec : 0.0);
        double dec = std::clamp(st.decDeg + st.pmDecMasYr * dtYears / 3.6e6, -90.0, 90.0);
        glm::dvec3 dir(Display::RaDecToDirection(ra, dec));
        double vmag = st.vmag;
        if (st.parallaxMas > 0.5 && !fromSun) { // > 0.5 mas: within ~2 kpc, parallax meaningful
            double dPc = 1000.0 / st.parallaxMas;
            glm::dvec3 rel = dir * dPc - observer;
            double dNew = glm::length(rel);
            if (dNew < 0.05) continue; // the observer's own host star
            dir = rel / dNew;
            vmag += 5.0 * std::log10(dNew / dPc);
        }
        if (vmag > kVisibleLimitVmag) continue;
        out.directions.push_back(glm::vec3(dir));
        out.colors.push_back(BvToColor(st.bv) * VmagToBrightness(vmag));
        out.sizes.push_back(VmagToPointSize(vmag));
    }

    if (!fromSun) {
        // The Sun (absolute magnitude 4.83) seen from this system.
        double d = glm::length(observer);
        out.sunApparentVmag = 4.83 + 5.0 * std::log10(d / 10.0);
        if (out.sunApparentVmag < kVisibleLimitVmag) {
            out.sunVisible = true;
            out.directions.push_back(glm::vec3(-observer / d));
            out.colors.push_back(BvToColor(0.65) * VmagToBrightness(out.sunApparentVmag) * 1.3f);
            out.sizes.push_back(VmagToPointSize(out.sunApparentVmag) + 1.5f);
        }
    }
    out.visibleCount = static_cast<int>(out.directions.size());
    return out;
}

} // namespace Sky

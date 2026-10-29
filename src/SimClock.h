#pragma once
#include <cstdint>
#include <limits>

// Deterministic simulation clock.
//
// Wall-clock time is accumulated and consumed in FIXED ticks (1/120 s of
// real time); each tick advances simulation time by exactly warp * tick.
// So simulation time depends only on the number of ticks and the warp in
// effect for each one - not on frame rate or frame-time jitter - which is
// the prerequisite for recorded-input replay (log tick + warp changes).
//
// A "stop" day can be given (the next mission event): the clock lands on
// it exactly instead of skipping over it, and drops warp to real time, the
// way flight-operations tools "warp to next event".
class SimClock {
public:
    static constexpr double kTickRealSec = 1.0 / 120.0;
    static constexpr int kMaxTicksPerFrame = 60; // a long stall doesn't fast-forward unboundedly

    double timeDays = 0.0;
    double warp = 5.0 * 86400.0;  // simulation seconds per real second (1 = real time)
    bool paused = false;
    std::uint64_t ticks = 0;

    struct Advance { int ticks = 0; bool hitStop = false; };

    Advance advance(double realDtSec, double stopAtDay = std::numeric_limits<double>::infinity()) {
        Advance a;
        m_accum += realDtSec;
        int budget = kMaxTicksPerFrame;
        while (m_accum >= kTickRealSec && budget-- > 0) {
            m_accum -= kTickRealSec;
            if (paused) continue;
            double next = timeDays + warp * kTickRealSec / 86400.0;
            if (timeDays < stopAtDay && next >= stopAtDay) {
                timeDays = stopAtDay;
                warp = 1.0;
                a.hitStop = true;
                ++ticks; ++a.ticks;
                break;
            }
            timeDays = next;
            ++ticks; ++a.ticks;
        }
        if (budget < 0) m_accum = 0.0; // drop the backlog rather than spiral
        return a;
    }

private:
    double m_accum = 0.0;
};

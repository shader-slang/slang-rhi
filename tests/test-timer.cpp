#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

#include "core/timer.h"

#include <chrono>

inline void highPrecisionSleep(double duration)
{
    using namespace std::chrono;
    auto start = high_resolution_clock::now();
    while (true)
    {
        auto now = high_resolution_clock::now();
        auto elapsed = duration_cast<nanoseconds>(now - start).count();
        if (elapsed >= duration * 1e9)
        {
            break;
        }
    }
}

TEST_CASE("timer")
{
    SUBCASE("now")
    {
        double deltaNS = 10000000.0;
        TimePoint t0 = Timer::now();
        highPrecisionSleep(deltaNS / 1000000000.0);
        TimePoint t1 = Timer::now();
        CHECK(t1 > t0 + deltaNS * 0.9);
    }

    SUBCASE("delta")
    {
        CHECK(Timer::delta(0.0, 1000000000.0) == 1.0);
        CHECK(Timer::deltaMS(0.0, 1000000000.0) == 1000.0);
        CHECK(Timer::deltaUS(0.0, 1000000000.0) == 1000000.0);
        CHECK(Timer::deltaNS(0.0, 1000000000.0) == 1000000000.0);
    }

    SUBCASE("elapsed")
    {
        double delta = 0.01;
        double deltaMS = delta * 1000.0;
        double deltaUS = deltaMS * 1000.0;
        double deltaNS = deltaUS * 1000.0;
        Timer timer;
        highPrecisionSleep(deltaNS / 1000000000.0);
        CHECK(timer.elapsed() > delta * 0.9);
        CHECK(timer.elapsedMS() > deltaMS * 0.9);
        CHECK(timer.elapsedUS() > deltaUS * 0.9);
        CHECK(timer.elapsedNS() > deltaNS * 0.9);
    }
}

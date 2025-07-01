#include "timer.h"

#include <chrono>

namespace rhi {

TimePoint Timer::now()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(high_resolution_clock::now().time_since_epoch()).count();
}

} // namespace rhi

#include "assert.h"

#include <cstdlib>
#include <cstdio>

namespace rhi {

void handleAssert(const char* message, const char* file, int line)
{
    std::fprintf(stderr, "Assertion failed: %s\n", message);
    std::fprintf(stderr, "At %s:%d\n", file, line);
    std::abort();
}

} // namespace rhi

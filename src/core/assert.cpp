#include "assert.h"

#include <cstdlib>
#include <cstdio>

namespace rhi {

thread_local int tls_disableAssert = 0;

ScopedDisableAssert::ScopedDisableAssert()
{
    tls_disableAssert++;
}

ScopedDisableAssert::~ScopedDisableAssert()
{
    tls_disableAssert--;
}


void handleAssert(const char* message, const char* file, int line)
{
    if (tls_disableAssert == 0)
    {
        std::fprintf(stderr, "Assertion failed: %s\n", message);
        std::fprintf(stderr, "At %s:%d\n", file, line);
        std::abort();
    }
}

} // namespace rhi

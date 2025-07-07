#pragma once

namespace rhi {

class ScopedDisableAssert
{
public:
    ScopedDisableAssert();
    ~ScopedDisableAssert();
};

void handleAssert(const char* message, const char* file, int line);

} // namespace rhi

#define SLANG_RHI_DISABLE_ASSERT_SCOPE() ::rhi::ScopedDisableAssert disable_assert__;

#define SLANG_RHI_ASSERT_FAILURE(what)                                                                                 \
    {                                                                                                                  \
        ::rhi::handleAssert(what, __FILE__, __LINE__);                                                                 \
    }

#define SLANG_RHI_ASSERT(x)                                                                                            \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
            SLANG_RHI_ASSERT_FAILURE(#x);                                                                              \
    }

#define SLANG_RHI_UNIMPLEMENTED(what)                                                                                  \
    {                                                                                                                  \
        SLANG_RHI_ASSERT_FAILURE("Not implemented");                                                                   \
    }

#define SLANG_RHI_UNREACHABLE(what)                                                                                    \
    {                                                                                                                  \
        SLANG_RHI_ASSERT_FAILURE("Unreachable");                                                                       \
    }

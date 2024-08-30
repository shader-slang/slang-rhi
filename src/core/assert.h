#pragma once

#include <cstdlib>

#define SLANG_RHI_ASSERT(x)                                                                                            \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
            std::abort();                                                                                              \
    }

#define SLANG_RHI_ASSERT_FAILURE(what)                                                                                 \
    {                                                                                                                  \
        std::abort();                                                                                                  \
    }

#define SLANG_RHI_UNIMPLEMENTED_X(what)                                                                                \
    {                                                                                                                  \
        std::abort();                                                                                                  \
    }

#define SLANG_RHI_UNREACHABLE(what)                                                                                    \
    {                                                                                                                  \
        std::abort();                                                                                                  \
    }

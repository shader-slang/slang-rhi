#include "smart-pointer.h"

#include <cstdio>

#if SLANG_VC
#if defined(_CPPRTTI)
#define RTTI_ENABLED
#endif
#elif SLANG_CLANG
#if __has_feature(cxx_rtti)
#define RTTI_ENABLED
#endif
#elif SLANG_GCC
#if defined(__GXX_RTTI)
#define RTTI_ENABLED
#endif
#endif

namespace rhi {

#if SLANG_RHI_ENABLE_REF_OBJECT_TRACKING
void RefObjectTracker::reportLiveObjects()
{
    if (!objects.empty())
    {
        printf("Found %zu live RHI objects!\n", objects.size());
        for (auto obj : objects)
        {
#ifdef RTTI_ENABLED
            printf("Live object: %p (%s)\n", static_cast<void*>(obj), typeid(*obj).name());
#else
            printf("Live object: %p\n", static_cast<void*>(obj));
#endif
        }
    }
}
#endif

#if SLANG_RHI_DEBUG
std::atomic<uint64_t> RefObject::s_objectCount = 0;
#endif

} // namespace rhi

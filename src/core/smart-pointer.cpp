#include "smart-pointer.h"

#include <cstdio>

namespace rhi {

#if SLANG_RHI_ENABLE_REF_OBJECT_TRACKING
void RefObjectTracker::reportLiveObjects()
{
    if (!objects.empty())
    {
        printf("Found %zu live RHI objects!\n", objects.size());
        for (auto obj : objects)
        {
            printf("Live object: %p\n", obj);
        }
    }
}
#endif

#if SLANG_RHI_DEBUG
std::atomic<uint64_t> RefObject::s_objectCount = 0;
#endif

} // namespace rhi

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
            uint64_t referenceCount = obj->getReferenceCount();
            uint64_t internalReferenceCount = obj->getInternalReferenceCount();
#ifdef RTTI_ENABLED
            const char* typeName = typeid(*obj).name();
#else
            const char* typeName = "unknown";
#endif
            printf(
                "Live object: 0x%" PRIXPTR " referenceCount=%llu internalReferenceCount=%llu type=\"%s\"\n",
                reinterpret_cast<uintptr_t>(obj),
                referenceCount,
                internalReferenceCount,
                typeName
            );
        }
    }
}
#endif

#if SLANG_RHI_DEBUG
std::atomic<uint64_t> RefObject::s_objectCount = 0;
#endif

} // namespace rhi

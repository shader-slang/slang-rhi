#pragma once

#include <slang-rhi.h>

#include "assert.h"

#include <atomic>
#include <type_traits>

#define SLANG_RHI_ENABLE_REF_OBJECT_TRACKING 0

#if SLANG_RHI_ENABLE_REF_OBJECT_TRACKING
#include <mutex>
#include <set>
namespace rhi {
class RefObject;
struct RefObjectTracker
{
    std::mutex mutex;
    std::set<RefObject*> objects;

    void trackObject(RefObject* obj)
    {
        std::lock_guard<std::mutex> lock(mutex);
        objects.insert(obj);
    }

    void untrackObject(RefObject* obj)
    {
        std::lock_guard<std::mutex> lock(mutex);
        objects.erase(obj);
    }

    void reportLiveObjects();

    static RefObjectTracker& instance()
    {
        static RefObjectTracker tracker;
        return tracker;
    }
};
#define SLANG_RHI_TRACK_OBJECT(obj) RefObjectTracker::instance().trackObject(obj)
#define SLANG_RHI_UNTRACK_OBJECT(obj) RefObjectTracker::instance().untrackObject(obj)
} // namespace rhi
#else
#define SLANG_RHI_TRACK_OBJECT(obj)
#define SLANG_RHI_UNTRACK_OBJECT(obj)
#endif


namespace rhi {

// Base class for all reference-counted objects
class SLANG_RHI_API RefObject
{
private:
    // The low 32 bits store the total reference count and the high 32 bits store the internal
    // reference count. Keeping both counts in one atomic state makes transitions between internal
    // and external ownership race-free.
    std::atomic<uint64_t> referenceCounts;

    static constexpr uint64_t kInternalReference = uint64_t(1) << 32;
    static constexpr uint64_t kReferenceCountMask = kInternalReference - 1;

    static uint32_t getReferenceCount(uint64_t counts) { return uint32_t(counts & kReferenceCountMask); }
    static uint32_t getInternalReferenceCount(uint64_t counts) { return uint32_t(counts >> 32); }

#if SLANG_RHI_DEBUG
    // Track the number of RefObject instances.
    static std::atomic<uint64_t> s_objectCount;
#endif

public:
    RefObject()
        : referenceCounts(0)
    {
        SLANG_RHI_TRACK_OBJECT(this);
#if SLANG_RHI_DEBUG
        s_objectCount.fetch_add(1);
#endif
    }

    RefObject(const RefObject&)
        : referenceCounts(0)
    {
        SLANG_RHI_TRACK_OBJECT(this);
#if SLANG_RHI_DEBUG
        s_objectCount.fetch_add(1);
#endif
    }

    virtual ~RefObject()
    {
        SLANG_RHI_UNTRACK_OBJECT(this);
#if SLANG_RHI_DEBUG
        s_objectCount.fetch_sub(1);
#endif
    }

    RefObject& operator=(const RefObject&) { return *this; }

    uint32_t addReference()
    {
        uint64_t counts = referenceCounts.fetch_add(1);
        uint32_t count = getReferenceCount(counts);
        uint32_t internalCount = getInternalReferenceCount(counts);
        SLANG_RHI_ASSERT(count < UINT32_MAX);
        if (internalCount > 0 && count == internalCount) [[unlikely]]
        {
            // Object is now externally referenced
            makeExternal();
        }
        return count + 1;
    }

    uint32_t releaseReference()
    {
        uint64_t counts = referenceCounts.fetch_sub(1);
        uint32_t count = getReferenceCount(counts);
        uint32_t internalCount = getInternalReferenceCount(counts);
        SLANG_RHI_ASSERT(count > 0);
        if (internalCount > 0 && count == internalCount + 1) [[unlikely]]
        {
            // Object is now internally referenced only
            makeInternal();
        }
        if (count == 1) [[unlikely]]
        {
            // Last reference, delete the object
            // Default behavior immediately calls 'delete this'
            deleteThis();
        }
        return count - 1;
    }

    uint32_t addInternalReference()
    {
        uint64_t counts = referenceCounts.fetch_add(kInternalReference + 1);
        uint32_t count = getReferenceCount(counts);
        uint32_t internalCount = getInternalReferenceCount(counts);
        SLANG_RHI_ASSERT(count < UINT32_MAX);
        SLANG_RHI_ASSERT(internalCount < UINT32_MAX);
        SLANG_RHI_ASSERT(count > internalCount);
        return count + 1;
    }

    uint32_t releaseInternalReference()
    {
        uint64_t counts = referenceCounts.fetch_sub(kInternalReference + 1);
        uint32_t count = getReferenceCount(counts);
        uint32_t internalCount = getInternalReferenceCount(counts);
        SLANG_RHI_ASSERT(count > 0);
        SLANG_RHI_ASSERT(internalCount > 0);
        if (count == 1) [[unlikely]]
        {
            deleteThis();
        }
        return count - 1;
    }

    // Set the number of references that are internal.
    // When the reference count becomes equal or smaller to this value,
    // the object is considered to be internally referenced and `makeInternal()` is called.
    // When the reference count is greater than this value, the object is considered to be externally referenced
    // and `makeExternal()` is called.
    // Note: Calling this function is not thread-safe and should be used with care (i.e. only be called when the object
    // is initially created).
    void setInternalReferenceCount(uint32_t count)
    {
        uint64_t oldCounts = referenceCounts.load();
        uint32_t currentCount = getReferenceCount(oldCounts);
        uint32_t oldInternalCount = getInternalReferenceCount(oldCounts);
        SLANG_RHI_ASSERT(count <= currentCount);
        referenceCounts.store((uint64_t(count) << 32) | currentCount);
        if (oldInternalCount == currentCount && count < currentCount)
        {
            // Object is now externally referenced
            makeExternal();
        }
        else if (oldInternalCount < currentCount && count == currentCount)
        {
            // Object is now internally referenced
            makeInternal();
        }
    }

    uint64_t getReferenceCount() const { return getReferenceCount(referenceCounts.load()); }
    uint64_t getInternalReferenceCount() const { return getInternalReferenceCount(referenceCounts.load()); }

    virtual void makeExternal() {}
    virtual void makeInternal() {}
    virtual void deleteThis() { delete this; }

#if SLANG_RHI_DEBUG
    // Get the number of RefObject instances currently alive.
    static uint64_t getObjectCount() { return s_objectCount.load(); }
#endif
};

SLANG_FORCE_INLINE void addReference(RefObject* obj)
{
    if (obj)
        obj->addReference();
}

SLANG_FORCE_INLINE void releaseReference(RefObject* obj)
{
    if (obj)
        obj->releaseReference();
}

// For straight dynamic cast.
// Use instead of dynamic_cast as it allows for replacement without using Rtti in the future
template<typename T>
SLANG_FORCE_INLINE T* dynamicCast(RefObject* obj)
{
    return dynamic_cast<T*>(obj);
}
template<typename T>
SLANG_FORCE_INLINE const T* dynamicCast(const RefObject* obj)
{
    return dynamic_cast<const T*>(obj);
}

// Like a dynamicCast, but allows a type to implement a specific implementation that is suitable for it
template<typename T>
SLANG_FORCE_INLINE T* as(RefObject* obj)
{
    return dynamicCast<T>(obj);
}
template<typename T>
SLANG_FORCE_INLINE const T* as(const RefObject* obj)
{
    return dynamicCast<T>(obj);
}

// "Smart" pointer to a reference-counted object
template<typename T>
struct SLANG_RHI_API RefPtr
{
    RefPtr()
        : pointer(nullptr)
    {
    }

    RefPtr(T* p)
        : pointer(p)
    {
        addReference(p);
    }

    RefPtr(const RefPtr<T>& p)
        : pointer(p.pointer)
    {
        addReference(p.pointer);
    }

    RefPtr(RefPtr<T>&& p)
        : pointer(p.pointer)
    {
        p.pointer = nullptr;
    }

    template<typename U>
    RefPtr(const RefPtr<U>& p, typename std::enable_if<std::is_convertible<U*, T*>::value, void>::type* = 0)
        : pointer(static_cast<U*>(p))
    {
        addReference(static_cast<U*>(p));
    }

#if 0
        void operator=(T* p)
        {
            T* old = pointer;
            addReference(p);
            pointer = p;
            releaseReference(old);
        }
#endif

    void operator=(const RefPtr<T>& p)
    {
        T* old = pointer;
        addReference(p.pointer);
        pointer = p.pointer;
        releaseReference(old);
    }

    void operator=(RefPtr<T>&& p)
    {
        T* old = pointer;
        pointer = p.pointer;
        p.pointer = old;
    }

    template<typename U>
    typename std::enable_if<std::is_convertible<U*, T*>::value, void>::type operator=(const RefPtr<U>& p)
    {
        T* old = pointer;
        addReference(p.pointer);
        pointer = p.pointer;
        releaseReference(old);
    }

    bool operator==(const T* ptr) const { return pointer == ptr; }

    bool operator!=(const T* ptr) const { return pointer != ptr; }

    bool operator==(const RefPtr<T>& ptr) const { return pointer == ptr.pointer; }

    bool operator!=(const RefPtr<T>& ptr) const { return pointer != ptr.pointer; }

    template<typename U>
    RefPtr<U> dynamicCast() const
    {
        return RefPtr<U>(dynamic_cast<U>(pointer));
    }

    ~RefPtr() { releaseReference(static_cast<RefObject*>(pointer)); }

    T& operator*() const { return *pointer; }

    T* operator->() const { return pointer; }

    T* get() const { return pointer; }

    operator T*() const { return pointer; }

    void attach(T* p)
    {
        T* old = pointer;
        pointer = p;
        releaseReference(old);
    }

    T* detach()
    {
        auto rs = pointer;
        pointer = nullptr;
        return rs;
    }

    void swapWith(RefPtr<T>& rhs)
    {
        auto rhsPtr = rhs.pointer;
        rhs.pointer = pointer;
        pointer = rhsPtr;
    }

    SLANG_FORCE_INLINE void setNull()
    {
        releaseReference(pointer);
        pointer = nullptr;
    }

    /// Get ready for writing (nulls contents)
    SLANG_FORCE_INLINE T** writeRef()
    {
        *this = nullptr;
        return &pointer;
    }

    /// Get for read access
    SLANG_FORCE_INLINE T* const* readRef() const { return &pointer; }

private:
    T* pointer;

    template<typename T2>
    friend struct RefPtr;
};

} // namespace rhi

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
    // Total number of references to this object.
    std::atomic<uint32_t> referenceCount;
    // Number references that are internal (i.e., not externally visible).
    // This can be used to detect whether the object is currently externally referenced or not.
    // For more details, see the comments in `setInternalReferenceCount()`.
    std::atomic<uint32_t> internalReferenceCount;

#if SLANG_RHI_DEBUG
    // Track the number of RefObject instances.
    static std::atomic<uint64_t> s_objectCount;
#endif

public:
    RefObject()
        : referenceCount(0)
        , internalReferenceCount(0)
    {
        SLANG_RHI_TRACK_OBJECT(this);
#if SLANG_RHI_DEBUG
        s_objectCount.fetch_add(1);
#endif
    }

    RefObject(const RefObject&)
        : referenceCount(0)
        , internalReferenceCount(0)
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
        uint32_t count = referenceCount.fetch_add(1);
        uint32_t internalCount = internalReferenceCount.load();
        if (internalCount > 0 && count == internalCount) [[unlikely]]
        {
            // Object is now externally referenced
            makeExternal();
        }
        return count + 1;
    }

    uint32_t releaseReference()
    {
        uint32_t count = referenceCount.fetch_sub(1);
        SLANG_RHI_ASSERT(count > 0);
        uint32_t internalCount = internalReferenceCount.load();
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

    // Add a reference that is owned by the RHI itself rather than by application code.
    // Internal references keep the object alive, but do not make it externally referenced:
    // the total and internal counts move together, so the object's classification is
    // unchanged. We use this for ownership such as a command queue retaining the command
    // buffers it has in flight, where the reference exists only to keep the object alive
    // until the GPU is done with it, and therefore must not keep the device alive.
    uint32_t addInternalReference()
    {
        // Raise the total count first so that no observer can ever see an internal count
        // that exceeds the total count.
        uint32_t count = referenceCount.fetch_add(1) + 1;
        internalReferenceCount.fetch_add(1);
        return count;
    }

    // Drop a reference previously taken with `addInternalReference()`.
    uint32_t releaseInternalReference()
    {
        // Lower the internal count first, for the same reason `addInternalReference()`
        // raises the total count first.
        SLANG_RHI_ASSERT(internalReferenceCount.load() > 0);
        internalReferenceCount.fetch_sub(1);
        return releaseReference();
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
        uint32_t currentCount = referenceCount.load();
        SLANG_RHI_ASSERT(count <= currentCount);
        internalReferenceCount.store(count);
        if (count == 0 && currentCount > 0)
        {
            // Object is now externally referenced
            makeExternal();
        }
        else if (count > 0 && currentCount == count)
        {
            // Object is now internally referenced
            makeInternal();
        }
    }

    uint64_t getReferenceCount() const { return referenceCount; }
    uint64_t getInternalReferenceCount() const { return internalReferenceCount; }

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

// Ownership policies for `RefPtrBase`.
//
// `ExternalOwnership` is the ordinary reference an application (or any code that wants the
// object to behave as if the application held it) takes. `InternalOwnership` is a reference
// the RHI takes on its own behalf; it keeps the object alive without marking it externally
// referenced, so ownership such as a command queue's in-flight list does not keep the device
// alive. Spelling the distinction in the pointer type is what keeps the classification
// correct as an object moves between containers.
struct ExternalOwnership
{
    static void add(RefObject* obj)
    {
        if (obj)
            obj->addReference();
    }
    static void release(RefObject* obj)
    {
        if (obj)
            obj->releaseReference();
    }
};

struct InternalOwnership
{
    static void add(RefObject* obj)
    {
        if (obj)
            obj->addInternalReference();
    }
    static void release(RefObject* obj)
    {
        if (obj)
            obj->releaseInternalReference();
    }
};

// "Smart" pointer to a reference-counted object
template<typename T, typename Ownership>
struct RefPtrBase
{
    RefPtrBase()
        : pointer(nullptr)
    {
    }

    RefPtrBase(T* p)
        : pointer(p)
    {
        Ownership::add(p);
    }

    RefPtrBase(const RefPtrBase& p)
        : pointer(p.pointer)
    {
        Ownership::add(p.pointer);
    }

    RefPtrBase(RefPtrBase&& p)
        : pointer(p.pointer)
    {
        p.pointer = nullptr;
    }

    template<typename U, typename UOwnership>
    RefPtrBase(
        const RefPtrBase<U, UOwnership>& p,
        typename std::enable_if<std::is_convertible<U*, T*>::value, void>::type* = 0
    )
        : pointer(static_cast<U*>(p))
    {
        Ownership::add(static_cast<U*>(p));
    }

    void operator=(const RefPtrBase& p)
    {
        T* old = pointer;
        Ownership::add(p.pointer);
        pointer = p.pointer;
        Ownership::release(old);
    }

    void operator=(RefPtrBase&& p)
    {
        T* old = pointer;
        pointer = p.pointer;
        p.pointer = old;
    }

    template<typename U, typename UOwnership>
    typename std::enable_if<std::is_convertible<U*, T*>::value, void>::type operator=(
        const RefPtrBase<U, UOwnership>& p
    )
    {
        T* old = pointer;
        Ownership::add(p.pointer);
        pointer = p.pointer;
        Ownership::release(old);
    }

    bool operator==(const T* ptr) const { return pointer == ptr; }

    bool operator!=(const T* ptr) const { return pointer != ptr; }

    bool operator==(const RefPtrBase& ptr) const { return pointer == ptr.pointer; }

    bool operator!=(const RefPtrBase& ptr) const { return pointer != ptr.pointer; }

    template<typename U>
    RefPtrBase<U, Ownership> dynamicCast() const
    {
        return RefPtrBase<U, Ownership>(dynamic_cast<U*>(pointer));
    }

    ~RefPtrBase() { Ownership::release(static_cast<RefObject*>(pointer)); }

    T& operator*() const { return *pointer; }

    T* operator->() const { return pointer; }

    T* get() const { return pointer; }

    operator T*() const { return pointer; }

    void attach(T* p)
    {
        T* old = pointer;
        pointer = p;
        Ownership::release(old);
    }

    T* detach()
    {
        auto rs = pointer;
        pointer = nullptr;
        return rs;
    }

    void swapWith(RefPtrBase& rhs)
    {
        auto rhsPtr = rhs.pointer;
        rhs.pointer = pointer;
        pointer = rhsPtr;
    }

    SLANG_FORCE_INLINE void setNull()
    {
        Ownership::release(pointer);
        pointer = nullptr;
    }

    /// Get ready for writing (nulls contents)
    SLANG_FORCE_INLINE T** writeRef()
    {
        *this = RefPtrBase();
        return &pointer;
    }

    /// Get for read access
    SLANG_FORCE_INLINE T* const* readRef() const { return &pointer; }

private:
    T* pointer;

    template<typename T2, typename TOwnership2>
    friend struct RefPtrBase;
};

template<typename T>
using RefPtr = RefPtrBase<T, ExternalOwnership>;

template<typename T>
using InternalRefPtr = RefPtrBase<T, InternalOwnership>;

} // namespace rhi

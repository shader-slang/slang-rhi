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
class RefObject
{
private:
    // Low 32 bits: external references. High 32 bits: internal references.
    // A single atomic makes ownership changes linearizable. Internal references retain
    // the object; each nonempty external lifetime additionally retains getLifetimeOwner().
    std::atomic<uint64_t> m_referenceCounts{0};

    static constexpr uint64_t kInternalReference = uint64_t(1) << 32;
    static uint32_t externalCount(uint64_t counts) { return uint32_t(counts); }
    static uint32_t internalCount(uint64_t counts) { return uint32_t(counts >> 32); }
    static uint64_t totalCount(uint64_t counts) { return uint64_t(externalCount(counts)) + internalCount(counts); }

#if SLANG_RHI_DEBUG
    // Track the number of RefObject instances.
    static std::atomic<uint64_t> s_objectCount;
#endif

public:
    RefObject()
    {
        SLANG_RHI_TRACK_OBJECT(this);
#if SLANG_RHI_DEBUG
        s_objectCount.fetch_add(1);
#endif
    }

    RefObject(const RefObject&)
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

    virtual uint32_t addReference()
    {
        uint64_t counts = m_referenceCounts.load();
        RefObject* owner = nullptr;
        bool resolvedOwner = false;
        for (;;)
        {
            SLANG_RHI_ASSERT(externalCount(counts) < UINT32_MAX);
            if (externalCount(counts) == 0 && !resolvedOwner)
            {
                // Retain before publishing an external reference. Concurrent promotions
                // may acquire provisional owner references; only the winner keeps one.
                owner = getLifetimeOwner();
                if (owner)
                    owner->addReference();
                resolvedOwner = true;
            }
            if (m_referenceCounts.compare_exchange_weak(counts, counts + 1))
            {
                if (owner && externalCount(counts) != 0)
                    owner->releaseReference();
                return uint32_t(totalCount(counts) + 1);
            }
        }
    }

    virtual uint32_t releaseReference()
    {
        uint64_t counts = m_referenceCounts.load();
        for (;;)
        {
            SLANG_RHI_ASSERT(externalCount(counts) > 0);
            if (externalCount(counts) > 1)
            {
                if (m_referenceCounts.compare_exchange_weak(counts, counts - 1))
                    return uint32_t(totalCount(counts) - 1);
            }
            else
            {
                SLANG_RHI_ASSERT(internalCount(counts) < UINT32_MAX);
                // Convert our last external reference into a temporary internal reference.
                // This keeps this object alive while we finish the old external lifetime.
                if (m_referenceCounts.compare_exchange_weak(counts, counts - 1 + kInternalReference))
                {
                    // Look up and save the owner while our temporary reference still
                    // protects this object. Ordinary releases need no virtual lookup.
                    RefObject* owner = getLifetimeOwner();
                    // This guard is local bookkeeping, not a new internal ownership edge.
                    // Bypass overrides that forward consumer references to another object.
                    uint32_t remaining = RefObject::releaseInternalReference(); // May delete this.
                    // A new external lifetime may already have acquired its own owner pin.
                    // Release only ours, after destruction/deferred enqueue has completed.
                    if (owner)
                        owner->releaseReference(); // May initiate device shutdown.
                    return remaining;
                }
            }
        }
    }

    // Internal references require a containing owner/operation that guarantees the lifetime
    // owner remains valid through use and destruction. They are not safe weak references.
    virtual uint32_t addInternalReference()
    {
        uint64_t counts = m_referenceCounts.fetch_add(kInternalReference);
        SLANG_RHI_ASSERT(internalCount(counts) < UINT32_MAX);
        return uint32_t(totalCount(counts) + 1);
    }

    virtual uint32_t releaseInternalReference()
    {
        uint64_t counts = m_referenceCounts.fetch_sub(kInternalReference);
        SLANG_RHI_ASSERT(internalCount(counts) > 0);
        uint64_t remaining = totalCount(counts) - 1;
        if (remaining == 0)
            deleteThis();
        return uint32_t(remaining);
    }

    uint64_t getReferenceCount() const { return totalCount(m_referenceCounts.load()); }
    uint64_t getInternalReferenceCount() const { return internalCount(m_referenceCounts.load()); }
    uint64_t getExternalReferenceCount() const { return externalCount(m_referenceCounts.load()); }

    virtual void deleteThis() { delete this; }

#if SLANG_RHI_DEBUG
    // Get the number of RefObject instances currently alive.
    static uint64_t getObjectCount() { return s_objectCount.load(); }
#endif

protected:
    // Return an immutable association without changing reference counts. The owner must
    // remain valid through internal reference use/destruction and promotion to external
    // ownership. Called only at external-lifetime boundaries while this object is alive.
    virtual RefObject* getLifetimeOwner() const noexcept { return nullptr; }
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
// object to behave as if the application held it) takes. `InternalOwnership` retains an
// object without retaining its lifetime owner. Use it only where the containing owner or
// operation guarantees the lifetime owner's validity through use and destruction, such as
// a device-owned queue's in-flight list or a resource's same-device dependencies. Temporary
// RHI code can still need external references. Internal ownership is not weak ownership.
struct ExternalOwnership
{
    template<typename T>
    static void add(T* obj)
    {
        if (obj)
            obj->addReference();
    }
    template<typename T>
    static void release(T* obj)
    {
        if (obj)
            obj->releaseReference();
    }
};

struct InternalOwnership
{
    template<typename T>
    static void add(T* obj)
    {
        if (obj)
            obj->addInternalReference();
    }
    template<typename T>
    static void release(T* obj)
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

    RefPtrBase(RefPtrBase&& p) noexcept
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

    void operator=(RefPtrBase&& p) noexcept
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

    ~RefPtrBase() { Ownership::release(pointer); }

    T& operator*() const { return *pointer; }

    T* operator->() const { return pointer; }

    T* get() const { return pointer; }

    operator T*() const { return pointer; }

    void attach(T* p)
    {
        static_assert(std::is_same_v<Ownership, ExternalOwnership>, "attach requires an external owned reference");
        T* old = pointer;
        pointer = p;
        Ownership::release(old);
    }

    T* detach()
    {
        static_assert(
            std::is_same_v<Ownership, ExternalOwnership>,
            "Internal ownership must not escape as an untyped reference"
        );
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
        T* old = pointer;
        pointer = nullptr;
        Ownership::release(old);
    }

    /// Get ready for writing (nulls contents)
    SLANG_FORCE_INLINE T** writeRef()
    {
        static_assert(
            std::is_same_v<Ownership, ExternalOwnership>,
            "Receive factory results into RefPtr, then convert ownership"
        );
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

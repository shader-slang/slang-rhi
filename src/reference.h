#pragma once

#include "core/common.h"

namespace rhi {

// We use a `BreakableReference` to avoid the cyclic reference situation in rhi implementation.
// It is a common scenario where objects created from an `IDevice` implementation needs to hold
// a strong reference to the device object that creates them. For example, a `Buffer` or a
// `CommandQueue` needs to store a `m_device` member that points to the `IDevice`. At the same
// time, the device implementation may also hold a reference to some of the objects it created
// to represent the current device/binding state. Both parties would like to maintain a strong
// reference to each other to achieve robustness against arbitrary ordering of destruction that
// can be triggered by the user. However this creates cyclic reference situations that break
// the `RefPtr` recyling mechanism. To solve this problem, we instead make each object reference
// the device via a `BreakableReference<TDeviceImpl>` pointer. A breakable reference can be
// turned into a weak reference via its `breakStrongReference()` call.
// If we know there is a cyclic reference between an API object and the device/pool that creates it,
// we can break the cycle when there is no longer any public references that come from `ComPtr`s to
// the API object, by turning the reference to the device object from the API object to a weak
// reference.
// The following example illustrate how this mechanism works:
// Suppose we have
// ```
// class DeviceImpl : IDevice { RefPtr<ShaderObject> m_currentObject; };
// class ShaderObjectImpl : IShaderObject { BreakableReference<DeviceImpl> m_device; };
// ```
// And the user creates a device and a shader object, then somehow having the device reference
// the shader object (this may not happen in actual implemetations, we just use it to illustrate
// the situation):
// ```
// ComPtr<IDevice> device = createDevice();
// ComPtr<ISomeResource> res = device->createResourceX(...);
// device->m_currentResource = res;
// ```
// This setup is robust to any destruction ordering. If user releases reference to `device` first,
// then the device object will not be freed yet, since there is still a strong reference to the device
// implementation via `res->m_device`. Next when the user releases reference to `res`, the public
// reference count to `res` via `ComPtr`s will go to 0, therefore triggering the call to
// `res->m_device.breakStrongReference()`, releasing the remaining reference to device. This will cause
// `device` to start destruction, which will release its strong reference to `res` during execution of
// its destructor. Finally, this will triger the actual destruction of `res`.
// On the other hand, if the user releases reference to `res` first, then the strong reference to `device`
// will be broken immediately, but the actual destruction of `res` will not start. Next when the user
// releases `device`, there will no longer be any other references to `device`, so the destruction of
// `device` will start, causing the release of the internal reference to `res`, leading to its destruction.
// Note that the above logic only works if it is known that there is a cyclic reference. If there are no
// such cyclic reference, then it will be incorrect to break the strong reference to `IDevice` upon
// public reference counter dropping to 0. This is because the actual destructor of `res` take place
// after breaking the cycle, but if the resource's strong reference to the device is already the last reference,
// turning that reference to weak reference will immediately trigger destruction of `device`, after which
// we can no longer destruct `res` if the destructor needs `device`. Therefore we need to be careful
// when using `BreakableReference`, and make sure we only call `breakStrongReference` only when it is known
// that there is a cyclic reference. Luckily for all scenarios so far this is statically known.
template<typename T>
class BreakableReference
{
private:
    RefPtr<T> m_strongPtr;
    T* m_weakPtr = nullptr;

public:
    BreakableReference() = default;

    BreakableReference(T* p) { *this = p; }

    BreakableReference(const RefPtr<T>& p) { *this = p; }

    void setWeakReference(T* p)
    {
        m_weakPtr = p;
        m_strongPtr = nullptr;
    }

    T& operator*() const { return *get(); }

    T* operator->() const { return get(); }

    T* get() const { return m_weakPtr; }

    operator T*() const { return get(); }

    void operator=(RefPtr<T>& p)
    {
        m_strongPtr = p;
        m_weakPtr = p.get();
    }

    void operator=(T* p)
    {
        m_strongPtr = p;
        m_weakPtr = p;
    }

    void breakStrongReference() { m_strongPtr = nullptr; }

    void establishStrongReference() { m_strongPtr = m_weakPtr; }
};

// Helpers for returning an object implementation as COM pointer.
template<typename TInterface, typename TImpl>
void returnComPtr(TInterface** outInterface, TImpl* rawPtr)
{
    static_assert(!std::is_base_of<RefObject, TInterface>::value, "TInterface must be an interface type.");
    rawPtr->addRef();
    *outInterface = rawPtr;
}

template<typename TInterface, typename TImpl>
void returnComPtr(TInterface** outInterface, const RefPtr<TImpl>& refPtr)
{
    static_assert(!std::is_base_of<RefObject, TInterface>::value, "TInterface must be an interface type.");
    refPtr->addRef();
    *outInterface = refPtr.get();
}

template<typename TInterface, typename TImpl>
void returnComPtr(TInterface** outInterface, ComPtr<TImpl>& comPtr)
{
    static_assert(!std::is_base_of<RefObject, TInterface>::value, "TInterface must be an interface type.");
    *outInterface = comPtr.detach();
}

// Helpers for returning an object implementation as RefPtr.
template<typename TDest, typename TImpl>
void returnRefPtr(TDest** outPtr, RefPtr<TImpl>& refPtr)
{
    static_assert(std::is_base_of<RefObject, TDest>::value, "TDest must be a non-interface type.");
    static_assert(std::is_base_of<RefObject, TImpl>::value, "TImpl must be a non-interface type.");
    *outPtr = refPtr.get();
    refPtr->addReference();
}

template<typename TDest, typename TImpl>
void returnRefPtrMove(TDest** outPtr, RefPtr<TImpl>& refPtr)
{
    static_assert(std::is_base_of<RefObject, TDest>::value, "TDest must be a non-interface type.");
    static_assert(std::is_base_of<RefObject, TImpl>::value, "TImpl must be a non-interface type.");
    *outPtr = refPtr.detach();
}

} // namespace rhi

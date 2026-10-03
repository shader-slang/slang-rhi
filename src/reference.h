#pragma once

#include "core/common.h"

namespace rhi {

// Return helpers transfer an external reference and clear the source.
// Copy variants add an external reference and leave the source unchanged.

// COM interface outputs.

template<typename TInterface, typename TImpl>
void returnComPtrCopy(TInterface** outInterface, TImpl* rawPtr)
{
    static_assert(!std::is_base_of<RefObject, TInterface>::value, "TInterface must be an interface type.");
    rawPtr->addRef();
    *outInterface = rawPtr;
}

template<typename TInterface, typename TImpl, typename Ownership>
void returnComPtrCopy(TInterface** outInterface, const RefPtrBase<TImpl, Ownership>& refPtr)
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

template<typename TInterface, typename TImpl>
void returnComPtr(TInterface** outInterface, RefPtr<TImpl>& refPtr)
{
    static_assert(!std::is_base_of<RefObject, TInterface>::value, "TInterface must be an interface type.");
    *outInterface = refPtr.detach();
}

// Implementation outputs.

template<typename TDest, typename TImpl, typename Ownership>
void returnRefPtrCopy(TDest** outPtr, const RefPtrBase<TImpl, Ownership>& refPtr)
{
    static_assert(std::is_base_of<RefObject, TDest>::value, "TDest must be a non-interface type.");
    static_assert(std::is_base_of<RefObject, TImpl>::value, "TImpl must be a non-interface type.");
    *outPtr = refPtr.get();
    refPtr->addReference();
}

template<typename TDest, typename TImpl>
void returnRefPtr(TDest** outPtr, RefPtr<TImpl>& refPtr)
{
    static_assert(std::is_base_of<RefObject, TDest>::value, "TDest must be a non-interface type.");
    static_assert(std::is_base_of<RefObject, TImpl>::value, "TImpl must be a non-interface type.");
    *outPtr = refPtr.detach();
}

} // namespace rhi

#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugResourceHeap : public DebugObject<IResourceHeap>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;
    IResourceHeap* getInterface(const Guid& guid);

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugResourceHeap);

public:
    virtual SLANG_NO_THROW const ResourceHeapDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::debug

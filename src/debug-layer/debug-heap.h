#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugHeap : public DebugObject<IHeap>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugHeap);

    IHeap* getInterface(const Guid& guid);

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL allocate(const HeapAllocDesc& desc, HeapAlloc* allocation) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL free(HeapAlloc allocation) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL report(HeapReport* outReport) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL flush() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL removeEmptyPages() override;
};

} // namespace rhi::debug

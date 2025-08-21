#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugGraphicsHeap : public DebugObject<IGraphicsHeap>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugGraphicsHeap);

    IGraphicsHeap* getInterface(const Guid& guid);

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL allocate(
        const GraphicsAllocDesc& desc,
        GraphicsAllocation* allocation
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL free(GraphicsAllocation allocation) override;

    virtual SLANG_NO_THROW Report SLANG_MCALL report() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL flush() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL cleanUp() override;
};

} // namespace rhi::debug

#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugQueryPool : public DebugObject<IQueryPool>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugQueryPool);

    QueryPoolDesc desc;

public:
    IQueryPool* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(uint32_t queryIndex, uint32_t count, uint64_t* data) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL reset() override;
};

} // namespace rhi::debug

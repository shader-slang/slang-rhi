#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class QueryPoolImpl : public QueryPoolBase
{
public:
    std::vector<uint64_t> m_queries;
    Result init(const QueryPoolDesc& desc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(GfxIndex queryIndex, GfxCount count, uint64_t* data) override;
};

} // namespace rhi::cpu

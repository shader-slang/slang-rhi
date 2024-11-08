#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class QueryPoolImpl : public QueryPool
{
public:
    std::vector<uint64_t> m_queries;

    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(GfxIndex queryIndex, GfxCount count, uint64_t* data) override;
};

} // namespace rhi::cpu

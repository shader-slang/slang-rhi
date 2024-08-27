// cpu-query.h
#pragma once
#include "cpu-base.h"

#include <vector>

namespace rhi
{
using namespace Slang;

namespace cpu
{

class QueryPoolImpl : public QueryPoolBase
{
public:
    std::vector<uint64_t> m_queries;
    Result init(const IQueryPool::Desc& desc);
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(
        GfxIndex queryIndex, GfxCount count, uint64_t* data) override;
};

} // namespace cpu
} // namespace rhi

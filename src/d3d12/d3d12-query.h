#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class QueryPoolImpl : public QueryPool
{
public:
    QueryPoolImpl(Device* device, const QueryPoolDesc& desc);
    ~QueryPoolImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL isResultReady(
        uint32_t queryIndex,
        uint32_t count,
        bool* outReady
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(
        uint32_t queryIndex,
        uint32_t count,
        uint64_t* outData
    ) override;

    void writeTimestamp(ID3D12GraphicsCommandList* cmdList, uint32_t index);

public:
    D3D12_QUERY_TYPE m_queryType;
    ComPtr<ID3D12QueryHeap> m_queryHeap;
    D3D12Resource m_readBackBuffer;
    uint8_t* m_mappedReadBackData = nullptr;
};

/// Implements the IQueryPool interface with a plain buffer.
/// Used for query types that does not correspond to a D3D query,
/// such as ray-tracing acceleration structure post-build info.
class PlainBufferProxyQueryPoolImpl : public QueryPool
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IQueryPool* getInterface(const Guid& guid);

public:
    PlainBufferProxyQueryPoolImpl(Device* device, const QueryPoolDesc& desc);
    ~PlainBufferProxyQueryPoolImpl();

    Result init(uint32_t stride);

    virtual SLANG_NO_THROW Result SLANG_MCALL isResultReady(
        uint32_t queryIndex,
        uint32_t count,
        bool* outReady
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(
        uint32_t queryIndex,
        uint32_t count,
        uint64_t* outData
    ) override;

public:
    QueryType m_queryType;
    RefPtr<BufferImpl> m_buffer;
    D3D12Resource m_readBackBuffer;
    uint8_t* m_mappedReadBackData = nullptr;
    uint32_t m_stride = 0;
    uint32_t m_count = 0;
};

} // namespace rhi::d3d12

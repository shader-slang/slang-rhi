#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class QueryPoolImpl : public QueryPool
{
public:
    Result init(const QueryPoolDesc& desc, DeviceImpl* device);

    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(uint32_t queryIndex, uint32_t count, uint64_t* data) override;

    void writeTimestamp(ID3D12GraphicsCommandList* cmdList, uint32_t index);

public:
    D3D12_QUERY_TYPE m_queryType;
    ComPtr<ID3D12QueryHeap> m_queryHeap;
    D3D12Resource m_readBackBuffer;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Fence> m_fence;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    HANDLE m_waitEvent;
    UINT64 m_eventValue = 0;
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
    Result init(const QueryPoolDesc& desc, DeviceImpl* device, uint32_t stride);

    virtual SLANG_NO_THROW Result SLANG_MCALL reset() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(uint32_t queryIndex, uint32_t count, uint64_t* data) override;

public:
    QueryType m_queryType;
    RefPtr<BufferImpl> m_buffer;
    RefPtr<DeviceImpl> m_device;
    std::vector<uint8_t> m_result;
    bool m_resultDirty = true;
    uint32_t m_stride = 0;
    uint32_t m_count = 0;
};

} // namespace rhi::d3d12

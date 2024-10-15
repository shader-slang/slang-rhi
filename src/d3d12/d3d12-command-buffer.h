#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class CommandBufferImpl : public ICommandBuffer, public ComObject
{
public:
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid);

public:
    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* handle) override;

public:
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
};

} // namespace rhi::d3d12

#pragma once

#include "metal-base.h"

#include <map>
#include <mutex>
#include <tuple>

namespace rhi::metal {

class BufferImpl : public Buffer
{
public:
    BufferImpl(Device* device, const BufferDesc& desc);
    ~BufferImpl();

    virtual void deleteThis() override;

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // IBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(
        DescriptorHandleAccess access,
        Format format,
        BufferRange range,
        DescriptorHandle* outHandle
    ) override;

    Result getTextureView(Format format, BufferRange range, DescriptorHandleAccess access, MTL::Texture** outView);

public:
    NS::SharedPtr<MTL::Buffer> m_buffer;
    DeviceAddress m_deviceAddress = 0;
    BufferImpl* m_nextAtSameAddr = nullptr;

private:
    // Views share the buffer's allocation and live until its deferred destruction.
    using TextureViewKey = std::tuple<Format, Offset, Size, DescriptorHandleAccess>;
    std::mutex m_textureViewsMutex;
    std::map<TextureViewKey, NS::SharedPtr<MTL::Texture>> m_textureViews;
};

} // namespace rhi::metal

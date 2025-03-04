#pragma once

#include "vk-base.h"

namespace rhi::vk {

class FenceImpl : public Fence
{
public:
    VkSemaphore m_semaphore = VK_NULL_HANDLE;

    FenceImpl(Device* device, const FenceDesc& desc);
    ~FenceImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentValue(uint64_t* outValue) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setCurrentValue(uint64_t value) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::vk

#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class BindlessDescriptorSet : public RefObject
{
public:
    BindlessDescriptorSet(DeviceImpl* device, const BindlessDesc& desc);
    ~BindlessDescriptorSet();

    Result initialize();

    Result allocBufferHandle(
        IBuffer* buffer,
        DescriptorHandleAccess access,
        Format format,
        BufferRange range,
        DescriptorHandle* outHandle
    );
    Result allocTextureHandle(ITextureView* textureView, DescriptorHandleAccess access, DescriptorHandle* outHandle);
    Result allocSamplerHandle(ISampler* sampler, DescriptorHandle* outHandle);
    Result allocAccelerationStructureHandle(IAccelerationStructure* accelerationStructure, DescriptorHandle* outHandle);
    Result freeHandle(const DescriptorHandle& handle);

public:
    DeviceImpl* m_device;
    BindlessDesc m_desc;

    GPUDescriptorRangeAllocation m_srvUavAllocation;
    GPUDescriptorRangeAllocation m_samplerAllocation;

    uint32_t m_srvUavHeapOffset;
    uint32_t m_samplerHeapOffset;

    uint32_t m_firstTextureHandle;
    uint32_t m_firstAccelerationStructureHandle;

    struct SlotAllocator
    {
        uint32_t capacity = 0;
        uint32_t count = 0;
        std::vector<uint32_t> freeSlots;

        Result allocate(uint32_t* outSlot)
        {
            if (!freeSlots.empty())
            {
                *outSlot = freeSlots.back();
                freeSlots.pop_back();
                return SLANG_OK;
            }
            if (count < capacity)
            {
                *outSlot = count++;
                return SLANG_OK;
            }
            return SLANG_E_OUT_OF_MEMORY;
        }

        Result free(uint32_t slot)
        {
            if (slot >= capacity)
            {
                return SLANG_E_INVALID_ARG;
            }
            freeSlots.push_back(slot);
            return SLANG_OK;
        }
    };

    SlotAllocator m_bufferAllocator;
    SlotAllocator m_textureAllocator;
    SlotAllocator m_samplerAllocator;
    SlotAllocator m_accelerationStructureAllocator;
};

} // namespace rhi::d3d12

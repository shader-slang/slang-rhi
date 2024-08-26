// d3d12-resource-views.h
#pragma once

#include "d3d12-base.h"
#include "../d3d/d3d-util.h"
#include "d3d12-buffer.h"

#include <map>

namespace gfx
{
namespace d3d12
{

using namespace Slang;

class ResourceViewImpl;

class ResourceViewInternalImpl
{
public:
    // The default descriptor for the view.
    D3D12Descriptor m_descriptor;

    // StructuredBuffer descriptors for different strides.
    std::map<uint32_t, D3D12Descriptor> m_mapBufferStrideToDescriptor;

    RefPtr<D3D12GeneralExpandingDescriptorHeap> m_allocator;

    ~ResourceViewInternalImpl();

    // Get a d3d12 descriptor from the buffer view with the given buffer element stride.
    SlangResult getBufferDescriptorForBinding(
        DeviceImpl* device,
        ResourceViewImpl* view,
        uint32_t bufferStride,
        D3D12Descriptor& outDescriptor);
};

SlangResult createD3D12BufferDescriptor(
    BufferResourceImpl* buffer,
    BufferResourceImpl* counterBuffer,
    IResourceView::Desc const& desc,
    uint32_t bufferStride,
    DeviceImpl* device,
    D3D12GeneralExpandingDescriptorHeap* descriptorHeap,
    D3D12Descriptor* outDescriptor);

class ResourceViewImpl
    : public ResourceViewBase
    , public ResourceViewInternalImpl
{
public:
    RefPtr<Resource> m_resource;
    // null, unless this is a structuredbuffer with a separate counter buffer
    RefPtr<Resource> m_counterResource;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(InteropHandle* outHandle) override;
};

#if SLANG_RHI_DXR

class AccelerationStructureImpl
    : public AccelerationStructureBase
    , public ResourceViewInternalImpl
{
public:
    RefPtr<BufferResourceImpl> m_buffer;
    uint64_t m_offset;
    uint64_t m_size;
    ComPtr<ID3D12Device5> m_device5;

public:
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(InteropHandle* outHandle) override;
};

#endif

} // namespace d3d12
} // namespace gfx

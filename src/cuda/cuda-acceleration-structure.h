#pragma once

#include "cuda-base.h"

#include "core/stable_vector.h"

#if SLANG_RHI_ENABLE_OPTIX

namespace rhi::cuda {

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    BreakableReference<DeviceImpl> m_device;
    CUdeviceptr m_buffer;
    CUdeviceptr m_propertyBuffer;
    OptixTraversableHandle m_handle;

public:
    AccelerationStructureImpl(DeviceImpl* device, const AccelerationStructureDesc& desc)
        : AccelerationStructure(desc)
        , m_device(device)
    {
    }

    ~AccelerationStructureImpl();

    virtual void comFree() override { m_device.breakStrongReference(); }

    // IAccelerationStructure implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW AccelerationStructureHandle getHandle() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
};

struct AccelerationStructureBuildInputBuilder
{
public:
    stable_vector<CUdeviceptr> pointerList;
    stable_vector<unsigned int> flagList;
    std::vector<OptixBuildInput> buildInputs;
    OptixAccelBuildOptions buildOptions;

    Result build(const AccelerationStructureBuildDesc& buildDesc, IDebugCallback* debugCallback);

private:
    unsigned int translateBuildFlags(AccelerationStructureBuildFlags flags) const;
    unsigned int translateGeometryFlags(AccelerationStructureGeometryFlags flags) const;
    OptixVertexFormat translateVertexFormat(Format format) const;
};

} // namespace rhi::cuda

#endif // SLANG_RHI_ENABLE_OPTIX

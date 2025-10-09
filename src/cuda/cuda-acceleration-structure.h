#pragma once

#include "cuda-base.h"

#include "core/stable_vector.h"

#if SLANG_RHI_ENABLE_OPTIX

namespace rhi::cuda {

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    CUdeviceptr m_buffer;
    CUdeviceptr m_propertyBuffer;
    OptixTraversableHandle m_handle;

public:
    AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc);
    ~AccelerationStructureImpl();

    // IAccelerationStructure implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW AccelerationStructureHandle getHandle() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;
};

struct AccelerationStructureBuildDescConverter
{
public:
    stable_vector<CUdeviceptr> pointerList;
    stable_vector<unsigned int> flagList;
    std::vector<OptixBuildInput> buildInputs;
    OptixAccelBuildOptions buildOptions;

    Result convert(const AccelerationStructureBuildDesc& buildDesc, IDebugCallback* debugCallback);

private:
    unsigned int translateBuildFlags(AccelerationStructureBuildFlags flags) const;
    unsigned int translateGeometryFlags(AccelerationStructureGeometryFlags flags) const;
    OptixVertexFormat translateVertexFormat(Format format) const;
};

} // namespace rhi::cuda

#endif // SLANG_RHI_ENABLE_OPTIX

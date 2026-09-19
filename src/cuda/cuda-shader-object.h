#pragma once

#include "../binding-data-storage.h"

#include "cuda-base.h"
#include "cuda-buffer.h"
#include "cuda-texture.h"
#include "cuda-shader-object-layout.h"
#include "cuda-constant-buffer-pool.h"

namespace rhi::cuda {

void shaderObjectSetBinding(
    ShaderObject* shaderObject,
    const ShaderOffset& offset,
    const ResourceSlot& slot,
    slang::BindingType bindingType
);

struct PreparedBindingData;

/// Borrows allocations and resource lifetimes from a command buffer or prepared record.
class BindingDataStorage : public rhi::BindingDataStorage
{
public:
    explicit BindingDataStorage(CommandBufferImpl& commandBuffer);
    BindingDataStorage(DeviceImpl* device, PreparedShaderObject& prepared);
    DeviceImpl* getDevice() const { return m_device; }
    /// Preserve CUDA stream-ordered retention for mutable roots. Prepared graphs retain all resources.
    void trackResources(RootShaderObject* rootObject);
    void trackResources(ShaderObject* shaderObject);
    Result allocateObjectData(
        ShaderObject* object,
        size_t size,
        ConstantBufferMemType memType,
        ConstantBufferPool::Allocation& allocation
    );
    Result finishObjectData(size_t size, ConstantBufferMemType memType, ConstantBufferPool::Allocation& allocation);

private:
    DeviceImpl* m_device;
    ConstantBufferPool* m_constantBufferPool = nullptr;
};

struct BindingDataBuilder
{
    explicit BindingDataBuilder(BindingDataStorage& storage)
        : m_storage(storage)
        , m_device(storage.getDevice())
    {
    }
    BindingDataBuilder(const BindingDataBuilder&) = delete;
    BindingDataBuilder& operator=(const BindingDataBuilder&) = delete;

    BindingDataStorage& m_storage;
    DeviceImpl* const m_device;
    BindingDataImpl* m_bindingData = nullptr;

    /// Bind this object as a root shader object
    Result bindAsRoot(
        RootShaderObject* shaderObject,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
    );

    struct ObjectData
    {
        void* host;
        CUdeviceptr device;
        size_t size;
    };

    Result writeObjectData(
        ShaderObject* shaderObject,
        ShaderObjectLayoutImpl* specializedLayout,
        ConstantBufferMemType memType,
        ObjectData& outData
    );
    Result writeObjectDataImpl(
        ShaderObject* shaderObject,
        ShaderObjectLayoutImpl* specializedLayout,
        ConstantBufferMemType memType,
        ObjectData& outData
    );

private:
    Result bindAsRootImpl(
        RootShaderObject* shaderObject,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
    );
};

struct BindingDataImpl : BindingData
{
    /// Global parameters in CUDA memory.
    CUdeviceptr globalParams;
    size_t globalParamsSize;

    struct EntryPointData
    {
        void* data;
        size_t size;
    };

    /// Entry point parameters in host memory.
    EntryPointData* entryPoints;
    uint32_t entryPointCount;
};

struct BindingCache
{
    void reset();
};

} // namespace rhi::cuda

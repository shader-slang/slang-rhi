#pragma once

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

struct BindingDataBuilder
{
    DeviceImpl* m_device;
    BindingCache* m_bindingCache;
    BindingDataImpl* m_bindingData;
    ConstantBufferPool* m_constantBufferPool;
    ArenaAllocator* m_allocator;

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

    Result writeObjectData(ShaderObject* shaderObject, ShaderObjectLayoutImpl* specializedLayout, ObjectData& outData);
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

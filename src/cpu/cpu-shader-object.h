#pragma once

#include "cpu-base.h"
#include "cpu-shader-object-layout.h"
#include "cpu-buffer.h"

namespace rhi::cpu {

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
    ArenaAllocator* m_allocator;

    /// Bind this object as a root shader object
    Result bindAsRoot(
        RootShaderObject* shaderObject,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
    );

    struct ObjectData
    {
        void* data;
        size_t size;
    };

    Result writeObjectData(ShaderObject* shaderObject, ShaderObjectLayoutImpl* specializedLayout, ObjectData& outData);
};

struct BindingDataImpl : BindingData
{
    void* globalData;
    struct EntryPointData
    {
        void* data;
    };
    EntryPointData* entryPoints;
    uint32_t entryPointCount;
};

struct BindingCache
{
    void reset();
};

} // namespace rhi::cpu

#pragma once

#include "../binding-data-storage.h"

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

struct PreparedBindingData;

/// Borrows allocations and resource lifetimes from a command buffer or prepared record.
class BindingDataStorage : public rhi::BindingDataStorage
{
public:
    explicit BindingDataStorage(CommandBufferImpl& commandBuffer);
    BindingDataStorage(DeviceImpl* device, PreparedShaderObject& prepared);
    DeviceImpl* getDevice() const { return m_device; }

private:
    DeviceImpl* m_device;
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
        void* data;
        size_t size;
    };

    Result writeObjectData(ShaderObject* shaderObject, ShaderObjectLayoutImpl* specializedLayout, ObjectData& outData);
    Result writeObjectDataImpl(
        ShaderObject* shaderObject,
        ShaderObjectLayoutImpl* specializedLayout,
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

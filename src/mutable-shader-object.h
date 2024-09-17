#pragma once

#include <slang-rhi.h>

#include "renderer-shared.h"
#include "core/common.h"

#include <map>
#include <set>
#include <vector>

namespace rhi {

class ShaderObjectLayout;

template<typename T>
class VersionedObjectPool
{
public:
    struct ObjectVersion
    {
        RefPtr<T> object;
        RefPtr<TransientResourceHeap> transientHeap;
        uint64_t transientHeapVersion;
        bool canRecycle() { return (transientHeap->getVersion() != transientHeapVersion); }
    };
    std::vector<ObjectVersion> objects;
    SlangInt lastAllocationIndex = -1;
    ObjectVersion& allocate(TransientResourceHeap* currentTransientHeap)
    {
        for (SlangInt i = 0; i < objects.size(); i++)
        {
            auto& object = objects[i];
            if (object.canRecycle())
            {
                object.transientHeap = currentTransientHeap;
                object.transientHeapVersion = currentTransientHeap->getVersion();
                lastAllocationIndex = i;
                return object;
            }
        }
        ObjectVersion v;
        v.transientHeap = currentTransientHeap;
        v.transientHeapVersion = currentTransientHeap->getVersion();
        objects.push_back(v);
        lastAllocationIndex = objects.size() - 1;
        return objects.back();
    }
    ObjectVersion& getLastAllocation() { return objects[lastAllocationIndex]; }
};

class MutableShaderObjectData
{
public:
    // Any "ordinary" / uniform data for this object
    std::vector<uint8_t> m_ordinaryData;

    bool m_dirty = true;

    Index getCount() { return m_ordinaryData.size(); }
    void setCount(Index count) { m_ordinaryData.resize(count); }
    uint8_t* getBuffer() { return m_ordinaryData.data(); }
    void markDirty() { m_dirty = true; }

    // We don't actually create any GPU buffers here, since they will be handled
    // by the immutable shader objects once the user calls `getCurrentVersion`.
    Buffer* getBufferResource(
        Device* device,
        slang::TypeLayoutReflection* elementLayout,
        slang::BindingType bindingType
    )
    {
        return nullptr;
    }
};

template<typename TShaderObject, typename TShaderObjectLayoutImpl>
class MutableShaderObject : public ShaderObjectBaseImpl<TShaderObject, TShaderObjectLayoutImpl, MutableShaderObjectData>
{
    typedef ShaderObjectBaseImpl<TShaderObject, TShaderObjectLayoutImpl, MutableShaderObjectData> Super;

protected:
    std::map<ShaderOffset, Binding> m_bindings;
    std::set<ShaderOffset> m_objectOffsets;
    VersionedObjectPool<ShaderObjectBase> m_shaderObjectVersions;
    bool m_dirty = true;
    bool isDirty()
    {
        if (m_dirty)
            return true;
        if (this->m_data.m_dirty)
            return true;
        for (auto& object : this->m_objects)
        {
            if (object && object->isDirty())
                return true;
        }
        return false;
    }

    void markDirty() { m_dirty = true; }

public:
    Result init(Device* device, ShaderObjectLayout* layout)
    {
        this->m_device = device;
        auto layoutImpl = static_cast<TShaderObjectLayoutImpl*>(layout);
        this->m_layout = layoutImpl;
        Index subObjectCount = layoutImpl->getSubObjectCount();
        this->m_objects.resize(subObjectCount);
        auto dataSize = layoutImpl->getElementTypeLayout()->getSize();
        SLANG_RHI_ASSERT(dataSize >= 0);
        this->m_data.setCount(dataSize);
        memset(this->m_data.getBuffer(), 0, dataSize);
        return SLANG_OK;
    }

public:
    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override { return this->m_data.getBuffer(); }
    virtual SLANG_NO_THROW size_t SLANG_MCALL getSize() override { return this->m_data.getCount(); }
    virtual SLANG_NO_THROW Result SLANG_MCALL
    setData(ShaderOffset const& offset, void const* data, size_t size) override
    {
        if (!size)
            return SLANG_OK;
        if (SlangInt(offset.uniformOffset + size) > this->m_data.getCount())
            this->m_data.setCount(offset.uniformOffset + size);
        memcpy(this->m_data.getBuffer() + offset.uniformOffset, data, size);
        this->m_data.markDirty();
        markDirty();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL setObject(ShaderOffset const& offset, IShaderObject* object) override
    {
        Super::setObject(offset, object);
        m_objectOffsets.emplace(offset);
        markDirty();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(ShaderOffset const& offset, Binding binding) override
    {
        m_bindings[offset] = binding;
        markDirty();
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    getCurrentVersion(ITransientResourceHeap* transientHeap, IShaderObject** outObject) override
    {
        if (!isDirty())
        {
            returnComPtr(outObject, getLastAllocatedShaderObject());
            return SLANG_OK;
        }

        RefPtr<ShaderObjectBase> object = allocateShaderObject(static_cast<TransientResourceHeap*>(transientHeap));
        SLANG_RETURN_ON_FAIL(object->setData(ShaderOffset(), this->m_data.getBuffer(), this->m_data.getCount()));
        for (auto it : m_bindings)
            SLANG_RETURN_ON_FAIL(object->setBinding(it.first, it.second));
        for (auto offset : m_objectOffsets)
        {
            if (offset.bindingRangeIndex < 0)
                return SLANG_E_INVALID_ARG;
            auto layout = this->getLayout();
            if (offset.bindingRangeIndex >= layout->getBindingRangeCount())
                return SLANG_E_INVALID_ARG;
            auto bindingRange = layout->getBindingRange(offset.bindingRangeIndex);

            auto subObject = this->m_objects[bindingRange.subObjectIndex + offset.bindingArrayIndex];
            if (subObject)
            {
                ComPtr<IShaderObject> subObjectVersion;
                SLANG_RETURN_ON_FAIL(subObject->getCurrentVersion(transientHeap, subObjectVersion.writeRef()));
                SLANG_RETURN_ON_FAIL(object->setObject(offset, subObjectVersion));
            }
        }
        m_dirty = false;
        this->m_data.m_dirty = false;
        returnComPtr(outObject, object);
        return SLANG_OK;
    }

public:
    RefPtr<ShaderObjectBase> allocateShaderObject(TransientResourceHeap* transientHeap)
    {
        auto& version = m_shaderObjectVersions.allocate(transientHeap);
        if (!version.object)
        {
            ComPtr<IShaderObject> shaderObject;
            SLANG_RETURN_NULL_ON_FAIL(this->m_device->createShaderObject(this->m_layout, shaderObject.writeRef()));
            version.object = static_cast<ShaderObjectBase*>(shaderObject.get());
        }
        return version.object;
    }
    RefPtr<ShaderObjectBase> getLastAllocatedShaderObject()
    {
        return m_shaderObjectVersions.getLastAllocation().object;
    }
};

// A proxy shader object to hold mutable shader parameters for global scope and entry-points.
class MutableRootShaderObject : public ShaderObjectBase
{
public:
    std::vector<uint8_t> m_data;
    std::map<ShaderOffset, Binding> m_bindings;
    std::map<ShaderOffset, RefPtr<ShaderObjectBase>> m_objects;
    std::map<ShaderOffset, std::vector<slang::SpecializationArg>> m_specializationArgs;
    std::vector<RefPtr<MutableRootShaderObject>> m_entryPoints;
    RefPtr<Buffer> m_constantBufferOverride;
    slang::TypeLayoutReflection* m_elementTypeLayout;

    MutableRootShaderObject(Device* device, slang::TypeLayoutReflection* entryPointLayout)
    {
        this->m_device = device;
        m_elementTypeLayout = entryPointLayout;
        m_data.resize(entryPointLayout->getSize());
        memset(m_data.data(), 0, m_data.size());
    }

    MutableRootShaderObject(Device* device, RefPtr<ShaderProgram> program)
    {
        this->m_device = device;
        auto programLayout = program->slangGlobalScope->getLayout();
        SlangInt entryPointCount = programLayout->getEntryPointCount();
        for (SlangInt e = 0; e < entryPointCount; ++e)
        {
            auto slangEntryPoint = programLayout->getEntryPointByIndex(e);
            RefPtr<MutableRootShaderObject> entryPointObject =
                new MutableRootShaderObject(device, slangEntryPoint->getTypeLayout()->getElementTypeLayout());

            m_entryPoints.push_back(entryPointObject);
        }
        m_data.resize(programLayout->getGlobalParamsTypeLayout()->getSize());
        memset(m_data.data(), 0, m_data.size());
        m_elementTypeLayout = programLayout->getGlobalParamsTypeLayout();
    }

    virtual SLANG_NO_THROW slang::TypeLayoutReflection* SLANG_MCALL getElementTypeLayout() override
    {
        return m_elementTypeLayout;
    }

    virtual SLANG_NO_THROW ShaderObjectContainerType SLANG_MCALL getContainerType() override
    {
        return ShaderObjectContainerType::None;
    }

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override { return (GfxCount)m_entryPoints.size(); }

    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** entryPoint) override
    {
        returnComPtr(entryPoint, m_entryPoints[index]);
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL setData(ShaderOffset const& offset, void const* data, Size size) override
    {
        auto newSize = Index(size + offset.uniformOffset);
        if (newSize > m_data.size())
            m_data.resize((Index)newSize);
        memcpy(m_data.data() + offset.uniformOffset, data, size);
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL getObject(ShaderOffset const& offset, IShaderObject** object) override
    {
        *object = nullptr;

        auto it = m_objects.find(offset);
        if (it != m_objects.end())
        {
            returnComPtr(object, it->second);
        }
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL setObject(ShaderOffset const& offset, IShaderObject* object) override
    {
        m_objects[offset] = static_cast<ShaderObjectBase*>(object);
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(ShaderOffset const& offset, Binding binding) override
    {
        m_bindings[offset] = binding;
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    setSpecializationArgs(ShaderOffset const& offset, const slang::SpecializationArg* args, GfxCount count) override
    {
        std::vector<slang::SpecializationArg> specArgs;
        for (GfxIndex i = 0; i < count; i++)
        {
            specArgs.push_back(args[i]);
        }
        m_specializationArgs[offset] = specArgs;
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    getCurrentVersion(ITransientResourceHeap* transientHeap, IShaderObject** outObject) override
    {
        return SLANG_FAIL;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    copyFrom(IShaderObject* other, ITransientResourceHeap* transientHeap) override
    {
        auto otherObject = static_cast<MutableRootShaderObject*>(other);
        *this = *otherObject;
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override { return m_data.data(); }

    virtual SLANG_NO_THROW Size SLANG_MCALL getSize() override { return (Size)m_data.size(); }

    virtual SLANG_NO_THROW Result SLANG_MCALL setConstantBufferOverride(IBuffer* constantBuffer) override
    {
        m_constantBufferOverride = static_cast<Buffer*>(constantBuffer);
        return SLANG_OK;
    }

    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override
    {
        SLANG_UNUSED(args);
        return SLANG_OK;
    }
};

} // namespace rhi

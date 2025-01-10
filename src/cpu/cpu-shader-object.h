#pragma once

#include "cpu-base.h"
#include "cpu-shader-object-layout.h"
#include "cpu-buffer.h"

namespace rhi::cpu {

class ShaderObjectImpl : public ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, SimpleShaderObjectData>
{
    typedef ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, SimpleShaderObjectData> Super;

public:
    static Result create(DeviceImpl* device, ShaderObjectLayoutImpl* layout, ShaderObjectImpl** outShaderObject);

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;

    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override;

    virtual SLANG_NO_THROW size_t SLANG_MCALL getSize() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    setData(ShaderOffset const& offset, void const* data, size_t size) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setObject(ShaderOffset const& offset, IShaderObject* object) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(ShaderOffset const& offset, Binding binding) override;

protected:
    virtual SLANG_NO_THROW Result SLANG_MCALL init(DeviceImpl* device, ShaderObjectLayoutImpl* typeLayout);

    uint8_t* getDataBuffer();

    std::vector<RefPtr<Resource>> m_resources;
};

class EntryPointShaderObjectImpl : public ShaderObjectImpl
{
public:
    EntryPointLayoutImpl* getLayout();
};

class RootShaderObjectImpl : public ShaderObjectImpl
{
public:
    // An overload for the `init` virtual function, with a more specific type
    Result init(DeviceImpl* device, RootShaderObjectLayoutImpl* programLayout);
    using ShaderObjectImpl::init;

    Result bake(Baker& baker, BakedRootShaderObjectImpl** outBakedObject) const;

    RootShaderObjectLayoutImpl* getLayout();

    EntryPointShaderObjectImpl* getEntryPoint(Index index);
    std::vector<RefPtr<EntryPointShaderObjectImpl>> m_entryPoints;

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;
    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;
};

struct BindingDataImpl : public BindingData
{
    std::unique_ptr<uint8_t[]> globalData;
    std::unique_ptr<uint8_t[]> entryPointData;
};

struct BindingCache : public RefObject
{
    std::vector<RefPtr<BindingData>> bindingData;
};

struct BindingContext
{
    DeviceImpl* device;
    BindingCache* bindingCache;

    BindingContext(DeviceImpl* device, BindingCache* bindingCache)
        : device(device)
        , bindingCache(bindingCache)
    {
    }
}


#if 0
class BakedRootShaderObjectImpl : public BakedShaderObjectImpl
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_layout;
    std::vector<RefPtr<BakedShaderObjectImpl>> m_entryPoints;

    RootShaderObjectLayoutImpl* getLayout() const { return m_layout; }
    BakedShaderObjectImpl* getEntryPoint(Index index) const { return m_entryPoints[index].get(); }
};

class Baker
{
public:
    std::vector<RefPtr<BakedRootShaderObjectImpl>> m_bakedRootObjects;

    Result bakeObject(ShaderObjectImpl* object, BakedShaderObjectImpl*& outBakedObject) { return SLANG_FAIL; }

    Result bakeRootObject(RootShaderObjectImpl* object, BakedRootShaderObjectImpl*& outBakedObject)
    {
        RefPtr<BakedRootShaderObjectImpl> bakedObject = new BakedRootShaderObjectImpl();
        size_t size = object->getSize();
        bakedObject->m_data.reset(new uint8_t[size]);
        std::memcpy(bakedObject->m_data.get(), object->getDataBuffer(), size);

        m_bakedRootObjects.push_back(bakedObject);
        outBakedObject = bakedObject.get();
        return SLANG_OK;
    }
};
#endif

} // namespace rhi::cpu

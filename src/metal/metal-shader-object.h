#pragma once

#include "metal-base.h"
#include "metal-helper-functions.h"
#include "metal-shader-object-layout.h"

#include <vector>

namespace rhi::metal {

class ShaderObjectImpl : public ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, SimpleShaderObjectData>
{
public:
    static Result create(DeviceImpl* device, ShaderObjectLayoutImpl* layout, ShaderObjectImpl** outShaderObject);

    ~ShaderObjectImpl();

    Device* getDevice() { return m_layout->getDevice(); }

    SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override { return 0; }

    SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override
    {
        *outEntryPoint = nullptr;
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override { return m_data.getBuffer(); }

    virtual SLANG_NO_THROW size_t SLANG_MCALL getSize() override { return (size_t)m_data.getCount(); }

    SLANG_NO_THROW Result SLANG_MCALL setData(ShaderOffset const& inOffset, void const* data, size_t inSize) override;

    SLANG_NO_THROW Result SLANG_MCALL setBinding(ShaderOffset const& offset, Binding binding) override;

public:
protected:
    friend class ProgramVars;

    Result init(DeviceImpl* device, ShaderObjectLayoutImpl* layout);

    /// Write the uniform/ordinary data of this object into the given `dest` buffer at the given `offset`
    Result _writeOrdinaryData(void* dest, size_t destSize, ShaderObjectLayoutImpl* layout);

    /// Ensure that the `m_ordinaryDataBuffer` has been created, if it is needed
    ///
    /// The `layout` type must represent a specialized layout for this
    /// type that includes any "pending" data.
    ///
    Result _ensureOrdinaryDataBufferCreatedIfNeeded(DeviceImpl* device, ShaderObjectLayoutImpl* layout);

    BufferImpl* _ensureArgumentBufferUpToDate(DeviceImpl* device, ShaderObjectLayoutImpl* layout);

    void writeOrdinaryDataIntoArgumentBuffer(
        slang::TypeLayoutReflection* argumentBufferTypeLayout,
        slang::TypeLayoutReflection* defaultTypeLayout,
        uint8_t* argumentBuffer,
        uint8_t* srcData
    );

    /// Bind the buffer for ordinary/uniform data, if needed
    ///
    /// The `ioOffset` parameter will be updated to reflect the constant buffer
    /// register consumed by the ordinary data buffer, if one was bound.
    ///
    Result _bindOrdinaryDataBufferIfNeeded(
        BindingContext* context,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* layout
    );

public:
    /// Bind this object as if it was declared as a `ConstantBuffer<T>` in Slang
    Result bindAsConstantBuffer(BindingContext* context, BindingOffset const& inOffset, ShaderObjectLayoutImpl* layout);

    /// Bind this object as if it was declared as a `ParameterBlock<T>` in Slang
    Result bindAsParameterBlock(BindingContext* context, BindingOffset const& inOffset, ShaderObjectLayoutImpl* layout);

    /// Bind this object as a value that appears in the body of another object.
    ///
    /// This case is directly used when binding an object for an interface-type
    /// sub-object range when static specialization is used. It is also used
    /// indirectly when binding sub-objects to constant buffer or parameter
    /// block ranges.
    ///
    Result bindAsValue(BindingContext* context, BindingOffset const& offset, ShaderObjectLayoutImpl* layout);

    // Because the binding ranges have already been reflected
    // and organized as part of each shader object layout,
    // the object itself can store its data in a small number
    // of simple arrays.

    /// The buffers that are part of the state of this object
    std::vector<RefPtr<BufferImpl>> m_buffers;
    std::vector<uint64_t> m_bufferOffsets;

    /// The texture views that are part of the state of this object
    std::vector<RefPtr<TextureViewImpl>> m_textureViews;

    /// The samplers that are part of the state of this object
    std::vector<RefPtr<SamplerImpl>> m_samplers;

    /// A constant buffer used to stored ordinary data for this object
    /// and existential-type sub-objects.
    ///
    /// Created on demand with `_createOrdinaryDataBufferIfNeeded()`
    RefPtr<BufferImpl> m_ordinaryDataBuffer;

    /// Argument buffer created on demand to bind as a parameter block.
    RefPtr<BufferImpl> m_argumentBuffer;

    bool m_isConstantBufferDirty = true;
    bool m_isArgumentBufferDirty = true;
};

class RootShaderObjectImpl : public ShaderObjectImpl
{
    typedef ShaderObjectImpl Super;

public:
    // virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    // virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

    static Result create(
        DeviceImpl* device,
        RootShaderObjectLayoutImpl* layout,
        RootShaderObjectImpl** outShaderObject
    );

    Result init(DeviceImpl* device, RootShaderObjectLayoutImpl* layout);

    RootShaderObjectLayoutImpl* getLayout() { return checked_cast<RootShaderObjectLayoutImpl*>(m_layout.Ptr()); }

    GfxCount SLANG_MCALL getEntryPointCount() override { return (GfxCount)m_entryPoints.size(); }
    Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override
    {
        returnComPtr(outEntryPoint, m_entryPoints[index]);
        return SLANG_OK;
    }

    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;

    /// Bind this object as a root shader object
    Result bindAsRoot(BindingContext* context, RootShaderObjectLayoutImpl* specializedLayout);

protected:
    std::vector<RefPtr<ShaderObjectImpl>> m_entryPoints;
};

} // namespace rhi::metal

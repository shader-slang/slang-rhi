#pragma once

#include "d3d11-base.h"
#include "d3d11-buffer.h"
#include "d3d11-helper-functions.h"
#include "d3d11-texture.h"
#include "d3d11-sampler.h"
#include "d3d11-shader-object-layout.h"

#include <set>

namespace rhi::d3d11 {

class ShaderObjectImpl : public ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, SimpleShaderObjectData>
{
public:
    static Result create(DeviceImpl* device, ShaderObjectLayoutImpl* layout, ShaderObjectImpl** outShaderObject);

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
    Result _writeOrdinaryData(void* dest, size_t destSize, ShaderObjectLayoutImpl* specializedLayout) const;

    /// Ensure that the `m_ordinaryDataBuffer` has been created, if it is needed
    ///
    /// The `specializedLayout` type must represent a specialized layout for this
    /// type that includes any "pending" data.
    ///
    Result _ensureOrdinaryDataBufferCreatedIfNeeded(DeviceImpl* device, ShaderObjectLayoutImpl* specializedLayout)
        const;

    /// Bind the buffer for ordinary/uniform data, if needed
    ///
    /// The `ioOffset` parameter will be updated to reflect the constant buffer
    /// register consumed by the ordinary data buffer, if one was bound.
    ///
    Result _bindOrdinaryDataBufferIfNeeded(
        BindingContext* context,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

public:
    /// Bind this object as if it was declared as a `ConstantBuffer<T>` in Slang
    Result bindAsConstantBuffer(
        BindingContext* context,
        BindingOffset const& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

    /// Bind this object as a value that appears in the body of another object.
    ///
    /// This case is directly used when binding an object for an interface-type
    /// sub-object range when static specialization is used. It is also used
    /// indirectly when binding sub-objects to constant buffer or parameter
    /// block ranges.
    ///
    Result bindAsValue(BindingContext* context, BindingOffset const& offset, ShaderObjectLayoutImpl* specializedLayout)
        const;

    // Set of resources to keep alive while this object is alive.
    std::set<RefPtr<Resource>> m_resources;

    // Because the binding ranges have already been reflected
    // and organized as part of each shader object layout,
    // the object itself can store its data in a small number
    // of simple arrays.
    /// The shader resource views (SRVs) that are part of the state of this object
    std::vector<ComPtr<ID3D11ShaderResourceView>> m_srvs;

    /// The unordered access views (UAVs) that are part of the state of this object
    std::vector<ComPtr<ID3D11UnorderedAccessView>> m_uavs;

    /// The samplers that are part of the state of this object
    std::vector<RefPtr<SamplerImpl>> m_samplers;

    /// A constant buffer used to stored ordinary data for this object
    /// and existential-type sub-objects.
    ///
    /// Created on demand with `_createOrdinaryDataBufferIfNeeded()`
    mutable RefPtr<BufferImpl> m_ordinaryDataBuffer;

    /// Get the layout of this shader object with specialization arguments considered
    ///
    /// This operation should only be called after the shader object has been
    /// fully filled in and finalized.
    ///
    Result _getSpecializedLayout(ShaderObjectLayoutImpl** outLayout);

    /// Create the layout for this shader object with specialization arguments considered
    ///
    /// This operation is virtual so that it can be customized by `ProgramVars`.
    ///
    virtual Result _createSpecializedLayout(ShaderObjectLayoutImpl** outLayout);

    RefPtr<ShaderObjectLayoutImpl> m_specializedLayout;
};

class RootShaderObjectImpl : public ShaderObjectImpl
{
    typedef ShaderObjectImpl Super;

public:
    static Result create(
        DeviceImpl* device,
        RootShaderObjectLayoutImpl* layout,
        RootShaderObjectImpl** outShaderObject
    );

    RootShaderObjectLayoutImpl* getLayout() { return checked_cast<RootShaderObjectLayoutImpl*>(m_layout.Ptr()); }

    GfxCount SLANG_MCALL getEntryPointCount() override { return (GfxCount)m_entryPoints.size(); }
    Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override
    {
        returnComPtr(outEntryPoint, m_entryPoints[index]);
        return SLANG_OK;
    }

    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;

    /// Bind this object as a root shader object
    Result bindAsRoot(BindingContext* context, RootShaderObjectLayoutImpl* specializedLayout) const;

protected:
    Result init(DeviceImpl* device, RootShaderObjectLayoutImpl* layout);

    Result _createSpecializedLayout(ShaderObjectLayoutImpl** outLayout) override;

    std::vector<RefPtr<ShaderObjectImpl>> m_entryPoints;
};

} // namespace rhi::d3d11

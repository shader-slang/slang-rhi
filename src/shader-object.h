#pragma once

#include <slang-rhi.h>

#include "core/common.h"
#include "core/short_vector.h"

#include "reference.h"

#include "device.h"

#include "rhi-shared-fwd.h"

#include <set>

namespace rhi {

struct ShaderObjectID
{
    uint32_t uid;
    uint32_t version;
    bool operator==(const ShaderObjectID& rhs) const { return uid == rhs.uid && version == rhs.version; }
    bool operator!=(const ShaderObjectID& rhs) const { return !(*this == rhs); }
};

struct ResourceSlot
{
    BindingType type = BindingType::Undefined;
    RefPtr<Resource> resource;
    RefPtr<Resource> resource2;
    Format format = Format::Undefined;
    union
    {
        BufferRange bufferRange = kEntireBuffer;
    };
    explicit operator bool() const { return type != BindingType::Undefined && resource; }
};

const ShaderComponentID kInvalidComponentID = 0xFFFFFFFF;

struct ExtendedShaderObjectType
{
    slang::TypeReflection* slangType;
    ShaderComponentID componentID;
};

struct ExtendedShaderObjectTypeList
{
    short_vector<ShaderComponentID, 16> componentIDs;
    short_vector<slang::SpecializationArg, 16> components;
    void add(const ExtendedShaderObjectType& component)
    {
        componentIDs.push_back(component.componentID);
        components.push_back(slang::SpecializationArg{slang::SpecializationArg::Kind::Type, {component.slangType}});
    }
    void addRange(const ExtendedShaderObjectTypeList& list)
    {
        for (uint32_t i = 0; i < list.getCount(); i++)
        {
            add(list[i]);
        }
    }
    ExtendedShaderObjectType operator[](uint32_t index) const
    {
        ExtendedShaderObjectType result;
        result.componentID = componentIDs[index];
        result.slangType = components[index].type;
        return result;
    }
    void clear()
    {
        componentIDs.clear();
        components.clear();
    }
    uint32_t getCount() const { return componentIDs.size(); }
};

class ExtendedShaderObjectTypeListObject : public ExtendedShaderObjectTypeList, public RefObject
{};

class ShaderObjectLayout : public RefObject
{
public:
    struct BindingRangeInfo
    {
        /// The type of bindings in this range
        slang::BindingType bindingType;

        /// The number of bindings in this range
        uint32_t count;

        /// An index into the binding slots array (for resources, samplers, etc.)
        uint32_t slotIndex;

        /// An index into the sub-object array if this binding range is treated
        /// as a sub-object.
        uint32_t subObjectIndex;

        /// Is this binding range specializable, e.g. an existential value or ParameterBlock<IFoo>.
        bool isSpecializable;
    };

    struct SubObjectRangeInfo
    {
        /// The index of the binding range that corresponds to this sub-object range
        uint32_t bindingRangeIndex;
    };

    struct EntryPointInfo
    {
        // TODO: remove if not needed
    };

protected:
    // We always use a weak reference to the `IDevice` object here.
    // `ShaderObject` implementations will make sure to hold a strong reference to `IDevice`
    // while a `ShaderObjectLayout` may still be used.
    Device* m_device;
    slang::TypeLayoutReflection* m_elementTypeLayout = nullptr;
    ShaderComponentID m_componentID = 0;

    /// The container type of this shader object. When `m_containerType` is `StructuredBuffer` or
    /// `UnsizedArray`, this shader object represents a collection instead of a single object.
    ShaderObjectContainerType m_containerType = ShaderObjectContainerType::None;

public:
    ComPtr<slang::ISession> m_slangSession;

    ShaderObjectContainerType getContainerType() { return m_containerType; }

    static slang::TypeLayoutReflection* _unwrapParameterGroups(
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectContainerType& outContainerType
    )
    {
        outContainerType = ShaderObjectContainerType::None;
        for (;;)
        {
            if (!typeLayout->getType())
            {
                if (auto elementTypeLayout = typeLayout->getElementTypeLayout())
                    typeLayout = elementTypeLayout;
            }
            switch (typeLayout->getKind())
            {
            case slang::TypeReflection::Kind::Array:
                SLANG_RHI_ASSERT(outContainerType == ShaderObjectContainerType::None);
                outContainerType = ShaderObjectContainerType::Array;
                typeLayout = typeLayout->getElementTypeLayout();
                return typeLayout;
            case slang::TypeReflection::Kind::Resource:
                if (typeLayout->getResourceShape() != SLANG_STRUCTURED_BUFFER)
                    return typeLayout;
                SLANG_RHI_ASSERT(outContainerType == ShaderObjectContainerType::None);
                outContainerType = ShaderObjectContainerType::StructuredBuffer;
                typeLayout = typeLayout->getElementTypeLayout();
                return typeLayout;
            case slang::TypeReflection::Kind::ConstantBuffer:
            case slang::TypeReflection::Kind::ParameterBlock:
                outContainerType = ShaderObjectContainerType::ParameterBlock;
                typeLayout = typeLayout->getElementTypeLayout();
                continue;
            default:
                return typeLayout;
            }
        }
    }

public:
    Device* getDevice() { return m_device; }

    slang::TypeLayoutReflection* getElementTypeLayout() { return m_elementTypeLayout; }

    ShaderComponentID getComponentID() { return m_componentID; }

    virtual uint32_t getSlotCount() const = 0;
    virtual uint32_t getSubObjectCount() const = 0;

    virtual uint32_t getBindingRangeCount() const = 0;
    virtual const BindingRangeInfo& getBindingRange(uint32_t index) const = 0;

    virtual uint32_t getSubObjectRangeCount() const = 0;
    virtual const SubObjectRangeInfo& getSubObjectRange(uint32_t index) const = 0;
    virtual ShaderObjectLayout* getSubObjectRangeLayout(uint32_t index) const = 0;

    virtual uint32_t getEntryPointCount() const { return 0; }
    virtual const EntryPointInfo& getEntryPoint(uint32_t index) const
    {
        SLANG_RHI_ASSERT_FAILURE("no entrypoints");
        static EntryPointInfo dummy = {};
        return dummy;
    }
    virtual ShaderObjectLayout* getEntryPointLayout(uint32_t index) const { return nullptr; }
    virtual slang::TypeLayoutReflection* getParameterBlockTypeLayout() { return m_elementTypeLayout; }

    void initBase(Device* device, slang::ISession* session, slang::TypeLayoutReflection* elementTypeLayout);
};


using ShaderObjectSetBindingHook = void (*)(
    ShaderObject* object,
    const ShaderOffset& offset,
    const ResourceSlot& slot,
    slang::BindingType bindingType
);

class ShaderObject : public IShaderObject, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IShaderObject* getInterface(const Guid& guid);

public:
    // A strong reference to `IDevice` to make sure the weak device reference in
    // `ShaderObjectLayout`s are valid whenever they might be used.
    BreakableReference<Device> m_device;

    // The shader object layout used to create this shader object.
    RefPtr<ShaderObjectLayout> m_layout;

    // The cached specialized shader object layout if the shader object has been finalized.
    RefPtr<ShaderObjectLayout> m_specializedLayout;

    short_vector<ResourceSlot> m_slots;
    short_vector<uint8_t> m_data;
    short_vector<RefPtr<ShaderObject>> m_objects;
    short_vector<RefPtr<ExtendedShaderObjectTypeListObject>> m_userProvidedSpecializationArgs;

    // Specialization args for a StructuredBuffer object.
    ExtendedShaderObjectTypeList m_structuredBufferSpecializationArgs;

    // Unique ID of the shader object (generated on construction).
    uint32_t m_uid;

    // Version of the shader object. Incremented on every modification.
    uint32_t m_version = 0;

    // True if the shader object is finalized and no further modifications are allowed.
    bool m_finalized = false;

    // The specialized shader object type.
    ExtendedShaderObjectType m_shaderObjectType = {nullptr, kInvalidComponentID};

    ShaderObjectSetBindingHook m_setBindingHook = nullptr;

public:
    void breakStrongReferenceToDevice() { m_device.breakStrongReference(); }

public:
    ShaderComponentID getComponentID() { return m_shaderObjectType.componentID; }

public:
    // IShaderObject implementation
    virtual SLANG_NO_THROW slang::TypeLayoutReflection* SLANG_MCALL getElementTypeLayout() override;
    virtual SLANG_NO_THROW ShaderObjectContainerType SLANG_MCALL getContainerType() override;
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(uint32_t index, IShaderObject** outEntryPoint) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setData(const ShaderOffset& offset, const void* data, Size size) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getObject(const ShaderOffset& offset, IShaderObject** outObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setObject(const ShaderOffset& offset, IShaderObject* object) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(const ShaderOffset& offset, const Binding& binding) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setDescriptorHandle(
        const ShaderOffset& offset,
        const DescriptorHandle& handle
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setSpecializationArgs(
        const ShaderOffset& offset,
        const slang::SpecializationArg* args,
        uint32_t count
    ) override;
    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override;
    virtual SLANG_NO_THROW Size SLANG_MCALL getSize() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setConstantBufferOverride(IBuffer* outBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL finalize() override;
    virtual SLANG_NO_THROW bool SLANG_MCALL isFinalized() override;

public:
    static Result create(Device* device, ShaderObjectLayout* layout, ShaderObject** outShaderObject);

    Result init(Device* device, ShaderObjectLayout* layout);

    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args);

    /// Write the uniform/ordinary data of this object into the given `dest` buffer at the given
    /// `offset`
    Result writeOrdinaryData(void* destData, Size destSize, ShaderObjectLayout* specializedLayout);

    Result writeStructuredBuffer(
        slang::TypeLayoutReflection* elementLayout,
        ShaderObjectLayout* specializedLayout,
        IBuffer** buffer
    );

    void trackResources(std::set<RefPtr<RefObject>>& resources);

protected:
    inline void incrementVersion() { m_version++; }

    inline Result checkFinalized() { return m_finalized ? SLANG_FAIL : SLANG_OK; }

    slang::TypeLayoutReflection* _getElementTypeLayout() { return m_layout->getElementTypeLayout(); }

    // Get the final type this shader object represents. If the shader object's type has existential fields,
    // this function will return a specialized type using the bound sub-objects' type as specialization argument.
    Result getSpecializedShaderObjectType(ExtendedShaderObjectType* outType);

    Result getExtendedShaderTypeListFromSpecializationArgs(
        ExtendedShaderObjectTypeList& list,
        const slang::SpecializationArg* args,
        uint32_t count
    );

    void setSpecializationArgsForContainerElement(ExtendedShaderObjectTypeList& specializationArgs);

    /// Sets the RTTI ID and RTTI witness table fields of an existential value.
    Result setExistentialHeader(
        slang::TypeReflection* existentialType,
        slang::TypeReflection* concreteType,
        ShaderOffset offset
    );
};

class RootShaderObject : public ShaderObject
{
public:
    RefPtr<ShaderProgram> m_shaderProgram;

    std::vector<RefPtr<ShaderObject>> m_entryPoints;

public:
    // IShaderObject implementation
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL getEntryPointCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(uint32_t index, IShaderObject** outEntryPoint) override;

public:
    static Result create(Device* device, ShaderProgram* program, RootShaderObject** outRootShaderObject);

    Result init(Device* device, ShaderProgram* program);

    bool isSpecializable() const;

    Result getSpecializedLayout(const ExtendedShaderObjectTypeList& args, ShaderObjectLayout*& outSpecializedLayout);
    Result getSpecializedLayout(ShaderObjectLayout*& outSpecializedLayout);

    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;

    void trackResources(std::set<RefPtr<RefObject>>& resources);
};

bool _doesValueFitInExistentialPayload(
    slang::TypeLayoutReflection* concreteTypeLayout,
    slang::TypeLayoutReflection* existentialFieldLayout
);


} // namespace rhi

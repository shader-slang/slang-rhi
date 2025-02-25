#pragma once

#include <slang-rhi.h>

#include "slang-context.h"
#include "resource-desc-utils.h"
#include "command-list.h"

#include "core/common.h"
#include "core/short_vector.h"

#include <map>
#include <set>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rhi {

class Device;
class CommandEncoder;
class CommandList;

/// Common header for Desc struct types.
struct DescStructHeader
{
    StructType type;
    DescStructHeader* next;
};

// We use a `BreakableReference` to avoid the cyclic reference situation in rhi implementation.
// It is a common scenario where objects created from an `IDevice` implementation needs to hold
// a strong reference to the device object that creates them. For example, a `Buffer` or a
// `CommandQueue` needs to store a `m_device` member that points to the `IDevice`. At the same
// time, the device implementation may also hold a reference to some of the objects it created
// to represent the current device/binding state. Both parties would like to maintain a strong
// reference to each other to achieve robustness against arbitrary ordering of destruction that
// can be triggered by the user. However this creates cyclic reference situations that break
// the `RefPtr` recyling mechanism. To solve this problem, we instead make each object reference
// the device via a `BreakableReference<TDeviceImpl>` pointer. A breakable reference can be
// turned into a weak reference via its `breakStrongReference()` call.
// If we know there is a cyclic reference between an API object and the device/pool that creates it,
// we can break the cycle when there is no longer any public references that come from `ComPtr`s to
// the API object, by turning the reference to the device object from the API object to a weak
// reference.
// The following example illustrate how this mechanism works:
// Suppose we have
// ```
// class DeviceImpl : IDevice { RefPtr<ShaderObject> m_currentObject; };
// class ShaderObjectImpl : IShaderObject { BreakableReference<DeviceImpl> m_device; };
// ```
// And the user creates a device and a shader object, then somehow having the device reference
// the shader object (this may not happen in actual implemetations, we just use it to illustrate
// the situation):
// ```
// ComPtr<IDevice> device = createDevice();
// ComPtr<ISomeResource> res = device->createResourceX(...);
// device->m_currentResource = res;
// ```
// This setup is robust to any destruction ordering. If user releases reference to `device` first,
// then the device object will not be freed yet, since there is still a strong reference to the device
// implementation via `res->m_device`. Next when the user releases reference to `res`, the public
// reference count to `res` via `ComPtr`s will go to 0, therefore triggering the call to
// `res->m_device.breakStrongReference()`, releasing the remaining reference to device. This will cause
// `device` to start destruction, which will release its strong reference to `res` during execution of
// its destructor. Finally, this will triger the actual destruction of `res`.
// On the other hand, if the user releases reference to `res` first, then the strong reference to `device`
// will be broken immediately, but the actual destruction of `res` will not start. Next when the user
// releases `device`, there will no longer be any other references to `device`, so the destruction of
// `device` will start, causing the release of the internal reference to `res`, leading to its destruction.
// Note that the above logic only works if it is known that there is a cyclic reference. If there are no
// such cyclic reference, then it will be incorrect to break the strong reference to `IDevice` upon
// public reference counter dropping to 0. This is because the actual destructor of `res` take place
// after breaking the cycle, but if the resource's strong reference to the device is already the last reference,
// turning that reference to weak reference will immediately trigger destruction of `device`, after which
// we can no longer destruct `res` if the destructor needs `device`. Therefore we need to be careful
// when using `BreakableReference`, and make sure we only call `breakStrongReference` only when it is known
// that there is a cyclic reference. Luckily for all scenarios so far this is statically known.
template<typename T>
class BreakableReference
{
private:
    RefPtr<T> m_strongPtr;
    T* m_weakPtr = nullptr;

public:
    BreakableReference() = default;

    BreakableReference(T* p) { *this = p; }

    BreakableReference(const RefPtr<T>& p) { *this = p; }

    void setWeakReference(T* p)
    {
        m_weakPtr = p;
        m_strongPtr = nullptr;
    }

    T& operator*() const { return *get(); }

    T* operator->() const { return get(); }

    T* get() const { return m_weakPtr; }

    operator T*() const { return get(); }

    void operator=(RefPtr<T>& p)
    {
        m_strongPtr = p;
        m_weakPtr = p.Ptr();
    }

    void operator=(T* p)
    {
        m_strongPtr = p;
        m_weakPtr = p;
    }

    void breakStrongReference() { m_strongPtr = nullptr; }

    void establishStrongReference() { m_strongPtr = m_weakPtr; }
};

// Helpers for returning an object implementation as COM pointer.
template<typename TInterface, typename TImpl>
void returnComPtr(TInterface** outInterface, TImpl* rawPtr)
{
    static_assert(!std::is_base_of<RefObject, TInterface>::value, "TInterface must be an interface type.");
    rawPtr->addRef();
    *outInterface = rawPtr;
}

template<typename TInterface, typename TImpl>
void returnComPtr(TInterface** outInterface, const RefPtr<TImpl>& refPtr)
{
    static_assert(!std::is_base_of<RefObject, TInterface>::value, "TInterface must be an interface type.");
    refPtr->addRef();
    *outInterface = refPtr.Ptr();
}

template<typename TInterface, typename TImpl>
void returnComPtr(TInterface** outInterface, ComPtr<TImpl>& comPtr)
{
    static_assert(!std::is_base_of<RefObject, TInterface>::value, "TInterface must be an interface type.");
    *outInterface = comPtr.detach();
}

// Helpers for returning an object implementation as RefPtr.
template<typename TDest, typename TImpl>
void returnRefPtr(TDest** outPtr, RefPtr<TImpl>& refPtr)
{
    static_assert(std::is_base_of<RefObject, TDest>::value, "TDest must be a non-interface type.");
    static_assert(std::is_base_of<RefObject, TImpl>::value, "TImpl must be a non-interface type.");
    *outPtr = refPtr.Ptr();
    refPtr->addReference();
}

template<typename TDest, typename TImpl>
void returnRefPtrMove(TDest** outPtr, RefPtr<TImpl>& refPtr)
{
    static_assert(std::is_base_of<RefObject, TDest>::value, "TDest must be a non-interface type.");
    static_assert(std::is_base_of<RefObject, TImpl>::value, "TImpl must be a non-interface type.");
    *outPtr = refPtr.detach();
}

class Fence : public IFence, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IFence* getInterface(const Guid& guid);

protected:
    NativeHandle sharedHandle = {};
};

class Resource : public ComObject
{};

class Buffer : public IBuffer, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IResource* getInterface(const Guid& guid);

public:
    Buffer(const BufferDesc& desc)
        : m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    BufferRange resolveBufferRange(const BufferRange& range);

    // IBuffer interface
    virtual SLANG_NO_THROW BufferDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

public:
    BufferDesc m_desc;
    StructHolder m_descHolder;
    NativeHandle m_sharedHandle;
};

class Texture : public ITexture, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IResource* getInterface(const Guid& guid);

public:
    Texture(const TextureDesc& desc)
        : m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    SubresourceRange resolveSubresourceRange(const SubresourceRange& range);
    bool isEntireTexture(const SubresourceRange& range);

    // ITexture interface
    virtual SLANG_NO_THROW TextureDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

public:
    TextureDesc m_desc;
    StructHolder m_descHolder;
    NativeHandle m_sharedHandle;
};

class TextureView : public ITextureView, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ITextureView* getInterface(const Guid& guid);

public:
    TextureView(const TextureViewDesc& desc)
        : m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    // ITextureView interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

public:
    TextureViewDesc m_desc;
    StructHolder m_descHolder;
};

class Sampler : public ISampler, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ISampler* getInterface(const Guid& guid);

public:
    Sampler(const SamplerDesc& desc)
        : m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    // ISampler interface
    virtual SLANG_NO_THROW const SamplerDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

public:
    SamplerDesc m_desc;
    StructHolder m_descHolder;
};

class AccelerationStructure : public IAccelerationStructure, public Resource
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IAccelerationStructure* getInterface(const Guid& guid);

public:
    AccelerationStructure(const AccelerationStructureDesc& desc)
        : m_desc(desc)
    {
        m_descHolder.holdString(m_desc.label);
    }

    // IAccelerationStructure interface
    virtual SLANG_NO_THROW AccelerationStructureHandle SLANG_MCALL getHandle() override;

public:
    AccelerationStructureDesc m_desc;
    StructHolder m_descHolder;
};

typedef uint32_t ShaderComponentID;
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

        uint32_t pendingOrdinaryDataOffset;
        uint32_t pendingOrdinaryDataStride;
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
            {
                if (typeLayout->getResourceShape() != SLANG_STRUCTURED_BUFFER)
                    break;
                SLANG_RHI_ASSERT(outContainerType == ShaderObjectContainerType::None);
                outContainerType = ShaderObjectContainerType::StructuredBuffer;
                typeLayout = typeLayout->getElementTypeLayout();
            }
                return typeLayout;
            case slang::TypeReflection::Kind::ConstantBuffer:
            case slang::TypeReflection::Kind::ParameterBlock:
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

    void initBase(Device* device, slang::ISession* session, slang::TypeLayoutReflection* elementTypeLayout);
};

bool _doesValueFitInExistentialPayload(
    slang::TypeLayoutReflection* concreteTypeLayout,
    slang::TypeLayoutReflection* existentialFieldLayout
);

struct ShaderObjectID
{
    uint32_t uid;
    uint32_t version;
    bool operator==(const ShaderObjectID& rhs) const { return uid == rhs.uid && version == rhs.version; }
    bool operator!=(const ShaderObjectID& rhs) const { return !(*this == rhs); }
};

struct ResourceSlot
{
    BindingType type = BindingType::Unknown;
    RefPtr<Resource> resource;
    RefPtr<Resource> resource2;
    Format format = Format::Unknown;
    union
    {
        BufferRange bufferRange = kEntireBuffer;
    };
    operator bool() const { return type != BindingType::Unknown && resource; }
};

class ShaderObject;

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
    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(const ShaderOffset& offset, Binding binding) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    setSpecializationArgs(const ShaderOffset& offset, const slang::SpecializationArg* args, uint32_t count) override;
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

class ShaderProgram;

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
};

struct BindingData
{};

struct SpecializationKey
{
    short_vector<ShaderComponentID> componentIDs;

    SpecializationKey(const ExtendedShaderObjectTypeList& args)
        : componentIDs(args.componentIDs)
    {
    }

    bool operator==(const SpecializationKey& rhs) const { return componentIDs == rhs.componentIDs; }

    struct Hasher
    {
        std::size_t operator()(const SpecializationKey& key) const
        {
            size_t hash = 0;
            for (auto& arg : key.componentIDs)
                hash_combine(hash, arg);
            return hash;
        }
    };
};

class ShaderProgram : public IShaderProgram, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IShaderProgram* getInterface(const Guid& guid);

    ShaderProgramDesc m_desc;

    ComPtr<slang::IComponentType> slangGlobalScope;
    std::vector<ComPtr<slang::IComponentType>> slangEntryPoints;

    // Linked program when linkingStyle is GraphicsCompute, or the original global scope
    // when linking style is RayTracing.
    ComPtr<slang::IComponentType> linkedProgram;

    // Linked program for each entry point when linkingStyle is RayTracing.
    std::vector<ComPtr<slang::IComponentType>> linkedEntryPoints;

    bool m_isSpecializable = false;

    bool m_compiledShaders = false;

    std::unordered_map<SpecializationKey, RefPtr<ShaderProgram>, SpecializationKey::Hasher> m_specializedPrograms;

    void init(const ShaderProgramDesc& desc);

    bool isSpecializable() const { return m_isSpecializable; }
    bool isMeshShaderProgram() const;

    Result compileShaders(Device* device);

    virtual Result createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode);

    virtual SLANG_NO_THROW slang::TypeReflection* SLANG_MCALL findTypeByName(const char* name) override
    {
        return linkedProgram->getLayout()->findTypeByName(name);
    }

    virtual ShaderObjectLayout* getRootShaderObjectLayout() = 0;

private:
    bool _isSpecializable()
    {
        if (slangGlobalScope->getSpecializationParamCount() != 0)
        {
            return true;
        }
        for (auto& entryPoint : slangEntryPoints)
        {
            if (entryPoint->getSpecializationParamCount() != 0)
            {
                return true;
            }
        }
        return false;
    }
};

class InputLayout : public IInputLayout, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IInputLayout* getInterface(const Guid& guid);
};

class QueryPool : public IQueryPool, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IQueryPool* getInterface(const Guid& guid);

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL reset() override { return SLANG_OK; }

public:
    QueryPoolDesc m_desc;
};

template<typename TDevice>
class CommandQueue : public ICommandQueue, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandQueue* getInterface(const Guid& guid)
    {
        if (guid == ISlangUnknown::getTypeGuid() || guid == ICommandQueue::getTypeGuid())
            return static_cast<ICommandQueue*>(this);
        return nullptr;
    }

    virtual void comFree() override { breakStrongReferenceToDevice(); }

public:
    CommandQueue(TDevice* device, QueueType type)
    {
        m_device.setWeakReference(device);
        m_type = type;
    }

    void breakStrongReferenceToDevice() { m_device.breakStrongReference(); }
    void establishStrongReferenceToDevice() { m_device.establishStrongReference(); }

    // ICommandQueue implementation
    virtual SLANG_NO_THROW QueueType SLANG_MCALL getType() override { return m_type; }

public:
    BreakableReference<TDevice> m_device;
    QueueType m_type;
};

class RenderPassEncoder : public IRenderPassEncoder
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_QUERY_INTERFACE
    IRenderPassEncoder* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

public:
    CommandEncoder* m_commandEncoder;
    ComPtr<IRenderPipeline> m_pipeline;
    RefPtr<RootShaderObject> m_rootObject;
    RenderState m_renderState;
    /// Command list, nullptr if pass encoder is not active.
    CommandList* m_commandList = nullptr;

    RenderPassEncoder(CommandEncoder* commandEncoder);

    void writeRenderState();

    // IRenderPassEncoder implementation
    virtual SLANG_NO_THROW IShaderObject* SLANG_MCALL bindPipeline(IRenderPipeline* pipeline) override;
    virtual SLANG_NO_THROW void SLANG_MCALL bindPipeline(IRenderPipeline* pipeline, IShaderObject* rootObject) override;
    virtual SLANG_NO_THROW void SLANG_MCALL setRenderState(const RenderState& state) override;
    virtual SLANG_NO_THROW void SLANG_MCALL draw(const DrawArguments& args) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexed(const DrawArguments& args) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndirect(
        uint32_t maxDrawCount,
        IBuffer* argBuffer,
        uint64_t argOffset,
        IBuffer* countBuffer = nullptr,
        uint64_t countOffset = 0
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexedIndirect(
        uint32_t maxDrawCount,
        IBuffer* argBuffer,
        uint64_t argOffset,
        IBuffer* countBuffer = nullptr,
        uint64_t countOffset = 0
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawMeshTasks(uint32_t x, uint32_t y, uint32_t z) override;

    // IPassEncoder implementation
    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;
};

class ComputePassEncoder : public IComputePassEncoder
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_QUERY_INTERFACE
    IComputePassEncoder* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

public:
    CommandEncoder* m_commandEncoder = nullptr;
    ComPtr<IComputePipeline> m_pipeline;
    RefPtr<RootShaderObject> m_rootObject;
    /// Command list, nullptr if pass encoder is not active.
    CommandList* m_commandList;

    ComputePassEncoder(CommandEncoder* commandEncoder);

    void writeComputeState();

    // IComputePassEncoder implementation
    virtual SLANG_NO_THROW IShaderObject* SLANG_MCALL bindPipeline(IComputePipeline* pipeline) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    bindPipeline(IComputePipeline* pipeline, IShaderObject* rootObject) override;
    virtual SLANG_NO_THROW void SLANG_MCALL dispatchCompute(uint32_t x, uint32_t y, uint32_t z) override;
    virtual SLANG_NO_THROW void SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, uint64_t offset) override;

    // IPassEncoder implementation
    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;
};

class RayTracingPassEncoder : public IRayTracingPassEncoder
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_QUERY_INTERFACE
    IRayTracingPassEncoder* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

public:
    CommandEncoder* m_commandEncoder = nullptr;
    ComPtr<IRayTracingPipeline> m_pipeline;
    ComPtr<IShaderTable> m_shaderTable;
    RefPtr<RootShaderObject> m_rootObject;
    /// Command list, nullptr if pass encoder is not active.
    CommandList* m_commandList;

    RayTracingPassEncoder(CommandEncoder* commandEncoder);

    void writeRayTracingState();

    // IRayTracingPassEncoder implementation
    virtual SLANG_NO_THROW IShaderObject* SLANG_MCALL
    bindPipeline(IRayTracingPipeline* pipeline, IShaderTable* shaderTable) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    bindPipeline(IRayTracingPipeline* pipeline, IShaderTable* shaderTable, IShaderObject* rootObject) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    dispatchRays(uint32_t rayGenShaderIndex, uint32_t width, uint32_t height, uint32_t depth) override;

    // IPassEncoder implementation
    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;
};

class CommandEncoder : public ICommandEncoder, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandEncoder* getInterface(const Guid& guid);

public:
    // Current command list to write to. Must be set by the derived class.
    CommandList* m_commandList = nullptr;

    RenderPassEncoder m_renderPassEncoder;
    ComputePassEncoder m_computePassEncoder;
    RayTracingPassEncoder m_rayTracingPassEncoder;

    // List of persisted pipeline specialization data.
    // This is populated during command encoding and later used when asynchronously resolving pipelines.
    std::vector<RefPtr<ExtendedShaderObjectTypeListObject>> m_pipelineSpecializationArgs;

    CommandEncoder();

    virtual Device* getDevice() = 0;
    virtual Result getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData) = 0;

    Result getPipelineSpecializationArgs(
        IPipeline* pipeline,
        IShaderObject* object,
        ExtendedShaderObjectTypeListObject*& outSpecializationArgs
    );
    Result resolvePipelines(Device* device);

    // ICommandEncoder implementation
    virtual SLANG_NO_THROW IRenderPassEncoder* SLANG_MCALL beginRenderPass(const RenderPassDesc& desc) override;
    virtual SLANG_NO_THROW IComputePassEncoder* SLANG_MCALL beginComputePass() override;
    virtual SLANG_NO_THROW IRayTracingPassEncoder* SLANG_MCALL beginRayTracingPass() override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyTexture(
        ITexture* dst,
        SubresourceRange dstSubresource,
        Offset3D dstOffset,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyTextureToBuffer(
        IBuffer* dst,
        Offset dstOffset,
        Size dstSize,
        Size dstRowStride,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL uploadTextureData(
        ITexture* dst,
        SubresourceRange subresourceRange,
        Offset3D offset,
        Extents extent,
        SubresourceData* subresourceData,
        uint32_t subresourceDataCount
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearBuffer(IBuffer* buffer, const BufferRange* range = nullptr) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearTexture(
        ITexture* texture,
        const ClearValue& clearValue = ClearValue(),
        const SubresourceRange* subresourceRange = nullptr,
        bool clearDepth = true,
        bool clearStencil = true
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    resolveQuery(IQueryPool* queryPool, uint32_t index, uint32_t count, IBuffer* buffer, uint64_t offset) override;

    virtual SLANG_NO_THROW void SLANG_MCALL buildAccelerationStructure(
        const AccelerationStructureBuildDesc& desc,
        IAccelerationStructure* dst,
        IAccelerationStructure* src,
        BufferWithOffset scratchBuffer,
        uint32_t propertyQueryCount,
        AccelerationStructureQueryDesc* queryDescs
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyAccelerationStructure(
        IAccelerationStructure* dst,
        IAccelerationStructure* src,
        AccelerationStructureCopyMode mode
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL queryAccelerationStructureProperties(
        uint32_t accelerationStructureCount,
        IAccelerationStructure** accelerationStructures,
        uint32_t queryCount,
        AccelerationStructureQueryDesc* queryDescs
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    convertCooperativeVectorMatrix(const ConvertCooperativeVectorMatrixDesc* descs, uint32_t descCount) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;
};

class CommandBuffer : public ICommandBuffer, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid);

public:
    CommandBuffer()
        : m_commandList(m_allocator, m_trackedObjects)
    {
    }
    virtual ~CommandBuffer() = default;

    virtual Result reset()
    {
        m_commandList.reset();
        m_allocator.reset();
        m_trackedObjects.clear();
        return SLANG_OK;
    }

    ArenaAllocator m_allocator;
    CommandList m_commandList;
    std::set<RefPtr<RefObject>> m_trackedObjects;
};

enum class PipelineType
{
    Render,
    Compute,
    RayTracing,
};

class Pipeline : public ComObject
{
public:
    RefPtr<ShaderProgram> m_program;

    virtual PipelineType getType() const = 0;
    virtual bool isVirtual() const { return false; }
};

class RenderPipeline : public IRenderPipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    virtual PipelineType getType() const override { return PipelineType::Render; }

    // IPipeline interface
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualRenderPipeline : public RenderPipeline
{
public:
    Device* m_device;
    RenderPipelineDesc m_desc;
    StructHolder m_descHolder;
    RefPtr<InputLayout> m_inputLayout;

    Result init(Device* device, const RenderPipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }

    // IRenderPipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipeline : public IComputePipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    virtual PipelineType getType() const override { return PipelineType::Compute; }

    // IPipeline interface
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualComputePipeline : public ComputePipeline
{
public:
    Device* m_device;
    ComputePipelineDesc m_desc;

    Result init(Device* device, const ComputePipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }

    // IComputePipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipeline : public IRayTracingPipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    virtual PipelineType getType() const override { return PipelineType::RayTracing; }

    // IPipeline interface
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualRayTracingPipeline : public RayTracingPipeline
{
public:
    Device* m_device;
    RayTracingPipelineDesc m_desc;
    StructHolder m_descHolder;

    Result init(Device* device, const RayTracingPipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }

    // IRayTracingPipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

struct ComponentKey
{
    std::string typeName;
    short_vector<ShaderComponentID> specializationArgs;
    size_t hash;
    void updateHash()
    {
        hash = std::hash<std::string_view>()(typeName);
        for (auto& arg : specializationArgs)
            hash_combine(hash, arg);
    }
    template<typename KeyType>
    bool operator==(const KeyType& other) const
    {
        if (typeName != other.typeName)
            return false;
        if (specializationArgs.size() != other.specializationArgs.size())
            return false;
        for (size_t i = 0; i < other.specializationArgs.size(); i++)
        {
            if (specializationArgs[i] != other.specializationArgs[i])
                return false;
        }
        return true;
    }
};

struct PipelineKey
{
    Pipeline* pipeline;
    short_vector<ShaderComponentID> specializationArgs;
    size_t hash;
    void updateHash()
    {
        hash = std::hash<void*>()(pipeline);
        for (auto& arg : specializationArgs)
            hash_combine(hash, arg);
    }
    bool operator==(const PipelineKey& other) const
    {
        if (pipeline != other.pipeline)
            return false;
        if (specializationArgs.size() != other.specializationArgs.size())
            return false;
        for (size_t i = 0; i < other.specializationArgs.size(); i++)
        {
            if (specializationArgs[i] != other.specializationArgs[i])
                return false;
        }
        return true;
    }
};

// A cache from specialization keys to a specialized `ShaderKernel`.
class ShaderCache : public RefObject
{
public:
    ShaderComponentID getComponentId(slang::TypeReflection* type);
    ShaderComponentID getComponentId(std::string_view name);
    ShaderComponentID getComponentId(ComponentKey key);

    RefPtr<Pipeline> getSpecializedPipeline(PipelineKey programKey)
    {
        auto it = specializedPipelines.find(programKey);
        if (it != specializedPipelines.end())
            return it->second;
        return nullptr;
    }

    void addSpecializedPipeline(PipelineKey key, RefPtr<Pipeline> specializedPipeline);

    void free()
    {
        componentIds = decltype(componentIds)();
        specializedPipelines = decltype(specializedPipelines)();
    }

protected:
    struct ComponentKeyHasher
    {
        std::size_t operator()(const ComponentKey& k) const { return k.hash; }
    };
    struct PipelineKeyHasher
    {
        std::size_t operator()(const PipelineKey& k) const { return k.hash; }
    };

    std::unordered_map<ComponentKey, ShaderComponentID, ComponentKeyHasher> componentIds;
    std::unordered_map<PipelineKey, RefPtr<Pipeline>, PipelineKeyHasher> specializedPipelines;
};

static const int kRayGenRecordSize = 64; // D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

class ShaderTable : public IShaderTable, public ComObject
{
public:
    std::vector<std::string> m_shaderGroupNames;
    std::vector<ShaderRecordOverwrite> m_recordOverwrites;

    uint32_t m_rayGenShaderCount;
    uint32_t m_missShaderCount;
    uint32_t m_hitGroupCount;
    uint32_t m_callableShaderCount;

    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IShaderTable* getInterface(const Guid& guid)
    {
        if (guid == ISlangUnknown::getTypeGuid() || guid == IShaderTable::getTypeGuid())
            return static_cast<IShaderTable*>(this);
        return nullptr;
    }

    Result init(const ShaderTableDesc& desc);
};

class Surface : public ISurface, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ISurface* getInterface(const Guid& guid);

public:
    const SurfaceInfo& getInfo() override { return m_info; }
    const SurfaceConfig& getConfig() override { return m_config; }

public:
    void setInfo(const SurfaceInfo& info);
    void setConfig(const SurfaceConfig& config);

    SurfaceInfo m_info;
    StructHolder m_infoHolder;
    SurfaceConfig m_config;
    StructHolder m_configHolder;
};


// Device implementation shared by all platforms.
// Responsible for shader compilation, specialization and caching.
class Device : public IDevice, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ADD_REF
    SLANG_COM_OBJECT_IUNKNOWN_RELEASE

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getFeatures(const char** outFeatures, size_t bufferSize, uint32_t* outFeatureCount) override;
    virtual SLANG_NO_THROW bool SLANG_MCALL hasFeature(const char* featureName) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getFormatSupport(Format format, FormatSupport* outFormatSupport) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSlangSession(slang::ISession** outSlangSession) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(const SlangUUID& uuid, void** outObject) override;
    IDevice* getInterface(const Guid& guid);

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& srcDesc, ITexture** outTexture) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureFromSharedHandle(
        NativeHandle handle,
        const TextureDesc& srcDesc,
        const Size size,
        ITexture** outTexture
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPipeline(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline(const ComputePipelineDesc& desc, IComputePipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRayTracingPipeline(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderObject(
        slang::ISession* session,
        slang::TypeReflection* type,
        ShaderObjectContainerType containerType,
        IShaderObject** outObject
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderObjectFromTypeLayout(slang::TypeLayoutReflection* typeLayout, IShaderObject** outObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRootShaderObject(IShaderProgram* program, IShaderObject** outObject) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE for platforms
    // without ray tracing support.
    virtual SLANG_NO_THROW Result SLANG_MCALL getAccelerationStructureSizes(
        const AccelerationStructureBuildDesc& desc,
        AccelerationStructureSizes* outSizes
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE for platforms
    // without ray tracing support.
    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE for platforms
    // without ray tracing support.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderTable(const ShaderTableDesc& desc, IShaderTable** outTable) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFences(uint32_t fenceCount, IFence** fences, uint64_t* fenceValues, bool waitForAll, uint64_t timeout)
        override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(size_t* outAlignment) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertyCount) override;

    // Provides a default implementation that returns SLANG_E_NOT_AVAILABLE.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    convertCooperativeVectorMatrix(const ConvertCooperativeVectorMatrixDesc* descs, uint32_t descCount) override;

    Result getEntryPointCodeFromShaderCache(
        slang::IComponentType* program,
        SlangInt entryPointIndex,
        SlangInt targetIndex,
        slang::IBlob** outCode,
        slang::IBlob** outDiagnostics = nullptr
    );

    Result getShaderObjectLayout(
        slang::ISession* session,
        slang::TypeReflection* type,
        ShaderObjectContainerType container,
        ShaderObjectLayout** outLayout
    );

    Result getShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    );

public:
    inline void handleMessage(DebugMessageType type, DebugMessageSource source, const char* message)
    {
        m_debugCallback->handleMessage(type, source, message);
    }

    inline void warning(const char* message)
    {
        handleMessage(DebugMessageType::Warning, DebugMessageSource::Layer, message);
    }

    Result createShaderObject(ShaderObjectLayout* layout, ShaderObject** outObject);
    Result createRootShaderObject(ShaderProgram* program, RootShaderObject** outObject);

    Result getSpecializedProgram(
        ShaderProgram* program,
        const ExtendedShaderObjectTypeList& specializationArgs,
        ShaderProgram** outSpecializedProgram
    );

    Result specializeProgram(
        ShaderProgram* program,
        const ExtendedShaderObjectTypeList& specializationArgs,
        ShaderProgram** outSpecializedProgram
    );

    Result getConcretePipeline(
        Pipeline* pipeline,
        ExtendedShaderObjectTypeList* specializationArgs,
        Pipeline*& outPipeline
    );

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    ) = 0;

    virtual Result createRootShaderObjectLayout(
        slang::IComponentType* program,
        slang::ProgramLayout* programLayout,
        ShaderObjectLayout** outLayout
    ) = 0;

    virtual void customizeShaderObject(ShaderObject* shaderObject) { SLANG_UNUSED(shaderObject); }

    virtual Result createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline);
    virtual Result createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline);
    virtual Result createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline);

protected:
    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc);

protected:
    std::vector<std::string> m_features;
    std::vector<CooperativeVectorProperties> m_cooperativeVectorProperties;

public:
    SlangContext m_slangContext;
    ShaderCache m_shaderCache;

    ComPtr<IPersistentShaderCache> m_persistentShaderCache;

    std::map<slang::TypeLayoutReflection*, RefPtr<ShaderObjectLayout>> m_shaderObjectLayoutCache;
    ComPtr<IPipelineCreationAPIDispatcher> m_pipelineCreationAPIDispatcher;

    IDebugCallback* m_debugCallback = nullptr;
};

bool isDepthFormat(Format format);
bool isStencilFormat(Format format);

bool isDebugLayersEnabled();

} // namespace rhi

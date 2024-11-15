#pragma once

#include <assert.h>
#include <float.h>

#include <slang-com-ptr.h>
#include <slang.h>

#if defined(SLANG_RHI_DYNAMIC)
#if defined(_MSC_VER)
#ifdef SLANG_RHI_DYNAMIC_EXPORT
#define SLANG_RHI_API SLANG_DLL_EXPORT
#else
#define SLANG_RHI_API __declspec(dllimport)
#endif
#else
// TODO: need to consider compiler capabilities
// #     ifdef SLANG_DYNAMIC_EXPORT
#define SLANG_RHI_API SLANG_DLL_EXPORT
// #     endif
#endif
#endif

#ifndef SLANG_RHI_API
#define SLANG_RHI_API
#endif

// clang-format off
/// Implement logical operators on a class enum for making it usable as a flags enum.
#define SLANG_RHI_ENUM_CLASS_OPERATORS(e_) \
    inline e_ operator& (e_ a, e_ b) { return static_cast<e_>(static_cast<int>(a)& static_cast<int>(b)); } \
    inline e_ operator| (e_ a, e_ b) { return static_cast<e_>(static_cast<int>(a)| static_cast<int>(b)); } \
    inline e_& operator|= (e_& a, e_ b) { a = a | b; return a; }; \
    inline e_& operator&= (e_& a, e_ b) { a = a & b; return a; }; \
    inline e_  operator~ (e_ a) { return static_cast<e_>(~static_cast<int>(a)); } \
    inline bool is_set(e_ val, e_ flag) { return (val & flag) != static_cast<e_>(0); } \
    inline void flip_bit(e_& val, e_ flag) { val = is_set(val, flag) ? (val & (~flag)) : (val | flag); }
// clang-format on

// Needed for building on cygwin with gcc
#undef Always
#undef None

// GLOBAL TODO: doc comments
// GLOBAL TODO: Rationalize integer types (not a smush of uint/int/Uint/Int/etc)
//    - need typedefs in rhi namespace for Count, Index, Size, Offset (ex. DeviceAddress)
//    - Index and Count are for arrays, and indexing into array - like things(XY coordinates of pixels, etc.)
//         - Count is also for anything where we need to measure how many of something there are. This includes things
//         like extents.
//    - Offset and Size are almost always for bytes and things measured in bytes.
namespace rhi {

using Slang::ComPtr;
using Slang::Guid;

typedef SlangResult Result;

// Had to move here, because Options needs types defined here
typedef SlangInt Int;
typedef SlangUInt UInt;
typedef uint64_t DeviceAddress;
typedef int GfxIndex;
typedef int GfxCount;
typedef size_t Size;
typedef size_t Offset;

const uint64_t kTimeoutInfinite = 0xFFFFFFFFFFFFFFFF;

enum class StructType
{
    D3D12DeviceExtendedDesc,
    D3D12ExperimentalFeaturesDesc,
    SlangSessionExtendedDesc,
    RayTracingValidationDesc
};

// TODO: Implementation or backend or something else?
enum class DeviceType
{
    Default,
    D3D11,
    D3D12,
    Vulkan,
    Metal,
    CPU,
    CUDA,
    WGPU,
};

// TODO: Is this actually a flag when there are no bit fields?
enum class AccessFlag
{
    None,
    Read,
    Write,
};

class IPersistentShaderCache;

/// Defines how linking should be performed for a shader program.
enum class LinkingStyle
{
    // Compose all entry-points in a single program, then compile all entry-points together with the same
    // set of root shader arguments.
    SingleProgram,

    // Link and compile each entry-point individually, potentially with different specializations.
    SeparateEntryPointCompilation
};

struct ShaderProgramDesc
{
    // TODO: Tess doesn't like this but doesn't know what to do about it
    // The linking style of this program.
    LinkingStyle linkingStyle = LinkingStyle::SingleProgram;

    // The global scope or a Slang composite component that represents the entire program.
    slang::IComponentType* slangGlobalScope;

    // An array of Slang entry points. The size of the array must be `slangEntryPointCount`.
    // Each element must define only 1 Slang EntryPoint.
    slang::IComponentType** slangEntryPoints = nullptr;

    // Number of separate entry point components in the `slangEntryPoints` array to link in.
    // If set to 0, then `slangGlobalScope` must contain Slang EntryPoint components.
    // If not 0, then `slangGlobalScope` must not contain any EntryPoint components.
    GfxCount slangEntryPointCount = 0;
};

class IShaderProgram : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x19cabd0d, 0xf3e3, 0x4b3d, {0x93, 0x43, 0xea, 0xcc, 0x00, 0x1e, 0xc5, 0xf2});

public:
    virtual SLANG_NO_THROW slang::TypeReflection* SLANG_MCALL findTypeByName(const char* name) = 0;
};

// clang-format on

// TODO: This should be generated from above
// TODO: enum class should be explicitly uint32_t or whatever's appropriate
/// Different formats of things like pixels or elements of vertices
/// NOTE! Any change to this type (adding, removing, changing order) - must also be reflected in changes
/// SLANG_RHI_FORMAT
enum class Format
{
    // D3D formats omitted: 19-22, 44-47, 65-66, 68-70, 73, 76, 79, 82, 88-89, 92-94, 97, 100-114
    // These formats are omitted due to lack of a corresponding Vulkan format. D24_UNORM_S8_UINT (DXGI_FORMAT 45)
    // has a matching Vulkan format but is also omitted as it is only supported by Nvidia.
    Unknown,

    R32G32B32A32_TYPELESS,
    R32G32B32_TYPELESS,
    R32G32_TYPELESS,
    R32_TYPELESS,

    R16G16B16A16_TYPELESS,
    R16G16_TYPELESS,
    R16_TYPELESS,

    R8G8B8A8_TYPELESS,
    R8G8_TYPELESS,
    R8_TYPELESS,
    B8G8R8A8_TYPELESS,

    R32G32B32A32_FLOAT,
    R32G32B32_FLOAT,
    R32G32_FLOAT,
    R32_FLOAT,

    R16G16B16A16_FLOAT,
    R16G16_FLOAT,
    R16_FLOAT,

    R32G32B32A32_UINT,
    R32G32B32_UINT,
    R32G32_UINT,
    R32_UINT,

    R16G16B16A16_UINT,
    R16G16_UINT,
    R16_UINT,

    R8G8B8A8_UINT,
    R8G8_UINT,
    R8_UINT,

    R32G32B32A32_SINT,
    R32G32B32_SINT,
    R32G32_SINT,
    R32_SINT,

    R16G16B16A16_SINT,
    R16G16_SINT,
    R16_SINT,

    R8G8B8A8_SINT,
    R8G8_SINT,
    R8_SINT,

    R16G16B16A16_UNORM,
    R16G16_UNORM,
    R16_UNORM,

    R8G8B8A8_UNORM,
    R8G8B8A8_UNORM_SRGB,
    R8G8_UNORM,
    R8_UNORM,
    B8G8R8A8_UNORM,
    B8G8R8A8_UNORM_SRGB,
    B8G8R8X8_UNORM,
    B8G8R8X8_UNORM_SRGB,

    R16G16B16A16_SNORM,
    R16G16_SNORM,
    R16_SNORM,

    R8G8B8A8_SNORM,
    R8G8_SNORM,
    R8_SNORM,

    D32_FLOAT,
    D16_UNORM,
    D32_FLOAT_S8_UINT,
    R32_FLOAT_X32_TYPELESS,

    B4G4R4A4_UNORM,
    B5G6R5_UNORM,
    B5G5R5A1_UNORM,

    R9G9B9E5_SHAREDEXP,
    R10G10B10A2_TYPELESS,
    R10G10B10A2_UNORM,
    R10G10B10A2_UINT,
    R11G11B10_FLOAT,

    BC1_UNORM,
    BC1_UNORM_SRGB,
    BC2_UNORM,
    BC2_UNORM_SRGB,
    BC3_UNORM,
    BC3_UNORM_SRGB,
    BC4_UNORM,
    BC4_SNORM,
    BC5_UNORM,
    BC5_SNORM,
    BC6H_UF16,
    BC6H_SF16,
    BC7_UNORM,
    BC7_UNORM_SRGB,

    R64_UINT,
    R64_SINT,

    _Count,
};

enum class IndexFormat
{
    UInt16,
    UInt32,
};

// TODO: Aspect = Color, Depth, Stencil, etc.
// TODO: Channel = R, G, B, A, D, S, etc.
// TODO: Pick : pixel or texel
// TODO: Block is a good term for what it is
// TODO: Width/Height/Depth/whatever should not be used. We should use extentX, extentY, etc.
struct FormatInfo
{
    const char* name;
    /// The amount of channels in the format. Only set if the channelType is set.
    GfxCount channelCount;
    /// One of SlangScalarType None if type isn't made up of elements of type. TODO: Change to uint32_t?
    uint8_t channelType;
    /// The size of a block in bytes.
    Size blockSizeInBytes;
    /// The number of pixels contained in a block.
    GfxCount pixelsPerBlock;
    /// The width of a block in pixels.
    GfxCount blockWidth;
    /// The height of a block in pixels.
    GfxCount blockHeight;

    bool isTypeless : 1;
    bool isCompressed : 1;
};

enum class FormatSupport
{
    None = 0x0,

    Buffer = 0x1,
    IndexBuffer = 0x2,
    VertexBuffer = 0x4,

    Texture = 0x8,
    DepthStencil = 0x10,
    RenderTarget = 0x20,
    Blendable = 0x40,

    ShaderLoad = 0x80,
    ShaderSample = 0x100,
    ShaderUavLoad = 0x200,
    ShaderUavStore = 0x400,
    ShaderAtomic = 0x800,
};
SLANG_RHI_ENUM_CLASS_OPERATORS(FormatSupport);

enum class InputSlotClass
{
    PerVertex,
    PerInstance
};

enum class PrimitiveTopology
{
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    PatchList,
};

enum class ResourceState
{
    Undefined,
    General,
    VertexBuffer,
    IndexBuffer,
    ConstantBuffer,
    StreamOutput,
    ShaderResource,
    UnorderedAccess,
    RenderTarget,
    DepthRead,
    DepthWrite,
    Present,
    IndirectArgument,
    CopySource,
    CopyDestination,
    ResolveSource,
    ResolveDestination,
    AccelerationStructure,
    AccelerationStructureBuildInput,
};

/// Describes how memory for the resource should be allocated for CPU access.
enum class MemoryType
{
    DeviceLocal,
    Upload,
    ReadBack,
};

enum class NativeHandleType
{
    Unknown = 0x00000000,

    Win32 = 0x00000001,
    FileDescriptor = 0x00000002,

    D3D12Device = 0x00020001,
    D3D12CommandQueue = 0x00020002,
    D3D12GraphicsCommandList = 0x00020003,
    D3D12Resource = 0x00020004,
    D3D12PipelineState = 0x00020005,
    D3D12StateObject = 0x00020006,
    D3D12CpuDescriptorHandle = 0x00020007,
    D3D12Fence = 0x00020008,
    D3D12DeviceAddress = 0x00020009,

    VkDevice = 0x00030001,
    VkPhysicalDevice = 0x00030002,
    VkInstance = 0x00030003,
    VkQueue = 0x00030004,
    VkCommandBuffer = 0x00030005,
    VkBuffer = 0x00030006,
    VkImage = 0x00030007,
    VkImageView = 0x00030008,
    VkAccelerationStructureKHR = 0x00030009,
    VkSampler = 0x0003000a,
    VkPipeline = 0x0003000b,
    VkSemaphore = 0x0003000c,

    MTLDevice = 0x00040001,
    MTLCommandQueue = 0x00040002,
    MTLCommandBuffer = 0x00040003,
    MTLTexture = 0x00040004,
    MTLBuffer = 0x00040005,
    MTLComputePipelineState = 0x00040006,
    MTLRenderPipelineState = 0x00040007,
    MTLSharedEvent = 0x00040008,
    MTLSamplerState = 0x00040009,
    MTLAccelerationStructure = 0x0004000a,

    CUdevice = 0x00050001,
    CUdeviceptr = 0x00050002,
    CUtexObject = 0x00050003,
    CUstream = 0x00050004,

    OptixDeviceContext = 0x00060001,
    OptixTraversableHandle = 0x00060002,

    WGPUDevice = 0x00070001,
    WGPUBuffer = 0x00070002,
    WGPUTexture = 0x00070003,
    WGPUSampler = 0x00070004,
    WGPURenderPipeline = 0x00070005,
    WGPUComputePipeline = 0x00070006,
    WGPUQueue = 0x00070007,
    WGPUCommandBuffer = 0x00070008,
    WGPUTextureView = 0x00070009,
    WGPUCommandEncoder = 0x0007000a,
};

struct NativeHandle
{
    NativeHandleType type = NativeHandleType::Unknown;
    uint64_t value = 0;

    operator bool() const { return type != NativeHandleType::Unknown; }
};

struct InputElementDesc
{
    /// The name of the corresponding parameter in shader code.
    char const* semanticName;
    /// The index of the corresponding parameter in shader code. Only needed if multiple parameters share a semantic
    /// name.
    GfxIndex semanticIndex;
    /// The format of the data being fetched for this element.
    Format format;
    /// The offset in bytes of this element from the start of the corresponding chunk of vertex stream data.
    Offset offset;
    /// The index of the vertex stream to fetch this element's data from.
    GfxIndex bufferSlotIndex;
};

struct VertexStreamDesc
{
    /// The stride in bytes for this vertex stream.
    Size stride;
    /// Whether the stream contains per-vertex or per-instance data.
    InputSlotClass slotClass;
    /// How many instances to draw per chunk of data.
    GfxCount instanceDataStepRate;
};

struct InputLayoutDesc
{
    InputElementDesc const* inputElements = nullptr;
    GfxCount inputElementCount = 0;
    VertexStreamDesc const* vertexStreams = nullptr;
    GfxCount vertexStreamCount = 0;
};

// Declare opaque type
class IInputLayout : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x8957d16c, 0xdbc6, 0x4bb4, {0xb9, 0xa4, 0x8e, 0x22, 0xa1, 0xe8, 0xcc, 0x72});
};

class IResource : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0xa8dd4704, 0xf000, 0x4278, {0x83, 0x4d, 0x29, 0x4c, 0xef, 0xfe, 0x95, 0x93});

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) = 0;
};

enum class CpuAccessMode
{
    Read,
    Write,
};

struct BufferRange
{
    /// Offset in bytes.
    Offset offset = 0;
    /// Size in bytes.
    Size size = 0;

    bool operator==(const BufferRange& other) const { return offset == other.offset && size == other.size; }
    bool operator!=(const BufferRange& other) const { return !(*this == other); }
};

static const BufferRange kEntireBuffer = BufferRange{0ull, ~0ull};

enum class BufferUsage
{
    None = 0,
    VertexBuffer = (1 << 0),
    IndexBuffer = (1 << 1),
    ConstantBuffer = (1 << 2),
    ShaderResource = (1 << 3),
    UnorderedAccess = (1 << 4),
    IndirectArgument = (1 << 5),
    CopySource = (1 << 6),
    CopyDestination = (1 << 7),
    AccelerationStructure = (1 << 8),
    AccelerationStructureBuildInput = (1 << 9),
    ShaderTable = (1 << 10),
};
SLANG_RHI_ENUM_CLASS_OPERATORS(BufferUsage);

struct BufferDesc
{
    /// Total size in bytes.
    Size size = 0;
    /// Get the element stride. If > 0, this is a structured buffer.
    Size elementSize = 0;
    /// Format used for typed views.
    Format format = Format::Unknown;

    MemoryType memoryType = MemoryType::DeviceLocal;

    BufferUsage usage = BufferUsage::None;
    ResourceState defaultState = ResourceState::Undefined;

    bool isShared = false;

    /// The name of the buffer for debugging purposes.
    const char* label = nullptr;
};

class IBuffer : public IResource
{
    SLANG_COM_INTERFACE(0xf3eeb08f, 0xa0cc, 0x4eea, {0x93, 0xfd, 0x2a, 0xfe, 0x95, 0x1c, 0x7f, 0x63});

public:
    virtual SLANG_NO_THROW const BufferDesc& SLANG_MCALL getDesc() = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) = 0;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() = 0;
};

struct DepthStencilClearValue
{
    float depth = 1.0f;
    uint32_t stencil = 0;
};

union ColorClearValue
{
    float floatValues[4];
    uint32_t uintValues[4];
    int32_t intValues[4];
};

struct ClearValue
{
    ColorClearValue color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    DepthStencilClearValue depthStencil;
};

enum class TextureUsage
{
    None = 0,
    ShaderResource = (1 << 0),
    UnorderedAccess = (1 << 1),
    RenderTarget = (1 << 2),
    DepthRead = (1 << 3),
    DepthWrite = (1 << 4),
    Present = (1 << 5),
    CopySource = (1 << 6),
    CopyDestination = (1 << 7),
    ResolveSource = (1 << 8),
    ResolveDestination = (1 << 9),
};
SLANG_RHI_ENUM_CLASS_OPERATORS(TextureUsage);

enum class TextureType
{
    Texture1D,   ///< A 1d texture
    Texture2D,   ///< A 2d texture
    Texture3D,   ///< A 3d texture
    TextureCube, ///< A cubemap consists of 6 Texture2D like faces
};

enum class TextureAspect : uint32_t
{
    All = 0,
    DepthOnly = 1,
    StencilOnly = 2,
};

struct SubresourceRange
{
    GfxIndex mipLevel;
    GfxCount mipLevelCount;
    GfxIndex baseArrayLayer; // For Texture3D, this is WSlice.
    GfxCount layerCount;     // For cube maps, this is a multiple of 6.
    bool operator==(const SubresourceRange& other) const
    {
        return mipLevel == other.mipLevel && mipLevelCount == other.mipLevelCount &&
               baseArrayLayer == other.baseArrayLayer && layerCount == other.layerCount;
    }
    bool operator!=(const SubresourceRange& other) const { return !(*this == other); }
};

static const SubresourceRange kEntireTexture = SubresourceRange{0l, 0x7fffffffl, 0l, 0x7fffffffl};

/// Data for a single subresource of a texture.
///
/// Each subresource is a tensor with `1 <= rank <= 3`,
/// where the rank is deterined by the base shape of the
/// texture (Buffer, 1D, 2D, 3D, or Cube). For the common
/// case of a 2D texture, `rank == 2` and each subresource
/// is a 2D image.
///
/// Subresource tensors must be stored in a row-major layout,
/// so that the X axis strides over texels, the Y axis strides
/// over 1D rows of texels, and the Z axis strides over 2D
/// "layers" of texels.
///
/// For a texture with multiple mip levels or array elements,
/// each mip level and array element is stores as a distinct
/// subresource. When indexing into an array of subresources,
/// the index of a subresoruce for mip level `m` and array
/// index `a` is `m + a*mipLevelCount`.
///
struct SubresourceData
{
    /// Pointer to texel data for the subresource tensor.
    void const* data;

    /// Stride in bytes between rows of the subresource tensor.
    ///
    /// This is the number of bytes to add to a pointer to a texel
    /// at (X,Y,Z) to get to a texel at (X,Y+1,Z).
    ///
    /// Devices may not support all possible values for `strideY`.
    /// In particular, they may only support strictly positive strides.
    ///
    Size strideY;

    /// Stride in bytes between layers of the subresource tensor.
    ///
    /// This is the number of bytes to add to a pointer to a texel
    /// at (X,Y,Z) to get to a texel at (X,Y,Z+1).
    ///
    /// Devices may not support all possible values for `strideZ`.
    /// In particular, they may only support strictly positive strides.
    ///
    Size strideZ;
};

static const GfxCount kRemainingTextureSize = 0xffffffff;
struct Offset3D
{
    GfxIndex x = 0;
    GfxIndex y = 0;
    GfxIndex z = 0;
    Offset3D() = default;
    Offset3D(GfxIndex _x, GfxIndex _y, GfxIndex _z)
        : x(_x)
        , y(_y)
        , z(_z)
    {
    }
};

struct Extents
{
    /// Width in pixels.
    GfxCount width = 0;
    /// Height in pixels (if 2d or 3d).
    GfxCount height = 0;
    /// Depth (if 3d).
    GfxCount depth = 0;
};
struct TextureDesc
{
    TextureType type = TextureType::Texture2D;

    MemoryType memoryType = MemoryType::DeviceLocal;

    TextureUsage usage = TextureUsage::None;
    ResourceState defaultState = ResourceState::Undefined;
    bool isShared = false;

    Extents size;
    /// Array length.
    GfxCount arrayLength = 0;
    /// Number of mip levels - if 0 will create all mip levels.
    GfxCount mipLevelCount = 0;
    /// The resources format.
    Format format;
    /// Number of samples per pixel.
    GfxCount sampleCount = 1;
    /// The quality measure for the samples.
    int sampleQuality = 0;

    ClearValue* optimalClearValue = nullptr;

    /// The name of the texture for debugging purposes.
    const char* label = nullptr;
};

class ITexture : public IResource
{
    SLANG_COM_INTERFACE(0x423090a2, 0x8be7, 0x4421, {0x98, 0x71, 0x7e, 0xe2, 0x63, 0xf4, 0xea, 0x3d});

public:
    virtual SLANG_NO_THROW const TextureDesc& SLANG_MCALL getDesc() = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) = 0;
};

struct TextureViewDesc
{
    Format format = Format::Unknown;
    TextureAspect aspect = TextureAspect::All;
    SubresourceRange subresourceRange = kEntireTexture;
    const char* label = nullptr;
};

class ITextureView : public IResource
{
    SLANG_COM_INTERFACE(0xe6078d78, 0x3bd3, 0x40e8, {0x90, 0x42, 0x3b, 0x5e, 0x0c, 0x45, 0xde, 0x1f});
};

enum class ComparisonFunc : uint8_t
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

enum class TextureFilteringMode
{
    Point,
    Linear,
};

enum class TextureAddressingMode
{
    Wrap,
    ClampToEdge,
    ClampToBorder,
    MirrorRepeat,
    MirrorOnce,
};

enum class TextureReductionOp
{
    Average,
    Comparison,
    Minimum,
    Maximum,
};

struct SamplerDesc
{
    TextureFilteringMode minFilter = TextureFilteringMode::Linear;
    TextureFilteringMode magFilter = TextureFilteringMode::Linear;
    TextureFilteringMode mipFilter = TextureFilteringMode::Linear;
    TextureReductionOp reductionOp = TextureReductionOp::Average;
    TextureAddressingMode addressU = TextureAddressingMode::Wrap;
    TextureAddressingMode addressV = TextureAddressingMode::Wrap;
    TextureAddressingMode addressW = TextureAddressingMode::Wrap;
    float mipLODBias = 0.0f;
    uint32_t maxAnisotropy = 1;
    ComparisonFunc comparisonFunc = ComparisonFunc::Never;
    float borderColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float minLOD = 0.0f;
    float maxLOD = 1000.0f;

    const char* label = nullptr;
};

class ISampler : public IResource
{
    SLANG_COM_INTERFACE(0x0ce3b435, 0x5fdb, 0x4335, {0xaf, 0x43, 0xe0, 0x2d, 0x8b, 0x80, 0x13, 0xbc});

public:
    virtual SLANG_NO_THROW const SamplerDesc& SLANG_MCALL getDesc() = 0;
};

struct BufferWithOffset
{
    IBuffer* buffer = nullptr;
    Offset offset = 0;

    BufferWithOffset() = default;
    BufferWithOffset(IBuffer* buffer, Offset offset = 0)
        : buffer(buffer)
        , offset(offset)
    {
    }
    BufferWithOffset(ComPtr<IBuffer> buffer, Offset offset = 0)
        : buffer(buffer.get())
        , offset(offset)
    {
    }

    operator bool() const { return buffer != nullptr; }

    DeviceAddress getDeviceAddress() const { return buffer->getDeviceAddress() + offset; }
};

/// Opaque unique handle to an acceleration structure.
struct AccelerationStructureHandle
{
    uint64_t value = {};
};

enum class AccelerationStructureGeometryFlags
{
    None = 0,
    Opaque = (1 << 0),
    NoDuplicateAnyHitInvocation = (1 << 1)
};
SLANG_RHI_ENUM_CLASS_OPERATORS(AccelerationStructureGeometryFlags);

// The enum values are kept consistent with D3D12_RAYTRACING_INSTANCE_FLAGS
// and VkGeometryInstanceFlagBitsKHR.
enum class AccelerationStructureInstanceFlags : uint32_t
{
    None = 0,
    TriangleFacingCullDisable = (1 << 0),
    TriangleFrontCounterClockwise = (1 << 1),
    ForceOpaque = (1 << 2),
    NoOpaque = (1 << 3)
};
SLANG_RHI_ENUM_CLASS_OPERATORS(AccelerationStructureInstanceFlags);

enum class AccelerationStructureInstanceDescType
{
    Generic,
    D3D12,
    Vulkan,
    Optix,
    Metal
};

/// Generic instance descriptor.
/// The layout of this struct is intentionally consistent with D3D12_RAYTRACING_INSTANCE_DESC
/// and VkAccelerationStructureInstanceKHR for fast conversion.
struct AccelerationStructureInstanceDescGeneric
{
    float transform[3][4];
    uint32_t instanceID : 24;
    uint32_t instanceMask : 8;
    uint32_t instanceContributionToHitGroupIndex : 24;
    AccelerationStructureInstanceFlags flags : 8;
    AccelerationStructureHandle accelerationStructure;
};

/// Instance descriptor matching D3D12_RAYTRACING_INSTANCE_DESC.
struct AccelerationStructureInstanceDescD3D12
{
    float Transform[3][4];
    uint32_t InstanceID : 24;
    uint32_t InstanceMask : 8;
    uint32_t InstanceContributionToHitGroupIndex : 24;
    uint32_t Flags : 8;
    uint64_t AccelerationStructure;
};

/// Instance descriptor matching VkAccelerationStructureInstanceKHR.
struct AccelerationStructureInstanceDescVulkan
{
    float transform[4][3];
    uint32_t instanceCustomIndex : 24;
    uint32_t mask : 8;
    uint32_t instanceShaderBindingTableRecordOffset : 24;
    uint32_t flags : 8;
    uint64_t accelerationStructureReference;
};

/// Instance descriptor matching OptixInstance.
struct AccelerationStructureInstanceDescOptix
{
    float transform[3][4];
    uint32_t instanceId;
    uint32_t sbtOffset;
    uint32_t visibilityMask;
    uint32_t flags;
    uint64_t traversableHandle;
    uint32_t pad[2];
};

/// Instance descriptor matching MTLAccelerationStructureUserIDInstanceDescriptor.
struct AccelerationStructureInstanceDescMetal
{
    float transform[4][3];
    uint32_t options;
    uint32_t mask;
    uint32_t intersectionFunctionTableOffset;
    uint32_t accelerationStructureIndex;
    uint32_t userID;
};

struct AccelerationStructureAABB
{
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
};

enum class AccelerationStructureBuildInputType
{
    Instances,
    Triangles,
    ProceduralPrimitives,
};

struct AccelerationStructureBuildInput
{};

struct AccelerationStructureBuildInputInstances : public AccelerationStructureBuildInput
{
    const AccelerationStructureBuildInputType type = AccelerationStructureBuildInputType::Instances;

    BufferWithOffset instanceBuffer;
    Size instanceStride;
    GfxCount instanceCount;
};

struct AccelerationStructureBuildInputTriangles : public AccelerationStructureBuildInput
{
    const AccelerationStructureBuildInputType type = AccelerationStructureBuildInputType::Triangles;

    /// List of vertex buffers, one for each motion step.
    BufferWithOffset* vertexBuffers = nullptr;
    GfxCount vertexBufferCount = 0;
    Format vertexFormat = Format::Unknown;
    GfxCount vertexCount = 0;
    Size vertexStride = 0;

    BufferWithOffset indexBuffer;
    IndexFormat indexFormat = IndexFormat::UInt32;
    GfxCount indexCount = 0;

    /// Optional buffer containing 3x4 transform matrix applied to each vertex.
    BufferWithOffset preTransformBuffer;

    AccelerationStructureGeometryFlags flags;
};

struct AccelerationStructureBuildInputProceduralPrimitives : public AccelerationStructureBuildInput
{
    const AccelerationStructureBuildInputType type = AccelerationStructureBuildInputType::ProceduralPrimitives;

    /// List of AABB buffers, one for each motion step.
    BufferWithOffset* aabbBuffers = nullptr;
    GfxCount aabbBufferCount = 0;
    Size aabbStride = 0;
    GfxCount primitiveCount = 0;

    AccelerationStructureGeometryFlags flags;
};

struct AccelerationStructureBuildInputMotionOptions
{
    GfxCount keyCount = 1;
    float timeStart = 0.f;
    float timeEnd = 1.f;
};

enum class AccelerationStructureBuildMode
{
    Build,
    Update
};

enum class AccelerationStructureBuildFlags
{
    None = 0,
    AllowUpdate = (1 << 0),
    AllowCompaction = (1 << 1),
    PreferFastTrace = (1 << 2),
    PreferFastBuild = (1 << 3),
    MinimizeMemory = (1 << 4)
};
SLANG_RHI_ENUM_CLASS_OPERATORS(AccelerationStructureBuildFlags);

struct AccelerationStructureBuildDesc
{
    /// List of build inputs. All inputs must be of the same type.
    AccelerationStructureBuildInput* inputs = nullptr;
    GfxCount inputCount = 0;

    AccelerationStructureBuildInputMotionOptions motionOptions;

    AccelerationStructureBuildMode mode = AccelerationStructureBuildMode::Build;
    AccelerationStructureBuildFlags flags = AccelerationStructureBuildFlags::None;
};

struct AccelerationStructureSizes
{
    Size accelerationStructureSize = 0;
    Size scratchSize = 0;
    Size updateScratchSize = 0;
};

struct AccelerationStructureDesc
{
    Size size;

    const char* label = nullptr;
};

class IAccelerationStructure : public IResource
{
    SLANG_COM_INTERFACE(0x38b056d5, 0x63de, 0x49ca, {0xa0, 0xed, 0x62, 0xa1, 0xbe, 0xc3, 0xd4, 0x65});

public:
    virtual SLANG_NO_THROW AccelerationStructureHandle SLANG_MCALL getHandle() = 0;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() = 0;
};

struct FenceDesc
{
    uint64_t initialValue = 0;
    bool isShared = false;

    const char* label = nullptr;
};

class IFence : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x9daf743c, 0xbc69, 0x4887, {0x80, 0x8b, 0xe6, 0xcf, 0x1f, 0x9e, 0x48, 0xa0});

public:
    /// Returns the currently signaled value on the device.
    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentValue(uint64_t* outValue) = 0;

    /// Signals the fence from the host with the specified value.
    virtual SLANG_NO_THROW Result SLANG_MCALL setCurrentValue(uint64_t value) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) = 0;
};

struct ShaderOffset
{
    SlangInt uniformOffset = 0; // TODO: Change to Offset?
    GfxIndex bindingRangeIndex = 0;
    GfxIndex bindingArrayIndex = 0;
    uint32_t getHashCode() const { return (uint32_t)(((bindingRangeIndex << 20) + bindingArrayIndex) ^ uniformOffset); }
    bool operator==(const ShaderOffset& other) const
    {
        return uniformOffset == other.uniformOffset && bindingRangeIndex == other.bindingRangeIndex &&
               bindingArrayIndex == other.bindingArrayIndex;
    }
    bool operator!=(const ShaderOffset& other) const { return !this->operator==(other); }
    bool operator<(const ShaderOffset& other) const
    {
        if (bindingRangeIndex < other.bindingRangeIndex)
            return true;
        if (bindingRangeIndex > other.bindingRangeIndex)
            return false;
        if (bindingArrayIndex < other.bindingArrayIndex)
            return true;
        if (bindingArrayIndex > other.bindingArrayIndex)
            return false;
        return uniformOffset < other.uniformOffset;
    }
    bool operator<=(const ShaderOffset& other) const { return (*this == other) || (*this) < other; }
    bool operator>(const ShaderOffset& other) const { return other < *this; }
    bool operator>=(const ShaderOffset& other) const { return other <= *this; }
};

enum class ShaderObjectContainerType
{
    None,
    Array,
    StructuredBuffer
};

enum class BindingType
{
    Unknown,
    Buffer,
    BufferWithCounter,
    Texture,
    TextureView,
    Sampler,
    CombinedTextureSampler,
    CombinedTextureViewSampler,
    AccelerationStructure,
};

struct Binding
{
    BindingType type = BindingType::Unknown;
    IResource* resource = nullptr;
    IResource* resource2 = nullptr;
    union
    {
        BufferRange bufferRange;
    };

    // clang-format off
    Binding() : type(BindingType::Unknown) {}

    Binding(IBuffer* buffer, const BufferRange& range = kEntireBuffer) : type(BindingType::Buffer), resource(buffer), bufferRange(range) {}
    Binding(const ComPtr<IBuffer>& buffer, const BufferRange& range = kEntireBuffer) : type(BindingType::Buffer), resource(buffer.get()), bufferRange(range) {}

    Binding(IBuffer* buffer, IBuffer* counter, const BufferRange& range = kEntireBuffer) : type(BindingType::BufferWithCounter), resource(buffer), resource2(counter), bufferRange(range) {}
    Binding(const ComPtr<IBuffer>& buffer, const ComPtr<IBuffer>& counter, const BufferRange& range = kEntireBuffer) : type(BindingType::BufferWithCounter), resource(buffer.get()), resource2(counter.get()), bufferRange(range) {}

    Binding(ITexture* texture) : type(BindingType::Texture), resource(texture) {}
    Binding(const ComPtr<ITexture>& texture) : type(BindingType::Texture), resource(texture.get()) {}

    Binding(ITextureView* textureView) : type(BindingType::TextureView), resource(textureView) {}
    Binding(const ComPtr<ITextureView>& textureView) : type(BindingType::TextureView), resource(textureView.get()) {}

    Binding(ISampler* sampler) : type(BindingType::Sampler) , resource(sampler) {}
    Binding(const ComPtr<ISampler>& sampler) : type(BindingType::Sampler) , resource(sampler.get()) {}

    Binding(ITexture* texture, ISampler* sampler) : type(BindingType::CombinedTextureSampler), resource(texture), resource2(sampler) {}
    Binding(const ComPtr<ITexture>& texture, const ComPtr<ISampler>& sampler) : type(BindingType::CombinedTextureSampler), resource(texture.get()), resource2(sampler.get()) {}

    Binding(ITextureView* textureView, ISampler* sampler) : type(BindingType::CombinedTextureViewSampler) , resource(textureView), resource2(sampler) {}
    Binding(const ComPtr<ITextureView>& textureView, const ComPtr<ISampler>& sampler) : type(BindingType::CombinedTextureViewSampler) , resource(textureView.get()), resource2(sampler.get()) {}

    Binding(IAccelerationStructure* as) : type(BindingType::AccelerationStructure), resource(as) {}
    Binding(const ComPtr<IAccelerationStructure>& as) : type(BindingType::AccelerationStructure), resource(as.get()) {}
    // clang-format on
};

class IShaderObject : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0xb1af6fe7, 0x5e6c, 0x4a11, {0xa9, 0x29, 0x06, 0x8f, 0x0c, 0x0f, 0xbe, 0x4f});

public:
    virtual SLANG_NO_THROW slang::TypeLayoutReflection* SLANG_MCALL getElementTypeLayout() = 0;
    virtual SLANG_NO_THROW ShaderObjectContainerType SLANG_MCALL getContainerType() = 0;
    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** entryPoint) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL setData(ShaderOffset const& offset, void const* data, Size size) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL getObject(ShaderOffset const& offset, IShaderObject** object) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL setObject(ShaderOffset const& offset, IShaderObject* object) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(ShaderOffset const& offset, Binding binding) = 0;

    /// Manually overrides the specialization argument for the sub-object binding at `offset`.
    /// Specialization arguments are passed to the shader compiler to specialize the type
    /// of interface-typed shader parameters.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    setSpecializationArgs(ShaderOffset const& offset, const slang::SpecializationArg* args, GfxCount count) = 0;

    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() = 0;

    virtual SLANG_NO_THROW Size SLANG_MCALL getSize() = 0;

    /// Use the provided constant buffer instead of the internally created one.
    virtual SLANG_NO_THROW Result SLANG_MCALL setConstantBufferOverride(IBuffer* constantBuffer) = 0;

    /// Finalizes the shader object, preparing it for use.
    /// This function must be called after all data has been set on the shader object.
    virtual SLANG_NO_THROW Result SLANG_MCALL finalize() = 0;

    /// Returns true if the shader object has been finalized.
    virtual SLANG_NO_THROW bool SLANG_MCALL isFinalized() = 0;

    inline ComPtr<IShaderObject> getObject(ShaderOffset const& offset)
    {
        ComPtr<IShaderObject> object = nullptr;
        SLANG_RETURN_NULL_ON_FAIL(getObject(offset, object.writeRef()));
        return object;
    }
    inline ComPtr<IShaderObject> getEntryPoint(GfxIndex index)
    {
        ComPtr<IShaderObject> entryPoint = nullptr;
        SLANG_RETURN_NULL_ON_FAIL(getEntryPoint(index, entryPoint.writeRef()));
        return entryPoint;
    }
};

enum class StencilOp : uint8_t
{
    Keep,
    Zero,
    Replace,
    IncrementSaturate,
    DecrementSaturate,
    Invert,
    IncrementWrap,
    DecrementWrap,
};

enum class FillMode : uint8_t
{
    Solid,
    Wireframe,
};

enum class CullMode : uint8_t
{
    None,
    Front,
    Back,
};

enum class FrontFaceMode : uint8_t
{
    CounterClockwise,
    Clockwise,
};

struct DepthStencilOpDesc
{
    StencilOp stencilFailOp = StencilOp::Keep;
    StencilOp stencilDepthFailOp = StencilOp::Keep;
    StencilOp stencilPassOp = StencilOp::Keep;
    ComparisonFunc stencilFunc = ComparisonFunc::Always;
};

struct DepthStencilState
{
    Format format = Format::Unknown;

    bool depthTestEnable = false;
    bool depthWriteEnable = true;
    ComparisonFunc depthFunc = ComparisonFunc::Less;

    bool stencilEnable = false;
    uint32_t stencilReadMask = 0xFFFFFFFF;
    uint32_t stencilWriteMask = 0xFFFFFFFF;
    DepthStencilOpDesc frontFace;
    DepthStencilOpDesc backFace;

    uint32_t stencilRef = 0; // TODO: this should be removed
};

struct RasterizerDesc
{
    FillMode fillMode = FillMode::Solid;
    CullMode cullMode = CullMode::None;
    FrontFaceMode frontFace = FrontFaceMode::CounterClockwise;
    int32_t depthBias = 0;
    float depthBiasClamp = 0.0f;
    float slopeScaledDepthBias = 0.0f;
    bool depthClipEnable = true;
    bool scissorEnable = false;
    bool multisampleEnable = false;
    bool antialiasedLineEnable = false;
    bool enableConservativeRasterization = false;
    uint32_t forcedSampleCount = 0;
};

enum class LogicOp
{
    NoOp,
};

enum class BlendOp
{
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

enum class BlendFactor
{
    Zero,
    One,
    SrcColor,
    InvSrcColor,
    SrcAlpha,
    InvSrcAlpha,
    DestAlpha,
    InvDestAlpha,
    DestColor,
    InvDestColor,
    SrcAlphaSaturate,
    BlendColor,
    InvBlendColor,
    SecondarySrcColor,
    InvSecondarySrcColor,
    SecondarySrcAlpha,
    InvSecondarySrcAlpha,
};

namespace RenderTargetWriteMask {
typedef uint8_t Type;
enum
{
    EnableNone = 0,
    EnableRed = 0x01,
    EnableGreen = 0x02,
    EnableBlue = 0x04,
    EnableAlpha = 0x08,
    EnableAll = 0x0F,
};
}; // namespace RenderTargetWriteMask
typedef RenderTargetWriteMask::Type RenderTargetWriteMaskT;

struct AspectBlendDesc
{
    BlendFactor srcFactor = BlendFactor::One;
    BlendFactor dstFactor = BlendFactor::Zero;
    BlendOp op = BlendOp::Add;
};

struct ColorTargetState
{
    Format format = Format::Unknown;
    AspectBlendDesc color;
    AspectBlendDesc alpha;
    bool enableBlend = false;
    LogicOp logicOp = LogicOp::NoOp;
    RenderTargetWriteMaskT writeMask = RenderTargetWriteMask::EnableAll;
};

struct MultisampleState
{
    GfxCount sampleCount = 1;
    uint32_t sampleMask = 0xFFFFFFFF;
    bool alphaToCoverageEnable = false;
    bool alphaToOneEnable = false;
};

struct RenderPipelineDesc
{
    IShaderProgram* program = nullptr;
    IInputLayout* inputLayout = nullptr;
    PrimitiveTopology primitiveTopology = PrimitiveTopology::TriangleList;
    ColorTargetState* targets = nullptr;
    GfxCount targetCount = 0;
    DepthStencilState depthStencil;
    RasterizerDesc rasterizer;
    MultisampleState multisample;
};

struct ComputePipelineDesc
{
    IShaderProgram* program = nullptr;
    void* d3d12RootSignatureOverride = nullptr;
};

enum class RayTracingPipelineFlags
{
    None = 0,
    SkipTriangles = (1 << 0),
    SkipProcedurals = (1 << 1),
};
SLANG_RHI_ENUM_CLASS_OPERATORS(RayTracingPipelineFlags);

struct HitGroupDesc
{
    const char* hitGroupName = nullptr;
    const char* closestHitEntryPoint = nullptr;
    const char* anyHitEntryPoint = nullptr;
    const char* intersectionEntryPoint = nullptr;
};

struct RayTracingPipelineDesc
{
    IShaderProgram* program = nullptr;
    GfxCount hitGroupCount = 0;
    HitGroupDesc* hitGroups = nullptr;
    int maxRecursion = 0;
    Size maxRayPayloadSize = 0;
    Size maxAttributeSizeInBytes = 8;
    RayTracingPipelineFlags flags = RayTracingPipelineFlags::None;
};

class IShaderTable : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x348abe3f, 0x5075, 0x4b3d, {0x88, 0xcf, 0x54, 0x83, 0xdc, 0x62, 0xb3, 0xb9});

public:
    // Specifies the bytes to overwrite into a record in the shader table.
    struct ShaderRecordOverwrite
    {
        Offset offset;   // Offset within the shader record.
        Size size;       // Number of bytes to overwrite.
        uint8_t data[8]; // Content to overwrite.
    };

    struct Desc
    {
        GfxCount rayGenShaderCount;
        const char** rayGenShaderEntryPointNames;
        const ShaderRecordOverwrite* rayGenShaderRecordOverwrites;

        GfxCount missShaderCount;
        const char** missShaderEntryPointNames;
        const ShaderRecordOverwrite* missShaderRecordOverwrites;

        GfxCount hitGroupCount;
        const char** hitGroupNames;
        const ShaderRecordOverwrite* hitGroupRecordOverwrites;

        GfxCount callableShaderCount;
        const char** callableShaderEntryPointNames;
        const ShaderRecordOverwrite* callableShaderRecordOverwrites;

        IShaderProgram* program;
    };
};

class IPipeline : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x2ad83bfc, 0x581d, 0x4b88, {0x81, 0x3c, 0x0c, 0x0e, 0xaf, 0x04, 0x0a, 0x00});

public:
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) = 0;
};

class IRenderPipeline : public IPipeline
{
    SLANG_COM_INTERFACE(0xf2eb0472, 0xfa25, 0x44f9, {0xb1, 0x90, 0xdc, 0x3e, 0x29, 0xaa, 0x56, 0x6a});
};

class IComputePipeline : public IPipeline
{
    SLANG_COM_INTERFACE(0x16eded28, 0xdc04, 0x434d, {0x85, 0xb7, 0xd6, 0xfa, 0xa0, 0x00, 0x5d, 0xf3});
};

class IRayTracingPipeline : public IPipeline
{
    SLANG_COM_INTERFACE(0x5047f5d7, 0xc6f6, 0x4482, {0xab, 0x49, 0x08, 0x57, 0x1b, 0xcf, 0xe8, 0xda});
};

struct ScissorRect
{
    int32_t minX = 0;
    int32_t minY = 0;
    int32_t maxX = 0;
    int32_t maxY = 0;

    ScissorRect() = default;

    ScissorRect(int32_t width, int32_t height)
        : maxX(width)
        , maxY(height)
    {
    }
};

struct Viewport
{
    float originX = 0.0f;
    float originY = 0.0f;
    float extentX = 0.0f;
    float extentY = 0.0f;
    float minZ = 0.0f;
    float maxZ = 1.0f;

    Viewport() = default;

    Viewport(float width, float height)
        : extentX(width)
        , extentY(height)
    {
    }
};

enum class WindowHandleType
{
    Unknown,
    HWND,
    NSWindow,
    XlibWindow,
};

struct WindowHandle
{
    WindowHandleType type = WindowHandleType::Unknown;
    intptr_t handleValues[2];

    static WindowHandle fromHwnd(void* hwnd)
    {
        WindowHandle handle = {};
        handle.type = WindowHandleType::HWND;
        handle.handleValues[0] = (intptr_t)(hwnd);
        return handle;
    }
    static WindowHandle fromNSWindow(void* nswindow)
    {
        WindowHandle handle = {};
        handle.type = WindowHandleType::NSWindow;
        handle.handleValues[0] = (intptr_t)(nswindow);
        return handle;
    }
    static WindowHandle fromXlibWindow(void* xdisplay, uint32_t xwindow)
    {
        WindowHandle handle = {};
        handle.type = WindowHandleType::XlibWindow;
        handle.handleValues[0] = (intptr_t)(xdisplay);
        handle.handleValues[1] = xwindow;
        return handle;
    }
};

enum class LoadOp
{
    Load,
    Clear,
    DontCare
};

enum class StoreOp
{
    Store,
    DontCare
};

struct RenderPassColorAttachment
{
    ITextureView* view = nullptr;
    ITextureView* resolveTarget = nullptr;
    LoadOp loadOp = LoadOp::DontCare;
    StoreOp storeOp = StoreOp::Store;
    float clearValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct RenderPassDepthStencilAttachment
{
    ITextureView* view = nullptr;
    LoadOp depthLoadOp = LoadOp::DontCare;
    StoreOp depthStoreOp = StoreOp::Store;
    float depthClearValue = 1.f;
    bool depthReadOnly = false;
    LoadOp stencilLoadOp = LoadOp::DontCare;
    StoreOp stencilStoreOp = StoreOp::DontCare;
    uint8_t stencilClearValue = 0;
    bool stencilReadOnly = false;
};

struct RenderPassDesc
{
    RenderPassColorAttachment* colorAttachments = nullptr;
    GfxCount colorAttachmentCount = 0;
    RenderPassDepthStencilAttachment* depthStencilAttachment = nullptr;
};

enum class QueryType
{
    Timestamp,
    AccelerationStructureCompactedSize,
    AccelerationStructureSerializedSize,
    AccelerationStructureCurrentSize,
};

struct QueryPoolDesc
{
    QueryType type;
    GfxCount count;

    const char* label = nullptr;
};

class IQueryPool : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0xe4b585e4, 0x9da9, 0x479b, {0x89, 0x5c, 0x48, 0x78, 0x8e, 0xf2, 0x33, 0x65});

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL getResult(GfxIndex queryIndex, GfxCount count, uint64_t* data) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL reset() = 0;
};

struct DrawArguments
{
    GfxCount vertexCount = 0;
    GfxCount instanceCount = 1;
    GfxIndex startVertexLocation = 0;
    GfxIndex startInstanceLocation = 0;
    GfxIndex startIndexLocation = 0;
};

struct IndirectDrawArguments
{
    GfxCount VertexCountPerInstance;
    GfxCount InstanceCount;
    GfxIndex StartVertexLocation;
    GfxIndex StartInstanceLocation;
};

struct IndirectDrawIndexedArguments
{
    GfxCount IndexCountPerInstance;
    GfxCount InstanceCount;
    GfxIndex StartIndexLocation;
    GfxIndex BaseVertexLocation;
    GfxIndex StartInstanceLocation;
};

struct IndirectDispatchArguments
{
    GfxCount ThreadGroupCountX;
    GfxCount ThreadGroupCountY;
    GfxCount ThreadGroupCountZ;
};

struct SamplePosition
{
    int8_t x;
    int8_t y;
};

struct RenderState
{
    IRenderPipeline* pipeline = nullptr;
    IShaderObject* rootObject = nullptr;
    uint32_t stencilRef = 0;
    Viewport viewports[16];
    GfxCount viewportCount = 0;
    ScissorRect scissorRects[16];
    GfxCount scissorRectCount = 0;
    BufferWithOffset vertexBuffers[16];
    GfxCount vertexBufferCount = 0;
    BufferWithOffset indexBuffer;
    IndexFormat indexFormat = IndexFormat::UInt32;
};

struct ComputeState
{
    IComputePipeline* pipeline = nullptr;
    IShaderObject* rootObject = nullptr;
};

struct RayTracingState
{
    IRayTracingPipeline* pipeline = nullptr;
    IShaderTable* shaderTable = nullptr;
    IShaderObject* rootObject = nullptr;
};

enum class AccelerationStructureCopyMode
{
    Clone,
    Compact
};

struct AccelerationStructureQueryDesc
{
    QueryType queryType;

    IQueryPool* queryPool;

    GfxIndex firstQueryIndex;
};

class ICommandBuffer : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x58e5d83f, 0xad31, 0x44ea, {0xa4, 0xd1, 0x5e, 0x65, 0x9c, 0xd9, 0xa7, 0x57});

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) = 0;
};

class IPassEncoder : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x159cd708, 0x4762, 0x4f30, {0xb5, 0x3f, 0xbe, 0x2a, 0xb5, 0x7d, 0x7c, 0x46});

public:
    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL end() = 0;
};

class IRenderPassEncoder : public IPassEncoder
{
    SLANG_COM_INTERFACE(0x4f904e1a, 0xa5ed, 0x4496, {0xaa, 0xc6, 0xde, 0xcf, 0x68, 0x1e, 0x6c, 0x74});

public:
    virtual SLANG_NO_THROW void SLANG_MCALL setRenderState(const RenderState& state) = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL draw(const DrawArguments& args) = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexed(const DrawArguments& args) = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndirect(
        GfxCount maxDrawCount,
        IBuffer* argBuffer,
        Offset argOffset,
        IBuffer* countBuffer = nullptr,
        Offset countOffset = 0
    ) = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexedIndirect(
        GfxCount maxDrawCount,
        IBuffer* argBuffer,
        Offset argOffset,
        IBuffer* countBuffer = nullptr,
        Offset countOffset = 0
    ) = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL drawMeshTasks(GfxCount x, GfxCount y, GfxCount z) = 0;
};

class IComputePassEncoder : public IPassEncoder
{
    SLANG_COM_INTERFACE(0x8479334f, 0xfb45, 0x471c, {0xb7, 0x75, 0x94, 0xa5, 0x76, 0x72, 0x32, 0xc8});

public:
    virtual SLANG_NO_THROW void SLANG_MCALL setComputeState(const ComputeState& state) = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL dispatchCompute(GfxCount x, GfxCount y, GfxCount z) = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, Offset offset) = 0;
};

class IRayTracingPassEncoder : public IPassEncoder
{
    SLANG_COM_INTERFACE(0x4fe41081, 0x819c, 0x4fdc, {0x80, 0x78, 0x40, 0x31, 0x9c, 0x01, 0xff, 0xad});

public:
    virtual SLANG_NO_THROW void SLANG_MCALL setRayTracingState(const RayTracingState& state) = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL
    dispatchRays(GfxIndex rayGenShaderIndex, GfxCount width, GfxCount height, GfxCount depth) = 0;
};

class ICommandEncoder : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x8ee39d55, 0x2b07, 0x4e61, {0x8f, 0x13, 0x1d, 0x6c, 0x01, 0xa9, 0x15, 0x43});

public:
    virtual SLANG_NO_THROW IRenderPassEncoder* SLANG_MCALL beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual SLANG_NO_THROW IComputePassEncoder* SLANG_MCALL beginComputePass() = 0;
    virtual SLANG_NO_THROW IRayTracingPassEncoder* SLANG_MCALL beginRayTracingPass() = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL
    copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size) = 0;

    /// Copies texture from src to dst. If dstSubresource and srcSubresource has mipLevelCount = 0
    /// and layerCount = 0, the entire resource is being copied and dstOffset, srcOffset and extent
    /// arguments are ignored.
    virtual SLANG_NO_THROW void SLANG_MCALL copyTexture(
        ITexture* dst,
        SubresourceRange dstSubresource,
        Offset3D dstOffset,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) = 0;

    /// Copies texture to a buffer. Each row is aligned to kTexturePitchAlignment.
    virtual SLANG_NO_THROW void SLANG_MCALL copyTextureToBuffer(
        IBuffer* dst,
        Offset dstOffset,
        Size dstSize,
        Size dstRowStride,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL uploadTextureData(
        ITexture* dst,
        SubresourceRange subresourceRange,
        Offset3D offset,
        Extents extent,
        SubresourceData* subresourceData,
        GfxCount subresourceDataCount
    ) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL clearBuffer(IBuffer* buffer, const BufferRange* range = nullptr) = 0;

    inline void clearBuffer(IBuffer* buffer, Offset offset, Size size)
    {
        BufferRange range = {offset, size};
        clearBuffer(buffer, &range);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL clearTexture(
        ITexture* texture,
        const ClearValue& clearValue = ClearValue(),
        const SubresourceRange* subresourceRange = nullptr,
        bool clearDepth = true,
        bool clearStencil = true
    ) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL
    resolveQuery(IQueryPool* queryPool, GfxIndex index, GfxCount count, IBuffer* buffer, Offset offset) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL buildAccelerationStructure(
        const AccelerationStructureBuildDesc& desc,
        IAccelerationStructure* dst,
        IAccelerationStructure* src,
        BufferWithOffset scratchBuffer,
        GfxCount propertyQueryCount,
        AccelerationStructureQueryDesc* queryDescs
    ) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL copyAccelerationStructure(
        IAccelerationStructure* dst,
        IAccelerationStructure* src,
        AccelerationStructureCopyMode mode
    ) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL queryAccelerationStructureProperties(
        GfxCount accelerationStructureCount,
        IAccelerationStructure* const* accelerationStructures,
        GfxCount queryCount,
        AccelerationStructureQueryDesc* queryDescs
    ) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL
    serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL
    deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) = 0;

    inline void setTextureState(ITexture* texture, ResourceState state)
    {
        setTextureState(texture, kEntireTexture, state);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) = 0;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* queryPool, GfxIndex queryIndex) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) = 0;

    inline ComPtr<ICommandBuffer> finish()
    {
        ComPtr<ICommandBuffer> commandBuffer;
        SLANG_RETURN_NULL_ON_FAIL(finish(commandBuffer.writeRef()));
        return commandBuffer;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) = 0;
};

#if 0
class ICommandBufferD3D12 : public ICommandBuffer
{
    SLANG_COM_INTERFACE(0xd0197946, 0xf266, 0x4638, {0x85, 0x16, 0x60, 0xdc, 0x35, 0x70, 0x8c, 0x0f});

public:
    virtual SLANG_NO_THROW void SLANG_MCALL invalidateDescriptorHeapBinding() = 0;
    virtual SLANG_NO_THROW void SLANG_MCALL ensureInternalDescriptorHeapsBound() = 0;
};
#endif

enum class QueueType
{
    Graphics,
};

class ICommandQueue : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0xc530a6bd, 0x6d1b, 0x475f, {0x9a, 0x71, 0xc2, 0x06, 0x67, 0x1f, 0x59, 0xc3});

public:
    virtual SLANG_NO_THROW QueueType SLANG_MCALL getType() = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) = 0;

    inline ComPtr<ICommandEncoder> createCommandEncoder()
    {
        ComPtr<ICommandEncoder> encoder;
        SLANG_RETURN_NULL_ON_FAIL(createCommandEncoder(encoder.writeRef()));
        return encoder;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    submit(GfxCount count, ICommandBuffer* const* commandBuffers, IFence* fenceToSignal, uint64_t newFenceValue) = 0;

    inline Result submit(ICommandBuffer* commandBuffer, IFence* fenceToSignal = nullptr, uint64_t newFenceValue = 0)
    {
        return submit(1, &commandBuffer, fenceToSignal, newFenceValue);
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() = 0;

    /// Queues a device side wait for the given fences.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) = 0;
};

struct SurfaceInfo
{
    Format preferredFormat;
    TextureUsage supportedUsage;
    const Format* formats;
    GfxCount formatCount;
};

struct SurfaceConfig
{
    Format format = Format::Unknown;
    TextureUsage usage = TextureUsage::RenderTarget;
    // size_t viewFormatCount;
    // const Format* viewFormats;
    Size width = 0;
    Size height = 0;
    Size desiredImageCount = 3;
    bool vsync = true;
};

class ISurface : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0xd6c37a71, 0x7d0d, 0x4714, {0xb7, 0xa3, 0x25, 0xca, 0x81, 0x73, 0x0c, 0x37});

public:
    virtual SLANG_NO_THROW const SurfaceInfo& SLANG_MCALL getInfo() = 0;
    virtual SLANG_NO_THROW const SurfaceConfig& SLANG_MCALL getConfig() = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL configure(const SurfaceConfig& config) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentTexture(ITexture** outTexture) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL present() = 0;
};

struct AdapterLUID
{
    uint8_t luid[16];

    bool operator==(const AdapterLUID& other) const
    {
        for (size_t i = 0; i < sizeof(AdapterLUID::luid); ++i)
            if (luid[i] != other.luid[i])
                return false;
        return true;
    }
    bool operator!=(const AdapterLUID& other) const { return !this->operator==(other); }
};

struct AdapterInfo
{
    // Descriptive name of the adapter.
    char name[128];

    // Unique identifier for the vendor (only available for D3D and Vulkan).
    uint32_t vendorID;

    // Unique identifier for the physical device among devices from the vendor (only available for D3D and Vulkan)
    uint32_t deviceID;

    // Logically unique identifier of the adapter.
    AdapterLUID luid;
};

class AdapterList
{
public:
    AdapterList(ISlangBlob* blob)
        : m_blob(blob)
    {
    }

    const AdapterInfo* getAdapters() const
    {
        return reinterpret_cast<const AdapterInfo*>(m_blob ? m_blob->getBufferPointer() : nullptr);
    }

    GfxCount getCount() const { return (GfxCount)(m_blob ? m_blob->getBufferSize() / sizeof(AdapterInfo) : 0); }

private:
    ComPtr<ISlangBlob> m_blob;
};

struct DeviceLimits
{
    /// Maximum dimension for 1D textures.
    uint32_t maxTextureDimension1D;
    /// Maximum dimensions for 2D textures.
    uint32_t maxTextureDimension2D;
    /// Maximum dimensions for 3D textures.
    uint32_t maxTextureDimension3D;
    /// Maximum dimensions for cube textures.
    uint32_t maxTextureDimensionCube;
    /// Maximum number of texture layers.
    uint32_t maxTextureArrayLayers;

    /// Maximum number of vertex input elements in a graphics pipeline.
    uint32_t maxVertexInputElements;
    /// Maximum offset of a vertex input element in the vertex stream.
    uint32_t maxVertexInputElementOffset;
    /// Maximum number of vertex streams in a graphics pipeline.
    uint32_t maxVertexStreams;
    /// Maximum stride of a vertex stream.
    uint32_t maxVertexStreamStride;

    /// Maximum number of threads per thread group.
    uint32_t maxComputeThreadsPerGroup;
    /// Maximum dimensions of a thread group.
    uint32_t maxComputeThreadGroupSize[3];
    /// Maximum number of thread groups per dimension in a single dispatch.
    uint32_t maxComputeDispatchThreadGroups[3];

    /// Maximum number of viewports per pipeline.
    uint32_t maxViewports;
    /// Maximum viewport dimensions.
    uint32_t maxViewportDimensions[2];
    /// Maximum framebuffer dimensions.
    uint32_t maxFramebufferDimensions[3];

    /// Maximum samplers visible in a shader stage.
    uint32_t maxShaderVisibleSamplers;
};

struct DeviceInfo
{
    DeviceType deviceType;

    DeviceLimits limits;

    /// An projection matrix that ensures x, y mapping to pixels
    /// is the same on all targets
    float identityProjectionMatrix[16];

    /// The name of the graphics API being used by this device.
    const char* apiName = nullptr;

    /// The name of the graphics adapter.
    const char* adapterName = nullptr;

    /// The clock frequency used in timestamp queries.
    uint64_t timestampFrequency = 0;
};

enum class DebugMessageType
{
    Info,
    Warning,
    Error
};
enum class DebugMessageSource
{
    Layer,
    Driver,
    Slang
};
class IDebugCallback
{
public:
    virtual SLANG_NO_THROW void SLANG_MCALL
    handleMessage(DebugMessageType type, DebugMessageSource source, const char* message) = 0;
};

struct SlangDesc
{
    /// (optional) A slang global session object, if null a new one will be created.
    slang::IGlobalSession* slangGlobalSession = nullptr;

    SlangMatrixLayoutMode defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;

    char const* const* searchPaths = nullptr;
    GfxCount searchPathCount = 0;

    slang::PreprocessorMacroDesc const* preprocessorMacros = nullptr;
    GfxCount preprocessorMacroCount = 0;

    /// (optional) Target shader profile. If null this will be set to platform dependent default.
    const char* targetProfile = nullptr;

    SlangFloatingPointMode floatingPointMode = SLANG_FLOATING_POINT_MODE_DEFAULT;
    SlangOptimizationLevel optimizationLevel = SLANG_OPTIMIZATION_LEVEL_DEFAULT;
    SlangTargetFlags targetFlags = kDefaultTargetFlags;
    SlangLineDirectiveMode lineDirectiveMode = SLANG_LINE_DIRECTIVE_MODE_DEFAULT;
};

struct DeviceNativeHandles
{
    NativeHandle handles[3] = {};
};

struct DeviceDesc
{
    // The underlying API/Platform of the device.
    DeviceType deviceType = DeviceType::Default;
    // The device's handles (if they exist) and their associated API. For D3D12, this contains a single
    // NativeHandle for the ID3D12Device. For Vulkan, the first NativeHandle is the VkInstance, the second is the
    // VkPhysicalDevice, and the third is the VkDevice. For CUDA, this only contains a single value for the
    // CUDADevice.
    DeviceNativeHandles existingDeviceHandles;
    // LUID of the adapter to use. Use getGfxAdapters() to get a list of available adapters.
    const AdapterLUID* adapterLUID = nullptr;
    // Number of required features.
    GfxCount requiredFeatureCount = 0;
    // Array of required feature names, whose size is `requiredFeatureCount`.
    const char** requiredFeatures = nullptr;
    // A command dispatcher object that intercepts and handles actual low-level API call.
    ISlangUnknown* apiCommandDispatcher = nullptr;
    // The slot (typically UAV) used to identify NVAPI intrinsics. If >=0 NVAPI is required.
    GfxIndex nvapiExtnSlot = -1;
    // Configurations for Slang compiler.
    SlangDesc slang = {};

    // Interface to persistent shader cache.
    IPersistentShaderCache* persistentShaderCache = nullptr;

    GfxCount extendedDescCount = 0;
    void** extendedDescs = nullptr;

    /// Enable RHI validation layer.
    bool enableValidation = false;
    /// Enable backend API validation layer.
    bool enableBackendValidation = false;
    /// Debug callback. If not null, this will be called for each debug message.
    IDebugCallback* debugCallback = nullptr;
};

class IDevice : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x311ee28b, 0xdb5a, 0x4a3c, {0x89, 0xda, 0xf0, 0x03, 0x0f, 0xd5, 0x70, 0x4b});

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) = 0;

    virtual SLANG_NO_THROW bool SLANG_MCALL hasFeature(const char* feature) = 0;

    /// Returns a list of features supported by the device.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    getFeatures(const char** outFeatures, Size bufferSize, GfxCount* outFeatureCount) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL getFormatSupport(Format format, FormatSupport* outFormatSupport) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSlangSession(slang::ISession** outSlangSession) = 0;

    inline ComPtr<slang::ISession> getSlangSession()
    {
        ComPtr<slang::ISession> result;
        getSlangSession(result.writeRef());
        return result;
    }

    /// Create a texture resource.
    ///
    /// If `initData` is non-null, then it must point to an array of
    /// `SubresourceData` with one element for each
    /// subresource of the texture being created.
    ///
    /// The number of subresources in a texture is:
    ///
    ///     effectiveElementCount * mipLevelCount
    ///
    /// where the effective element count is computed as:
    ///
    ///     effectiveElementCount = (isArray ? arrayElementCount : 1) * (isCube ? 6 : 1);
    ///
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture) = 0;

    /// Create a texture resource. initData holds the initialize data to set the contents of the texture when
    /// constructed.
    inline SLANG_NO_THROW ComPtr<ITexture> createTexture(
        const TextureDesc& desc,
        const SubresourceData* initData = nullptr
    )
    {
        ComPtr<ITexture> texture;
        SLANG_RETURN_NULL_ON_FAIL(createTexture(desc, initData, texture.writeRef()));
        return texture;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& srcDesc, ITexture** outTexture) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureFromSharedHandle(
        NativeHandle handle,
        const TextureDesc& srcDesc,
        const Size size,
        ITexture** outTexture
    ) = 0;

    /// Create a buffer resource
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBuffer(const BufferDesc& desc, const void* initData, IBuffer** outBuffer) = 0;

    inline SLANG_NO_THROW ComPtr<IBuffer> createBuffer(const BufferDesc& desc, const void* initData = nullptr)
    {
        ComPtr<IBuffer> buffer;
        SLANG_RETURN_NULL_ON_FAIL(createBuffer(desc, initData, buffer.writeRef()));
        return buffer;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmapBuffer(IBuffer* buffer) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(SamplerDesc const& desc, ISampler** outSampler) = 0;

    inline ComPtr<ISampler> createSampler(SamplerDesc const& desc)
    {
        ComPtr<ISampler> sampler;
        SLANG_RETURN_NULL_ON_FAIL(createSampler(desc, sampler.writeRef()));
        return sampler;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView) = 0;

    inline ComPtr<ITextureView> createTextureView(ITexture* texture, const TextureViewDesc& desc)
    {
        ComPtr<ITextureView> view;
        SLANG_RETURN_NULL_ON_FAIL(createTextureView(texture, desc, view.writeRef()));
        return view;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) = 0;

    inline ComPtr<ISurface> createSurface(WindowHandle windowHandle)
    {
        ComPtr<ISurface> surface;
        SLANG_RETURN_NULL_ON_FAIL(createSurface(windowHandle, surface.writeRef()));
        return surface;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout) = 0;

    inline ComPtr<IInputLayout> createInputLayout(InputLayoutDesc const& desc)
    {
        ComPtr<IInputLayout> layout;
        SLANG_RETURN_NULL_ON_FAIL(createInputLayout(desc, layout.writeRef()));
        return layout;
    }

    inline Result createInputLayout(
        Size vertexSize,
        InputElementDesc const* inputElements,
        GfxCount inputElementCount,
        IInputLayout** outLayout
    )
    {
        VertexStreamDesc streamDesc = {vertexSize, InputSlotClass::PerVertex, 0};

        InputLayoutDesc inputLayoutDesc = {};
        inputLayoutDesc.inputElementCount = inputElementCount;
        inputLayoutDesc.inputElements = inputElements;
        inputLayoutDesc.vertexStreamCount = 1;
        inputLayoutDesc.vertexStreams = &streamDesc;
        return createInputLayout(inputLayoutDesc, outLayout);
    }

    inline ComPtr<IInputLayout> createInputLayout(
        Size vertexSize,
        InputElementDesc const* inputElements,
        GfxCount inputElementCount
    )
    {
        ComPtr<IInputLayout> layout;
        SLANG_RETURN_NULL_ON_FAIL(createInputLayout(vertexSize, inputElements, inputElementCount, layout.writeRef()));
        return layout;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) = 0;
    inline ComPtr<ICommandQueue> getQueue(QueueType type)
    {
        ComPtr<ICommandQueue> queue;
        SLANG_RETURN_NULL_ON_FAIL(getQueue(type, queue.writeRef()));
        return queue;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderObject(
        slang::ISession* slangSession,
        slang::TypeReflection* type,
        ShaderObjectContainerType container,
        IShaderObject** outObject
    ) = 0;

    inline ComPtr<IShaderObject> createShaderObject(
        slang::TypeReflection* type,
        ShaderObjectContainerType container = ShaderObjectContainerType::None
    )
    {
        ComPtr<IShaderObject> object;
        SLANG_RETURN_NULL_ON_FAIL(createShaderObject(nullptr, type, container, object.writeRef()));
        return object;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderObjectFromTypeLayout(slang::TypeLayoutReflection* typeLayout, IShaderObject** outObject) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRootShaderObject(IShaderProgram* program, IShaderObject** outObject) = 0;

    inline ComPtr<IShaderObject> createRootShaderObject(IShaderProgram* program)
    {
        ComPtr<IShaderObject> object;
        SLANG_RETURN_NULL_ON_FAIL(createRootShaderObject(program, object.writeRef()));
        return object;
    }

    inline ComPtr<IShaderObject> createRootShaderObject(IPipeline* pipeline)
    {
        ComPtr<IShaderObject> object;
        SLANG_RETURN_NULL_ON_FAIL(createRootShaderObject(pipeline->getProgram(), object.writeRef()));
        return object;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createShaderTable(const IShaderTable::Desc& desc, IShaderTable** outTable) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob = nullptr
    ) = 0;

    inline ComPtr<IShaderProgram> createShaderProgram(
        const ShaderProgramDesc& desc,
        ISlangBlob** outDiagnosticBlob = nullptr
    )
    {
        ComPtr<IShaderProgram> program;
        SLANG_RETURN_NULL_ON_FAIL(createShaderProgram(desc, program.writeRef(), outDiagnosticBlob));
        return program;
    }

    inline ComPtr<IShaderProgram> createShaderProgram(
        slang::IComponentType* linkedProgram,
        ISlangBlob** outDiagnosticBlob = nullptr
    )
    {
        ShaderProgramDesc desc = {};
        desc.slangGlobalScope = linkedProgram;
        return createShaderProgram(desc, outDiagnosticBlob);
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPipeline(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline) = 0;

    inline ComPtr<IRenderPipeline> createRenderPipeline(const RenderPipelineDesc& desc)
    {
        ComPtr<IRenderPipeline> pipeline;
        SLANG_RETURN_NULL_ON_FAIL(createRenderPipeline(desc, pipeline.writeRef()));
        return pipeline;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline(const ComputePipelineDesc& desc, IComputePipeline** outPipeline) = 0;

    inline ComPtr<IComputePipeline> createComputePipeline(const ComputePipelineDesc& desc)
    {
        ComPtr<IComputePipeline> pipeline;
        SLANG_RETURN_NULL_ON_FAIL(createComputePipeline(desc, pipeline.writeRef()));
        return pipeline;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRayTracingPipeline(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline) = 0;

    inline ComPtr<IRayTracingPipeline> createRayTracingPipeline(const RayTracingPipelineDesc& desc)
    {
        ComPtr<IRayTracingPipeline> pipeline;
        SLANG_RETURN_NULL_ON_FAIL(createRayTracingPipeline(desc, pipeline.writeRef()));
        return pipeline;
    }

    /// Read back texture resource and stores the result in `outBlob`.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, ISlangBlob** outBlob, Size* outRowPitch, Size* outPixelSize) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readBuffer(IBuffer* buffer, Offset offset, Size size, ISlangBlob** outBlob) = 0;

    /// Get information about the device.
    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    getAccelerationStructureSizes(const AccelerationStructureBuildDesc& desc, AccelerationStructureSizes* outSizes) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL createAccelerationStructure(
        const AccelerationStructureDesc& desc,
        IAccelerationStructure** outAccelerationStructure
    ) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) = 0;

    /// Wait on the host for the fences to signals.
    /// `timeout` is in nanoseconds, can be set to `kTimeoutInfinite`.
    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFences(GfxCount fenceCount, IFence** fences, uint64_t* values, bool waitForAll, uint64_t timeout) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment) = 0;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Size* outAlignment) = 0;
};

class IPersistentShaderCache : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x68981742, 0x7fd6, 0x4700, {0x8a, 0x71, 0xe8, 0xea, 0x42, 0x91, 0x3b, 0x28});

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL writeCache(ISlangBlob* key, ISlangBlob* data) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL queryCache(ISlangBlob* key, ISlangBlob** outData) = 0;
};

class IPipelineCreationAPIDispatcher : public ISlangUnknown
{
    SLANG_COM_INTERFACE(0x8d7aa89d, 0x07f1, 0x4e21, {0xbc, 0xd2, 0x9a, 0x71, 0xc7, 0x95, 0xba, 0x91});

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL createComputePipeline(
        IDevice* device,
        slang::IComponentType* program,
        void* pipelineDesc,
        void** outPipelineState
    ) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL createRenderPipeline(
        IDevice* device,
        slang::IComponentType* program,
        void* pipelineDesc,
        void** outPipelineState
    ) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL createMeshPipeline(
        IDevice* device,
        slang::IComponentType* program,
        void* pipelineDesc,
        void** outPipelineState
    ) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    beforeCreateRayTracingState(IDevice* device, slang::IComponentType* program) = 0;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    afterCreateRayTracingState(IDevice* device, slang::IComponentType* program) = 0;
};

class IRHI
{
public:
    virtual SLANG_NO_THROW const FormatInfo& SLANG_MCALL getFormatInfo(Format format) = 0;

    virtual SLANG_NO_THROW const char* SLANG_MCALL getDeviceTypeName(DeviceType type) = 0;

    virtual SLANG_NO_THROW bool SLANG_MCALL isDeviceTypeSupported(DeviceType type) = 0;

    /// Gets a list of available adapters for a given device type.
    virtual SLANG_NO_THROW Result SLANG_MCALL getAdapters(DeviceType type, ISlangBlob** outAdaptersBlob) = 0;

    inline AdapterList getAdapters(DeviceType type)
    {
        ComPtr<ISlangBlob> blob;
        SLANG_RETURN_NULL_ON_FAIL(getAdapters(type, blob.writeRef()));
        return AdapterList(blob);
    }

    /// Creates a device.
    virtual SLANG_NO_THROW Result SLANG_MCALL createDevice(const DeviceDesc& desc, IDevice** outDevice) = 0;

    ComPtr<IDevice> createDevice(const DeviceDesc& desc)
    {
        ComPtr<IDevice> device;
        SLANG_RETURN_NULL_ON_FAIL(createDevice(desc, device.writeRef()));
        return device;
    }

    /// Reports current set of live objects.
    /// Currently this just calls D3D's ReportLiveObjects.
    virtual SLANG_NO_THROW Result SLANG_MCALL reportLiveObjects() = 0;
};

// Global public functions

extern "C"
{
    /// Get the global interface to the RHI.
    SLANG_RHI_API IRHI* SLANG_MCALL getRHI();
}

inline const FormatInfo& getFormatInfo(Format format)
{
    return getRHI()->getFormatInfo(format);
}

// Extended descs.
struct D3D12ExperimentalFeaturesDesc
{
    StructType structType = StructType::D3D12ExperimentalFeaturesDesc;
    uint32_t featureCount;
    const void* featureIIDs;
    void* configurationStructs;
    uint32_t* configurationStructSizes;
};

struct D3D12DeviceExtendedDesc
{
    StructType structType = StructType::D3D12DeviceExtendedDesc;
    const char* rootParameterShaderAttributeName = nullptr;
    bool debugBreakOnD3D12Error = false;
    uint32_t highestShaderModel = 0;
};

struct SlangSessionExtendedDesc
{
    StructType structType = StructType::SlangSessionExtendedDesc;
    uint32_t compilerOptionEntryCount = 0;
    slang::CompilerOptionEntry* compilerOptionEntries = nullptr;
};

/// Whether to enable ray tracing validation (currently only Vulkan - D3D requires app layer to use NVAPI)
struct RayTracingValidationDesc
{
    StructType structType = StructType::RayTracingValidationDesc;
    bool enableRaytracingValidation = false;
};

} // namespace rhi

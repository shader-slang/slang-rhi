#include "rhi-shared.h"
#include "command-list.h"

#include "core/common.h"

#include <slang.h>

#include <atomic>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace rhi {

class NullDebugCallback : public IDebugCallback
{
public:
    virtual void handleMessage(DebugMessageType type, DebugMessageSource source, const char* message) override
    {
        SLANG_UNUSED(type);
        SLANG_UNUSED(source);
        SLANG_UNUSED(message);
    }

    static IDebugCallback* getInstance()
    {
        static NullDebugCallback instance;
        return &instance;
    }
};

IFence* Fence::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IFence::getTypeGuid())
        return static_cast<IFence*>(this);
    return nullptr;
}

IResource* Buffer::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == IBuffer::getTypeGuid())
        return static_cast<IBuffer*>(this);
    return nullptr;
}

BufferRange Buffer::resolveBufferRange(const BufferRange& range)
{
    BufferRange resolved = range;
    resolved.offset = min(resolved.offset, m_desc.size);
    resolved.size = min(resolved.size, m_desc.size - resolved.offset);
    return resolved;
}

BufferDesc& Buffer::getDesc()
{
    return m_desc;
}

Result Buffer::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Buffer::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

IResource* Texture::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ITexture::getTypeGuid())
        return static_cast<ITexture*>(this);
    return nullptr;
}

SubresourceRange Texture::resolveSubresourceRange(const SubresourceRange& range)
{
    SubresourceRange resolved = range;
    resolved.mipLevel = min(resolved.mipLevel, m_desc.mipLevelCount);
    resolved.mipLevelCount = min(resolved.mipLevelCount, m_desc.mipLevelCount - resolved.mipLevel);
    uint32_t arrayLayerCount = m_desc.arrayLength * (m_desc.type == TextureType::TextureCube ? 6 : 1);
    resolved.baseArrayLayer = min(resolved.baseArrayLayer, arrayLayerCount);
    resolved.layerCount = min(resolved.layerCount, arrayLayerCount - resolved.baseArrayLayer);
    return resolved;
}

bool Texture::isEntireTexture(const SubresourceRange& range)
{
    if (range.mipLevel > 0 || range.mipLevelCount < m_desc.mipLevelCount)
    {
        return false;
    }
    uint32_t arrayLayerCount = m_desc.arrayLength * (m_desc.type == TextureType::TextureCube ? 6 : 1);
    if (range.baseArrayLayer > 0 || range.layerCount < arrayLayerCount)
    {
        return false;
    }
    return true;
}

TextureDesc& Texture::getDesc()
{
    return m_desc;
}

Result Texture::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Texture::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

ITextureView* TextureView::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ITextureView::getTypeGuid())
        return static_cast<ITextureView*>(this);
    return nullptr;
}

Result TextureView::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

ISampler* Sampler::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ISampler::getTypeGuid())
        return static_cast<ISampler*>(this);
    return nullptr;
}

const SamplerDesc& Sampler::getDesc()
{
    return m_desc;
}

Result Sampler::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_IMPLEMENTED;
}

IAccelerationStructure* AccelerationStructure::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() ||
        guid == IAccelerationStructure::getTypeGuid())
        return static_cast<IAccelerationStructure*>(this);
    return nullptr;
}

AccelerationStructureHandle AccelerationStructure::getHandle()
{
    return {};
}

bool _doesValueFitInExistentialPayload(
    slang::TypeLayoutReflection* concreteTypeLayout,
    slang::TypeLayoutReflection* existentialTypeLayout
)
{
    // Our task here is to figure out if a value of `concreteTypeLayout`
    // can fit into an existential value using `existentialTypelayout`.

    // We can start by asking how many bytes the concrete type of the object consumes.
    //
    auto concreteValueSize = concreteTypeLayout->getSize();

    // We can also compute how many bytes the existential-type value provides,
    // but we need to remember that the *payload* part of that value comes after
    // the header with RTTI and witness-table IDs, so the payload is 16 bytes
    // smaller than the entire value.
    //
    auto existentialValueSize = existentialTypeLayout->getSize();
    auto existentialPayloadSize = existentialValueSize - 16;

    // If the concrete type consumes more ordinary bytes than we have in the payload,
    // it cannot possibly fit.
    //
    if (concreteValueSize > existentialPayloadSize)
        return false;

    // It is possible that the ordinary bytes of `concreteTypeLayout` can fit
    // in the payload, but that type might also use storage other than ordinary
    // bytes. In that case, the value would *not* fit, because all the non-ordinary
    // data can't fit in the payload at all.
    //
    auto categoryCount = concreteTypeLayout->getCategoryCount();
    for (unsigned int i = 0; i < categoryCount; ++i)
    {
        auto category = concreteTypeLayout->getCategoryByIndex(i);
        switch (category)
        {
        // We want to ignore any ordinary/uniform data usage, since that
        // was already checked above.
        //
        case slang::ParameterCategory::Uniform:
            break;

        // Any other kind of data consumed means the value cannot possibly fit.
        default:
            return false;

            // TODO: Are there any cases of resource usage that need to be ignored here?
            // E.g., if the sub-object contains its own existential-type fields (which
            // get reflected as consuming "existential value" storage) should that be
            // ignored?
        }
    }

    // If we didn't reject the concrete type above for either its ordinary
    // data or some use of non-ordinary data, then it seems like it must fit.
    //
    return true;
}

IShaderProgram* ShaderProgram::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IShaderProgram::getTypeGuid())
        return static_cast<IShaderProgram*>(this);
    return nullptr;
}

IInputLayout* InputLayout::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IInputLayout::getTypeGuid())
        return static_cast<IInputLayout*>(this);
    return nullptr;
}

IQueryPool* QueryPool::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IQueryPool::getTypeGuid())
        return static_cast<IQueryPool*>(this);
    return nullptr;
}

IRenderPassEncoder* RenderPassEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IRenderPassEncoder::getTypeGuid())
        return static_cast<IRenderPassEncoder*>(this);
    return nullptr;
}

RenderPassEncoder::RenderPassEncoder(CommandEncoder* commandEncoder)
    : m_commandEncoder(commandEncoder)
{
}

void RenderPassEncoder::writeRenderState()
{
    commands::SetRenderState cmd;
    cmd.state = m_renderState;
    cmd.pipeline = m_pipeline;
    // TODO(shaderobject) handle errors
    m_commandEncoder->getPipelineSpecializationArgs(m_pipeline, m_rootObject, cmd.specializationArgs);
    m_commandEncoder->getBindingData(m_rootObject, cmd.bindingData);
    m_commandList->write(std::move(cmd));
}

IShaderObject* RenderPassEncoder::bindPipeline(IRenderPipeline* pipeline)
{
    if (m_commandList)
    {
        m_pipeline = pipeline;
        ShaderProgram* program = checked_cast<ShaderProgram*>(pipeline->getProgram());
        if (!SLANG_SUCCEEDED(m_commandEncoder->getDevice()->createRootShaderObject(program, m_rootObject.writeRef())))
            return nullptr;
        return m_rootObject;
    }
    return nullptr;
}

void RenderPassEncoder::bindPipeline(IRenderPipeline* pipeline, IShaderObject* rootObject)
{
    if (m_commandList)
    {
        m_pipeline = checked_cast<RenderPipeline*>(pipeline);
        m_rootObject = checked_cast<RootShaderObject*>(rootObject);
    }
}

void RenderPassEncoder::setRenderState(const RenderState& state)
{
    if (m_commandList)
    {
        m_renderState = state;
    }
}

void RenderPassEncoder::draw(const DrawArguments& args)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::Draw cmd;
        cmd.args = args;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawIndexed(const DrawArguments& args)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawIndexed cmd;
        cmd.args = args;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawIndirect(
    uint32_t maxDrawCount,
    IBuffer* argBuffer,
    uint64_t argOffset,
    IBuffer* countBuffer,
    uint64_t countOffset
)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawIndirect cmd;
        cmd.maxDrawCount = maxDrawCount;
        cmd.argBuffer = argBuffer;
        cmd.argOffset = argOffset;
        cmd.countBuffer = countBuffer;
        cmd.countOffset = countOffset;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawIndexedIndirect(
    uint32_t maxDrawCount,
    IBuffer* argBuffer,
    uint64_t argOffset,
    IBuffer* countBuffer,
    uint64_t countOffset
)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawIndexedIndirect cmd;
        cmd.maxDrawCount = maxDrawCount;
        cmd.argBuffer = argBuffer;
        cmd.argOffset = argOffset;
        cmd.countBuffer = countBuffer;
        cmd.countOffset = countOffset;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawMeshTasks(uint32_t x, uint32_t y, uint32_t z)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawMeshTasks cmd;
        cmd.x = x;
        cmd.y = y;
        cmd.z = z;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::PushDebugGroup cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::popDebugGroup()
{
    if (m_commandList)
    {
        commands::PopDebugGroup cmd;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::InsertDebugMarker cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::end()
{
    if (m_commandList)
    {
        commands::EndRenderPass cmd;
        m_commandList->write(std::move(cmd));
        m_commandList = nullptr;
    }
}

IComputePassEncoder* ComputePassEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IComputePassEncoder::getTypeGuid())
        return static_cast<IComputePassEncoder*>(this);
    return nullptr;
}

ComputePassEncoder::ComputePassEncoder(CommandEncoder* commandEncoder)
    : m_commandEncoder(commandEncoder)
{
}

void ComputePassEncoder::writeComputeState()
{
    commands::SetComputeState cmd;
    cmd.pipeline = m_pipeline;
    // TODO(shaderobject) handle errors
    m_commandEncoder->getPipelineSpecializationArgs(m_pipeline, m_rootObject, cmd.specializationArgs);
    m_commandEncoder->getBindingData(m_rootObject, cmd.bindingData);
    m_commandList->write(std::move(cmd));
}

IShaderObject* ComputePassEncoder::bindPipeline(IComputePipeline* pipeline)
{
    if (m_commandList)
    {
        m_pipeline = pipeline;
        ShaderProgram* program = checked_cast<ShaderProgram*>(pipeline->getProgram());
        if (!SLANG_SUCCEEDED(m_commandEncoder->getDevice()->createRootShaderObject(program, m_rootObject.writeRef())))
            return nullptr;
        return m_rootObject;
    }
    return nullptr;
}

void ComputePassEncoder::bindPipeline(IComputePipeline* pipeline, IShaderObject* rootObject)
{
    if (m_commandList)
    {
        m_pipeline = checked_cast<ComputePipeline*>(pipeline);
        m_rootObject = checked_cast<RootShaderObject*>(rootObject);
    }
}

void ComputePassEncoder::dispatchCompute(uint32_t x, uint32_t y, uint32_t z)
{
    if (m_commandList)
    {
        writeComputeState();
        commands::DispatchCompute cmd;
        cmd.x = x;
        cmd.y = y;
        cmd.z = z;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::dispatchComputeIndirect(IBuffer* argBuffer, uint64_t offset)
{
    if (m_commandList)
    {
        writeComputeState();
        commands::DispatchComputeIndirect cmd;
        cmd.argBuffer = argBuffer;
        cmd.offset = offset;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::PushDebugGroup cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::popDebugGroup()
{
    if (m_commandList)
    {
        commands::PopDebugGroup cmd;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::InsertDebugMarker cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::end()
{
    if (m_commandList)
    {
        commands::EndComputePass cmd;
        m_commandList->write(std::move(cmd));
        m_commandList = nullptr;
    }
}

IRayTracingPassEncoder* RayTracingPassEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IRayTracingPassEncoder::getTypeGuid())
        return static_cast<IRayTracingPassEncoder*>(this);
    return nullptr;
}

RayTracingPassEncoder::RayTracingPassEncoder(CommandEncoder* commandEncoder)
    : m_commandEncoder(commandEncoder)
{
}

void RayTracingPassEncoder::writeRayTracingState()
{
    commands::SetRayTracingState cmd;
    cmd.pipeline = m_pipeline;
    cmd.shaderTable = m_shaderTable;
    // TODO handle errors
    m_commandEncoder->getPipelineSpecializationArgs(m_pipeline, m_rootObject, cmd.specializationArgs);
    m_commandEncoder->getBindingData(m_rootObject, cmd.bindingData);
    m_commandList->write(std::move(cmd));
}

IShaderObject* RayTracingPassEncoder::bindPipeline(IRayTracingPipeline* pipeline, IShaderTable* shaderTable)
{
    if (m_commandList)
    {
        m_pipeline = pipeline;
        m_shaderTable = shaderTable;
        ShaderProgram* program = checked_cast<ShaderProgram*>(pipeline->getProgram());
        if (!SLANG_SUCCEEDED(m_commandEncoder->getDevice()->createRootShaderObject(program, m_rootObject.writeRef())))
            return nullptr;
        return m_rootObject;
    }
    return nullptr;
}

void RayTracingPassEncoder::bindPipeline(
    IRayTracingPipeline* pipeline,
    IShaderTable* shaderTable,
    IShaderObject* rootObject
)
{
    if (m_commandList)
    {
        m_pipeline = checked_cast<RayTracingPipeline*>(pipeline);
        m_shaderTable = checked_cast<ShaderTable*>(shaderTable);
        m_rootObject = checked_cast<RootShaderObject*>(rootObject);
    }
}

void RayTracingPassEncoder::dispatchRays(uint32_t rayGenShaderIndex, uint32_t width, uint32_t height, uint32_t depth)
{
    if (m_commandList)
    {
        writeRayTracingState();
        commands::DispatchRays cmd;
        cmd.rayGenShaderIndex = rayGenShaderIndex;
        cmd.width = width;
        cmd.height = height;
        cmd.depth = depth;
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::PushDebugGroup cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::popDebugGroup()
{
    if (m_commandList)
    {
        commands::PopDebugGroup cmd;
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::InsertDebugMarker cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::end()
{
    if (m_commandList)
    {
        commands::EndRayTracingPass cmd;
        m_commandList->write(std::move(cmd));
        m_commandList = nullptr;
    }
}

ICommandEncoder* CommandEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ICommandEncoder::getTypeGuid())
        return static_cast<ICommandEncoder*>(this);
    return nullptr;
}

CommandEncoder::CommandEncoder()
    : m_renderPassEncoder(this)
    , m_computePassEncoder(this)
    , m_rayTracingPassEncoder(this)
{
}

IRenderPassEncoder* CommandEncoder::beginRenderPass(const RenderPassDesc& desc)
{
    commands::BeginRenderPass cmd;
    cmd.desc = desc;
    m_commandList->write(std::move(cmd));
    m_renderPassEncoder.m_commandList = m_commandList;
    return &m_renderPassEncoder;
}

IComputePassEncoder* CommandEncoder::beginComputePass()
{
    commands::BeginComputePass cmd;
    m_commandList->write(std::move(cmd));
    m_computePassEncoder.m_commandList = m_commandList;
    return &m_computePassEncoder;
}

IRayTracingPassEncoder* CommandEncoder::beginRayTracingPass()
{
    commands::BeginRayTracingPass cmd;
    m_commandList->write(std::move(cmd));
    m_rayTracingPassEncoder.m_commandList = m_commandList;
    return &m_rayTracingPassEncoder;
}

void CommandEncoder::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    commands::CopyBuffer cmd;
    cmd.dst = dst;
    cmd.dstOffset = dstOffset;
    cmd.src = src;
    cmd.srcOffset = srcOffset;
    cmd.size = size;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::copyTexture(
    ITexture* dst,
    SubresourceRange dstSubresource,
    Offset3D dstOffset,
    ITexture* src,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    commands::CopyTexture cmd;
    cmd.dst = dst;
    cmd.dstSubresource = dstSubresource;
    cmd.dstOffset = dstOffset;
    cmd.src = src;
    cmd.srcSubresource = srcSubresource;
    cmd.srcOffset = srcOffset;
    cmd.extent = extent;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::copyTextureToBuffer(
    IBuffer* dst,
    Offset dstOffset,
    Size dstSize,
    Size dstRowStride,
    ITexture* src,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    commands::CopyTextureToBuffer cmd;
    cmd.dst = dst;
    cmd.dstOffset = dstOffset;
    cmd.dstSize = dstSize;
    cmd.dstRowStride = dstRowStride;
    cmd.src = src;
    cmd.srcSubresource = srcSubresource;
    cmd.srcOffset = srcOffset;
    cmd.extent = extent;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::uploadTextureData(
    ITexture* dst,
    SubresourceRange subresourceRange,
    Offset3D offset,
    Extents extent,
    SubresourceData* subresourceData,
    uint32_t subresourceDataCount
)
{
    commands::UploadTextureData cmd;
    cmd.dst = dst;
    cmd.subresourceRange = subresourceRange;
    cmd.offset = offset;
    cmd.extent = extent;
    cmd.subresourceData = subresourceData;
    cmd.subresourceDataCount = subresourceDataCount;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    commands::UploadBufferData cmd;
    cmd.dst = dst;
    cmd.offset = offset;
    cmd.size = size;
    cmd.data = data;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    commands::ClearBuffer cmd;
    cmd.buffer = buffer;
    cmd.range = range ? *range : checked_cast<Buffer*>(buffer)->resolveBufferRange(kEntireBuffer);
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::clearTexture(
    ITexture* texture,
    const ClearValue& clearValue,
    const SubresourceRange* subresourceRange,
    bool clearDepth,
    bool clearStencil
)
{
    commands::ClearTexture cmd;
    cmd.texture = texture;
    cmd.clearValue = clearValue;
    cmd.subresourceRange =
        subresourceRange ? *subresourceRange : checked_cast<Texture*>(texture)->resolveSubresourceRange(kEntireTexture);
    cmd.clearDepth = clearDepth;
    cmd.clearStencil = clearStencil;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::resolveQuery(
    IQueryPool* queryPool,
    uint32_t index,
    uint32_t count,
    IBuffer* buffer,
    uint64_t offset
)
{
    commands::ResolveQuery cmd;
    cmd.queryPool = queryPool;
    cmd.index = index;
    cmd.count = count;
    cmd.buffer = buffer;
    cmd.offset = offset;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::buildAccelerationStructure(
    const AccelerationStructureBuildDesc& desc,
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    BufferWithOffset scratchBuffer,
    uint32_t propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    commands::BuildAccelerationStructure cmd;
    cmd.desc = desc;
    cmd.dst = dst;
    cmd.src = src;
    cmd.scratchBuffer = scratchBuffer;
    cmd.propertyQueryCount = propertyQueryCount;
    cmd.queryDescs = queryDescs;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::copyAccelerationStructure(
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    commands::CopyAccelerationStructure cmd;
    cmd.dst = checked_cast<AccelerationStructure*>(dst);
    cmd.src = checked_cast<AccelerationStructure*>(src);
    cmd.mode = mode;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::queryAccelerationStructureProperties(
    uint32_t accelerationStructureCount,
    IAccelerationStructure** accelerationStructures,
    uint32_t queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_RHI_UNIMPLEMENTED("queryAccelerationStructureProperties");
}

void CommandEncoder::serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src)
{
    commands::SerializeAccelerationStructure cmd;
    cmd.dst = dst;
    cmd.src = checked_cast<AccelerationStructure*>(src);
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src)
{
    commands::DeserializeAccelerationStructure cmd;
    cmd.dst = checked_cast<AccelerationStructure*>(dst);
    cmd.src = src;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::convertCooperativeVectorMatrix(const ConvertCooperativeVectorMatrixDesc* descs, uint32_t descCount)
{
    commands::ConvertCooperativeVectorMatrix cmd;
    cmd.descs = descs;
    cmd.descCount = descCount;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::setBufferState(IBuffer* buffer, ResourceState state)
{
    commands::SetBufferState cmd;
    cmd.buffer = checked_cast<Buffer*>(buffer);
    cmd.state = state;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    commands::SetTextureState cmd;
    cmd.texture = checked_cast<Texture*>(texture);
    cmd.subresourceRange = subresourceRange;
    cmd.state = state;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    commands::PushDebugGroup cmd;
    cmd.name = name;
    cmd.rgbColor[0] = rgbColor[0];
    cmd.rgbColor[1] = rgbColor[1];
    cmd.rgbColor[2] = rgbColor[2];
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::popDebugGroup()
{
    commands::PopDebugGroup cmd;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    commands::InsertDebugMarker cmd;
    cmd.name = name;
    cmd.rgbColor[0] = rgbColor[0];
    cmd.rgbColor[1] = rgbColor[1];
    cmd.rgbColor[2] = rgbColor[2];
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    commands::WriteTimestamp cmd;
    cmd.queryPool = checked_cast<QueryPool*>(queryPool);
    cmd.queryIndex = queryIndex;
    m_commandList->write(std::move(cmd));
}

Result CommandEncoder::finish(ICommandBuffer** outCommandBuffer)
{
    // iterate over commands and specialize pipelines
    return SLANG_FAIL;
}

Result CommandEncoder::getPipelineSpecializationArgs(
    IPipeline* pipeline,
    IShaderObject* object,
    ExtendedShaderObjectTypeListObject*& outSpecializationArgs
)
{
    if (checked_cast<ShaderProgram*>(pipeline->getProgram())->isSpecializable())
    {
        RootShaderObject* rootObject = checked_cast<RootShaderObject*>(object);
        RefPtr<ExtendedShaderObjectTypeListObject> specializationArgs = new ExtendedShaderObjectTypeListObject();
        rootObject->collectSpecializationArgs(*specializationArgs);
        m_pipelineSpecializationArgs.push_back(specializationArgs);
        outSpecializationArgs = specializationArgs.get();
    }
    else
    {
        outSpecializationArgs = nullptr;
    }
    return SLANG_OK;
}

Result CommandEncoder::resolvePipelines(Device* device)
{
    CommandList* commandList = m_commandList;
    auto command = commandList->getCommands();
    while (command)
    {
        if (command->id == CommandID::SetRenderState)
        {
            auto& cmd = commandList->getCommand<commands::SetRenderState>(command);
            RenderPipeline* pipeline = checked_cast<RenderPipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<RenderPipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        else if (command->id == CommandID::SetComputeState)
        {
            auto& cmd = commandList->getCommand<commands::SetComputeState>(command);
            ComputePipeline* pipeline = checked_cast<ComputePipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<ComputePipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        else if (command->id == CommandID::SetRayTracingState)
        {
            auto& cmd = commandList->getCommand<commands::SetRayTracingState>(command);
            RayTracingPipeline* pipeline = checked_cast<RayTracingPipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<RayTracingPipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        command = command->next;
    }
    return SLANG_OK;
}

ICommandBuffer* CommandBuffer::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ICommandBuffer::getTypeGuid())
        return static_cast<ICommandBuffer*>(this);
    return nullptr;
}

IPipeline* RenderPipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IRenderPipeline::getTypeGuid())
        return static_cast<IRenderPipeline*>(this);
    return nullptr;
}

Result VirtualRenderPipeline::init(Device* device, const RenderPipelineDesc& desc)
{
    m_device = device;
    m_desc = desc;
    m_descHolder.holdList(m_desc.targets, m_desc.targetCount);
    m_program = checked_cast<ShaderProgram*>(desc.program);
    m_inputLayout = checked_cast<InputLayout*>(desc.inputLayout);
    return SLANG_OK;
}

Result VirtualRenderPipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

IPipeline* ComputePipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IComputePipeline::getTypeGuid())
        return static_cast<IComputePipeline*>(this);
    return nullptr;
}

Result VirtualComputePipeline::init(Device* device, const ComputePipelineDesc& desc)
{
    m_device = device;
    m_desc = desc;
    m_program = checked_cast<ShaderProgram*>(desc.program);
    return SLANG_OK;
}

Result VirtualComputePipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

IPipeline* RayTracingPipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IRayTracingPipeline::getTypeGuid())
        return static_cast<IRayTracingPipeline*>(this);
    return nullptr;
}

Result VirtualRayTracingPipeline::init(Device* device, const RayTracingPipelineDesc& desc)
{
    m_device = device;
    m_desc = desc;
    m_descHolder.holdList(m_desc.hitGroups, m_desc.hitGroupCount);
    for (uint32_t i = 0; i < m_desc.hitGroupCount; i++)
    {
        m_descHolder.holdString(m_desc.hitGroups[i].hitGroupName);
        m_descHolder.holdString(m_desc.hitGroups[i].closestHitEntryPoint);
        m_descHolder.holdString(m_desc.hitGroups[i].anyHitEntryPoint);
        m_descHolder.holdString(m_desc.hitGroups[i].intersectionEntryPoint);
    }
    m_program = checked_cast<ShaderProgram*>(desc.program);
    return SLANG_OK;
}

Result VirtualRayTracingPipeline::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getEntryPointCodeFromShaderCache(
    slang::IComponentType* program,
    SlangInt entryPointIndex,
    SlangInt targetIndex,
    slang::IBlob** outCode,
    slang::IBlob** outDiagnostics
)
{
    // Immediately call getEntryPointCode if shader cache is not available.
    if (!m_persistentShaderCache)
    {
        return program->getEntryPointCode(entryPointIndex, targetIndex, outCode, outDiagnostics);
    }

    // Hash all relevant state for generating the entry point shader code to use as a key
    // for the shader cache.
    ComPtr<ISlangBlob> hashBlob;
    program->getEntryPointHash(entryPointIndex, targetIndex, hashBlob.writeRef());

    // Query the shader cache.
    ComPtr<ISlangBlob> codeBlob;
    if (m_persistentShaderCache->queryCache(hashBlob, codeBlob.writeRef()) != SLANG_OK)
    {
        // No cached entry found. Generate the code and add it to the cache.
        SLANG_RETURN_ON_FAIL(
            program->getEntryPointCode(entryPointIndex, targetIndex, codeBlob.writeRef(), outDiagnostics)
        );
        m_persistentShaderCache->writeCache(hashBlob, codeBlob);
    }

    *outCode = codeBlob.detach();
    return SLANG_OK;
}

Result Device::queryInterface(const SlangUUID& uuid, void** outObject)
{
    *outObject = getInterface(uuid);
    return SLANG_OK;
}

IDevice* Device::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IDevice::getTypeGuid())
        return static_cast<IDevice*>(this);
    return nullptr;
}

Result Device::initialize(const DeviceDesc& desc)
{
    m_debugCallback = desc.debugCallback ? desc.debugCallback : NullDebugCallback::getInstance();

    m_persistentShaderCache = desc.persistentShaderCache;

    if (desc.apiCommandDispatcher)
    {
        desc.apiCommandDispatcher->queryInterface(
            IPipelineCreationAPIDispatcher::getTypeGuid(),
            (void**)m_pipelineCreationAPIDispatcher.writeRef()
        );
    }
    return SLANG_OK;
}

Result Device::getNativeDeviceHandles(DeviceNativeHandles* outHandles)
{
    return SLANG_OK;
}

Result Device::getFeatures(const char** outFeatures, size_t bufferSize, uint32_t* outFeatureCount)
{
    if (bufferSize >= m_features.size())
    {
        for (size_t i = 0; i < m_features.size(); i++)
        {
            outFeatures[i] = m_features[i].data();
        }
    }
    if (outFeatureCount)
        *outFeatureCount = (uint32_t)m_features.size();
    return SLANG_OK;
}

bool Device::hasFeature(const char* featureName)
{
    return std::any_of(
        m_features.begin(),
        m_features.end(),
        [&](const std::string& feature) { return feature == featureName; }
    );
}

Result Device::getFormatSupport(Format format, FormatSupport* outFormatSupport)
{
    SLANG_UNUSED(format);
    FormatSupport support = FormatSupport::None;
    support |= FormatSupport::Buffer;
    support |= FormatSupport::IndexBuffer;
    support |= FormatSupport::VertexBuffer;
    support |= FormatSupport::Texture;
    support |= FormatSupport::DepthStencil;
    support |= FormatSupport::RenderTarget;
    support |= FormatSupport::Blendable;
    support |= FormatSupport::ShaderLoad;
    support |= FormatSupport::ShaderSample;
    support |= FormatSupport::ShaderUavLoad;
    support |= FormatSupport::ShaderUavStore;
    support |= FormatSupport::ShaderAtomic;
    *outFormatSupport = support;
    return SLANG_OK;
}

Result Device::getSlangSession(slang::ISession** outSlangSession)
{
    *outSlangSession = m_slangContext.session.get();
    m_slangContext.session->addRef();
    return SLANG_OK;
}

Result Device::createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& srcDesc, ITexture** outTexture)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(srcDesc);
    SLANG_UNUSED(outTexture);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createTextureFromSharedHandle(
    NativeHandle handle,
    const TextureDesc& srcDesc,
    const Size size,
    ITexture** outTexture
)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(srcDesc);
    SLANG_UNUSED(size);
    SLANG_UNUSED(outTexture);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(srcDesc);
    SLANG_UNUSED(outBuffer);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(srcDesc);
    SLANG_UNUSED(outBuffer);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outLayout);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createRenderPipeline(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    ShaderProgram* program = checked_cast<ShaderProgram*>(desc.program);
    bool createVirtual = program->isSpecializable();
    if (createVirtual)
    {
        RefPtr<VirtualRenderPipeline> pipeline = new VirtualRenderPipeline();
        SLANG_RETURN_ON_FAIL(pipeline->init(this, desc));
        returnComPtr(outPipeline, pipeline);
        return SLANG_OK;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(program->compileShaders(this));
        return createRenderPipeline2(desc, outPipeline);
    }
}

Result Device::createComputePipeline(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    ShaderProgram* program = checked_cast<ShaderProgram*>(desc.program);
    bool createVirtual = program->isSpecializable();
    if (createVirtual)
    {
        RefPtr<VirtualComputePipeline> pipeline = new VirtualComputePipeline();
        SLANG_RETURN_ON_FAIL(pipeline->init(this, desc));
        returnComPtr(outPipeline, pipeline);
        return SLANG_OK;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(program->compileShaders(this));
        return createComputePipeline2(desc, outPipeline);
    }
}

Result Device::createRayTracingPipeline(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    ShaderProgram* program = checked_cast<ShaderProgram*>(desc.program);
    bool createVirtual = program->isSpecializable();
    if (createVirtual)
    {
        RefPtr<VirtualRayTracingPipeline> pipeline = new VirtualRayTracingPipeline();
        SLANG_RETURN_ON_FAIL(pipeline->init(this, desc));
        returnComPtr(outPipeline, pipeline);
        return SLANG_OK;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(program->compileShaders(this));
        return createRayTracingPipeline2(desc, outPipeline);
    }
}

Result Device::createShaderObject(
    slang::ISession* slangSession,
    slang::TypeReflection* type,
    ShaderObjectContainerType container,
    IShaderObject** outObject
)
{
    if (slangSession == nullptr)
        slangSession = m_slangContext.session.get();
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(slangSession, type, container, shaderObjectLayout.writeRef()));
    RefPtr<ShaderObject> shaderObject;
    SLANG_RETURN_ON_FAIL(createShaderObject(shaderObjectLayout, shaderObject.writeRef()));
    returnComPtr(outObject, shaderObject);
    return SLANG_OK;
}

Result Device::createShaderObjectFromTypeLayout(slang::TypeLayoutReflection* typeLayout, IShaderObject** outObject)
{
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(m_slangContext.session, typeLayout, shaderObjectLayout.writeRef()));
    RefPtr<ShaderObject> shaderObject;
    SLANG_RETURN_ON_FAIL(createShaderObject(shaderObjectLayout, shaderObject.writeRef()));
    returnComPtr(outObject, shaderObject);
    return SLANG_OK;
}

Result Device::createRootShaderObject(IShaderProgram* program, IShaderObject** outObject)
{
    ShaderProgram* shaderProgram = checked_cast<ShaderProgram*>(program);
    RefPtr<RootShaderObject> rootShaderObject;
    SLANG_RETURN_ON_FAIL(createRootShaderObject(shaderProgram, rootShaderObject.writeRef()));
    returnComPtr(outObject, rootShaderObject);
    return SLANG_OK;
}

Result Device::getAccelerationStructureSizes(
    const AccelerationStructureBuildDesc& desc,
    AccelerationStructureSizes* outSizes
)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outSizes);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outAccelerationStructure);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createShaderTable(const ShaderTableDesc& desc, IShaderTable** outTable)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outTable);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createFence(const FenceDesc& desc, IFence** outFence)
{
    SLANG_UNUSED(desc);
    *outFence = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::waitForFences(
    uint32_t fenceCount,
    IFence** fences,
    uint64_t* fenceValues,
    bool waitForAll,
    uint64_t timeout
)
{
    SLANG_UNUSED(fenceCount);
    SLANG_UNUSED(fences);
    SLANG_UNUSED(fenceValues);
    SLANG_UNUSED(waitForAll);
    SLANG_UNUSED(timeout);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment)
{
    SLANG_UNUSED(desc);
    *outSize = 0;
    *outAlignment = 0;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getTextureRowAlignment(Size* outAlignment)
{
    *outAlignment = 0;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    SLANG_UNUSED(windowHandle);
    *outSurface = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertyCount)
{
    if (m_cooperativeVectorProperties.empty())
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    if (*propertyCount == 0)
    {
        *propertyCount = uint32_t(m_cooperativeVectorProperties.size());
        return SLANG_OK;
    }
    else
    {
        uint32_t count = min(*propertyCount, uint32_t(m_cooperativeVectorProperties.size()));
        ::memcpy(properties, m_cooperativeVectorProperties.data(), count * sizeof(CooperativeVectorProperties));
        Result result = count == *propertyCount ? SLANG_OK : SLANG_E_BUFFER_TOO_SMALL;
        *propertyCount = count;
        return result;
    }
}

Result Device::convertCooperativeVectorMatrix(const ConvertCooperativeVectorMatrixDesc* descs, uint32_t descCount)
{
    SLANG_UNUSED(descs);
    SLANG_UNUSED(descCount);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getShaderObjectLayout(
    slang::ISession* session,
    slang::TypeReflection* type,
    ShaderObjectContainerType container,
    ShaderObjectLayout** outLayout
)
{
    switch (container)
    {
    case ShaderObjectContainerType::StructuredBuffer:
        type = session->getContainerType(type, slang::ContainerType::StructuredBuffer);
        break;
    case ShaderObjectContainerType::Array:
        type = session->getContainerType(type, slang::ContainerType::UnsizedArray);
        break;
    default:
        break;
    }

    auto typeLayout = session->getTypeLayout(type);
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(session, typeLayout, outLayout));
    (*outLayout)->m_slangSession = session;
    return SLANG_OK;
}

Result Device::getShaderObjectLayout(
    slang::ISession* session,
    slang::TypeLayoutReflection* typeLayout,
    ShaderObjectLayout** outLayout
)
{
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    auto it = m_shaderObjectLayoutCache.find(typeLayout);
    if (it != m_shaderObjectLayoutCache.end())
    {
        shaderObjectLayout = it->second;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(createShaderObjectLayout(session, typeLayout, shaderObjectLayout.writeRef()));
        m_shaderObjectLayoutCache.emplace(typeLayout, shaderObjectLayout);
    }
    *outLayout = shaderObjectLayout.detach();
    return SLANG_OK;
}

ShaderComponentID ShaderCache::getComponentId(slang::TypeReflection* type)
{
    ComponentKey key;
    key.typeName = string::from_cstr(type->getName());
    switch (type->getKind())
    {
    case slang::TypeReflection::Kind::Specialized:
    {
        auto baseType = type->getElementType();

        std::string str;
        str += string::from_cstr(baseType->getName());

        auto rawType = (SlangReflectionType*)type;

        str += '<';
        SlangInt argCount = spReflectionType_getSpecializedTypeArgCount(rawType);
        for (SlangInt a = 0; a < argCount; ++a)
        {
            if (a != 0)
                str += ',';
            if (auto rawArgType = spReflectionType_getSpecializedTypeArgType(rawType, a))
            {
                auto argType = (slang::TypeReflection*)rawArgType;
                str += string::from_cstr(argType->getName());
            }
        }
        str += '>';
        key.typeName = std::move(str);
        key.updateHash();
        return getComponentId(key);
    }
        // TODO: collect specialization arguments and append them to `key`.
        SLANG_RHI_UNIMPLEMENTED("specialized type");
    default:
        break;
    }
    key.updateHash();
    return getComponentId(key);
}

ShaderComponentID ShaderCache::getComponentId(std::string_view name)
{
    ComponentKey key;
    key.typeName = name;
    key.updateHash();
    return getComponentId(key);
}

ShaderComponentID ShaderCache::getComponentId(ComponentKey key)
{
    auto it = componentIds.find(key);
    if (it != componentIds.end())
        return it->second;
    ShaderComponentID resultId = static_cast<ShaderComponentID>(componentIds.size());
    componentIds.emplace(key, resultId);
    return resultId;
}

void ShaderCache::addSpecializedPipeline(PipelineKey key, RefPtr<Pipeline> specializedPipeline)
{
    specializedPipelines[key] = specializedPipeline;
}

// ----------------------------------------------------------------------------
// ShaderObjectLayout
// ----------------------------------------------------------------------------

void ShaderObjectLayout::initBase(
    Device* device,
    slang::ISession* session,
    slang::TypeLayoutReflection* elementTypeLayout
)
{
    m_device = device;
    m_slangSession = session;
    m_elementTypeLayout = elementTypeLayout;
    m_componentID = m_device->m_shaderCache.getComponentId(m_elementTypeLayout->getType());
}

// ----------------------------------------------------------------------------
// ShaderObject
// ----------------------------------------------------------------------------

IShaderObject* ShaderObject::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IShaderObject::getTypeGuid())
        return static_cast<IShaderObject*>(this);
    return nullptr;
}

slang::TypeLayoutReflection* ShaderObject::getElementTypeLayout()
{
    return m_layout->getElementTypeLayout();
}

ShaderObjectContainerType ShaderObject::getContainerType()
{
    return m_layout->getContainerType();
}

uint32_t ShaderObject::getEntryPointCount()
{
    return 0;
}

Result ShaderObject::getEntryPoint(uint32_t index, IShaderObject** outEntryPoint)
{
    *outEntryPoint = nullptr;
    return SLANG_OK;
}

Result ShaderObject::setData(const ShaderOffset& offset, const void* data, Size size)
{
    SLANG_RETURN_ON_FAIL(checkFinalized());

    size_t dataOffset = offset.uniformOffset;
    size_t dataSize = size;

    uint8_t* dest = m_data.data();
    size_t availableSize = m_data.size();

    // TODO(shaderobject): We really should bounds-check access rather than silently ignoring sets
    // that are too large, but we have several test cases that set more data than
    // an object actually stores on several targets...
    //
    if ((dataOffset + dataSize) >= availableSize)
    {
        dataSize = availableSize - dataOffset;
    }

    ::memcpy(dest + dataOffset, data, dataSize);

    incrementVersion();

    return SLANG_OK;
}

Result ShaderObject::getObject(const ShaderOffset& offset, IShaderObject** outObject)
{
    SLANG_RHI_ASSERT(outObject);

    if (offset.bindingRangeIndex >= m_layout->getBindingRangeCount())
        return SLANG_E_INVALID_ARG;
    const auto& bindingRange = m_layout->getBindingRange(offset.bindingRangeIndex);

    returnComPtr(outObject, m_objects[bindingRange.subObjectIndex + offset.bindingArrayIndex]);
    return SLANG_OK;
}

Result ShaderObject::setObject(const ShaderOffset& offset, IShaderObject* object)
{
    if (m_finalized)
        return SLANG_FAIL;

    incrementVersion();

    ShaderObject* subObject = checked_cast<ShaderObject*>(object);
    // There are three different cases in `setObject`.
    // 1. `this` object represents a StructuredBuffer, and `object` is an
    //    element to be written into the StructuredBuffer.
    // 2. `object` represents a StructuredBuffer and we are setting it into
    //    a StructuredBuffer typed field in `this` object.
    // 3. We are setting `object` as an ordinary sub-object, e.g. an existential
    //    field, a constant buffer or a parameter block.
    // We handle each case separately below.

    if (m_layout->getContainerType() != ShaderObjectContainerType::None)
    {
        // Case 1:
        // We are setting an element into a `StructuredBuffer` object.
        // We need to hold a reference to the element object, as well as
        // writing uniform data to the plain buffer.
        if (offset.bindingArrayIndex >= m_objects.size())
        {
            m_objects.resize(offset.bindingArrayIndex + 1);
            auto stride = m_layout->getElementTypeLayout()->getStride();
            m_data.resize(m_objects.size() * stride);
        }
        m_objects[offset.bindingArrayIndex] = subObject;

        ExtendedShaderObjectTypeList specializationArgs;

        auto payloadOffset = offset;

        // If the element type of the StructuredBuffer field is an existential type,
        // we need to make sure to fill in the existential value header (RTTI ID and
        // witness table IDs).
        if (m_layout->getElementTypeLayout()->getKind() == slang::TypeReflection::Kind::Interface)
        {
            auto existentialType = m_layout->getElementTypeLayout()->getType();
            ExtendedShaderObjectType concreteType;
            SLANG_RETURN_ON_FAIL(subObject->getSpecializedShaderObjectType(&concreteType));
            SLANG_RETURN_ON_FAIL(setExistentialHeader(existentialType, concreteType.slangType, offset));
            payloadOffset.uniformOffset += 16;
        }
        SLANG_RETURN_ON_FAIL(setData(payloadOffset, subObject->m_data.data(), subObject->m_data.size()));
        return SLANG_OK;
    }

    // Case 2 & 3, setting object as an StructuredBuffer, ConstantBuffer, ParameterBlock or
    // existential value.

    if (offset.bindingRangeIndex >= m_layout->getBindingRangeCount())
        return SLANG_E_INVALID_ARG;

    auto bindingRangeIndex = offset.bindingRangeIndex;
    const auto& bindingRange = m_layout->getBindingRange(bindingRangeIndex);

    m_objects[bindingRange.subObjectIndex + offset.bindingArrayIndex] = subObject;

    switch (bindingRange.bindingType)
    {
    case slang::BindingType::ExistentialValue:
    {
        // If the range being assigned into represents an interface/existential-type
        // leaf field, then we need to consider how the `object` being assigned here
        // affects specialization. We may also need to assign some data from the
        // sub-object into the ordinary data buffer for the parent object.
        //
        // A leaf field of interface type is laid out inside of the parent object
        // as a tuple of `(RTTI, WitnessTable, Payload)`. The layout of these fields
        // is a contract between the compiler and any runtime system, so we will
        // need to rely on details of the binary layout.

        // We start by querying the layout/type of the concrete value that the
        // application is trying to store into the field, and also the layout/type of
        // the leaf existential-type field itself.
        //
        auto concreteTypeLayout = subObject->getElementTypeLayout();
        auto concreteType = concreteTypeLayout->getType();
        //
        auto existentialTypeLayout = m_layout->getElementTypeLayout()->getBindingRangeLeafTypeLayout(bindingRangeIndex);
        auto existentialType = existentialTypeLayout->getType();

        // Fills in the first and second field of the tuple that specify RTTI type ID
        // and witness table ID.
        SLANG_RETURN_ON_FAIL(setExistentialHeader(existentialType, concreteType, offset));

        // The third field of the tuple (offset 16) is the "payload" that is supposed to
        // hold the data for a value of the given concrete type.
        //
        auto payloadOffset = offset;
        payloadOffset.uniformOffset += 16;

        // There are two cases we need to consider here for how the payload might be
        // used:
        //
        // * If the concrete type of the value being bound is one that can "fit" into
        // the
        //   available payload space,  then it should be stored in the payload.
        //
        // * If the concrete type of the value cannot fit in the payload space, then it
        //   will need to be stored somewhere else.
        //
        if (_doesValueFitInExistentialPayload(concreteTypeLayout, existentialTypeLayout))
        {
            // If the value can fit in the payload area, then we will go ahead and copy
            // its bytes into that area.
            //
            setData(payloadOffset, subObject->m_data.data(), subObject->m_data.size());
        }
        else
        {
            // If the value does *not *fit in the payload area, then there is nothing
            // we can do at this point (beyond saving a reference to the sub-object,
            // which was handled above).
            //
            // Once all the sub-objects have been set into the parent object, we can
            // compute a specialized layout for it, and that specialized layout can tell
            // us where the data for these sub-objects has been laid out.
            return SLANG_E_NOT_IMPLEMENTED;
        }
        break;
    }
    case slang::BindingType::MutableRawBuffer:
    case slang::BindingType::RawBuffer:
    {
        // TODO(shaderobject) this is a temporary solution to allow slang render test to work
        // some tests use ShaderObject to build polymorphic structured buffers
        // we should consider this use case and provide a better solution
        // this implementation also doesn't handle the case where CPU/CUDA backends are allowed
        // to place resources into plain structured buffers (because on these backends they are just pointers)
        ComPtr<IBuffer> buffer;
        SLANG_RETURN_ON_FAIL(
            subObject->writeStructuredBuffer(subObject->getElementTypeLayout(), m_layout, buffer.writeRef())
        );
        SLANG_RETURN_ON_FAIL(setBinding(offset, buffer));
        break;
    }
    default:
        break;
    }
    return SLANG_OK;
}

Result ShaderObject::setBinding(const ShaderOffset& offset, Binding binding)
{
    SLANG_RETURN_ON_FAIL(checkFinalized());

    auto bindingRangeIndex = offset.bindingRangeIndex;
    if (bindingRangeIndex >= m_layout->getBindingRangeCount())
        return SLANG_E_INVALID_ARG;
    const auto& bindingRange = m_layout->getBindingRange(bindingRangeIndex);
    auto slotIndex = bindingRange.slotIndex + offset.bindingArrayIndex;
    if (slotIndex >= m_slots.size())
        return SLANG_E_INVALID_ARG;

    ResourceSlot& slot = m_slots[slotIndex];

    switch (binding.type)
    {
    case BindingType::Buffer:
    case BindingType::BufferWithCounter:
    {
        Buffer* buffer = checked_cast<Buffer*>(binding.resource);
        if (!buffer)
            return SLANG_E_INVALID_ARG;
        slot.type = BindingType::Buffer;
        slot.resource = buffer;
        if (binding.type == BindingType::BufferWithCounter)
            slot.resource2 = checked_cast<Buffer*>(binding.resource2);
        slot.format = buffer->m_desc.format;
        slot.bufferRange = buffer->resolveBufferRange(binding.bufferRange);
        break;
    }
    case BindingType::Texture:
    {
        // TODO(shaderobject)
        // we might want to remove BindingType::Texture and instead introduce
        // a getDefaultView() method on ITexture objects that can be used when
        // creating the binding
        Texture* texture = checked_cast<Texture*>(binding.resource);
        if (!texture)
            return SLANG_E_INVALID_ARG;
        return setBinding(offset, m_device->createTextureView(texture, {}));
    }
    case BindingType::TextureView:
    {
        TextureView* textureView = checked_cast<TextureView*>(binding.resource);
        if (!textureView)
            return SLANG_E_INVALID_ARG;
        slot.type = BindingType::TextureView;
        slot.resource = textureView;
        break;
    }
    case BindingType::Sampler:
    {
        Sampler* sampler = checked_cast<Sampler*>(binding.resource);
        if (!sampler)
            return SLANG_E_INVALID_ARG;
        slot.type = BindingType::Sampler;
        slot.resource = sampler;
        break;
    }
    case BindingType::AccelerationStructure:
    {
        AccelerationStructure* accelerationStructure = checked_cast<AccelerationStructure*>(binding.resource);
        if (!accelerationStructure)
            return SLANG_E_INVALID_ARG;
        slot.type = BindingType::AccelerationStructure;
        slot.resource = accelerationStructure;
        break;
    }
    case BindingType::CombinedTextureSampler:
    {
        // TODO(shaderobject)
        // we might want to remove BindingType::CombinedTextureSampler and instead introduce
        // a getDefaultView() method on ITexture objects that can be used when
        // creating the binding
        Texture* texture = checked_cast<Texture*>(binding.resource);
        Sampler* sampler = checked_cast<Sampler*>(binding.resource2);
        if (!texture || !sampler)
            return SLANG_E_INVALID_ARG;
        return setBinding(offset, {m_device->createTextureView(texture, {}), sampler});
    }
    case BindingType::CombinedTextureViewSampler:
    {
        TextureView* textureView = checked_cast<TextureView*>(binding.resource);
        Sampler* sampler = checked_cast<Sampler*>(binding.resource2);
        if (!textureView || !sampler)
            return SLANG_E_INVALID_ARG;
        slot.type = BindingType::CombinedTextureViewSampler;
        slot.resource = textureView;
        slot.resource2 = sampler;
        break;
    }
    default:
        return SLANG_E_INVALID_ARG;
    }

    if (m_setBindingHook)
        m_setBindingHook(this, offset, slot, bindingRange.bindingType);

    incrementVersion();

    return SLANG_OK;
}

Result ShaderObject::setSpecializationArgs(
    const ShaderOffset& offset,
    const slang::SpecializationArg* args,
    uint32_t count
)
{
    // If the shader object is a container, delegate the processing to
    // `setSpecializationArgsForContainerElements`.
    if (m_layout->getContainerType() != ShaderObjectContainerType::None)
    {
        ExtendedShaderObjectTypeList argList;
        SLANG_RETURN_ON_FAIL(getExtendedShaderTypeListFromSpecializationArgs(argList, args, count));
        setSpecializationArgsForContainerElement(argList);
        return SLANG_OK;
    }

    if (offset.bindingRangeIndex >= m_layout->getBindingRangeCount())
        return SLANG_E_INVALID_ARG;

    auto bindingRangeIndex = offset.bindingRangeIndex;
    const auto& bindingRange = m_layout->getBindingRange(bindingRangeIndex);
    uint32_t objectIndex = bindingRange.subObjectIndex + offset.bindingArrayIndex;
    if (objectIndex >= m_userProvidedSpecializationArgs.size())
        m_userProvidedSpecializationArgs.resize(objectIndex + 1);
    if (!m_userProvidedSpecializationArgs[objectIndex])
    {
        m_userProvidedSpecializationArgs[objectIndex] = new ExtendedShaderObjectTypeListObject();
    }
    else
    {
        m_userProvidedSpecializationArgs[objectIndex]->clear();
    }
    SLANG_RETURN_ON_FAIL(
        getExtendedShaderTypeListFromSpecializationArgs(*m_userProvidedSpecializationArgs[objectIndex], args, count)
    );
    return SLANG_OK;
}

const void* ShaderObject::getRawData()
{
    return m_data.data();
}

Size ShaderObject::getSize()
{
    return m_data.size();
}

Result ShaderObject::setConstantBufferOverride(IBuffer* outBuffer)
{
    return SLANG_E_NOT_AVAILABLE;
}

Result ShaderObject::finalize()
{
    if (m_finalized)
        return SLANG_FAIL;

    for (auto& object : m_objects)
    {
        if (object && !object->isFinalized())
            SLANG_RETURN_ON_FAIL(object->finalize());
    }

    return SLANG_OK;
}

bool ShaderObject::isFinalized()
{
    return m_finalized;
}

Result ShaderObject::create(Device* device, ShaderObjectLayout* layout, ShaderObject** outShaderObject)
{
    RefPtr<ShaderObject> shaderObject = new ShaderObject();
    SLANG_RETURN_ON_FAIL(shaderObject->init(device, layout));
    returnRefPtr(outShaderObject, shaderObject);
    return SLANG_OK;
}

Result ShaderObject::init(Device* device, ShaderObjectLayout* layout)
{
    m_device = device;
    m_layout = layout;

    // If the layout tells us that there is any uniform data,
    // then we will allocate a CPU memory buffer to hold that data
    // while it is being set from the host.
    //
    // Once the user is done setting the parameters/fields of this
    // shader object, we will produce a GPU-memory version of the
    // uniform data (which includes values from this object and
    // any existential-type sub-objects).
    //
    size_t uniformSize = layout->getElementTypeLayout()->getSize();
    if (uniformSize)
    {
        m_data.resize(uniformSize);
        ::memset(m_data.data(), 0, uniformSize);
    }

    m_slots.resize(layout->getSlotCount());

    // If the layout specifies that we have any sub-objects, then
    // we need to size the array to account for them.
    //
    uint32_t subObjectCount = layout->getSubObjectCount();
    m_objects.resize(subObjectCount);

    for (uint32_t subObjectRangeIndex = 0; subObjectRangeIndex < layout->getSubObjectRangeCount();
         ++subObjectRangeIndex)
    {
        const auto& subObjectRange = layout->getSubObjectRange(subObjectRangeIndex);
        auto subObjectLayout = layout->getSubObjectRangeLayout(subObjectRangeIndex);

        // In the case where the sub-object range represents an
        // existential-type leaf field (e.g., an `IBar`), we
        // cannot pre-allocate the object(s) to go into that
        // range, since we can't possibly know what to allocate
        // at this point.
        //
        if (!subObjectLayout)
            continue;
        //
        // Otherwise, we will allocate a sub-object to fill
        // in each entry in this range, based on the layout
        // information we already have.

        const auto& bindingRange = layout->getBindingRange(subObjectRange.bindingRangeIndex);
        for (uint32_t i = 0; i < bindingRange.count; ++i)
        {
            RefPtr<ShaderObject> subObject;
            SLANG_RETURN_ON_FAIL(ShaderObject::create(device, subObjectLayout, subObject.writeRef()));
            m_objects[bindingRange.subObjectIndex + i] = subObject;
        }
    }

    device->customizeShaderObject(this);

    return SLANG_OK;
}

Result ShaderObject::collectSpecializationArgs(ExtendedShaderObjectTypeList& args)
{
    if (m_layout->getContainerType() != ShaderObjectContainerType::None)
    {
        args.addRange(m_structuredBufferSpecializationArgs);
        return SLANG_OK;
    }

    // The following logic is built on the assumption that all fields that involve
    // existential types (and therefore require specialization) will results in a sub-object
    // range in the type layout. This allows us to simply scan the sub-object ranges to find
    // out all specialization arguments.
    uint32_t subObjectRangeCount = m_layout->getSubObjectRangeCount();
    for (uint32_t subObjectRangeIndex = 0; subObjectRangeIndex < subObjectRangeCount; subObjectRangeIndex++)
    {
        const auto& subObjectRange = m_layout->getSubObjectRange(subObjectRangeIndex);
        const auto& bindingRange = m_layout->getBindingRange(subObjectRange.bindingRangeIndex);

        uint32_t oldArgsCount = args.getCount();

        uint32_t count = bindingRange.count;

        for (uint32_t subObjectIndexInRange = 0; subObjectIndexInRange < count; subObjectIndexInRange++)
        {
            ExtendedShaderObjectTypeList typeArgs;
            uint32_t objectIndex = bindingRange.subObjectIndex + subObjectIndexInRange;
            auto subObject = m_objects[objectIndex];

            if (!subObject)
                continue;

            if (objectIndex < m_userProvidedSpecializationArgs.size() && m_userProvidedSpecializationArgs[objectIndex])
            {
                args.addRange(*m_userProvidedSpecializationArgs[objectIndex]);
                continue;
            }

            switch (bindingRange.bindingType)
            {
            case slang::BindingType::ExistentialValue:
            {
                // A binding type of `ExistentialValue` means the sub-object represents a
                // interface-typed field. In this case the specialization argument for this
                // field is the actual specialized type of the bound shader object. If the
                // shader object's type is an ordinary type without existential fields, then
                // the type argument will simply be the ordinary type. But if the sub
                // object's type is itself a specialized type, we need to make sure to use
                // that type as the specialization argument.

                ExtendedShaderObjectType specializedSubObjType;
                SLANG_RETURN_ON_FAIL(subObject->getSpecializedShaderObjectType(&specializedSubObjType));
                typeArgs.add(specializedSubObjType);
                break;
            }
            case slang::BindingType::ParameterBlock:
            case slang::BindingType::ConstantBuffer:
                // If the field's type is `ParameterBlock<IFoo>`, we want to pull in the type argument
                // from the sub object for specialization.
                if (bindingRange.isSpecializable)
                {
                    ExtendedShaderObjectType specializedSubObjType;
                    SLANG_RETURN_ON_FAIL(subObject->getSpecializedShaderObjectType(&specializedSubObjType));
                    typeArgs.add(specializedSubObjType);
                }

                // If field's type is `ParameterBlock<SomeStruct>` or `ConstantBuffer<SomeStruct>`, where
                // `SomeStruct` is a struct type (not directly an interface type), we need to recursively
                // collect the specialization arguments from the bound sub object.
                SLANG_RETURN_ON_FAIL(subObject->collectSpecializationArgs(typeArgs));
                break;
            default:
                break;
            }

            auto addedTypeArgCountForCurrentRange = args.getCount() - oldArgsCount;
            if (addedTypeArgCountForCurrentRange == 0)
            {
                args.addRange(typeArgs);
            }
            else
            {
                // If type arguments for each elements in the array is different, use
                // `__Dynamic` type for the differing argument to disable specialization.
                SLANG_RHI_ASSERT(addedTypeArgCountForCurrentRange == typeArgs.getCount());
                for (uint32_t i = 0; i < addedTypeArgCountForCurrentRange; i++)
                {
                    if (args[i + oldArgsCount].componentID != typeArgs[i].componentID)
                    {
                        auto dynamicType = m_device->m_slangContext.session->getDynamicType();
                        args.componentIDs[i + oldArgsCount] = m_device->m_shaderCache.getComponentId(dynamicType);
                        args.components[i + oldArgsCount] = slang::SpecializationArg::fromType(dynamicType);
                    }
                }
            }
        }
    }
    return SLANG_OK;
}

Result ShaderObject::writeOrdinaryData(void* destData, Size destSize, ShaderObjectLayout* specializedLayout)
{
    SLANG_RHI_ASSERT(m_data.size() <= destSize);
    std::memcpy(destData, m_data.data(), m_data.size());

    // In the case where this object has any sub-objects of
    // existential/interface type, we need to recurse on those objects
    // that need to write their state into an appropriate "pending" allocation.
    //
    // Note: Any values that could fit into the "payload" included
    // in the existential-type field itself will have already been
    // written as part of `setObject()`. This loop only needs to handle
    // those sub-objects that do not "fit."
    //
    // An implementers looking at this code might wonder if things could be changed
    // so that *all* writes related to sub-objects for interface-type fields could
    // be handled in this one location, rather than having some in `setObject()` and
    // others handled here.
    //
    uint32_t subObjectRangeCount = specializedLayout->getSubObjectRangeCount();
    for (uint32_t subObjectRangeIndex = 0; subObjectRangeIndex < subObjectRangeCount; subObjectRangeIndex++)
    {
        const ShaderObjectLayout::SubObjectRangeInfo& subObjectRangeInfo =
            specializedLayout->getSubObjectRange(subObjectRangeIndex);
        const ShaderObjectLayout::BindingRangeInfo& bindingRangeInfo =
            specializedLayout->getBindingRange(subObjectRangeInfo.bindingRangeIndex);

        // We only need to handle sub-object ranges for interface/existential-type fields,
        // because fields of constant-buffer or parameter-block type are responsible for
        // the ordinary/uniform data of their own existential/interface-type sub-objects.
        //
        if (bindingRangeInfo.bindingType != slang::BindingType::ExistentialValue)
            continue;

        // Each sub-object range represents a single "leaf" field, but might be nested
        // under zero or more outer arrays, such that the number of existential values
        // in the same range can be one or more.
        //
        uint32_t count = bindingRangeInfo.count;

        // We are not concerned with the case where the existential value(s) in the range
        // git into the payload part of the leaf field.
        //
        // In the case where the value didn't fit, the Slang layout strategy would have
        // considered the requirements of the value as a "pending" allocation, and would
        // allocate storage for the ordinary/uniform part of that pending allocation inside
        // of the parent object's type layout.
        //
        // Here we assume that the Slang reflection API can provide us with a single byte
        // offset and stride for the location of the pending data allocation in the
        // specialized type layout, which will store the values for this sub-object range.
        //
        // TODO: The reflection API functions we are assuming here haven't been implemented
        // yet, so the functions being called here are stubs.
        //
        // TODO: It might not be that a single sub-object range can reliably map to a single
        // contiguous array with a single stride; we need to carefully consider what the
        // layout logic does for complex cases with multiple layers of nested arrays and
        // structures.
        //
        uint32_t subObjectRangePendingDataOffset = subObjectRangeInfo.pendingOrdinaryDataOffset;
        uint32_t subObjectRangePendingDataStride = subObjectRangeInfo.pendingOrdinaryDataStride;

        // If the range doesn't actually need/use the "pending" allocation at all, then
        // we need to detect that case and skip such ranges.
        //
        // TODO: This should probably be handled on a per-object basis by caching a "does it
        // fit?" bit as part of the information for bound sub-objects, given that we already
        // compute the "does it fit?" status as part of `setObject()`.
        //
        if (subObjectRangePendingDataOffset == 0)
            continue;

        for (uint32_t i = 0; i < count; ++i)
        {
            ShaderObject* subObject = m_objects[bindingRangeInfo.subObjectIndex + i];
            ShaderObjectLayout* subObjectLayout = specializedLayout->getSubObjectRangeLayout(subObjectRangeIndex);

            uint64_t subObjectOffset = subObjectRangePendingDataOffset + i * subObjectRangePendingDataStride;

            SLANG_RETURN_ON_FAIL(subObject->writeOrdinaryData(
                (uint8_t*)destData + subObjectOffset,
                destSize - subObjectOffset,
                subObjectLayout
            ));
        }
    }
    return SLANG_OK;
}

Result ShaderObject::writeStructuredBuffer(
    slang::TypeLayoutReflection* elementLayout,
    ShaderObjectLayout* specializedLayout,
    IBuffer** buffer
)
{
    BufferDesc bufferDesc = {};
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess;
    bufferDesc.defaultState = ResourceState::ShaderResource;
    bufferDesc.size = m_data.size();
    bufferDesc.elementSize = elementLayout->getSize();
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(bufferDesc, m_data.data(), buffer));
    return SLANG_OK;
}

void ShaderObject::trackResources(std::set<RefPtr<RefObject>>& resources)
{
    for (const auto& slot : m_slots)
    {
        if (slot.resource)
            resources.insert(slot.resource);
        if (slot.resource2)
            resources.insert(slot.resource2);
    }
    for (const auto& object : m_objects)
    {
        if (object)
            object->trackResources(resources);
    }
}

Result ShaderObject::getSpecializedShaderObjectType(ExtendedShaderObjectType* outType)
{
    if (m_shaderObjectType.slangType)
        *outType = m_shaderObjectType;
    ExtendedShaderObjectTypeList specializationArgs;
    SLANG_RETURN_ON_FAIL(collectSpecializationArgs(specializationArgs));
    if (specializationArgs.getCount() == 0)
    {
        m_shaderObjectType.componentID = m_layout->getComponentID();
        m_shaderObjectType.slangType = m_layout->getElementTypeLayout()->getType();
    }
    else
    {
        m_shaderObjectType.slangType = m_device->m_slangContext.session->specializeType(
            _getElementTypeLayout()->getType(),
            specializationArgs.components.data(),
            specializationArgs.getCount()
        );
        m_shaderObjectType.componentID = m_device->m_shaderCache.getComponentId(m_shaderObjectType.slangType);
    }
    *outType = m_shaderObjectType;
    return SLANG_OK;
}

Result ShaderObject::getExtendedShaderTypeListFromSpecializationArgs(
    ExtendedShaderObjectTypeList& list,
    const slang::SpecializationArg* args,
    uint32_t count
)
{
    for (uint32_t i = 0; i < count; i++)
    {
        ExtendedShaderObjectType extendedType;
        switch (args[i].kind)
        {
        case slang::SpecializationArg::Kind::Type:
            extendedType.slangType = args[i].type;
            extendedType.componentID = m_device->m_shaderCache.getComponentId(args[i].type);
            break;
        default:
            SLANG_RHI_ASSERT_FAILURE("Unexpected specialization argument kind.");
            return SLANG_FAIL;
        }
        list.add(extendedType);
    }
    return SLANG_OK;
}

void ShaderObject::setSpecializationArgsForContainerElement(ExtendedShaderObjectTypeList& specializationArgs)
{
    // Compute specialization args for the structured buffer object.
    // If we haven't filled anything to `m_structuredBufferSpecializationArgs` yet,
    // use `specializationArgs` directly.
    if (m_structuredBufferSpecializationArgs.getCount() == 0)
    {
        m_structuredBufferSpecializationArgs = _Move(specializationArgs);
    }
    else
    {
        // If `m_structuredBufferSpecializationArgs` already contains some arguments, we
        // need to check if they are the same as `specializationArgs`, and replace
        // anything that is different with `__Dynamic` because we cannot specialize the
        // buffer type if the element types are not the same.
        SLANG_RHI_ASSERT(m_structuredBufferSpecializationArgs.getCount() == specializationArgs.getCount());
        for (uint32_t i = 0; i < m_structuredBufferSpecializationArgs.getCount(); i++)
        {
            if (m_structuredBufferSpecializationArgs[i].componentID != specializationArgs[i].componentID)
            {
                auto dynamicType = m_device->m_slangContext.session->getDynamicType();
                m_structuredBufferSpecializationArgs.componentIDs[i] =
                    m_device->m_shaderCache.getComponentId(dynamicType);
                m_structuredBufferSpecializationArgs.components[i] = slang::SpecializationArg::fromType(dynamicType);
            }
        }
    }
}

Result ShaderObject::setExistentialHeader(
    slang::TypeReflection* existentialType,
    slang::TypeReflection* concreteType,
    ShaderOffset offset
)
{
    // The first field of the tuple (offset zero) is the run-time type information
    // (RTTI) ID for the concrete type being stored into the field.
    //
    // TODO: We need to be able to gather the RTTI type ID from `object` and then
    // use `setData(offset, &TypeID, sizeof(TypeID))`.

    // The second field of the tuple (offset 8) is the ID of the "witness" for the
    // conformance of the concrete type to the interface used by this field.
    //
    auto witnessTableOffset = offset;
    witnessTableOffset.uniformOffset += 8;
    //
    // Conformances of a type to an interface are computed and then stored by the
    // Slang runtime, so we can look up the ID for this particular conformance (which
    // will create it on demand).
    //
    // Note: If the type doesn't actually conform to the required interface for
    // this sub-object range, then this is the point where we will detect that
    // fact and error out.
    //
    uint32_t conformanceID = 0xFFFFFFFF;
    SLANG_RETURN_ON_FAIL(
        m_layout->m_slangSession->getTypeConformanceWitnessSequentialID(concreteType, existentialType, &conformanceID)
    );
    //
    // Once we have the conformance ID, then we can write it into the object
    // at the required offset.
    //
    SLANG_RETURN_ON_FAIL(setData(witnessTableOffset, &conformanceID, sizeof(conformanceID)));

    return SLANG_OK;
}

// ----------------------------------------------------------------------------
// RootShaderObject
// ----------------------------------------------------------------------------

uint32_t RootShaderObject::getEntryPointCount()
{
    return m_entryPoints.size();
}

Result RootShaderObject::getEntryPoint(uint32_t index, IShaderObject** outEntryPoint)
{
    if (index >= m_entryPoints.size())
        return SLANG_E_INVALID_ARG;
    returnComPtr(outEntryPoint, m_entryPoints[index]);
    return SLANG_OK;
}

Result RootShaderObject::create(Device* device, ShaderProgram* program, RootShaderObject** outRootShaderObject)
{
    RefPtr<RootShaderObject> rootShaderObject = new RootShaderObject();
    SLANG_RETURN_ON_FAIL(rootShaderObject->init(device, program));
    returnRefPtr(outRootShaderObject, rootShaderObject);
    return SLANG_OK;
}

Result RootShaderObject::init(Device* device, ShaderProgram* program)
{
    ShaderObjectLayout* layout = program->getRootShaderObjectLayout();
    SLANG_RETURN_ON_FAIL(ShaderObject::init(device, layout));
    m_shaderProgram = program;
    for (uint32_t entryPointIndex = 0; entryPointIndex < layout->getEntryPointCount(); entryPointIndex++)
    {
        ShaderObjectLayout* entryPointLayout = layout->getEntryPointLayout(entryPointIndex);
        RefPtr<ShaderObject> entryPoint;
        SLANG_RETURN_ON_FAIL(ShaderObject::create(device, entryPointLayout, entryPoint.writeRef()));
        m_entryPoints.push_back(entryPoint);
    }
    return SLANG_OK;
}

bool RootShaderObject::isSpecializable() const
{
    return m_shaderProgram->isSpecializable();
}

Result RootShaderObject::getSpecializedLayout(
    const ExtendedShaderObjectTypeList& args,
    ShaderObjectLayout*& outSpecializedLayout
)
{
    outSpecializedLayout = m_shaderProgram->getRootShaderObjectLayout();
    if (m_shaderProgram->isSpecializable() && args.getCount() > 0)
    {
        RefPtr<ShaderProgram> specializedProgram;
        SLANG_RETURN_ON_FAIL(m_device->getSpecializedProgram(m_shaderProgram, args, specializedProgram.writeRef()));
        outSpecializedLayout = specializedProgram->getRootShaderObjectLayout();
    }
    return SLANG_OK;
}

Result RootShaderObject::getSpecializedLayout(ShaderObjectLayout*& outSpecializedLayout)
{
    // Note: There is an important policy decision being made here that we need
    // to approach carefully.
    //
    // We are doing two different things that affect the layout of a program:
    //
    // 1. We are *composing* one or more pieces of code (notably the shared global/module
    //    stuff and the per-entry-point stuff).
    //
    // 2. We are *specializing* code that includes generic/existential parameters
    //    to concrete types/values.
    //
    // We need to decide the relative *order* of these two steps, because of how it impacts
    // layout. The layout for `specialize(compose(A,B), X, Y)` is potentially different
    // form that of `compose(specialize(A,X), specialize(B,Y))`, even when both are
    // semantically equivalent programs.
    //
    // Right now we are using the first option: we are first generating a full composition
    // of all the code we plan to use (global scope plus all entry points), and then
    // specializing it to the concatenated specialization arguments for all of that.
    //
    // In some cases, though, this model isn't appropriate. For example, when dealing with
    // ray-tracing shaders and local root signatures, we really want the parameters of each
    // entry point (actually, each entry-point *group*) to be allocated distinct storage,
    // which really means we want to compute something like:
    //
    //      SpecializedGlobals = specialize(compose(ModuleA, ModuleB, ...), X, Y, ...)
    //
    //      SpecializedEP1 = compose(SpecializedGlobals, specialize(EntryPoint1, T, U, ...))
    //      SpecializedEP2 = compose(SpecializedGlobals, specialize(EntryPoint2, A, B, ...))
    //
    // Note how in this case all entry points agree on the layout for the shared/common
    // parameters, but their layouts are also independent of one another.
    //
    // Furthermore, in this example, loading another entry point into the system would not
    // require re-computing the layouts (or generated kernel code) for any of the entry points
    // that had already been loaded (in contrast to a compose-then-specialize approach).
    //

    outSpecializedLayout = m_shaderProgram->getRootShaderObjectLayout();
    if (m_shaderProgram->isSpecializable())
    {
        ExtendedShaderObjectTypeList args;
        SLANG_RETURN_ON_FAIL(collectSpecializationArgs(args));
        SLANG_RETURN_ON_FAIL(getSpecializedLayout(args, outSpecializedLayout));
    }
    return SLANG_OK;
}

Result RootShaderObject::collectSpecializationArgs(ExtendedShaderObjectTypeList& args)
{
    SLANG_RETURN_ON_FAIL(ShaderObject::collectSpecializationArgs(args));
    for (auto& entryPoint : m_entryPoints)
    {
        SLANG_RETURN_ON_FAIL(entryPoint->collectSpecializationArgs(args));
    }
    return SLANG_OK;
}

void ShaderProgram::init(const ShaderProgramDesc& desc)
{
    m_desc = desc;

    slangGlobalScope = desc.slangGlobalScope;
    for (uint32_t i = 0; i < desc.slangEntryPointCount; i++)
    {
        slangEntryPoints.push_back(ComPtr<slang::IComponentType>(desc.slangEntryPoints[i]));
    }

    auto session = desc.slangGlobalScope ? desc.slangGlobalScope->getSession() : nullptr;
    if (desc.linkingStyle == LinkingStyle::SingleProgram)
    {
        std::vector<slang::IComponentType*> components;
        if (desc.slangGlobalScope)
        {
            components.push_back(desc.slangGlobalScope);
        }
        for (uint32_t i = 0; i < desc.slangEntryPointCount; i++)
        {
            if (!session)
            {
                session = desc.slangEntryPoints[i]->getSession();
            }
            components.push_back(desc.slangEntryPoints[i]);
        }
        session->createCompositeComponentType(components.data(), components.size(), linkedProgram.writeRef());
    }
    else
    {
        for (uint32_t i = 0; i < desc.slangEntryPointCount; i++)
        {
            if (desc.slangGlobalScope)
            {
                slang::IComponentType* entryPointComponents[2] = {desc.slangGlobalScope, desc.slangEntryPoints[i]};
                ComPtr<slang::IComponentType> linkedEntryPoint;
                session->createCompositeComponentType(entryPointComponents, 2, linkedEntryPoint.writeRef());
                linkedEntryPoints.push_back(linkedEntryPoint);
            }
            else
            {
                linkedEntryPoints.push_back(ComPtr<slang::IComponentType>(desc.slangEntryPoints[i]));
            }
        }
        linkedProgram = desc.slangGlobalScope;
    }

    m_isSpecializable = _isSpecializable();
}

Result ShaderProgram::compileShaders(Device* device)
{
    if (m_compiledShaders)
        return SLANG_OK;

    if (device->getDeviceInfo().deviceType == DeviceType::CPU)
    {
        // CPU device does not need to compile shaders.
        m_compiledShaders = true;
        return SLANG_OK;
    }

    // For a fully specialized program, read and store its kernel code in `shaderProgram`.
    auto compileShader = [&](slang::EntryPointReflection* entryPointInfo,
                             slang::IComponentType* entryPointComponent,
                             SlangInt entryPointIndex)
    {
        ComPtr<ISlangBlob> kernelCode;
        ComPtr<ISlangBlob> diagnostics;
        auto compileResult = device->getEntryPointCodeFromShaderCache(
            entryPointComponent,
            entryPointIndex,
            0,
            kernelCode.writeRef(),
            diagnostics.writeRef()
        );
        if (diagnostics)
        {
            DebugMessageType msgType = DebugMessageType::Warning;
            if (compileResult != SLANG_OK)
                msgType = DebugMessageType::Error;
            device->handleMessage(msgType, DebugMessageSource::Slang, (char*)diagnostics->getBufferPointer());
        }
        SLANG_RETURN_ON_FAIL(compileResult);
        SLANG_RETURN_ON_FAIL(createShaderModule(entryPointInfo, kernelCode));
        return SLANG_OK;
    };

    if (linkedEntryPoints.size() == 0)
    {
        // If the user does not explicitly specify entry point components, find them from
        // `linkedEntryPoints`.
        auto programReflection = linkedProgram->getLayout();
        for (SlangUInt i = 0; i < programReflection->getEntryPointCount(); i++)
        {
            SLANG_RETURN_ON_FAIL(compileShader(programReflection->getEntryPointByIndex(i), linkedProgram, (SlangInt)i));
        }
    }
    else
    {
        // If the user specifies entry point components via the separated entry point array,
        // compile code from there.
        for (auto& entryPoint : linkedEntryPoints)
        {
            SLANG_RETURN_ON_FAIL(compileShader(entryPoint->getLayout()->getEntryPointByIndex(0), entryPoint, 0));
        }
    }

    m_compiledShaders = true;

    return SLANG_OK;
}

Result ShaderProgram::createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    SLANG_UNUSED(entryPointInfo);
    SLANG_UNUSED(kernelCode);
    return SLANG_OK;
}

bool ShaderProgram::isMeshShaderProgram() const
{
    // Similar to above, interrogate either explicity specified entry point
    // componenets or the ones in the linked program entry point array
    if (linkedEntryPoints.size())
    {
        for (const auto& e : linkedEntryPoints)
            if (e->getLayout()->getEntryPointByIndex(0)->getStage() == SLANG_STAGE_MESH)
                return true;
    }
    else
    {
        const auto programReflection = linkedProgram->getLayout();
        for (SlangUInt i = 0; i < programReflection->getEntryPointCount(); ++i)
            if (programReflection->getEntryPointByIndex(i)->getStage() == SLANG_STAGE_MESH)
                return true;
    }
    return false;
}

Result Device::createShaderObject(ShaderObjectLayout* layout, ShaderObject** outObject)
{
    return ShaderObject::create(this, layout, outObject);
}

Result Device::createRootShaderObject(ShaderProgram* program, RootShaderObject** outObject)
{
    return RootShaderObject::create(this, program, outObject);
}

Result Device::getSpecializedProgram(
    ShaderProgram* program,
    const ExtendedShaderObjectTypeList& specializationArgs,
    ShaderProgram** outSpecializedProgram
)
{
    // TODO make thread-safe
    SpecializationKey key(specializationArgs);
    auto it = program->m_specializedPrograms.find(key);
    if (it != program->m_specializedPrograms.end())
    {
        returnRefPtr(outSpecializedProgram, it->second);
        return SLANG_OK;
    }
    else
    {
        RefPtr<ShaderProgram> specializedProgram;
        SLANG_RETURN_ON_FAIL(specializeProgram(program, specializationArgs, specializedProgram.writeRef()));
        program->m_specializedPrograms[key] = specializedProgram;
        // Program is owned by the cache
        specializedProgram->comFree();
        returnRefPtr(outSpecializedProgram, specializedProgram);
        return SLANG_OK;
    }
}


Result Device::specializeProgram(
    ShaderProgram* program,
    const ExtendedShaderObjectTypeList& specializationArgs,
    ShaderProgram** outSpecializedProgram
)
{
    ComPtr<slang::IComponentType> specializedComponentType;
    ComPtr<slang::IBlob> diagnosticBlob;
    Result result = program->linkedProgram->specialize(
        specializationArgs.components.data(),
        specializationArgs.getCount(),
        specializedComponentType.writeRef(),
        diagnosticBlob.writeRef()
    );
    if (diagnosticBlob)
    {
        handleMessage(
            result == SLANG_OK ? DebugMessageType::Warning : DebugMessageType::Error,
            DebugMessageSource::Slang,
            (char*)diagnosticBlob->getBufferPointer()
        );
    }
    SLANG_RETURN_ON_FAIL(result);

    // Now create the specialized shader program using compiled binaries.
    RefPtr<ShaderProgram> specializedProgram;
    ShaderProgramDesc programDesc = program->m_desc;
    programDesc.slangGlobalScope = specializedComponentType;

    if (programDesc.linkingStyle == LinkingStyle::SingleProgram)
    {
        // When linking style is SingleProgram, the specialized global scope already contains
        // entry-points, so we do not need to supply them again when creating the specialized
        // pipeline.
        programDesc.slangEntryPointCount = 0;
    }
    SLANG_RETURN_ON_FAIL(createShaderProgram(programDesc, (IShaderProgram**)specializedProgram.writeRef()));
    returnRefPtr(outSpecializedProgram, specializedProgram);
    return SLANG_OK;
}

Result Device::getConcretePipeline(
    Pipeline* pipeline,
    ExtendedShaderObjectTypeList* specializationArgs,
    Pipeline*& outPipeline
)
{
    // If this is already a concrete pipeline, then we are done.
    if (!pipeline->isVirtual())
    {
        outPipeline = pipeline;
        return SLANG_OK;
    }

    // Create key for looking up cached pipelines.
    PipelineKey pipelineKey;
    pipelineKey.pipeline = pipeline;

    // If the pipeline is specializable, collect specialization arguments from bound shader objects.
    if (pipeline->m_program->isSpecializable())
    {
        if (!specializationArgs)
            return SLANG_FAIL;
        for (const auto& componentID : specializationArgs->componentIDs)
        {
            pipelineKey.specializationArgs.push_back(componentID);
        }
    }

    // Look up pipeline in cache.
    pipelineKey.updateHash();
    RefPtr<Pipeline> concretePipeline = m_shaderCache.getSpecializedPipeline(pipelineKey);
    if (!concretePipeline)
    {
        // Specialize program if needed.
        RefPtr<ShaderProgram> program = pipeline->m_program;
        if (program->isSpecializable())
        {
            RefPtr<ShaderProgram> specializedProgram;
            SLANG_RETURN_ON_FAIL(specializeProgram(program, *specializationArgs, specializedProgram.writeRef()));
            program = specializedProgram;
            // Program is owned by the specialized pipeline.
            program->comFree();
        }

        // Ensure sure shaders are compiled.
        SLANG_RETURN_ON_FAIL(program->compileShaders(this));

        switch (pipeline->getType())
        {
        case PipelineType::Render:
        {
            RenderPipelineDesc desc = checked_cast<VirtualRenderPipeline*>(pipeline)->m_desc;
            desc.program = program;
            ComPtr<IRenderPipeline> renderPipeline;
            SLANG_RETURN_ON_FAIL(createRenderPipeline2(desc, renderPipeline.writeRef()));
            concretePipeline = checked_cast<RenderPipeline*>(renderPipeline.get());
            break;
        }
        case PipelineType::Compute:
        {
            ComputePipelineDesc desc = checked_cast<VirtualComputePipeline*>(pipeline)->m_desc;
            desc.program = program;
            ComPtr<IComputePipeline> computePipeline;
            SLANG_RETURN_ON_FAIL(createComputePipeline2(desc, computePipeline.writeRef()));
            concretePipeline = checked_cast<ComputePipeline*>(computePipeline.get());
            break;
        }
        case PipelineType::RayTracing:
        {
            RayTracingPipelineDesc desc = checked_cast<VirtualRayTracingPipeline*>(pipeline)->m_desc;
            desc.program = program;
            ComPtr<IRayTracingPipeline> rayTracingPipeline;
            SLANG_RETURN_ON_FAIL(createRayTracingPipeline2(desc, rayTracingPipeline.writeRef()));
            concretePipeline = checked_cast<RayTracingPipeline*>(rayTracingPipeline.get());
            break;
        }
        }
        m_shaderCache.addSpecializedPipeline(pipelineKey, concretePipeline);
        // Pipeline is owned by the cache.
        concretePipeline->comFree();
    }

    outPipeline = concretePipeline;
    return SLANG_OK;
}

Result Device::createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outPipeline);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outPipeline);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outPipeline);
    return SLANG_E_NOT_AVAILABLE;
}

Result ShaderTable::init(const ShaderTableDesc& desc)
{
    m_rayGenShaderCount = desc.rayGenShaderCount;
    m_missShaderCount = desc.missShaderCount;
    m_hitGroupCount = desc.hitGroupCount;
    m_callableShaderCount = desc.callableShaderCount;
    m_shaderGroupNames.reserve(
        desc.hitGroupCount + desc.missShaderCount + desc.rayGenShaderCount + desc.callableShaderCount
    );
    m_recordOverwrites.reserve(
        desc.hitGroupCount + desc.missShaderCount + desc.rayGenShaderCount + desc.callableShaderCount
    );
    for (uint32_t i = 0; i < desc.rayGenShaderCount; i++)
    {
        m_shaderGroupNames.push_back(desc.rayGenShaderEntryPointNames[i]);
        if (desc.rayGenShaderRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.rayGenShaderRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
    for (uint32_t i = 0; i < desc.missShaderCount; i++)
    {
        m_shaderGroupNames.push_back(desc.missShaderEntryPointNames[i]);
        if (desc.missShaderRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.missShaderRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
    for (uint32_t i = 0; i < desc.hitGroupCount; i++)
    {
        m_shaderGroupNames.push_back(desc.hitGroupNames[i]);
        if (desc.hitGroupRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.hitGroupRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
    for (uint32_t i = 0; i < desc.callableShaderCount; i++)
    {
        m_shaderGroupNames.push_back(desc.callableShaderEntryPointNames[i]);
        if (desc.callableShaderRecordOverwrites)
        {
            m_recordOverwrites.push_back(desc.callableShaderRecordOverwrites[i]);
        }
        else
        {
            m_recordOverwrites.push_back(ShaderRecordOverwrite{});
        }
    }
    return SLANG_OK;
}

ISurface* Surface::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ISurface::getTypeGuid())
        return static_cast<ISurface*>(this);
    return nullptr;
}

void Surface::setInfo(const SurfaceInfo& info)
{
    m_info = info;
    m_infoHolder.reset();
    m_infoHolder.holdList(m_info.formats, m_info.formatCount);
}

void Surface::setConfig(const SurfaceConfig& config)
{
    m_config = config;
}

bool isDepthFormat(Format format)
{
    switch (format)
    {
    case Format::D16_UNORM:
    case Format::D32_FLOAT:
    case Format::D32_FLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

bool isStencilFormat(Format format)
{
    switch (format)
    {
    case Format::D32_FLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

} // namespace rhi

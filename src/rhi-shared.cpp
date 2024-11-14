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
    GfxCount arrayLayerCount = m_desc.arrayLength * (m_desc.type == TextureType::TextureCube ? 6 : 1);
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
    GfxCount arrayLayerCount = m_desc.arrayLength * (m_desc.type == TextureType::TextureCube ? 6 : 1);
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

void RenderPassEncoder::end()
{
    if (m_commandList)
    {
        commands::EndRenderPass cmd;
        m_commandList->write(std::move(cmd));
        m_commandList = nullptr;
    }
}

void RenderPassEncoder::setRenderState(const RenderState& state)
{
    if (m_commandList)
    {
        commands::SetRenderState cmd;
        cmd.state = state;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::draw(const DrawArguments& args)
{
    if (m_commandList)
    {
        commands::Draw cmd;
        cmd.args = args;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawIndexed(const DrawArguments& args)
{
    if (m_commandList)
    {
        commands::DrawIndexed cmd;
        cmd.args = args;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawIndirect(
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    if (m_commandList)
    {
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
    GfxCount maxDrawCount,
    IBuffer* argBuffer,
    Offset argOffset,
    IBuffer* countBuffer,
    Offset countOffset
)
{
    if (m_commandList)
    {
        commands::DrawIndexedIndirect cmd;
        cmd.maxDrawCount = maxDrawCount;
        cmd.argBuffer = argBuffer;
        cmd.argOffset = argOffset;
        cmd.countBuffer = countBuffer;
        cmd.countOffset = countOffset;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawMeshTasks(GfxCount x, GfxCount y, GfxCount z)
{
    if (m_commandList)
    {
        commands::DrawMeshTasks cmd;
        cmd.x = x;
        cmd.y = y;
        cmd.z = z;
        m_commandList->write(std::move(cmd));
    }
}

IComputePassEncoder* ComputePassEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IComputePassEncoder::getTypeGuid())
        return static_cast<IComputePassEncoder*>(this);
    return nullptr;
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

void ComputePassEncoder::setComputeState(const ComputeState& state)
{
    if (m_commandList)
    {
        commands::SetComputeState cmd;
        cmd.state = state;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::dispatchCompute(GfxCount x, GfxCount y, GfxCount z)
{
    if (m_commandList)
    {
        commands::DispatchCompute cmd;
        cmd.x = x;
        cmd.y = y;
        cmd.z = z;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::dispatchComputeIndirect(IBuffer* argBuffer, Offset offset)
{
    if (m_commandList)
    {
        commands::DispatchComputeIndirect cmd;
        cmd.argBuffer = argBuffer;
        cmd.offset = offset;
        m_commandList->write(std::move(cmd));
    }
}

IRayTracingPassEncoder* RayTracingPassEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IRayTracingPassEncoder::getTypeGuid())
        return static_cast<IRayTracingPassEncoder*>(this);
    return nullptr;
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

void RayTracingPassEncoder::setRayTracingState(const RayTracingState& state)
{
    if (m_commandList)
    {
        commands::SetRayTracingState cmd;
        cmd.state = state;
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::dispatchRays(GfxIndex rayGenShaderIndex, GfxCount width, GfxCount height, GfxCount depth)
{
    if (m_commandList)
    {
        commands::DispatchRays cmd;
        cmd.rayGenShaderIndex = rayGenShaderIndex;
        cmd.width = width;
        cmd.height = height;
        cmd.depth = depth;
        m_commandList->write(std::move(cmd));
    }
}

ICommandEncoder* CommandEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ICommandEncoder::getTypeGuid())
        return static_cast<ICommandEncoder*>(this);
    return nullptr;
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
    GfxCount subresourceDataCount
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

void CommandEncoder::resolveQuery(IQueryPool* queryPool, GfxIndex index, GfxCount count, IBuffer* buffer, Offset offset)
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
    GfxCount propertyQueryCount,
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
    GfxCount accelerationStructureCount,
    IAccelerationStructure* const* accelerationStructures,
    GfxCount queryCount,
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

void CommandEncoder::writeTimestamp(IQueryPool* queryPool, GfxIndex queryIndex)
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

Result CommandEncoder::resolvePipelines(Device* device)
{
    CommandList* commandList = m_commandList;
    auto command = commandList->getCommands();
    while (command)
    {
        switch (command->id)
        {
        case CommandID::SetRenderState:
        {
            auto& cmd = commandList->getCommand<commands::SetRenderState>(command);
            RenderPipeline* pipeline = checked_cast<RenderPipeline*>(cmd.state.pipeline);
            ShaderObjectBase* rootObject = checked_cast<ShaderObjectBase*>(cmd.state.rootObject);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, rootObject, concretePipeline));
            cmd.state.pipeline = static_cast<RenderPipeline*>(concretePipeline);
            break;
        }
        case CommandID::SetComputeState:
        {
            auto& cmd = commandList->getCommand<commands::SetComputeState>(command);
            ComputePipeline* pipeline = checked_cast<ComputePipeline*>(cmd.state.pipeline);
            ShaderObjectBase* rootObject = checked_cast<ShaderObjectBase*>(cmd.state.rootObject);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, rootObject, concretePipeline));
            cmd.state.pipeline = static_cast<ComputePipeline*>(concretePipeline);
            break;
        }
        case CommandID::SetRayTracingState:
        {
            auto& cmd = commandList->getCommand<commands::SetRayTracingState>(command);
            RayTracingPipeline* pipeline = checked_cast<RayTracingPipeline*>(cmd.state.pipeline);
            ShaderObjectBase* rootObject = checked_cast<ShaderObjectBase*>(cmd.state.rootObject);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, rootObject, concretePipeline));
            cmd.state.pipeline = static_cast<RayTracingPipeline*>(concretePipeline);
            break;
        }
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
    for (Index i = 0; i < m_desc.hitGroupCount; i++)
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
    if (!persistentShaderCache)
    {
        return program->getEntryPointCode(entryPointIndex, targetIndex, outCode, outDiagnostics);
    }

    // Hash all relevant state for generating the entry point shader code to use as a key
    // for the shader cache.
    ComPtr<ISlangBlob> hashBlob;
    program->getEntryPointHash(entryPointIndex, targetIndex, hashBlob.writeRef());

    // Query the shader cache.
    ComPtr<ISlangBlob> codeBlob;
    if (persistentShaderCache->queryCache(hashBlob, codeBlob.writeRef()) != SLANG_OK)
    {
        // No cached entry found. Generate the code and add it to the cache.
        SLANG_RETURN_ON_FAIL(
            program->getEntryPointCode(entryPointIndex, targetIndex, codeBlob.writeRef(), outDiagnostics)
        );
        persistentShaderCache->writeCache(hashBlob, codeBlob);
    }

    *outCode = codeBlob.detach();
    return SLANG_OK;
}

Result Device::queryInterface(SlangUUID const& uuid, void** outObject)
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

    persistentShaderCache = desc.persistentShaderCache;

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

Result Device::getFeatures(const char** outFeatures, Size bufferSize, GfxCount* outFeatureCount)
{
    if (bufferSize >= (UInt)m_features.size())
    {
        for (Index i = 0; i < m_features.size(); i++)
        {
            outFeatures[i] = m_features[i].data();
        }
    }
    if (outFeatureCount)
        *outFeatureCount = (GfxCount)m_features.size();
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
    *outSlangSession = slangContext.session.get();
    slangContext.session->addRef();
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

Result Device::createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout)
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
        slangSession = slangContext.session.get();
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(slangSession, type, container, shaderObjectLayout.writeRef()));
    return createShaderObject(shaderObjectLayout, outObject);
}

Result Device::createShaderObjectFromTypeLayout(slang::TypeLayoutReflection* typeLayout, IShaderObject** outObject)
{
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(slangContext.session, typeLayout, shaderObjectLayout.writeRef()));
    return createShaderObject(shaderObjectLayout, outObject);
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

Result Device::createShaderTable(const IShaderTable::Desc& desc, IShaderTable** outTable)
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
    GfxCount fenceCount,
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

void ShaderObjectLayout::initBase(
    Device* device,
    slang::ISession* session,
    slang::TypeLayoutReflection* elementTypeLayout
)
{
    m_device = device;
    m_slangSession = session;
    m_elementTypeLayout = elementTypeLayout;
    m_componentID = m_device->shaderCache.getComponentId(m_elementTypeLayout->getType());
}

// Get the final type this shader object represents. If the shader object's type has existential fields,
// this function will return a specialized type using the bound sub-objects' type as specialization argument.
Result ShaderObjectBase::getSpecializedShaderObjectType(ExtendedShaderObjectType* outType)
{
    return _getSpecializedShaderObjectType(outType);
}

Result ShaderObjectBase::_getSpecializedShaderObjectType(ExtendedShaderObjectType* outType)
{
    if (shaderObjectType.slangType)
        *outType = shaderObjectType;
    ExtendedShaderObjectTypeList specializationArgs;
    SLANG_RETURN_ON_FAIL(collectSpecializationArgs(specializationArgs));
    if (specializationArgs.getCount() == 0)
    {
        shaderObjectType.componentID = getLayoutBase()->getComponentID();
        shaderObjectType.slangType = getLayoutBase()->getElementTypeLayout()->getType();
    }
    else
    {
        shaderObjectType.slangType = getDevice()->slangContext.session->specializeType(
            _getElementTypeLayout()->getType(),
            specializationArgs.components.data(),
            specializationArgs.getCount()
        );
        shaderObjectType.componentID = getDevice()->shaderCache.getComponentId(shaderObjectType.slangType);
    }
    *outType = shaderObjectType;
    return SLANG_OK;
}

Result ShaderObjectBase::setExistentialHeader(
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
    SLANG_RETURN_ON_FAIL(getLayoutBase()->m_slangSession->getTypeConformanceWitnessSequentialID(
        concreteType,
        existentialType,
        &conformanceID
    ));
    //
    // Once we have the conformance ID, then we can write it into the object
    // at the required offset.
    //
    SLANG_RETURN_ON_FAIL(setData(witnessTableOffset, &conformanceID, sizeof(conformanceID)));

    return SLANG_OK;
}

Buffer* SimpleShaderObjectData::getBufferResource(
    Device* device,
    slang::TypeLayoutReflection* elementLayout,
    slang::BindingType bindingType
)
{
    if (!m_structuredBuffer)
    {
        // Create structured buffer resource if it has not been created.
        BufferDesc desc = {};
        desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess;
        desc.defaultState = ResourceState::ShaderResource;
        desc.elementSize = (int)elementLayout->getSize();
        desc.format = Format::Unknown;
        desc.size = (Size)m_ordinaryData.size();
        ComPtr<IBuffer> buffer;
        SLANG_RETURN_NULL_ON_FAIL(device->createBuffer(desc, m_ordinaryData.data(), buffer.writeRef()));
        m_structuredBuffer = checked_cast<Buffer*>(buffer.get());
    }

    return m_structuredBuffer;
}

void ShaderProgram::init(const ShaderProgramDesc& inDesc)
{
    desc = inDesc;

    slangGlobalScope = desc.slangGlobalScope;
    for (GfxIndex i = 0; i < desc.slangEntryPointCount; i++)
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
        for (GfxIndex i = 0; i < desc.slangEntryPointCount; i++)
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
        for (GfxIndex i = 0; i < desc.slangEntryPointCount; i++)
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
}

Result ShaderProgram::compileShaders(Device* device)
{
    // For a fully specialized program, read and store its kernel code in `shaderProgram`.
    auto compileShader = [&](slang::EntryPointReflection* entryPointInfo,
                             slang::IComponentType* entryPointComponent,
                             SlangInt entryPointIndex)
    {
        auto stage = entryPointInfo->getStage();
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
    ShaderProgramDesc programDesc = program->desc;
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

Result Device::getConcretePipeline(Pipeline* pipeline, ShaderObjectBase* rootObject, Pipeline*& outPipeline)
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
    ExtendedShaderObjectTypeList specializationArgs;
    if (pipeline->m_program->isSpecializable())
    {
        SLANG_RETURN_ON_FAIL(rootObject->collectSpecializationArgs(specializationArgs));
        for (const auto& componentID : specializationArgs.componentIDs)
        {
            pipelineKey.specializationArgs.push_back(componentID);
        }
    }

    // Look up pipeline in cache.
    pipelineKey.updateHash();
    RefPtr<Pipeline> concretePipeline = shaderCache.getSpecializedPipeline(pipelineKey);
    if (!concretePipeline)
    {
        // Specialize program if needed.
        RefPtr<ShaderProgram> program = pipeline->m_program;
        if (program->isSpecializable())
        {
            RefPtr<ShaderProgram> specializedProgram;
            SLANG_RETURN_ON_FAIL(specializeProgram(program, specializationArgs, specializedProgram.writeRef()));
            program = specializedProgram;
        }

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
        shaderCache.addSpecializedPipeline(pipelineKey, concretePipeline);
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

Result ShaderTable::init(const IShaderTable::Desc& desc)
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
    for (GfxIndex i = 0; i < desc.rayGenShaderCount; i++)
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
    for (GfxIndex i = 0; i < desc.missShaderCount; i++)
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
    for (GfxIndex i = 0; i < desc.hitGroupCount; i++)
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
    for (GfxIndex i = 0; i < desc.callableShaderCount; i++)
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

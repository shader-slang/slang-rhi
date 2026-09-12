#include "rhi-shared.h"
#include "device-child.h"
#include "command-list.h"

#include "core/common.h"

#include <slang.h>

#include <atomic>
#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace rhi {

// ----------------------------------------------------------------------------
// Extent3D
// ----------------------------------------------------------------------------

Extent3D Extent3D::kWholeTexture = {kRemainingTextureSize, kRemainingTextureSize, kRemainingTextureSize};

// ----------------------------------------------------------------------------
// Fence
// ----------------------------------------------------------------------------

IFence* Fence::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IFence::getTypeGuid())
        return static_cast<IFence*>(this);
    return nullptr;
}

Fence::Fence(Device* device, const FenceDesc& desc)
    : DeviceChild(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}


// ----------------------------------------------------------------------------
// Buffer
// ----------------------------------------------------------------------------

IResource* Buffer::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == IBuffer::getTypeGuid())
        return static_cast<IBuffer*>(this);
    return nullptr;
}

Buffer::Buffer(Device* device, const BufferDesc& desc)
    : Resource(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}

BufferRange Buffer::resolveBufferRange(const BufferRange& range)
{
    BufferRange resolved = range;
    resolved.offset = min(resolved.offset, m_desc.size);
    resolved.size = min(resolved.size, m_desc.size - resolved.offset);
    return resolved;
}

Result Buffer::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Buffer::getDescriptorHandle(
    DescriptorHandleAccess access,
    Format format,
    BufferRange range,
    DescriptorHandle* outHandle
)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Buffer::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// Texture helpers
// ----------------------------------------------------------------------------

Result calcSubresourceRegionLayout(
    const TextureDesc& desc,
    uint32_t mip,
    Offset3D offset,
    Extent3D extent,
    Size rowAlignment,
    SubresourceLayout* outLayout
)
{
    Extent3D textureSize = desc.size;
    const FormatInfo& formatInfo = getFormatInfo(desc.format);

    if (extent.width == kRemainingTextureSize)
    {
        extent.width = max(1u, (textureSize.width >> mip));
        if (offset.x >= extent.width)
            return SLANG_E_INVALID_ARG;
        extent.width -= offset.x;
    }
    if (extent.height == kRemainingTextureSize)
    {
        extent.height = max(1u, (textureSize.height >> mip));
        if (offset.y >= extent.height)
            return SLANG_E_INVALID_ARG;
        extent.height -= offset.y;
    }
    if (extent.depth == kRemainingTextureSize)
    {
        extent.depth = max(1u, (textureSize.depth >> mip));
        if (offset.z >= extent.depth)
            return SLANG_E_INVALID_ARG;
        extent.depth -= offset.z;
    }

    size_t rowSize = math::divideRoundedUp(extent.width, formatInfo.blockWidth) * formatInfo.blockSizeInBytes;
    size_t rowCount = math::divideRoundedUp(extent.height, formatInfo.blockHeight);
    size_t rowPitch = math::calcAligned(rowSize, rowAlignment);
    size_t layerPitch = rowPitch * rowCount;

    outLayout->size = extent;
    outLayout->colPitch = formatInfo.blockSizeInBytes;
    outLayout->rowPitch = rowPitch;
    outLayout->slicePitch = layerPitch;
    outLayout->sizeInBytes = layerPitch * extent.depth;
    outLayout->rowCount = rowCount;
    outLayout->blockWidth = formatInfo.blockWidth;
    outLayout->blockHeight = formatInfo.blockHeight;

    return SLANG_OK;
}

// ----------------------------------------------------------------------------
// Texture
// ----------------------------------------------------------------------------

IResource* Texture::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ITexture::getTypeGuid())
        return static_cast<ITexture*>(this);
    return nullptr;
}

Texture::Texture(Device* device, const TextureDesc& desc)
    : Resource(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
    m_sampler = checked_cast<Sampler*>(m_desc.sampler);
}

SubresourceRange Texture::resolveSubresourceRange(const SubresourceRange& range)
{
    SubresourceRange resolved = range;
    resolved.layer = min(resolved.layer, (m_desc.getLayerCount() - 1));
    resolved.layerCount = min(resolved.layerCount, m_desc.getLayerCount() - resolved.layer);
    resolved.mip = min(resolved.mip, m_desc.mipCount - 1);
    resolved.mipCount = min(resolved.mipCount, m_desc.mipCount - resolved.mip);
    return resolved;
}

bool Texture::isEntireTexture(const SubresourceRange& range)
{
    if (range.layer > 0 || range.layerCount < m_desc.getLayerCount())
    {
        return false;
    }
    if (range.mip > 0 || range.mipCount < m_desc.mipCount)
    {
        return false;
    }
    return true;
}

Result Texture::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Texture::getSubresourceRegionLayout(
    uint32_t mip,
    Offset3D offset,
    Extent3D extent,
    size_t rowAlignment,
    SubresourceLayout* outLayout
)
{
    if (rowAlignment == kDefaultAlignment)
    {
        SLANG_RETURN_ON_FAIL(m_device->getTextureRowAlignment(m_desc.format, &rowAlignment));
    }
    return calcSubresourceRegionLayout(m_desc, mip, offset, extent, rowAlignment, outLayout);
}

Result Texture::createView(const TextureViewDesc& desc, ITextureView** outTextureView)
{
    return m_device->createTextureView(this, desc, outTextureView);
}

Result Texture::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// TextureView
// ----------------------------------------------------------------------------

ITextureView* TextureView::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ITextureView::getTypeGuid())
        return static_cast<ITextureView*>(this);
    return nullptr;
}

TextureView::TextureView(Device* device, const TextureViewDesc& desc)
    : Resource(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
    m_sampler = checked_cast<Sampler*>(m_desc.sampler);
}

Result TextureView::getDescriptorHandle(DescriptorHandleAccess access, DescriptorHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result TextureView::getCombinedTextureSamplerDescriptorHandle(DescriptorHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result TextureView::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// Sampler
// ----------------------------------------------------------------------------

ISampler* Sampler::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == ISampler::getTypeGuid())
        return static_cast<ISampler*>(this);
    return nullptr;
}

Sampler::Sampler(Device* device, const SamplerDesc& desc)
    : Resource(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}

Result Sampler::getDescriptorHandle(DescriptorHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result Sampler::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// AccelerationStructure
// ----------------------------------------------------------------------------

IAccelerationStructure* AccelerationStructure::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() ||
        guid == IAccelerationStructure::getTypeGuid())
        return static_cast<IAccelerationStructure*>(this);
    return nullptr;
}

AccelerationStructure::AccelerationStructure(Device* device, const AccelerationStructureDesc& desc)
    : Resource(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}

AccelerationStructureHandle AccelerationStructure::getHandle()
{
    return {};
}

Result AccelerationStructure::getDescriptorHandle(DescriptorHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// ----------------------------------------------------------------------------
// Micromap
// ----------------------------------------------------------------------------

IMicromap* Micromap::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IResource::getTypeGuid() || guid == IMicromap::getTypeGuid())
        return static_cast<IMicromap*>(this);
    return nullptr;
}

Micromap::Micromap(Device* device, const MicromapDesc& desc)
    : Resource(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
}

// ----------------------------------------------------------------------------
// InputLayout
// ----------------------------------------------------------------------------

IInputLayout* InputLayout::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IInputLayout::getTypeGuid())
        return static_cast<IInputLayout*>(this);
    return nullptr;
}

// ----------------------------------------------------------------------------
// QueryPool
// ----------------------------------------------------------------------------

IQueryPool* QueryPool::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IQueryPool::getTypeGuid())
        return static_cast<IQueryPool*>(this);
    return nullptr;
}

QueryPool::QueryPool(Device* device, const QueryPoolDesc& desc)
    : DeviceChild(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
    m_querySlotStates.resize(m_desc.count);
}

Result QueryPool::getResultState(uint32_t queryIndex, uint32_t count, QueryResultState* outState)
{
    if (!outState || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }

    *outState = getQueryRangeInfo(queryIndex, count).state;

    return SLANG_OK;
}

Result QueryPool::reset()
{
    return reset(0, m_desc.count);
}

Result QueryPool::reset(uint32_t queryIndex, uint32_t count)
{
    if (!isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(m_queryStateMutex);
    for (uint32_t i = 0; i < count; ++i)
    {
        QuerySlotState& slotState = m_querySlotStates[queryIndex + i];
        slotState.set(QueryResultState::Reset, 0);
    }

    return SLANG_OK;
}

bool QueryPool::isValidQueryRange(uint32_t queryIndex, uint32_t count) const
{
    if (count == 0)
    {
        return queryIndex <= m_desc.count;
    }
    return queryIndex < m_desc.count && count <= m_desc.count - queryIndex;
}

void QueryPool::markQueryRangeSubmitted(uint32_t queryIndex, uint32_t count, uint64_t submissionID)
{
    if (!isValidQueryRange(queryIndex, count))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_queryStateMutex);
    for (uint32_t i = 0; i < count; ++i)
    {
        QuerySlotState& slotState = m_querySlotStates[queryIndex + i];
        slotState.set(QueryResultState::Pending, submissionID);
    }
}

void QueryPool::markQueryRangeResolved(uint32_t queryIndex, uint32_t count, uint64_t completedSubmissionID)
{
    if (!isValidQueryRange(queryIndex, count))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_queryStateMutex);
    for (uint32_t i = 0; i < count; ++i)
    {
        QuerySlotState& slotState = m_querySlotStates[queryIndex + i];
        QueryResultState state = slotState.getState();
        uint64_t submissionID = slotState.getSubmissionID();
        if (state == QueryResultState::Pending && submissionID <= completedSubmissionID)
        {
            slotState.set(QueryResultState::Resolved, submissionID);
        }
    }
}

QueryPool::QueryRangeInfo QueryPool::getQueryRangeInfo(uint32_t queryIndex, uint32_t count) const
{
    if (!isValidQueryRange(queryIndex, count))
    {
        return {QueryResultState::Reset, 0};
    }

    if (count == 0)
    {
        return {QueryResultState::Resolved, 0};
    }

    QueryRangeInfo info;
    std::lock_guard<std::mutex> lock(m_queryStateMutex);
    for (uint32_t i = 0; i < count; ++i)
    {
        const QuerySlotState& slotState = m_querySlotStates[queryIndex + i];
        QueryResultState state = slotState.getState();
        if (state == QueryResultState::Reset)
        {
            return {QueryResultState::Reset, 0};
        }
        if (state == QueryResultState::Pending)
        {
            info.state = QueryResultState::Pending;
        }
        info.submissionID = std::max(info.submissionID, slotState.getSubmissionID());
    }
    if (info.state != QueryResultState::Pending)
    {
        info.state = QueryResultState::Resolved;
    }
    return info;
}

// ----------------------------------------------------------------------------
// ShaderTable
// ----------------------------------------------------------------------------

IShaderTable* ShaderTable::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IShaderTable::getTypeGuid())
        return static_cast<IShaderTable*>(this);
    return nullptr;
}

Size ShaderTable::OwnedRecord::getSize(Size headerSize) const
{
    // Backend creation validates this invariant before the descriptor is copied into OwnedRecord.
    SLANG_RHI_ASSERT(data.size() <= std::numeric_limits<Size>::max() - headerSize);
    return std::max(headerSize + data.size(), Size(overwrite.offset) + Size(overwrite.size));
}

void ShaderTable::OwnedRecord::writeData(void* destination, Size headerSize) const
{
    if (!data.empty())
    {
        memcpy(static_cast<uint8_t*>(destination) + headerSize, data.data(), data.size());
    }
    if (overwrite.size > 0)
    {
        memcpy(static_cast<uint8_t*>(destination) + overwrite.offset, overwrite.data, overwrite.size);
    }
}

Size ShaderTable::getMaxRecordSize(const std::vector<OwnedRecord>& records, Size headerSize)
{
    Size maxSize = headerSize;
    for (const auto& record : records)
        maxSize = std::max(maxSize, record.getSize(headerSize));
    return maxSize;
}

bool ShaderTable::tryAddSize(Size left, Size right, Size* outSize)
{
    SLANG_RHI_ASSERT(outSize);
    if (right > std::numeric_limits<Size>::max() - left)
        return false;
    *outSize = left + right;
    return true;
}

bool ShaderTable::tryMultiplySize(Size left, Size right, Size* outSize)
{
    SLANG_RHI_ASSERT(outSize);
    if (left != 0 && right > std::numeric_limits<Size>::max() / left)
        return false;
    *outSize = left * right;
    return true;
}

bool ShaderTable::tryAlignSize(Size size, Size alignment, Size* outSize)
{
    SLANG_RHI_ASSERT(outSize);
    SLANG_RHI_ASSERT(math::isPowerOf2(alignment));

    Size sizeWithPadding = 0;
    if (!tryAddSize(size, alignment - 1, &sizeWithPadding))
        return false;
    *outSize = sizeWithPadding & ~(alignment - 1);
    return true;
}

ShaderTable::RecordLayoutError ShaderTable::calculateRecordStride(
    Size dataSize,
    Size overwriteEnd,
    const RecordLayout& layout,
    Size* outStride
)
{
    SLANG_RHI_ASSERT(outStride);
    SLANG_RHI_ASSERT(math::isPowerOf2(layout.alignment));

    Size sizeWithHeader = 0;
    if (!tryAddSize(layout.headerSize, dataSize, &sizeWithHeader))
        return RecordLayoutError::SizeOverflow;

    const Size unalignedSize = std::max(sizeWithHeader, overwriteEnd);
    Size stride = 0;
    if (!tryAlignSize(unalignedSize, layout.alignment, &stride))
        return RecordLayoutError::AlignmentOverflow;

    *outStride = stride;
    if (stride > layout.maximumStride)
        return RecordLayoutError::StrideTooLarge;

    return RecordLayoutError::None;
}

Result ShaderTable::validateRecordData(Device* device, const ShaderTableDesc& desc, const RecordLayout& layout)
{
    SLANG_RHI_ASSERT(device);
    SLANG_RHI_ASSERT(math::isPowerOf2(layout.alignment));

    auto validateRecords = [&](const char* fieldName,
                               const ShaderRecordData* recordData,
                               const ShaderRecordOverwrite* overwrites,
                               uint32_t count,
                               Size* outSectionSize)
    {
        SLANG_RHI_ASSERT(outSectionSize);
        Size maximumStride = 0;

        for (uint32_t i = 0; i < count; ++i)
        {
            const ShaderRecordData* record = recordData ? &recordData[i] : nullptr;
            if (record && record->size > 0 && !record->data)
            {
                device
                    ->printError("ShaderTableDesc.%s[%u].data must not be null when size is nonzero.\n", fieldName, i);
                return SLANG_E_INVALID_ARG;
            }

            Size overwriteEnd = 0;
            if (overwrites)
                overwriteEnd = Size(overwrites[i].offset) + Size(overwrites[i].size);

            Size stride = 0;
            switch (calculateRecordStride(record ? record->size : 0, overwriteEnd, layout, &stride))
            {
            case RecordLayoutError::None:
                break;
            case RecordLayoutError::SizeOverflow:
                device->printError(
                    "ShaderTableDesc.%s[%u] is too large to add the %zu-byte native shader-record header.\n",
                    fieldName,
                    i,
                    layout.headerSize
                );
                return SLANG_E_INVALID_ARG;
            case RecordLayoutError::AlignmentOverflow:
                device->printError(
                    "ShaderTableDesc.%s[%u] is too large to align its native shader-record stride to %zu bytes.\n",
                    fieldName,
                    i,
                    layout.alignment
                );
                return SLANG_E_INVALID_ARG;
            case RecordLayoutError::StrideTooLarge:
                device->printError(
                    "ShaderTableDesc.%s[%u] requires a %zu-byte native shader-record stride, exceeding the backend "
                    "limit of %zu bytes.\n",
                    fieldName,
                    i,
                    stride,
                    layout.maximumStride
                );
                return SLANG_E_INVALID_ARG;
            }
            maximumStride = std::max(maximumStride, stride);
        }

        if (!tryMultiplySize(count, maximumStride, outSectionSize))
        {
            device->printError("ShaderTableDesc.%s section size overflows the host address space.\n", fieldName);
            return SLANG_E_INVALID_ARG;
        }
        return SLANG_OK;
    };

    Size missSectionSize = 0;
    Size hitGroupSectionSize = 0;
    Size callableSectionSize = 0;
    SLANG_RETURN_ON_FAIL(validateRecords(
        "missShaderRecordData",
        desc.missShaderRecordData,
        desc.missShaderRecordOverwrites,
        desc.missShaderCount,
        &missSectionSize
    ));
    SLANG_RETURN_ON_FAIL(validateRecords(
        "hitGroupRecordData",
        desc.hitGroupRecordData,
        desc.hitGroupRecordOverwrites,
        desc.hitGroupCount,
        &hitGroupSectionSize
    ));
    SLANG_RETURN_ON_FAIL(validateRecords(
        "callableShaderRecordData",
        desc.callableShaderRecordData,
        desc.callableShaderRecordOverwrites,
        desc.callableShaderCount,
        &callableSectionSize
    ));

    Size totalRecordSize = 0;
    if (!tryAddSize(missSectionSize, hitGroupSectionSize, &totalRecordSize) ||
        !tryAddSize(totalRecordSize, callableSectionSize, &totalRecordSize))
    {
        device->printError("ShaderTableDesc shader-record section sizes overflow the host address space.\n");
        return SLANG_E_INVALID_ARG;
    }
    return SLANG_OK;
}

ShaderTable::ShaderTable(Device* device, const ShaderTableDesc& desc)
    : DeviceChild(device)
{
    auto getMaxOverrideSize = [](const std::vector<ShaderRecordOverwrite>& overwrites)
    {
        uint32_t maxSize = 0;
        for (const auto& overwrite : overwrites)
        {
            uint32_t size = static_cast<uint32_t>(overwrite.offset) + static_cast<uint32_t>(overwrite.size);
            maxSize = std::max(maxSize, size);
        }
        return maxSize;
    };

    auto initializeRecords = [](std::vector<OwnedRecord>& records,
                                uint32_t count,
                                const ShaderRecordOverwrite* overwrites,
                                const ShaderRecordData* recordData)
    {
        records.resize(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            if (overwrites)
                records[i].overwrite = overwrites[i];
            if (recordData && recordData[i].size > 0)
            {
                SLANG_RHI_ASSERT(recordData[i].data);
                const uint8_t* data = static_cast<const uint8_t*>(recordData[i].data);
                records[i].data.assign(data, data + recordData[i].size);
            }
        }
    };

    m_rayGenShaderCount = desc.rayGenShaderCount;
    m_missShaderCount = desc.missShaderCount;
    m_hitGroupCount = desc.hitGroupCount;
    m_callableShaderCount = desc.callableShaderCount;

    m_rayGenShaderEntryPointNames.assign(
        desc.rayGenShaderEntryPointNames,
        desc.rayGenShaderEntryPointNames + desc.rayGenShaderCount
    );
    if (desc.rayGenShaderRecordOverwrites)
    {
        m_rayGenRecordOverwrites.assign(
            desc.rayGenShaderRecordOverwrites,
            desc.rayGenShaderRecordOverwrites + desc.rayGenShaderCount
        );
    }
    m_rayGenRecordOverwriteMaxSize = getMaxOverrideSize(m_rayGenRecordOverwrites);

    m_missShaderEntryPointNames.assign(
        desc.missShaderEntryPointNames,
        desc.missShaderEntryPointNames + desc.missShaderCount
    );
    initializeRecords(m_missRecords, desc.missShaderCount, desc.missShaderRecordOverwrites, desc.missShaderRecordData);

    m_hitGroupNames.assign(desc.hitGroupNames, desc.hitGroupNames + desc.hitGroupCount);
    initializeRecords(m_hitGroupRecords, desc.hitGroupCount, desc.hitGroupRecordOverwrites, desc.hitGroupRecordData);

    m_callableShaderEntryPointNames.assign(
        desc.callableShaderEntryPointNames,
        desc.callableShaderEntryPointNames + desc.callableShaderCount
    );
    initializeRecords(
        m_callableRecords,
        desc.callableShaderCount,
        desc.callableShaderRecordOverwrites,
        desc.callableShaderRecordData
    );
}

// ----------------------------------------------------------------------------
// Surface
// ----------------------------------------------------------------------------

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

Result Surface::validateConfig(const SurfaceConfig& config) const
{
    if (config.format != Format::Undefined && !contains(m_info.formats, m_info.formatCount, config.format))
        return SLANG_E_INVALID_ARG;
    if (config.usage != (config.usage & m_info.supportedUsage))
        return SLANG_E_INVALID_ARG;
    if (config.width == 0 || config.height == 0)
        return SLANG_E_INVALID_ARG;
    if (config.desiredImageCount == 0)
        return SLANG_E_INVALID_ARG;
    return SLANG_OK;
}

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

bool isDepthFormat(Format format)
{
    switch (format)
    {
    case Format::D16Unorm:
    case Format::D32Float:
    case Format::D32FloatS8Uint:
        return true;
    default:
        return false;
    }
}

bool isStencilFormat(Format format)
{
    switch (format)
    {
    case Format::D32FloatS8Uint:
        return true;
    default:
        return false;
    }
}

} // namespace rhi

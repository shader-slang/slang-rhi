// debug-swap-chain.cpp
#include "debug-swap-chain.h"

#include "debug-command-queue.h"
#include "debug-texture.h"

#include "debug-helper-functions.h"

namespace rhi
{
using namespace Slang;

namespace debug
{

const ISwapchain::Desc& DebugSwapchain::getDesc()
{
    SLANG_RHI_API_FUNC;
    desc = baseObject->getDesc();
    desc.queue = queue.Ptr();
    return desc;
}

Result DebugSwapchain::getImage(GfxIndex index, ITextureResource** outResource)
{
    SLANG_RHI_API_FUNC;
    maybeRebuildImageList();
    if (index > (GfxCount)m_images.size())
    {
        RHI_VALIDATION_ERROR_FORMAT(
            "`index`(%d) must not exceed total number of images (%d) in the swapchain.",
            index,
            (uint32_t)m_images.size());
    }
    returnComPtr(outResource, m_images[index]);
    return SLANG_OK;
}

Result DebugSwapchain::present()
{
    SLANG_RHI_API_FUNC;
    return baseObject->present();
}

int DebugSwapchain::acquireNextImage()
{
    SLANG_RHI_API_FUNC;
    return baseObject->acquireNextImage();
}

Result DebugSwapchain::resize(GfxCount width, GfxCount height)
{
    SLANG_RHI_API_FUNC;
    for (auto& image : m_images)
    {
        if (image->debugGetReferenceCount() != 1)
        {
            // Only warn here because tools like NSight might keep
            // an additional reference to swapchain images.
            RHI_VALIDATION_WARNING("all swapchain images must be released before calling resize().");
            break;
        }
    }
    m_images.clear();
    return baseObject->resize(width, height);
}

bool DebugSwapchain::isOccluded()
{
    SLANG_RHI_API_FUNC;
    return baseObject->isOccluded();
}

Result DebugSwapchain::setFullScreenMode(bool mode)
{
    SLANG_RHI_API_FUNC;
    return baseObject->setFullScreenMode(mode);
}

void DebugSwapchain::maybeRebuildImageList()
{
    SLANG_RHI_API_FUNC;
    if (m_images.empty())
        return;
    m_images.clear();
    for (GfxIndex i = 0; i < baseObject->getDesc().imageCount; i++)
    {
        RefPtr<DebugTextureResource> image = new DebugTextureResource();
        baseObject->getImage(i, image->baseObject.writeRef());
        m_images.push_back(image);
    }
}

} // namespace debug
} // namespace rhi

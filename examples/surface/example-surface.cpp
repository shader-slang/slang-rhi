// Surface example: acquire a window surface image, clear it to an animated
// grayscale value, and present it. Demonstrates the minimal frame lifecycle
// with a render pass and command submission.
// The overlay lists controls; F1 toggles it.

#include "../base/example.h"

namespace {

using namespace rhi;

class ExampleSurface : public ExampleBase
{
public:
    Result init(DeviceType deviceType) override
    {
        SLANG_RETURN_ON_FAIL(
            createDevice(deviceType, {Feature::Surface, Feature::Rasterization}, {}, m_device.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createWindow(m_device));
        SLANG_RETURN_ON_FAIL(createSurface(m_device, Format::Undefined, m_surface.writeRef()));
        SLANG_RETURN_ON_FAIL(initOverlay(m_device, m_surface->getConfig()->format));

        SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));

        return SLANG_OK;
    }

    virtual void shutdown() override
    {
        if (m_queue)
            m_queue->waitOnHost();
        m_queue.setNull();
        m_surface.setNull();
        m_device.setNull();
    }

    virtual Result update(double time) override
    {
        m_grey = (m_grey + (1.f / 60.f)) > 1.f ? 0.f : m_grey + (1.f / 60.f);
        return SLANG_OK;
    }

    virtual Result draw() override
    {
        // Skip rendering if surface is not configured (eg. when window is minimized)
        if (!m_surface->getConfig())
        {
            return SLANG_OK;
        }

        // Acquire next image from the surface
        ComPtr<ITexture> image;
        m_surface->acquireNextImage(image.writeRef());
        if (!image)
        {
            return SLANG_OK;
        }

        // Start command encoding
        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(m_queue->createCommandEncoder(commandEncoder.writeRef()));

        // Setup render pass
        RenderPassColorAttachment colorAttachment;
        colorAttachment.clearValue[0] = m_grey;
        colorAttachment.clearValue[1] = m_grey;
        colorAttachment.clearValue[2] = m_grey;
        colorAttachment.clearValue[3] = 1.f;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.view = image->getDefaultView();
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        IRenderPassEncoder* passEncoder = commandEncoder->beginRenderPass(renderPass);
        passEncoder->end();

        clearOverlay();
        printOverlayHeader();
        SLANG_RETURN_ON_FAIL(drawOverlay(commandEncoder, image));

        // Submit command buffer
        ComPtr<ICommandBuffer> commandBuffer;
        SLANG_RETURN_ON_FAIL(commandEncoder->finish(commandBuffer.writeRef()));
        SLANG_RETURN_ON_FAIL(m_queue->submit(commandBuffer));

        // Present the surface
        return presentSurface();
    }

    virtual Result onResize(int width, int height, int framebufferWidth, int framebufferHeight) override
    {
        // Wait for GPU to be idle before resizing
        SLANG_RETURN_ON_FAIL(m_queue->waitOnHost());
        // Configure or unconfigure the surface based on the new framebuffer size
        if (framebufferWidth > 0 && framebufferHeight > 0)
        {
            SurfaceConfig surfaceConfig;
            surfaceConfig.width = framebufferWidth;
            surfaceConfig.height = framebufferHeight;
            return configureSurface(m_surface, surfaceConfig);
        }
        else
        {
            return m_surface->unconfigure();
        }
    }

public:
    ComPtr<IDevice> m_device;
    ComPtr<ISurface> m_surface;
    ComPtr<ICommandQueue> m_queue;

    float m_grey = 0.5f;
};

} // namespace

EXAMPLE_MAIN(ExampleSurface, "surface", "Surface")

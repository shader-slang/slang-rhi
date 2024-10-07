#include <slang.h>

#if SLANG_WINDOWS_FAMILY
#define GLFW_EXPOSE_NATIVE_WIN32
#elif SLANG_LINUX_FAMILY
#define GLFW_EXPOSE_NATIVE_X11
#elif SLANG_APPLE_FAMILY
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <slang-rhi.h>
#include <slang-rhi/glfw.h>

#include <cstdio>

#include "example-base.h"

using namespace rhi;

class ExampleSurface : public ExampleBase
{
public:
    ComPtr<ITransientResourceHeap> transientHeap;
    ComPtr<ICommandQueue> queue;
    float grey = 0.5f;

    const char* getName() const override { return "Surface"; }

    Result init(DeviceType deviceType) override
    {
        createDevice(deviceType);
        createWindow();
        createSurface();

        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096;
        transientHeap = device->createTransientResourceHeap(transientHeapDesc);

        queue = device->getQueue(QueueType::Graphics);

        return SLANG_OK;
    }

    virtual void shutdown() override
    {
        queue->waitOnHost();
        surface.setNull();
        device.setNull();
    }

    virtual void update() override {}

    virtual void draw() override
    {
        ComPtr<ITexture> texture;
        surface->getCurrentTexture(texture.writeRef());
        if (!texture)
        {
            return;
        }
        ComPtr<ITextureView> view = device->createTextureView(texture, {});

        auto commandBuffer = transientHeap->createCommandBuffer();
        RenderPassColorAttachment colorAttachment;
        colorAttachment.clearValue[0] = grey;
        colorAttachment.clearValue[1] = grey;
        colorAttachment.clearValue[2] = grey;
        colorAttachment.clearValue[3] = 1.f;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.view = view;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto encoder = commandBuffer->beginRenderPass(renderPass);
        encoder->end();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);

        surface->present();

        grey = (grey + (1.f / 60.f)) > 1.f ? 0.f : grey + (1.f / 60.f);

        transientHeap->finish();
        transientHeap->synchronizeAndReset();
    }
};

EXAMPLE_MAIN(ExampleSurface)

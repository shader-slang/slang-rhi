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
    ComPtr<ICommandQueue> queue;
    float grey = 0.5f;

    const char* getName() const override { return "Surface"; }

    Result init(DeviceType deviceType) override
    {
        createDevice(deviceType);
        createWindow();
        createSurface();

        queue = device->getQueue(QueueType::Graphics);

        return SLANG_OK;
    }

    virtual void shutdown() override { queue->waitOnHost(); }

    virtual void update() override {}

    virtual void draw() override
    {
        if (!surface->getConfig())
            return;

        ComPtr<ITexture> texture;
        surface->acquireNextImage(texture.writeRef());
        if (!texture)
        {
            return;
        }
        ComPtr<ITextureView> view = device->createTextureView(texture, {});

        auto commandEncoder = queue->createCommandEncoder();
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
        auto passEncoder = commandEncoder->beginRenderPass(renderPass);
        passEncoder->end();
        queue->submit(commandEncoder->finish());

        surface->present();

        grey = (grey + (1.f / 60.f)) > 1.f ? 0.f : grey + (1.f / 60.f);
    }
};

EXAMPLE_MAIN(ExampleSurface)

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
    static ExampleDesc getDesc()
    {
        ExampleDesc desc;
        desc.name = "Surface";
        desc.requireFeatures = {Feature::Surface, Feature::Rasterization};
        return desc;
    }

    Result init() override { return SLANG_OK; }

    virtual void shutdown() override {}

    virtual void update() override { m_grey = (m_grey + (1.f / 60.f)) > 1.f ? 0.f : m_grey + (1.f / 60.f); }

    virtual void draw(ITexture* image) override
    {
        ComPtr<ITextureView> imageView = m_device->createTextureView(image, {});

        ComPtr<ICommandEncoder> commandEncoder = m_queue->createCommandEncoder();
        RenderPassColorAttachment colorAttachment;
        colorAttachment.clearValue[0] = m_grey;
        colorAttachment.clearValue[1] = m_grey;
        colorAttachment.clearValue[2] = m_grey;
        colorAttachment.clearValue[3] = 1.f;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.view = imageView;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        IRenderPassEncoder* passEncoder = commandEncoder->beginRenderPass(renderPass);
        passEncoder->end();
        m_queue->submit(commandEncoder->finish());
    }

    float m_grey = 0.5f;
};

EXAMPLE_MAIN(ExampleSurface)

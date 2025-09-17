#include "example-base.h"

using namespace rhi;

class ExampleShaderToy : public ExampleBase
{
public:
    static ExampleDesc getDesc()
    {
        ExampleDesc desc;
        desc.name = "ShaderToy";
        desc.requireFeatures = {Feature::Surface};
        return desc;
    }

    Result init() override
    {
        SLANG_RETURN_ON_FAIL(createComputePipeline(m_device, "circle.slang", "mainCompute", m_pipeline.writeRef()));
        return SLANG_OK;
    }

    virtual void shutdown() override {}

    virtual Result update() override
    {
        if (m_mouseDown[0])
        {
            m_stickyMousePos[0] = m_mousePos[0];
            m_stickyMousePos[1] = m_mousePos[1];
        }
        bool wasMouseDown = m_combinedMouseDown;
        m_combinedMouseDown = m_mouseDown[0] || m_mouseDown[1] || m_mouseDown[2];
        if (m_combinedMouseDown && !wasMouseDown)
        {
            m_combinedMouseClicked = true;
        }
        else
        {
            m_combinedMouseClicked = false;
        }
        return SLANG_OK;
    }

    virtual Result draw(ITexture* image) override
    {
        uint32_t width = image->getDesc().size.width;
        uint32_t height = image->getDesc().size.height;

        if (!m_texture || m_texture->getDesc().size.width != width || m_texture->getDesc().size.height != height)
        {
            TextureDesc textureDesc = {};
            textureDesc.type = TextureType::Texture2D;
            textureDesc.size.width = width;
            textureDesc.size.height = height;
            textureDesc.format = Format::RGBA32Float;
            textureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::ShaderResource | TextureUsage::CopySource;
            m_device->createTexture(textureDesc, nullptr, m_texture.writeRef());
        }

        ComPtr<ICommandEncoder> commandEncoder = m_queue->createCommandEncoder();
        IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
        ShaderCursor cursor(passEncoder->bindPipeline(m_pipeline));
        float resolution[3] = {float(width), float(height), 1.0f};
        cursor["iResolution"].setData(resolution);
        cursor["iTime"].setData(float(m_time));
        cursor["iTimeDelta"].setData(float(m_timeDelta));
        cursor["iFrameRate"].setData(float(m_frameRate));
        cursor["iFrame"].setData(m_frame);
        float mouse[4] = {
            m_stickyMousePos[0],
            m_stickyMousePos[1],
            m_combinedMouseDown ? 1.f : 0.f,
            m_combinedMouseClicked ? 1.f : 0.f
        };
        cursor["iMouse"].setData(mouse);
        cursor["texture"].setBinding(m_texture);
        passEncoder->dispatchCompute((width + 15) / 16, (height + 15) / 16, 1);
        passEncoder->end();
        blit(image, m_texture, commandEncoder);
        m_queue->submit(commandEncoder->finish());
        return SLANG_OK;
    }

    ComPtr<IComputePipeline> m_pipeline;
    ComPtr<ITexture> m_texture;

    float m_stickyMousePos[2] = {0.0f, 0.0f};
    bool m_combinedMouseDown = false;
    bool m_combinedMouseClicked = false;
};

EXAMPLE_MAIN(ExampleShaderToy)

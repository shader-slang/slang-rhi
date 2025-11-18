#include "example-base.h"

#include <future>
#include <memory>

using namespace rhi;

// List of shaders to cycle through using the left/right arrow keys.
static const std::vector<const char*> kShaders = {
    "circle.slang",
    "ocean.slang",
};

// Example for running "ShaderToy"-style shaders using a compute shader to render to a texture.
class ExampleShaderToy : public ExampleBase
{
public:
    Result init(DeviceType deviceType) override
    {
        SLANG_RETURN_ON_FAIL(createDevice(deviceType, {Feature::Surface}, {}, m_device.writeRef()));
        SLANG_RETURN_ON_FAIL(createWindow(m_device, "ShaderToy"));
        SLANG_RETURN_ON_FAIL(createSurface(m_device, Format::Undefined, m_surface.writeRef()));

        SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));

        m_blitter = std::make_unique<Blitter>(m_device);

        m_pipelines.resize(kShaders.size());
        loadShader();
        return SLANG_OK;
    }

    virtual void shutdown() override
    {
        m_queue->waitOnHost();
        m_queue.setNull();
        m_blitter.reset();
        m_surface.setNull();
        m_device.setNull();
    }

    virtual Result update(double time) override
    {
        if (m_time == 0.0)
        {
            m_time = time;
        }
        m_timeDelta = time - m_time;
        m_time = time;
        m_frameRate = 0.9 * m_frameRate + 0.1 * (m_timeDelta > 0.0 ? (1.0 / m_timeDelta) : 0.0);

        if (isMouseDown(0))
        {
            m_stickyMousePos[0] = getMouseX();
            m_stickyMousePos[1] = getMouseY();
        }
        bool wasMouseDown = m_combinedMouseDown;
        m_combinedMouseDown = isMouseDown(0) || isMouseDown(1) || isMouseDown(2);
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

    virtual Result draw() override
    {
        // Wait for shader to be loaded
        if (m_future.valid())
        {
            Result result = m_future.get();
            SLANG_RETURN_ON_FAIL(result);
        }

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

        uint32_t width = image->getDesc().size.width;
        uint32_t height = image->getDesc().size.height;

        // Create or resize render texture if needed
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

        // Start command encoding
        ComPtr<ICommandEncoder> commandEncoder = m_queue->createCommandEncoder();

        // Start compute pass
        IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
        ShaderCursor cursor(passEncoder->bindPipeline(m_pipelines[m_shaderIndex]));
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

        // Blit result to the surface image
        m_blitter->blit(image, m_texture, commandEncoder);

        // Submit command buffer
        m_queue->submit(commandEncoder->finish());

        m_frame += 1;

        // Present the surface
        return m_surface->present();
    }

    virtual void onResize(int width, int height, int framebufferWidth, int framebufferHeight) override
    {
        // Wait for GPU to be idle before resizing
        m_device->getQueue(QueueType::Graphics)->waitOnHost();
        // Configure or unconfigure the surface based on the new framebuffer size
        if (framebufferWidth > 0 && framebufferHeight > 0)
        {
            SurfaceConfig surfaceConfig;
            surfaceConfig.width = framebufferWidth;
            surfaceConfig.height = framebufferHeight;
            m_surface->configure(surfaceConfig);
        }
        else
        {
            m_surface->unconfigure();
        }
    }

    virtual void onKey(int key, int scancode, int action, int mods) override
    {
        if (key == GLFW_KEY_RIGHT && action == GLFW_PRESS)
        {
            m_shaderIndex = (m_shaderIndex + 1) % kShaders.size();
            loadShader();
        }
        else if (key == GLFW_KEY_LEFT && action == GLFW_PRESS)
        {
            m_shaderIndex = (m_shaderIndex - 1 + kShaders.size()) % kShaders.size();
            loadShader();
        }
    }

    void loadShader()
    {
        if (m_pipelines[m_shaderIndex])
        {
            return;
        }
        // Load shader asynchronously
        m_future = std::async(
            [this]() -> Result
            {
                return createComputePipeline(
                    m_device,
                    kShaders[m_shaderIndex],
                    "mainCompute",
                    m_pipelines[m_shaderIndex].writeRef()
                );
            }
        );
    }

public:
    ComPtr<IDevice> m_device;
    ComPtr<ISurface> m_surface;
    ComPtr<ICommandQueue> m_queue;
    std::unique_ptr<Blitter> m_blitter;
    std::vector<ComPtr<IComputePipeline>> m_pipelines;
    ComPtr<ITexture> m_texture;

    std::future<Result> m_future;

    float m_stickyMousePos[2] = {0.0f, 0.0f};
    bool m_combinedMouseDown = false;
    bool m_combinedMouseClicked = false;

    double m_time = 0.0;
    double m_timeDelta = 0.0;
    double m_frameRate = 0.0;
    uint32_t m_frame = 0;

    int m_shaderIndex = 0;
};

EXAMPLE_MAIN(ExampleShaderToy)

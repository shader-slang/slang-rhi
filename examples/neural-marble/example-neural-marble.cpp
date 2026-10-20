// Procedural texture fitting with Slang autodiff and optional cooperative vectors.
#include "../base/example.h"
#include "trainer.h"

namespace {

using namespace rhi;

class ExampleNeuralMarble : public ExampleBase
{
public:
    Result init(DeviceType deviceType) override
    {
        std::vector<Feature> features = {Feature::Surface};
        // The differentiated shader requires DXC on Direct3D.
        if (deviceType == DeviceType::D3D11 || deviceType == DeviceType::D3D12)
            features.push_back(Feature::SM_6_0);
        SLANG_RETURN_ON_FAIL(createDevice(deviceType, features, {}, m_device.writeRef()));
        SLANG_RETURN_ON_FAIL(createWindow(m_device));
        SLANG_RETURN_ON_FAIL(createSurface(m_device, Format::Undefined, m_surface.writeRef()));
        SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));
        SLANG_RETURN_ON_FAIL(initOverlay(m_device, m_surface->getConfig()->format));
        SLANG_RETURN_ON_FAIL(initProfiler(m_device));
        getProfiler().setEnabled(true);
        m_trainer = std::make_unique<neural_marble::Trainer>();
        SLANG_RETURN_ON_FAIL(m_trainer->init(m_device));
        SLANG_RETURN_ON_FAIL(createComputePipeline(m_device, "display.slang", "compose", m_compose.writeRef()));
        m_blitter = std::make_unique<Blitter>(m_device);
        SamplerDesc sampler = {};
        sampler.minFilter = sampler.magFilter = TextureFilteringMode::Linear;
        sampler.addressU = sampler.addressV = TextureAddressingMode::ClampToEdge;
        SLANG_RETURN_ON_FAIL(m_device->createSampler(sampler, m_sampler.writeRef()));

        auto pixels = bitmap_font::createAtlas();
        TextureDesc font = {};
        font.size = {bitmap_font::kWidth, bitmap_font::kHeight, 1};
        font.format = Format::R8Unorm;
        font.usage = TextureUsage::ShaderResource;
        SubresourceData data = {pixels.data(), bitmap_font::kWidth, pixels.size()};
        SLANG_RETURN_ON_FAIL(m_device->createTexture(font, &data, m_font.writeRef()));
        const char* strings[] = {"REFERENCE", "LEARNED", "ERROR (RGB RMSE)", "0          0.25+"};
        uint32_t labels[64];
        std::fill(std::begin(labels), std::end(labels), uint32_t(' '));
        for (uint32_t row = 0; row < 4; ++row)
            for (uint32_t i = 0; strings[row][i]; ++i)
                labels[row * 16 + i] = uint32_t(strings[row][i]);
        SLANG_RETURN_ON_FAIL(
            uploadBuffer<uint32_t>(m_device, labels, BufferUsage::ShaderResource, "Marble labels", m_labels.writeRef())
        );

        FenceDesc fence = {};
        SLANG_RETURN_ON_FAIL(m_device->createFence(fence, m_fence.writeRef()));
        for (auto& slot : m_readbacks)
        {
            BufferDesc buffer = {};
            buffer.size = MARBLE_VALIDATION_SIZE * MARBLE_VALIDATION_SIZE * sizeof(float);
            buffer.usage = BufferUsage::CopyDestination;
            buffer.memoryType = MemoryType::ReadBack;
            SLANG_RETURN_ON_FAIL(m_device->createBuffer(buffer, nullptr, slot.buffer.writeRef()));
        }
        return SLANG_OK;
    }

    void shutdown() override
    {
        if (m_queue)
            m_queue->waitOnHost();
        m_trainer.reset();
        m_blitter.reset();
        m_readbacks = {};
        m_fence.setNull();
        m_compose.setNull();
        m_output.setNull();
        m_font.setNull();
        m_labels.setNull();
        m_sampler.setNull();
        m_surface.setNull();
        m_queue.setNull();
        m_device.setNull();
    }

    Result update(double time) override { return pollValidation(); }

    Result draw() override
    {
        if (!m_surface->getConfig())
            return SLANG_OK;
        ComPtr<ITexture> image;
        SLANG_RETURN_ON_FAIL(m_surface->acquireNextImage(image.writeRef()));
        if (!image)
            return SLANG_OK;
        uint32_t width = image->getDesc().size.width, height = image->getDesc().size.height;
        if (!m_output || m_output->getDesc().size.width != width || m_output->getDesc().size.height != height)
        {
            TextureDesc desc = {};
            desc.size = {width, height, 1};
            desc.format = Format::RGBA32Float;
            desc.usage = TextureUsage::UnorderedAccess | TextureUsage::ShaderResource;
            SLANG_RETURN_ON_FAIL(m_device->createTexture(desc, nullptr, m_output.writeRef()));
        }
        auto profile = getProfiler().frame();
        SLANG_RETURN_ON_FAIL(profile.begin());
        ComPtr<ICommandEncoder> encoder;
        SLANG_RETURN_ON_FAIL(m_queue->createCommandEncoder(encoder.writeRef()));
        if (m_reset)
        {
            SLANG_RETURN_ON_FAIL(m_trainer->reset(encoder));
            m_reset = false;
        }
        for (uint32_t i = 0; !m_paused && i < MARBLE_STEPS_PER_FRAME; ++i)
        {
            Profiler::pushRegion(encoder.get(), "Matrix preparation");
            SLANG_RETURN_ON_FAIL(m_trainer->prepareWeights(encoder));
            Profiler::popRegion(encoder.get());
            Profiler::pushRegion(encoder.get(), "Training + Adam");
            SLANG_RETURN_ON_FAIL(m_trainer->step(encoder));
            Profiler::popRegion(encoder.get());
        }
        Profiler::pushRegion(encoder.get(), "Matrix preparation");
        SLANG_RETURN_ON_FAIL(m_trainer->prepareWeights(encoder));
        Profiler::popRegion(encoder.get());
        Profiler::pushRegion(encoder.get(), "Inference + reference");
        SLANG_RETURN_ON_FAIL(m_trainer->infer(encoder));
        Profiler::popRegion(encoder.get());

        Readback* readback = nullptr;
        if (m_fence && m_frame++ % 15 == 0)
        {
            for (auto& slot : m_readbacks)
                if (!slot.value)
                {
                    readback = &slot;
                    SLANG_RETURN_ON_FAIL(m_trainer->validate(encoder));
                    encoder->copyBuffer(slot.buffer, 0, m_trainer->validation(), 0, slot.buffer->getDesc().size);
                    break;
                }
        }
        auto pass = encoder->beginComputePass();
        ShaderCursor cursor(pass->bindPipeline(m_compose));
        uint32_t size[] = {width, height};
        float fontScale = width >= 900 ? 2.f : 1.f;
        float top = std::min(290.f, float(height) * 0.42f);
        float panelSize = std::max(1.f, std::min(float(width) / 3.f - 24.f, float(height) - top - 60.f));
        SLANG_RETURN_ON_FAIL(cursor["preview"].setBinding(m_trainer->preview()));
        SLANG_RETURN_ON_FAIL(cursor["fontAtlas"].setBinding(m_font));
        SLANG_RETURN_ON_FAIL(cursor["labels"].setBinding(m_labels));
        SLANG_RETURN_ON_FAIL(cursor["linearSampler"].setBinding(m_sampler));
        SLANG_RETURN_ON_FAIL(cursor["output"].setBinding(m_output));
        SLANG_RETURN_ON_FAIL(cursor["outputSize"].setData(size));
        SLANG_RETURN_ON_FAIL(cursor["panelSize"].setData(panelSize));
        SLANG_RETURN_ON_FAIL(cursor["panelTop"].setData(top));
        SLANG_RETURN_ON_FAIL(cursor["fontScale"].setData(fontScale));
        pass->dispatchCompute((width + 7) / 8, (height + 7) / 8, 1);
        pass->end();
        SLANG_RETURN_ON_FAIL(m_blitter->blit(image, m_output, encoder));
        clearOverlay();
        printOverlayHeader();
        printOverlay("Space: Pause | R: Restart training | G: New marble | C: Execution mode\n\n");
        printOverlay(
            "%s | Step %u | Marble %u | %s\n",
            m_paused ? "Paused" : "Training",
            m_trainer->stepCount(),
            m_trainer->seed(),
            m_trainer->cooperative() ? "Cooperative vectors" : "Ordinary math"
        );
        if (!m_trainer->cooperativeSupported())
            printOverlay("Cooperative vectors unavailable on this device\n");
        if (m_hasLoss)
            printOverlay("Validation MSE: %.6f (delayed) | %u parameters\n", m_loss, m_trainer->parameterCount());
        else
            printOverlay(
                "Validation MSE: %s | %u parameters\n",
                m_fence ? "pending" : "readback unavailable",
                m_trainer->parameterCount()
            );
        SLANG_RETURN_ON_FAIL(drawOverlay(encoder, image));

        ComPtr<ICommandBuffer> commands;
        SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
        ICommandBuffer* command = commands;
        IFence* fence = m_fence;
        SubmitDesc submit = {};
        submit.commandBuffers = &command;
        submit.commandBufferCount = 1;
        if (readback)
        {
            readback->value = ++m_fenceValue;
            readback->generation = m_generation;
            submit.signalFences = &fence;
            submit.signalFenceValues = &readback->value;
            submit.signalFenceCount = 1;
        }
        SLANG_RETURN_ON_FAIL(m_queue->submit(submit));
        SLANG_RETURN_ON_FAIL(profile.end());
        return presentSurface();
    }

    Result onResize(int width, int height, int framebufferWidth, int framebufferHeight) override
    {
        SLANG_RETURN_ON_FAIL(m_queue->waitOnHost());
        if (framebufferWidth <= 0 || framebufferHeight <= 0)
            return m_surface->unconfigure();
        SurfaceConfig config = {};
        config.width = framebufferWidth;
        config.height = framebufferHeight;
        return configureSurface(m_surface, config);
    }

    void onKey(int key, int scancode, int action, int mods) override
    {
        if (action != GLFW_PRESS || !m_trainer)
            return;
        switch (key)
        {
        case GLFW_KEY_SPACE:
            m_paused = !m_paused;
            break;
        case GLFW_KEY_R:
            m_reset = true;
            invalidateLoss();
            break;
        case GLFW_KEY_G:
            m_trainer->nextTarget();
            invalidateLoss();
            break;
        case GLFW_KEY_C:
            if (m_trainer->cooperativeSupported())
            {
                m_trainer->setCooperative(!m_trainer->cooperative());
                invalidateLoss();
                getProfiler().setEnabled(getProfiler().enabled());
            }
            break;
        }
    }

private:
    void invalidateLoss()
    {
        ++m_generation;
        m_hasLoss = false;
        m_frame = 0;
    }
    Result pollValidation()
    {
        if (!m_fence)
            return SLANG_OK;
        uint64_t completed = 0;
        SLANG_RETURN_ON_FAIL(m_fence->getCurrentValue(&completed));
        for (auto& slot : m_readbacks)
            if (slot.value && slot.value <= completed)
            {
                if (slot.generation == m_generation && slot.value > m_lastReadback)
                {
                    void* mapped = nullptr;
                    SLANG_RETURN_ON_FAIL(m_device->mapBuffer(slot.buffer, CpuAccessMode::Read, &mapped));
                    double sum = 0;
                    const auto* values = static_cast<const float*>(mapped);
                    for (uint32_t i = 0; i < MARBLE_VALIDATION_SIZE * MARBLE_VALIDATION_SIZE; ++i)
                        sum += values[i];
                    SLANG_RETURN_ON_FAIL(m_device->unmapBuffer(slot.buffer));
                    m_loss = float(sum / (MARBLE_VALIDATION_SIZE * MARBLE_VALIDATION_SIZE));
                    m_hasLoss = true;
                    m_lastReadback = slot.value;
                }
                slot.value = 0;
            }
        return SLANG_OK;
    }

    struct Readback
    {
        ComPtr<IBuffer> buffer;
        uint64_t value = 0, generation = 0;
    };
    std::array<Readback, 3> m_readbacks;
    ComPtr<IDevice> m_device;
    ComPtr<ICommandQueue> m_queue;
    ComPtr<ISurface> m_surface;
    ComPtr<IComputePipeline> m_compose;
    ComPtr<ITexture> m_output, m_font;
    ComPtr<IBuffer> m_labels;
    ComPtr<ISampler> m_sampler;
    ComPtr<IFence> m_fence;
    std::unique_ptr<neural_marble::Trainer> m_trainer;
    std::unique_ptr<Blitter> m_blitter;
    uint64_t m_fenceValue = 0, m_lastReadback = 0, m_generation = 0;
    uint32_t m_frame = 0;
    bool m_paused = false, m_reset = false, m_hasLoss = false;
    float m_loss = 0;
};

} // namespace

EXAMPLE_MAIN(ExampleNeuralMarble, "neural-marble", "Neural Marble")

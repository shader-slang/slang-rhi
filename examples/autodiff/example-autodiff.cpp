// Automatic differentiation example: a small neural graphics primitive (NGP) for 2D
// texture fitting. A trainable hash grid and neural network learn a procedural marble
// texture entirely on the GPU. Slang autodiff differentiates the RGB loss through the
// network and grid interpolation; custom derivatives write gradients into shared
// buffers, and separate compute passes update parameters with Adam.
// Supported Vulkan devices can use FP16 cooperative vectors for network evaluation
// and backpropagation, while master weights and optimizer state remain FP32.
// The three panels show the reference, learned texture, and error. Training starts
// immediately: Space pauses, R resets training, G changes the target, and C switches
// execution mode. The overlay shows validation loss, GPU timings, and shared controls.

#include "../base/example.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {

using namespace rhi;

struct GridLevel
{
    uint32_t resolution;
    uint32_t offset;
    uint32_t entryCount;
};
static_assert(sizeof(GridLevel) == 12);

// Trainable hash grid and MLP, with ordinary and cooperative-vector execution.
class Trainer
{
public:
    static_assert(NGP_GRID_LEVELS > 1 && NGP_GRID_FEATURES == 2 && NGP_OUTPUTS == 4);
    static_assert(NGP_GRID_MIN_RESOLUTION > 0 && NGP_GRID_MAX_RESOLUTION >= NGP_GRID_MIN_RESOLUTION);
    static_assert(NGP_GRID_MAX_ENTRIES > 0);
    static_assert(NGP_WIDTH > 0 && NGP_WIDTH % 8 == 0);
    static_assert(NGP_BATCH_SIZE > 0);

    Result init(IDevice* device)
    {
        // FXC cannot efficiently compile the local arrays in the autodiff kernel.
        if (device->getDeviceType() == DeviceType::D3D11)
            return SLANG_E_NOT_AVAILABLE;
        m_device = device;
        m_initialWeights.resize(NGP_PARAMETER_COUNT, 0.f);
        RNG random(0x4d415242);
        auto initialize = [&](uint32_t offset, uint32_t inputs, uint32_t outputs)
        {
            float scale = std::sqrt(6.f / inputs);
            for (uint32_t i = 0; i < inputs * outputs; ++i)
                m_initialWeights[offset + i] = (2.f * random.nextFloat() - 1.f) * scale;
        };
        initialize(NGP_W0, NGP_INPUTS, NGP_WIDTH);
        initialize(NGP_W1, NGP_WIDTH, NGP_WIDTH);
        initialize(NGP_W2, NGP_WIDTH, NGP_OUTPUTS);
        // Start near the palette's average instead of spending the first frames
        // removing large random RGB excursions from the output layer.
        for (uint32_t i = NGP_W2; i < NGP_B2; ++i)
            m_initialWeights[i] *= 0.1f;
        m_initialWeights[NGP_B2 + 0] = 0.2f;
        m_initialWeights[NGP_B2 + 1] = 0.25f;
        m_initialWeights[NGP_B2 + 2] = 0.3f;

        std::vector<GridLevel> levels;
        for (uint32_t i = 0; i < NGP_GRID_LEVELS; ++i)
        {
            float scale =
                std::pow(float(NGP_GRID_MAX_RESOLUTION) / NGP_GRID_MIN_RESOLUTION, float(i) / (NGP_GRID_LEVELS - 1));
            uint32_t resolution = uint32_t(std::round(NGP_GRID_MIN_RESOLUTION * scale));
            uint32_t entries = std::min((resolution + 1) * (resolution + 1), uint32_t(NGP_GRID_MAX_ENTRIES));
            levels.push_back({resolution, m_params.gridEntryCount, entries});
            m_params.gridEntryCount += entries;
        }
        m_initialGrid.resize(m_params.gridEntryCount * NGP_GRID_FEATURES);
        for (float& value : m_initialGrid)
            value = (2.f * random.nextFloat() - 1.f) * 1e-4f;
        SLANG_RETURN_ON_FAIL(
            buffer(levels.size() * sizeof(GridLevel), levels.data(), "Autodiff grid levels", m_gridLevels)
        );
        SLANG_RETURN_ON_FAIL(
            buffer(m_initialGrid.size() * sizeof(float), m_initialGrid.data(), "Autodiff grid features", m_gridFeatures)
        );
        std::vector<float> gridZeros(m_initialGrid.size() * 2, 0.f);
        SLANG_RETURN_ON_FAIL(
            buffer(gridZeros.size() * sizeof(float), gridZeros.data(), "Autodiff grid Adam state", m_gridMoments)
        );
        SLANG_RETURN_ON_FAIL(
            buffer(m_params.gridEntryCount * sizeof(uint32_t), nullptr, "Autodiff grid heads", m_gridHeads)
        );
        uint32_t nodeCount = NGP_BATCH_SIZE * NGP_GRID_LEVELS * 4;
        SLANG_RETURN_ON_FAIL(buffer(nodeCount * sizeof(uint32_t), nullptr, "Autodiff grid links", m_gridNext));
        SLANG_RETURN_ON_FAIL(
            buffer(nodeCount * 2 * sizeof(float), nullptr, "Autodiff sparse grid gradients", m_gridGradients)
        );

        SLANG_RETURN_ON_FAIL(
            buffer(NGP_PARAMETER_COUNT * sizeof(float), m_initialWeights.data(), "Autodiff weights", m_weights)
        );
        std::vector<float> zeros(NGP_PARAMETER_COUNT * 2, 0.f);
        SLANG_RETURN_ON_FAIL(buffer(zeros.size() * sizeof(float), zeros.data(), "Autodiff Adam state", m_moments));
        SLANG_RETURN_ON_FAIL(
            buffer(NGP_PARAMETER_COUNT * NGP_BATCH_SIZE * sizeof(float), nullptr, "Autodiff gradients", m_gradients)
        );
        SLANG_RETURN_ON_FAIL(buffer(
            NGP_VALIDATION_SIZE * NGP_VALIDATION_SIZE * sizeof(float),
            nullptr,
            "Autodiff validation",
            m_validation
        ));

        TextureDesc texture = {};
        texture.size = {3 * NGP_PREVIEW_SIZE, NGP_PREVIEW_SIZE, 1};
        texture.format = Format::RGBA32Float;
        texture.usage = TextureUsage::UnorderedAccess | TextureUsage::ShaderResource | TextureUsage::CopySource;
        texture.label = "Autodiff reference, learned, error";
        SLANG_RETURN_ON_FAIL(device->createTexture(texture, nullptr, m_preview.writeRef()));

        SLANG_RETURN_ON_FAIL(loadPipelines("autodiff.slang", m_scalar));
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, "autodiff.slang", "optimize", m_optimize.writeRef()));
        SLANG_RETURN_ON_FAIL(
            createComputePipeline(device, "autodiff.slang", "optimizeGrid", m_optimizeGrid.writeRef())
        );

        return initCooperative();
    }

    Result reset(ICommandEncoder* encoder)
    {
        SLANG_RETURN_ON_FAIL(
            encoder->uploadBufferData(m_weights, 0, m_initialWeights.size() * sizeof(float), m_initialWeights.data())
        );
        encoder->clearBuffer(m_moments, kEntireBuffer);
        SLANG_RETURN_ON_FAIL(
            encoder->uploadBufferData(m_gridFeatures, 0, m_initialGrid.size() * sizeof(float), m_initialGrid.data())
        );
        encoder->clearBuffer(m_gridMoments, kEntireBuffer);
        m_params.step = 0;
        m_dirty = true;
        return SLANG_OK;
    }

    void nextTarget() { m_params.seed += 97; }
    uint32_t seed() const { return m_params.seed; }
    uint32_t parameterCount() const { return NGP_PARAMETER_COUNT + uint32_t(m_initialGrid.size()); }
    uint32_t stepCount() const { return m_params.step; }
    bool cooperativeSupported() const { return m_cooperativeSupported; }
    bool cooperative() const { return m_cooperative; }
    void setCooperative(bool enabled) { m_cooperative = enabled && m_cooperativeSupported; }

    Result step(ICommandEncoder* encoder)
    {
        SLANG_RETURN_ON_FAIL(prepareWeights(encoder));
        Profiler::pushRegion(encoder, "Training + Adam");
        encoder->clearBuffer(m_gridHeads, kEntireBuffer);
        SLANG_RETURN_ON_FAIL(dispatch(encoder, pipelines().train, (NGP_BATCH_SIZE + 31) / 32));
        SLANG_RETURN_ON_FAIL(dispatch(encoder, m_optimize, (NGP_PARAMETER_COUNT + 63) / 64));
        SLANG_RETURN_ON_FAIL(dispatch(encoder, m_optimizeGrid, (m_params.gridEntryCount + 63) / 64));
        Profiler::popRegion(encoder);
        ++m_params.step;
        m_dirty = true;
        return SLANG_OK;
    }

    Result infer(ICommandEncoder* encoder)
    {
        SLANG_RETURN_ON_FAIL(prepareWeights(encoder));
        Profiler::pushRegion(encoder, "Inference + reference");
        SLANG_RETURN_ON_FAIL(
            dispatch(encoder, pipelines().infer, (NGP_PREVIEW_SIZE + 7) / 8, (NGP_PREVIEW_SIZE + 7) / 8)
        );
        Profiler::popRegion(encoder);
        return SLANG_OK;
    }

    Result validate(ICommandEncoder* encoder)
    {
        SLANG_RETURN_ON_FAIL(prepareWeights(encoder));
        return dispatch(encoder, pipelines().validate, (NGP_VALIDATION_SIZE + 7) / 8, (NGP_VALIDATION_SIZE + 7) / 8);
    }

    ITexture* preview() const { return m_preview; }
    IBuffer* validation() const { return m_validation; }

private:
    Result initCooperative()
    {
        // CUDA's native cooperative-vector buffer overload is not supported by
        // this shader. Vulkan uses the exact FP16 tuple below.
        if (m_device->getDeviceType() == DeviceType::Vulkan && m_device->hasFeature(Feature::CooperativeVector) &&
            m_device->hasFeature(Feature::Half))
        {
            uint32_t count = 0;
            SLANG_RETURN_ON_FAIL(m_device->getCooperativeVectorProperties(nullptr, &count));
            std::vector<CooperativeVectorProperties> properties(count);
            SLANG_RETURN_ON_FAIL(m_device->getCooperativeVectorProperties(properties.data(), &count));
            for (const auto& p : properties)
                if (p.inputType == CooperativeVectorComponentType::Float16 &&
                    p.inputInterpretation == CooperativeVectorComponentType::Float16 &&
                    p.matrixInterpretation == CooperativeVectorComponentType::Float16 &&
                    p.resultType == CooperativeVectorComponentType::Float16)
                    m_cooperativeSupported = true;
        }
        if (m_cooperativeSupported)
        {
            const uint32_t inputs[] = {NGP_INPUTS, NGP_WIDTH, NGP_WIDTH};
            const uint32_t outputs[] = {NGP_WIDTH, NGP_WIDTH, NGP_OUTPUTS};
            const uint32_t offsets[] = {NGP_W0, NGP_W1, NGP_W2};
            size_t totalSize = 0;
            for (uint32_t i = 0; i < 3; ++i)
            {
                auto& src = m_sourceMatrices[i];
                src.rowCount = outputs[i];
                src.colCount = inputs[i];
                src.componentType = CooperativeVectorComponentType::Float16;
                src.layout = CooperativeVectorMatrixLayout::RowMajor;
                src.offset = offsets[i] * 2;
                src.size = inputs[i] * outputs[i] * 2;
                src.rowColumnStride = inputs[i] * 2;
                auto& dst = m_optimalMatrices[i];
                dst = src;
                dst.layout = CooperativeVectorMatrixLayout::InferencingOptimal;
                dst.rowColumnStride = 0;
                dst.offset = (totalSize + 255) & ~size_t(255);
                SLANG_RETURN_ON_FAIL(m_device->getCooperativeVectorMatrixSize(
                    dst.rowCount,
                    dst.colCount,
                    dst.componentType,
                    dst.layout,
                    0,
                    &dst.size
                ));
                m_params.matrixOffsets[i] = uint32_t(dst.offset);
                totalSize = dst.offset + dst.size;
            }
            SLANG_RETURN_ON_FAIL(buffer(NGP_PARAMETER_COUNT * 2, nullptr, "Autodiff FP16 weights", m_packedWeights));
            SLANG_RETURN_ON_FAIL(buffer(totalSize, nullptr, "Autodiff optimal matrices", m_optimalWeights));
            SLANG_RETURN_ON_FAIL(createComputePipeline(m_device, "autodiff.slang", "pack", m_pack.writeRef()));
            SLANG_RETURN_ON_FAIL(loadPipelines("autodiff-coop.slang", m_coop));
        }
        m_cooperative = m_cooperativeSupported;
        return SLANG_OK;
    }

    struct Params
    {
        uint32_t step = 0, seed = 7, previewSize = NGP_PREVIEW_SIZE, gridEntryCount = 0;
        uint32_t matrixOffsets[4] = {};
    } m_params;
    static_assert(sizeof(Params) == 32);

    struct Pipelines
    {
        ComPtr<IComputePipeline> train, infer, validate;
    } m_scalar, m_coop;
    const Pipelines& pipelines() const { return m_cooperative ? m_coop : m_scalar; }

    Result prepareWeights(ICommandEncoder* encoder)
    {
        if (!m_cooperative || !m_dirty)
            return SLANG_OK;
        Profiler::pushRegion(encoder, "Matrix preparation");
        SLANG_RETURN_ON_FAIL(dispatch(encoder, m_pack, (NGP_PARAMETER_COUNT / 2 + 63) / 64));
        encoder
            ->convertCooperativeVectorMatrix(m_optimalWeights, m_optimalMatrices, m_packedWeights, m_sourceMatrices, 3);
        Profiler::popRegion(encoder);
        m_dirty = false;
        return SLANG_OK;
    }

    Result loadPipelines(const char* file, Pipelines& result)
    {
        SLANG_RETURN_ON_FAIL(createComputePipeline(m_device, file, "train", result.train.writeRef()));
        SLANG_RETURN_ON_FAIL(createComputePipeline(m_device, file, "infer", result.infer.writeRef()));
        return createComputePipeline(m_device, file, "validate", result.validate.writeRef());
    }

    Result buffer(size_t size, const void* data, const char* label, ComPtr<IBuffer>& result)
    {
        BufferDesc desc = {};
        desc.size = size;
        desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource |
                     BufferUsage::CopyDestination;
        desc.label = label;
        return m_device->createBuffer(desc, data, result.writeRef());
    }

    Result dispatch(ICommandEncoder* encoder, IComputePipeline* pipeline, uint32_t x, uint32_t y = 1)
    {
        auto pass = encoder->beginComputePass();
        ShaderCursor cursor(pass->bindPipeline(pipeline));
        SLANG_RETURN_ON_FAIL(cursor["params"].setData(m_params));
        SLANG_RETURN_ON_FAIL(cursor["weights"].setBinding(m_weights));
        SLANG_RETURN_ON_FAIL(cursor["moments"].setBinding(m_moments));
        SLANG_RETURN_ON_FAIL(cursor["gradients"].setBinding(m_gradients));
        SLANG_RETURN_ON_FAIL(cursor["gridLevels"].setBinding(m_gridLevels));
        SLANG_RETURN_ON_FAIL(cursor["gridFeatures"].setBinding(m_gridFeatures));
        SLANG_RETURN_ON_FAIL(cursor["gridMoments"].setBinding(m_gridMoments));
        SLANG_RETURN_ON_FAIL(cursor["gridHeads"].setBinding(m_gridHeads));
        SLANG_RETURN_ON_FAIL(cursor["gridNext"].setBinding(m_gridNext));
        SLANG_RETURN_ON_FAIL(cursor["gridGradients"].setBinding(m_gridGradients));
        SLANG_RETURN_ON_FAIL(cursor["validation"].setBinding(m_validation));
        SLANG_RETURN_ON_FAIL(cursor["preview"].setBinding(m_preview));
        if (m_packedWeights)
        {
            // Do not bind the buffer as both UAV and SRV in the same dispatch.
            if (pipeline == m_pack.get())
            {
                SLANG_RETURN_ON_FAIL(cursor["packedWeights"].setBinding(m_packedWeights));
            }
            else
            {
                SLANG_RETURN_ON_FAIL(cursor["packedWeightsRead"].setBinding(m_packedWeights));
            }
            SLANG_RETURN_ON_FAIL(cursor["optimalWeights"].setBinding(m_optimalWeights));
        }
        pass->dispatchCompute(x, y, 1);
        pass->end();
        return SLANG_OK;
    }

    ComPtr<IDevice> m_device;
    ComPtr<IBuffer> m_weights, m_moments, m_gradients, m_packedWeights, m_optimalWeights, m_validation;
    ComPtr<IBuffer> m_gridLevels, m_gridFeatures, m_gridMoments, m_gridHeads, m_gridNext, m_gridGradients;
    ComPtr<ITexture> m_preview;
    ComPtr<IComputePipeline> m_optimize, m_optimizeGrid, m_pack;
    CooperativeVectorMatrixDesc m_sourceMatrices[3] = {}, m_optimalMatrices[3] = {};
    std::vector<float> m_initialWeights;
    std::vector<float> m_initialGrid;
    bool m_cooperativeSupported = false, m_cooperative = false, m_dirty = true;
};

class ExampleAutodiff : public ExampleBase
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
        m_trainer = std::make_unique<Trainer>();
        SLANG_RETURN_ON_FAIL(m_trainer->init(m_device));
        SLANG_RETURN_ON_FAIL(createComputePipeline(m_device, "display.slang", "compose", m_compose.writeRef()));
        m_blitter = std::make_unique<Blitter>(m_device);
        SamplerDesc sampler = {};
        sampler.minFilter = sampler.magFilter = TextureFilteringMode::Linear;
        sampler.addressU = sampler.addressV = TextureAddressingMode::ClampToEdge;
        SLANG_RETURN_ON_FAIL(m_device->createSampler(sampler, m_sampler.writeRef()));

        FenceDesc fence = {};
        SLANG_RETURN_ON_FAIL(m_device->createFence(fence, m_fence.writeRef()));
        BufferDesc buffer = {};
        buffer.size = NGP_VALIDATION_SIZE * NGP_VALIDATION_SIZE * sizeof(float);
        buffer.usage = BufferUsage::CopyDestination;
        buffer.memoryType = MemoryType::ReadBack;
        return m_device->createBuffer(buffer, nullptr, m_readback.buffer.writeRef());
    }

    void shutdown() override
    {
        if (m_queue)
            m_queue->waitOnHost();
        m_trainer.reset();
        m_blitter.reset();
        m_readback = {};
        m_fence.setNull();
        m_compose.setNull();
        m_output.setNull();
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
        auto profile = getProfiler().frame();
        SLANG_RETURN_ON_FAIL(profile.begin());
        ComPtr<ICommandEncoder> encoder;
        SLANG_RETURN_ON_FAIL(m_queue->createCommandEncoder(encoder.writeRef()));
        if (m_reset)
        {
            SLANG_RETURN_ON_FAIL(m_trainer->reset(encoder));
            m_reset = false;
        }
        for (uint32_t i = 0; !m_paused && i < NGP_STEPS_PER_FRAME; ++i)
            SLANG_RETURN_ON_FAIL(m_trainer->step(encoder));
        SLANG_RETURN_ON_FAIL(m_trainer->infer(encoder));

        // Skip this sample if the previous readback is still pending.
        bool readback = m_frame++ % 15 == 0 && !m_readback.value;
        if (readback)
        {
            SLANG_RETURN_ON_FAIL(m_trainer->validate(encoder));
            encoder->copyBuffer(m_readback.buffer, 0, m_trainer->validation(), 0, m_readback.buffer->getDesc().size);
        }
        SLANG_RETURN_ON_FAIL(composePanels(encoder, image));
        clearOverlay();
        printOverlayHeader();
        printOverlay("Space: Pause | R: Restart training | G: New marble | C: Execution mode\n");
        printOverlay("Left: Reference | Middle: Learned | Right: RGB RMSE\n");
        printOverlay("Error scale: 0 (dark) to 0.25+ (orange)\n");
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
            printOverlay("Validation MSE: pending | %u parameters\n", m_trainer->parameterCount());
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
            m_readback.value = ++m_fenceValue;
            m_readback.generation = m_generation;
            submit.signalFences = &fence;
            submit.signalFenceValues = &m_readback.value;
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
    Result composePanels(ICommandEncoder* encoder, ITexture* image)
    {
        uint32_t width = image->getDesc().size.width, height = image->getDesc().size.height;
        if (!m_output || m_output->getDesc().size.width != width || m_output->getDesc().size.height != height)
        {
            TextureDesc desc = {};
            desc.size = {width, height, 1};
            desc.format = Format::RGBA32Float;
            desc.usage = TextureUsage::UnorderedAccess | TextureUsage::ShaderResource;
            SLANG_RETURN_ON_FAIL(m_device->createTexture(desc, nullptr, m_output.writeRef()));
        }
        auto pass = encoder->beginComputePass();
        ShaderCursor cursor(pass->bindPipeline(m_compose));
        uint32_t size[] = {width, height};
        float top = std::min(290.f, float(height) * 0.5f);
        float panelSize = std::max(1.f, std::min(float(width) / 3.f - 24.f, float(height) - top - 30.f));
        SLANG_RETURN_ON_FAIL(cursor["preview"].setBinding(m_trainer->preview()));
        SLANG_RETURN_ON_FAIL(cursor["linearSampler"].setBinding(m_sampler));
        SLANG_RETURN_ON_FAIL(cursor["output"].setBinding(m_output));
        SLANG_RETURN_ON_FAIL(cursor["outputSize"].setData(size));
        SLANG_RETURN_ON_FAIL(cursor["panelSize"].setData(panelSize));
        SLANG_RETURN_ON_FAIL(cursor["panelTop"].setData(top));
        pass->dispatchCompute((width + 7) / 8, (height + 7) / 8, 1);
        pass->end();
        SLANG_RETURN_ON_FAIL(m_blitter->blit(image, m_output, encoder));
        return SLANG_OK;
    }

    void invalidateLoss()
    {
        // Discard stale results without reusing a buffer still being written by the GPU.
        ++m_generation;
        m_hasLoss = false;
        m_frame = 0;
    }
    Result pollValidation()
    {
        if (!m_readback.value)
            return SLANG_OK;
        uint64_t completed = 0;
        SLANG_RETURN_ON_FAIL(m_fence->getCurrentValue(&completed));
        if (completed < m_readback.value)
            return SLANG_OK;
        if (m_readback.generation == m_generation)
        {
            void* mapped = nullptr;
            SLANG_RETURN_ON_FAIL(m_device->mapBuffer(m_readback.buffer, CpuAccessMode::Read, &mapped));
            double sum = 0;
            const auto* values = static_cast<const float*>(mapped);
            for (uint32_t i = 0; i < NGP_VALIDATION_SIZE * NGP_VALIDATION_SIZE; ++i)
                sum += values[i];
            SLANG_RETURN_ON_FAIL(m_device->unmapBuffer(m_readback.buffer));
            m_loss = float(sum / (NGP_VALIDATION_SIZE * NGP_VALIDATION_SIZE));
            m_hasLoss = true;
        }
        m_readback.value = 0;
        return SLANG_OK;
    }

    struct Readback
    {
        ComPtr<IBuffer> buffer;
        uint64_t value = 0, generation = 0;
    } m_readback;
    ComPtr<IDevice> m_device;
    ComPtr<ICommandQueue> m_queue;
    ComPtr<ISurface> m_surface;
    ComPtr<IComputePipeline> m_compose;
    ComPtr<ITexture> m_output;
    ComPtr<ISampler> m_sampler;
    ComPtr<IFence> m_fence;
    std::unique_ptr<Trainer> m_trainer;
    std::unique_ptr<Blitter> m_blitter;
    uint64_t m_fenceValue = 0, m_generation = 0;
    uint32_t m_frame = 0;
    bool m_paused = false, m_reset = false, m_hasLoss = false;
    float m_loss = 0;
};

} // namespace

EXAMPLE_MAIN(ExampleAutodiff, "autodiff", "Autodiff: Neural Texture Fitting")

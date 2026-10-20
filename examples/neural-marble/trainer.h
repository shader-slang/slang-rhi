#pragma once

#include "../base/utils.h"
#include "settings.h"

namespace neural_marble {

using namespace rhi;

// Headless training/rendering core, shared by the interactive example and tests.
class Trainer
{
public:
    static_assert(MARBLE_GRID_LEVELS > 1 && MARBLE_GRID_FEATURES == 2 && MARBLE_OUTPUTS == 4);
    static_assert(MARBLE_GRID_MIN_RESOLUTION > 0 && MARBLE_GRID_MAX_RESOLUTION >= MARBLE_GRID_MIN_RESOLUTION);
    static_assert(MARBLE_GRID_MAX_ENTRIES > 0);
    static_assert(MARBLE_WIDTH > 0 && MARBLE_WIDTH % 8 == 0);
    static_assert(MARBLE_BATCH_SIZE > 0);

    Result init(IDevice* device)
    {
        // FXC cannot efficiently compile the local arrays in the autodiff kernel.
        if (device->getDeviceType() == DeviceType::D3D11)
            return SLANG_E_NOT_AVAILABLE;
        m_device = device;
        m_initialWeights.resize(MARBLE_PARAMETER_COUNT, 0.f);
        RNG random(0x4d415242);
        auto initialize = [&](uint32_t offset, uint32_t inputs, uint32_t outputs)
        {
            float scale = std::sqrt(6.f / inputs);
            for (uint32_t i = 0; i < inputs * outputs; ++i)
                m_initialWeights[offset + i] = (2.f * random.nextFloat() - 1.f) * scale;
        };
        initialize(MARBLE_W0, MARBLE_INPUTS, MARBLE_WIDTH);
        initialize(MARBLE_W1, MARBLE_WIDTH, MARBLE_WIDTH);
        initialize(MARBLE_W2, MARBLE_WIDTH, MARBLE_OUTPUTS);
        // Start near the palette's average instead of spending the first frames
        // removing large random RGB excursions from the output layer.
        for (uint32_t i = MARBLE_W2; i < MARBLE_B2; ++i)
            m_initialWeights[i] *= 0.1f;
        m_initialWeights[MARBLE_B2 + 0] = 0.2f;
        m_initialWeights[MARBLE_B2 + 1] = 0.25f;
        m_initialWeights[MARBLE_B2 + 2] = 0.3f;

        std::vector<uint32_t> levels;
        for (uint32_t i = 0; i < MARBLE_GRID_LEVELS; ++i)
        {
            float scale = std::pow(
                float(MARBLE_GRID_MAX_RESOLUTION) / MARBLE_GRID_MIN_RESOLUTION,
                float(i) / (MARBLE_GRID_LEVELS - 1)
            );
            uint32_t resolution = uint32_t(std::round(MARBLE_GRID_MIN_RESOLUTION * scale));
            uint32_t entries = std::min((resolution + 1) * (resolution + 1), uint32_t(MARBLE_GRID_MAX_ENTRIES));
            levels.insert(levels.end(), {resolution, m_params.gridEntryCount, entries, 0});
            m_params.gridEntryCount += entries;
        }
        m_initialGrid.resize(m_params.gridEntryCount * MARBLE_GRID_FEATURES);
        for (float& value : m_initialGrid)
            value = (2.f * random.nextFloat() - 1.f) * 1e-4f;
        SLANG_RETURN_ON_FAIL(buffer(levels.size() * 4, levels.data(), "Marble grid levels", m_gridLevels));
        SLANG_RETURN_ON_FAIL(
            buffer(m_initialGrid.size() * 4, m_initialGrid.data(), "Marble grid features", m_gridFeatures)
        );
        std::vector<float> gridZeros(m_initialGrid.size() * 2, 0.f);
        SLANG_RETURN_ON_FAIL(buffer(gridZeros.size() * 4, gridZeros.data(), "Marble grid Adam state", m_gridMoments));
        SLANG_RETURN_ON_FAIL(buffer(m_params.gridEntryCount * 4, nullptr, "Marble grid heads", m_gridHeads));
        uint32_t nodeCount = MARBLE_BATCH_SIZE * MARBLE_GRID_LEVELS * 4;
        SLANG_RETURN_ON_FAIL(buffer(nodeCount * 4, nullptr, "Marble grid links", m_gridNext));
        SLANG_RETURN_ON_FAIL(buffer(nodeCount * 8, nullptr, "Marble sparse grid gradients", m_gridGradients));

        SLANG_RETURN_ON_FAIL(buffer(MARBLE_PARAMETER_COUNT * 4, m_initialWeights.data(), "Marble weights", m_weights));
        std::vector<float> zeros(MARBLE_PARAMETER_COUNT * 2, 0.f);
        SLANG_RETURN_ON_FAIL(buffer(zeros.size() * 4, zeros.data(), "Marble Adam state", m_moments));
        SLANG_RETURN_ON_FAIL(
            buffer(MARBLE_PARAMETER_COUNT * MARBLE_BATCH_SIZE * 4, nullptr, "Marble gradients", m_gradients)
        );
        SLANG_RETURN_ON_FAIL(
            buffer(MARBLE_VALIDATION_SIZE * MARBLE_VALIDATION_SIZE * 4, nullptr, "Marble validation", m_validation)
        );

        TextureDesc texture = {};
        texture.size = {3 * MARBLE_PREVIEW_SIZE, MARBLE_PREVIEW_SIZE, 1};
        texture.format = Format::RGBA32Float;
        texture.usage = TextureUsage::UnorderedAccess | TextureUsage::ShaderResource | TextureUsage::CopySource;
        texture.label = "Marble reference, learned, error";
        SLANG_RETURN_ON_FAIL(device->createTexture(texture, nullptr, m_preview.writeRef()));

        SLANG_RETURN_ON_FAIL(loadPipelines("neural-marble.slang", m_scalar));
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, "neural-marble.slang", "optimize", m_optimize.writeRef()));
        SLANG_RETURN_ON_FAIL(
            createComputePipeline(device, "neural-marble.slang", "optimizeGrid", m_optimizeGrid.writeRef())
        );

        // CUDA's native cooperative-vector buffer overload is not supported by
        // this shader. Vulkan and D3D12 use the exact FP16 tuple below.
        if ((device->getDeviceType() == DeviceType::Vulkan || device->getDeviceType() == DeviceType::D3D12) &&
            device->hasFeature(Feature::CooperativeVector) && device->hasFeature(Feature::Half))
        {
            uint32_t count = 0;
            SLANG_RETURN_ON_FAIL(device->getCooperativeVectorProperties(nullptr, &count));
            std::vector<CooperativeVectorProperties> properties(count);
            SLANG_RETURN_ON_FAIL(device->getCooperativeVectorProperties(properties.data(), &count));
            for (const auto& p : properties)
                if (p.inputType == CooperativeVectorComponentType::Float16 &&
                    p.inputInterpretation == CooperativeVectorComponentType::Float16 &&
                    p.matrixInterpretation == CooperativeVectorComponentType::Float16 &&
                    p.resultType == CooperativeVectorComponentType::Float16)
                    m_cooperativeSupported = true;
        }
        if (m_cooperativeSupported)
        {
            const uint32_t inputs[] = {MARBLE_INPUTS, MARBLE_WIDTH, MARBLE_WIDTH};
            const uint32_t outputs[] = {MARBLE_WIDTH, MARBLE_WIDTH, MARBLE_OUTPUTS};
            const uint32_t offsets[] = {MARBLE_W0, MARBLE_W1, MARBLE_W2};
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
                SLANG_RETURN_ON_FAIL(device->getCooperativeVectorMatrixSize(
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
            SLANG_RETURN_ON_FAIL(buffer(MARBLE_PARAMETER_COUNT * 2, nullptr, "Marble FP16 weights", m_packedWeights));
            SLANG_RETURN_ON_FAIL(buffer(totalSize, nullptr, "Marble optimal matrices", m_optimalWeights));
            SLANG_RETURN_ON_FAIL(createComputePipeline(device, "neural-marble.slang", "pack", m_pack.writeRef()));
            SLANG_RETURN_ON_FAIL(loadPipelines("neural-marble-coop.slang", m_coop));
        }
        m_cooperative = m_cooperativeSupported;
        return SLANG_OK;
    }

    Result reset(ICommandEncoder* encoder)
    {
        SLANG_RETURN_ON_FAIL(
            encoder->uploadBufferData(m_weights, 0, m_initialWeights.size() * 4, m_initialWeights.data())
        );
        encoder->clearBuffer(m_moments, kEntireBuffer);
        SLANG_RETURN_ON_FAIL(
            encoder->uploadBufferData(m_gridFeatures, 0, m_initialGrid.size() * 4, m_initialGrid.data())
        );
        encoder->clearBuffer(m_gridMoments, kEntireBuffer);
        m_params.step = 0;
        m_dirty = true;
        return SLANG_OK;
    }

    void nextTarget() { m_params.seed += 97; }
    uint32_t seed() const { return m_params.seed; }
    uint32_t gridParameterCount() const { return uint32_t(m_initialGrid.size()); }
    uint32_t parameterCount() const { return MARBLE_PARAMETER_COUNT + gridParameterCount(); }
    uint32_t stepCount() const { return m_params.step; }
    bool cooperativeSupported() const { return m_cooperativeSupported; }
    bool cooperative() const { return m_cooperative; }
    void setCooperative(bool enabled) { m_cooperative = enabled && m_cooperativeSupported; }

    // Call before training or inference. Conversion is separately measurable.
    Result prepareWeights(ICommandEncoder* encoder)
    {
        if (!m_cooperative || !m_dirty)
            return SLANG_OK;
        SLANG_RETURN_ON_FAIL(dispatch(encoder, m_pack, (MARBLE_PARAMETER_COUNT / 2 + 63) / 64));
        encoder
            ->convertCooperativeVectorMatrix(m_optimalWeights, m_optimalMatrices, m_packedWeights, m_sourceMatrices, 3);
        m_dirty = false;
        return SLANG_OK;
    }

    Result step(ICommandEncoder* encoder)
    {
        SLANG_RETURN_ON_FAIL(prepareWeights(encoder));
        encoder->clearBuffer(m_gridHeads, kEntireBuffer);
        SLANG_RETURN_ON_FAIL(dispatch(encoder, pipelines().train, (MARBLE_BATCH_SIZE + 31) / 32));
        SLANG_RETURN_ON_FAIL(dispatch(encoder, m_optimize, (MARBLE_PARAMETER_COUNT + 63) / 64));
        SLANG_RETURN_ON_FAIL(dispatch(encoder, m_optimizeGrid, (m_params.gridEntryCount + 63) / 64));
        ++m_params.step;
        m_dirty = true;
        return SLANG_OK;
    }

    Result infer(ICommandEncoder* encoder)
    {
        SLANG_RETURN_ON_FAIL(prepareWeights(encoder));
        return dispatch(encoder, pipelines().infer, (MARBLE_PREVIEW_SIZE + 7) / 8, (MARBLE_PREVIEW_SIZE + 7) / 8);
    }

    Result validate(ICommandEncoder* encoder)
    {
        SLANG_RETURN_ON_FAIL(prepareWeights(encoder));
        return dispatch(
            encoder,
            pipelines().validate,
            (MARBLE_VALIDATION_SIZE + 7) / 8,
            (MARBLE_VALIDATION_SIZE + 7) / 8
        );
    }

    ITexture* preview() const { return m_preview; }
    IBuffer* validation() const { return m_validation; }
    IBuffer* weights() const { return m_weights; }
    IBuffer* gridFeatures() const { return m_gridFeatures; }

private:
    struct Params
    {
        uint32_t step = 0, seed = 7, previewSize = MARBLE_PREVIEW_SIZE, gridEntryCount = 0;
        uint32_t matrixOffsets[4] = {};
    } m_params;
    static_assert(sizeof(Params) == 32);

    struct Pipelines
    {
        ComPtr<IComputePipeline> train, infer, validate;
    } m_scalar, m_coop;
    const Pipelines& pipelines() const { return m_cooperative ? m_coop : m_scalar; }

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

} // namespace neural_marble

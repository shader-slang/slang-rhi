#include "testing.h"
#include "../examples/neural-marble/trainer.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("neural-marble-grid-gradients", D3D12 | Vulkan | Metal | CUDA | WGPU)
{
    // Tiny tables deliberately force collisions. The first two levels are
    // dense, and all later levels hash their vertices into four shared entries.
    std::vector<uint32_t> levels;
    uint32_t entryCount = 0;
    for (uint32_t i = 0; i < MARBLE_GRID_LEVELS; ++i)
    {
        uint32_t resolution = i < 2 ? i + 1 : 16 + i;
        uint32_t count = i < 2 ? (resolution + 1) * (resolution + 1) : 4;
        levels.insert(levels.end(), {resolution, entryCount, count, 0});
        entryCount += count;
    }
    std::vector<float> features(entryCount * MARBLE_GRID_FEATURES);
    for (size_t i = 0; i < features.size(); ++i)
        features[i] = float(i % 7) * 0.01f;
    uint32_t nodeCount = MARBLE_BATCH_SIZE * MARBLE_GRID_LEVELS * 4;
    auto buffer = [&](size_t bytes, const void* data = nullptr)
    {
        BufferDesc desc = {};
        desc.size = bytes;
        desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource |
                     BufferUsage::CopyDestination;
        auto result = device->createBuffer(desc, data);
        REQUIRE(result);
        return result;
    };
    auto levelBuffer = buffer(levels.size() * 4, levels.data());
    auto featureBuffer = buffer(features.size() * 4, features.data());
    auto headsBuffer = buffer(entryCount * 4);
    auto nextBuffer = buffer(nodeCount * 4);
    auto gradientBuffer = buffer(nodeCount * 8);
    auto valuesBuffer = buffer(MARBLE_BATCH_SIZE * 4);
    ComPtr<IComputePipeline> backward, forward;
    REQUIRE_CALL(
        neural_marble::createComputePipeline(device, "test-neural-marble-grid.slang", "backward", backward.writeRef())
    );
    REQUIRE_CALL(
        neural_marble::createComputePipeline(device, "test-neural-marble-grid.slang", "forward", forward.writeRef())
    );
    auto queue = device->getQueue(QueueType::Graphics);
    auto dispatch = [&](IComputePipeline* pipeline)
    {
        auto encoder = queue->createCommandEncoder();
        encoder->clearBuffer(headsBuffer, kEntireBuffer);
        REQUIRE_CALL(encoder->uploadBufferData(featureBuffer, 0, features.size() * 4, features.data()));
        auto pass = encoder->beginComputePass();
        ShaderCursor cursor(pass->bindPipeline(pipeline));
        REQUIRE_CALL(cursor["gridLevels"].setBinding(levelBuffer));
        REQUIRE_CALL(cursor["gridFeatures"].setBinding(featureBuffer));
        REQUIRE_CALL(cursor["gridHeads"].setBinding(headsBuffer));
        REQUIRE_CALL(cursor["gridNext"].setBinding(nextBuffer));
        REQUIRE_CALL(cursor["gridGradients"].setBinding(gradientBuffer));
        REQUIRE_CALL(cursor["validation"].setBinding(valuesBuffer));
        pass->dispatchCompute((MARBLE_BATCH_SIZE + 31) / 32, 1, 1);
        pass->end();
        auto commands = encoder->finish();
        REQUIRE(commands);
        REQUIRE_CALL(queue->submit(commands));
        REQUIRE_CALL(queue->waitOnHost());
    };
    dispatch(backward);
    std::vector<uint32_t> heads(entryCount), next(nodeCount);
    std::vector<float> gradients(nodeCount * 2);
    REQUIRE_CALL(device->readBuffer(headsBuffer, 0, heads.size() * 4, heads.data()));
    REQUIRE_CALL(device->readBuffer(nextBuffer, 0, next.size() * 4, next.data()));
    REQUIRE_CALL(device->readBuffer(gradientBuffer, 0, gradients.size() * 4, gradients.data()));
    std::vector<bool> visited(nodeCount, false);
    std::vector<double> analytic(features.size(), 0.0);
    for (uint32_t entry = 0; entry < entryCount; ++entry)
    {
        for (uint32_t link = heads[entry]; link; link = next[link - 1])
        {
            REQUIRE(link <= nodeCount);
            REQUIRE(!visited[link - 1]);
            visited[link - 1] = true;
            for (uint32_t f = 0; f < 2; ++f)
                analytic[2 * entry + f] += gradients[2 * (link - 1) + f] / MARBLE_BATCH_SIZE;
        }
    }
    CHECK(
        std::all_of(
            visited.begin(),
            visited.end(),
            [](bool value)
            {
                return value;
            }
        )
    );
    auto evaluate = [&]()
    {
        dispatch(forward);
        std::vector<float> values(MARBLE_BATCH_SIZE);
        REQUIRE_CALL(device->readBuffer(valuesBuffer, 0, values.size() * 4, values.data()));
        double sum = 0;
        for (float value : values)
            sum += value;
        return sum / values.size();
    };
    // Check both features at a dense and a heavily collided entry per level.
    for (uint32_t level = 0; level < MARBLE_GRID_LEVELS; ++level)
        for (uint32_t f = 0; f < 2; ++f)
        {
            uint32_t index = 2 * levels[4 * level + 1] + f;
            float original = features[index];
            const float epsilon = 0.005f;
            features[index] = original + epsilon;
            double plus = evaluate();
            features[index] = original - epsilon;
            double minus = evaluate();
            features[index] = original;
            double numerical = (plus - minus) / (2 * epsilon);
            CAPTURE(level);
            CAPTURE(f);
            CAPTURE(numerical);
            CAPTURE(analytic[index]);
            CHECK(std::abs(numerical - analytic[index]) < 1e-5);
        }
}

GPU_TEST_CASE("neural-marble-learning", D3D12 | Vulkan | Metal | CUDA | WGPU | DontCreateDevice)
{
    std::string shaderPath = std::string(getTestsDir()) + "/../examples/neural-marble";
    DeviceExtraOptions options;
    options.searchPaths.push_back(shaderPath.c_str());
    auto testDevice = createTestingDevice(ctx, ctx->deviceType, false, &options);
    device = testDevice;
    REQUIRE(device);
    neural_marble::Trainer trainer;
    REQUIRE_CALL(trainer.init(device));
    trainer.setCooperative(false);
    auto queue = device->getQueue(QueueType::Graphics);

    auto submit = [&](ICommandEncoder* encoder)
    {
        ComPtr<ICommandBuffer> commands;
        REQUIRE_CALL(encoder->finish(commands.writeRef()));
        REQUIRE_CALL(queue->submit(commands));
        REQUIRE_CALL(queue->waitOnHost());
    };
    auto measure = [&]()
    {
        auto encoder = queue->createCommandEncoder();
        REQUIRE_CALL(trainer.validate(encoder));
        submit(encoder);
        std::vector<float> values(MARBLE_VALIDATION_SIZE * MARBLE_VALIDATION_SIZE);
        REQUIRE_CALL(device->readBuffer(trainer.validation(), 0, values.size() * sizeof(float), values.data()));
        double sum = 0;
        for (float value : values)
        {
            REQUIRE(std::isfinite(value));
            sum += value;
        }
        return float(sum / values.size());
    };
    auto train = [&](uint32_t count)
    {
        // Bound each submission, including when tests use a software adapter.
        for (uint32_t i = 0; i < count; i += 16)
        {
            auto encoder = queue->createCommandEncoder();
            for (uint32_t j = i; j < std::min(i + 16, count); ++j)
                REQUIRE_CALL(trainer.step(encoder));
            submit(encoder);
        }
    };
    auto readWeights = [&]()
    {
        std::vector<float> values(MARBLE_PARAMETER_COUNT);
        REQUIRE_CALL(device->readBuffer(trainer.weights(), 0, values.size() * sizeof(float), values.data()));
        return values;
    };
    auto readGrid = [&]()
    {
        std::vector<float> values(trainer.gridParameterCount());
        REQUIRE_CALL(device->readBuffer(trainer.gridFeatures(), 0, values.size() * sizeof(float), values.data()));
        return values;
    };

    float initial = measure();
    auto initialWeights = readWeights();
    auto initialGrid = readGrid();
    train(256);
    float learned = measure();
    CAPTURE(initial);
    CAPTURE(learned);
    CHECK(learned < initial * 0.25f);
    CHECK(learned < 0.001f);
    CHECK(trainer.stepCount() == 256);

    auto trainedWeights = readWeights();
    auto trainedGrid = readGrid();
    CHECK(trainedGrid != initialGrid);
    trainer.nextTarget();
    CHECK(readWeights() == trainedWeights);
    CHECK(readGrid() == trainedGrid);
    CHECK(trainer.stepCount() == 256);
    float changed = measure();
    CHECK(changed > learned);
    train(128);
    float adapted = measure();
    MESSAGE(
        "MSE: initial=",
        initial,
        ", learned=",
        learned,
        ", new target=",
        changed,
        ", adapted=",
        adapted,
        "; cooperative vectors=",
        trainer.cooperativeSupported()
    );
    CAPTURE(changed);
    CAPTURE(adapted);
    CHECK(adapted < changed);

    if (trainer.cooperativeSupported())
    {
        auto beforeSwitch = readWeights();
        auto gridBeforeSwitch = readGrid();
        uint32_t steps = trainer.stepCount();
        trainer.setCooperative(true);
        float cooperative = measure();
        CHECK(std::abs(cooperative - adapted) < 0.003f);
        CHECK(readWeights() == beforeSwitch);
        CHECK(readGrid() == gridBeforeSwitch);
        CHECK(trainer.stepCount() == steps);
        train(32);
        float cooperativeLearned = measure();
        CHECK(cooperativeLearned < cooperative);
        CHECK(trainer.stepCount() == steps + 32);
        trainer.setCooperative(false);
        CHECK(std::abs(measure() - cooperativeLearned) < 0.003f);
    }

    auto encoder = queue->createCommandEncoder();
    REQUIRE_CALL(trainer.reset(encoder));
    submit(encoder);
    CHECK(trainer.stepCount() == 0);
    CHECK(readWeights() == initialWeights);
    CHECK(readGrid() == initialGrid);
    uint32_t seed = trainer.seed();
    train(1);
    auto firstStep = readWeights();
    auto firstGridStep = readGrid();
    encoder = queue->createCommandEncoder();
    REQUIRE_CALL(trainer.reset(encoder));
    submit(encoder);
    CHECK(trainer.seed() == seed);
    train(1);
    CHECK(readWeights() == firstStep); // Adam moments and step were reset too.
    auto repeatedGridStep = readGrid();
    // The sparse lists can sum shared-entry gradients in a different order.
    for (size_t i = 0; i < firstGridStep.size(); ++i)
        CHECK(std::abs(firstGridStep[i] - repeatedGridStep[i]) < 1e-6f);

    for (int target = 0; target < 2; ++target)
    {
        trainer.nextTarget();
        encoder = queue->createCommandEncoder();
        REQUIRE_CALL(trainer.reset(encoder));
        submit(encoder);
        train(512);
        float error = measure();
        MESSAGE("Grid convergence: seed=", trainer.seed(), ", steps=512, MSE=", error);
        CHECK(error < 0.001f);
    }
}

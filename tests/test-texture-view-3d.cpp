// UnorderedAccess tests create textures with unique values at each pixel and then sets up textureViews on a subrange
// of the texture. The textureViews are bound to shaders that read values off the subregion and stores them in a
// buffer. The buffer output is then compared to the expected values for that region to verify that the textureView
// was created and bound correctly. Tests should not result in API errors etc.

// TODO: Implement RenderTarget tests.
// RenderTarget tests should clear a render target texture or texture array and all mip levels to some default. Then
// a specific region should be setup in a textureView such that this region can be cleared to a non default color
// to verify correctness.

// TODO: Implement additional tests for various TextureType's

#include "testing.h"
#include "../src/core/string.h"
#include <map>
#include <algorithm>

using namespace rhi;
using namespace rhi::testing;

// Do we need to clean anything up like created resources?

struct TestTextureViews
{
    ComPtr<IDevice> device;
    std::map<std::string, ComPtr<IComputePipeline>> cachedPipelines;

    void init(IDevice* device_) { this->device = device_; }

    ComPtr<IBuffer> createResultBuffer(int size)
    {
        BufferDesc bufferDesc = {};
        bufferDesc.size = size;
        bufferDesc.format = Format::R32Float;
        bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        bufferDesc.defaultState = ResourceState::UnorderedAccess;
        bufferDesc.memoryType = MemoryType::DeviceLocal;

        ComPtr<IBuffer> resultBuffer = device->createBuffer(bufferDesc);
        return resultBuffer;
    }

    ComPtr<ITextureView> createTextureAndTextureView(
        TextureType textureType,
        TextureUsage usage,
        uint32_t mipCount,
        Extent3D textureSize,
        SubresourceRange textureViewRange,
        SubresourceData* data
    )
    {
        TextureDesc texDesc = {};
        texDesc.type = textureType;
        texDesc.mipCount = mipCount;
        texDesc.size = textureSize;
        texDesc.usage = usage;
        texDesc.defaultState =
            usage == TextureUsage::UnorderedAccess ? ResourceState::UnorderedAccess : ResourceState::RenderTarget;
        texDesc.format = Format::R32Float; // Assuming Format::R32Float until there are test that require something
                                           // different

        ComPtr<ITexture> texture;
        REQUIRE_CALL(device->createTexture(texDesc, data, texture.writeRef()));

        ComPtr<ITextureView> view;
        TextureViewDesc viewDesc = {};
        viewDesc.format = Format::R32Float;
        viewDesc.subresourceRange = textureViewRange;
        REQUIRE_CALL(device->createTextureView(texture, viewDesc, view.writeRef()));
        return view;
    }

    void testTextureViewUnorderedAccess(
        TextureType textureType,
        uint32_t mipCount,
        Extent3D textureSize,
        SubresourceRange textureViewRange,
        SubresourceData* textureData
    )
    {
        ComPtr<ITextureView> textureView = createTextureAndTextureView(
            textureType,
            TextureUsage::UnorderedAccess,
            mipCount,
            textureSize,
            textureViewRange,
            textureData
        );

        // Create result buffer
        Extent3D textureViewSize = {
            std::max(textureSize.width >> textureViewRange.mip, 1u),
            std::max(textureSize.height >> textureViewRange.mip, 1u),
            std::max(textureSize.depth >> textureViewRange.mip, 1u)
        };
        // In bytes
        int dataLength =
            textureViewSize.width * textureViewSize.height * textureViewSize.depth * sizeof(float); // Assuming
                                                                                                    // Format::R32Float
        ComPtr<IBuffer> resultBuffer = createResultBuffer(dataLength);

        // Do setup work ...

        // Use this as a default until we run other tests
        std::string entryPointName = "testRWTex3DViewFloat";

        ComPtr<IComputePipeline>& pipeline = cachedPipelines[entryPointName.c_str()];
        if (!pipeline)
        {
            ComPtr<IShaderProgram> shaderProgram;
            REQUIRE_CALL(loadProgram(device, "test-texture-view-3d", entryPointName.c_str(), shaderProgram.writeRef()));

            ComputePipelineDesc pipelineDesc = {};
            pipelineDesc.program = shaderProgram.get();
            REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        }

        // We have done all the set up work, now it is time to start recording a command buffer for
        // GPU execution.
        {
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor cursor(rootObject->getEntryPoint(0)); // get a cursor to the first entry-point.
            // Bind texture view to the entry point
            cursor["tex"].setBinding(textureView);

            // Bind buffer view to the entry point.
            cursor["buffer"].setBinding(resultBuffer);

            // Dispatch compute shader with thread groups matching the dimensions of the textureView
            // as the basic test shader runs 1x1x1 threads per group for easy texture sampling.
            passEncoder->dispatchCompute(textureViewSize.width, textureViewSize.height, textureViewSize.depth);
            passEncoder->end();

            queue->submit(commandEncoder->finish());
            queue->waitOnHost();
        }

        // Check results

        // Read back the results.
        ComPtr<ISlangBlob> bufferData;
        REQUIRE(!SLANG_FAILED(device->readBuffer(resultBuffer, 0, dataLength, bufferData.writeRef())));
        REQUIRE_EQ(bufferData->getBufferSize(), dataLength);
        const float* result = reinterpret_cast<const float*>(bufferData->getBufferPointer());
        const float* expectedResult = reinterpret_cast<const float*>(textureData[textureViewRange.mip].data);

        // We need to divide data length by sizeof(float) as the compare function
        // does not compare on bytes.
        compareResultFuzzy(result, expectedResult, dataLength / sizeof(float));
    }

    void run()
    {


        // Test a texture view for a 3D RW texture
        {
            // Enough space for a 16x16x16 Format::R32Float Texture3D and it's mip levels
            float texData[4681] = {}; // 16^3 + 8^3 + ... + 1^3

            // Populate it such that every element has a unique value.
            // That will let us verify correct sampling of sub regions.
            for (int i = 0; i < 4681; i++)
            {
                texData[i] = (float)i;
            }

            // Our SubresourceData array needs enough elements for each mip level
            // 16x16x16 -> 8x8x8 -> ... -> 1x1x1
            SubresourceData subData[5];
            // SubresourceData expects strides to be in bytes, x4 since we are using Format::R32Float here
            // Should probably use sizeof
            subData[0] = {(void*)texData, 16 * 4, 16 * 16 * 4};
            subData[1] = {(void*)&texData[4096], 8 * 4, 8 * 8 * 4};
            subData[2] = {(void*)&texData[4608], 4 * 4, 4 * 4 * 4};
            subData[3] = {(void*)&texData[4672], 2 * 4, 2 * 2 * 4};
            subData[4] = {(void*)&texData[4680], 1 * 4, 1 * 1 * 4};

            TextureType type = TextureType::Texture3D;
            // Texture size
            Extent3D size = {16 /*width*/, 16 /*height*/, 16 /*depth*/};
            // This subrange/textureView will give a 8x8x8 texture and verifies a fix for issue #220
            // We use 3 for layer as this was previously used for FirstWSlice and we want
            // to verify that selecting a subset of depth slices is not currently supported.
            SubresourceRange range = {3 /*layer*/, 1 /*layerCount*/, 1 /*mip*/, 4 /*mipCount*/};
            testTextureViewUnorderedAccess(type, 5 /*mipCount*/, size, range, subData);
        }
    }
};

GPU_TEST_CASE("texture-view-3d", D3D12 | Vulkan | CUDA)
{
    TestTextureViews test;
    test.init(device);
    test.run();
}

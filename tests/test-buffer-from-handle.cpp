#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

static void dispatchIncrement(IDevice* device, IComputePipeline* pipeline, IBuffer* buffer)
{
    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();

    auto passEncoder = commandEncoder->beginComputePass();
    auto rootObject = passEncoder->bindPipeline(pipeline);
    ShaderCursor(rootObject)["buffer"].setBinding(buffer);
    passEncoder->dispatchCompute(1, 1, 1);
    passEncoder->end();

    queue->submit(commandEncoder->finish());
    queue->waitOnHost();
}

GPU_TEST_CASE("buffer-from-handle", D3D12 | Vulkan | Metal | CUDA | WGPU)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-trivial", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4;
    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> originalNumbersBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, originalNumbersBuffer.writeRef()));

    NativeHandle handle;
    REQUIRE_CALL(originalNumbersBuffer->getNativeHandle(&handle));

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBufferFromNativeHandle(handle, bufferDesc, buffer.writeRef()));
    compareComputeResult(device, buffer, makeArray<float>(0.0f, 1.0f, 2.0f, 3.0f));

    dispatchIncrement(device, pipeline, buffer);
    compareComputeResult(device, buffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));

    compareComputeResult(device, originalNumbersBuffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));

    // Destroy a wrapper created from the native handle, then keep using the original buffer.
    {
        buffer.setNull();

        {
            ComPtr<IBuffer> scopedWrapper;
            REQUIRE_CALL(device->createBufferFromNativeHandle(handle, bufferDesc, scopedWrapper.writeRef()));
            dispatchIncrement(device, pipeline, scopedWrapper);
            compareComputeResult(device, scopedWrapper, makeArray<float>(2.0f, 3.0f, 4.0f, 5.0f));
        }

        compareComputeResult(device, originalNumbersBuffer, makeArray<float>(2.0f, 3.0f, 4.0f, 5.0f));

        dispatchIncrement(device, pipeline, originalNumbersBuffer);
        compareComputeResult(device, originalNumbersBuffer, makeArray<float>(3.0f, 4.0f, 5.0f, 6.0f));
    }

    // Invalid native handles should fail without producing a wrapper object.
    {
        NativeHandle wrongTypeHandle = handle;
        wrongTypeHandle.type = NativeHandleType::Undefined;

        ComPtr<IBuffer> invalidBuffer;
        CHECK_EQ(
            device->createBufferFromNativeHandle(wrongTypeHandle, bufferDesc, invalidBuffer.writeRef()),
            SLANG_E_INVALID_HANDLE
        );
        CHECK_EQ(invalidBuffer.get(), nullptr);

        NativeHandle zeroValueHandle = handle;
        zeroValueHandle.value = 0;

        CHECK_EQ(
            device->createBufferFromNativeHandle(zeroValueHandle, bufferDesc, invalidBuffer.writeRef()),
            SLANG_E_INVALID_HANDLE
        );
        CHECK_EQ(invalidBuffer.get(), nullptr);
    }
}

// shader-slang/slang-rhi#860: an imported (native-handle) buffer has no per-buffer Vulkan API
// pointer, so binding it as a typed/texel view (which requires a VkBufferView) must not crash when
// the view is created or when the wrapper is destroyed.
GPU_TEST_CASE("buffer-from-handle-typed-view", D3D12 | Vulkan)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-buffer-from-handle", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4;
    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::R32Float; // typed buffer: prerequisite for a texel buffer view
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> originalBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, originalBuffer.writeRef()));

    NativeHandle handle;
    REQUIRE_CALL(originalBuffer->getNativeHandle(&handle));

    // Binding the imported wrapper as RWBuffer<float> forces creation of a texel VkBufferView.
    {
        ComPtr<IBuffer> imported;
        REQUIRE_CALL(device->createBufferFromNativeHandle(handle, bufferDesc, imported.writeRef()));
        dispatchIncrement(device, pipeline, imported);
        compareComputeResult(device, imported, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
    }

    // `imported` and `originalBuffer` alias the same device memory, so the increment above persists
    // and the counts accumulate to {2,3,4,5}. Releasing the wrapper queued its VkBufferView for
    // destruction; this second dispatch's waitOnHost flushes the deferred-delete queue, running
    // ~BufferImpl (the destroy path) while the device is still alive.
    dispatchIncrement(device, pipeline, originalBuffer);
    compareComputeResult(device, originalBuffer, makeArray<float>(2.0f, 3.0f, 4.0f, 5.0f));
}

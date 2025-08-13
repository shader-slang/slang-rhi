#include "webgpu-input-layout.h"
#include "webgpu-device.h"
#include "webgpu-utils.h"

namespace rhi::webgpu {

Result DeviceImpl::createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout)
{
    RefPtr<InputLayoutImpl> layout = new InputLayoutImpl();
    layout->m_device = this;
    layout->m_vertexBufferLayouts.resize(desc.vertexStreamCount);
    layout->m_vertexAttributes.resize(desc.vertexStreamCount);

    for (uint32_t i = 0; i < desc.inputElementCount; ++i)
    {
        const InputElementDesc& elementDesc = desc.inputElements[i];
        if (elementDesc.bufferSlotIndex >= desc.vertexStreamCount)
        {
            return SLANG_FAIL;
        }

        WebGPUVertexAttribute vertexAttribute = {};
        vertexAttribute.format = translateVertexFormat(elementDesc.format);
        vertexAttribute.offset = elementDesc.offset;
        // TODO determine shader location from name
        vertexAttribute.shaderLocation = i;

        layout->m_vertexAttributes[elementDesc.bufferSlotIndex].push_back(vertexAttribute);
    }

    for (uint32_t i = 0; i < desc.vertexStreamCount; ++i)
    {
        const VertexStreamDesc& streamDesc = desc.vertexStreams[i];
        WebGPUVertexBufferLayout& bufferLayout = layout->m_vertexBufferLayouts[i];

        bufferLayout.arrayStride = streamDesc.stride;
        bufferLayout.stepMode =
            streamDesc.slotClass == InputSlotClass::PerVertex ? WebGPUVertexStepMode_Vertex : WebGPUVertexStepMode_Instance;
        bufferLayout.attributes = layout->m_vertexAttributes[i].data();
        bufferLayout.attributeCount = layout->m_vertexAttributes[i].size();
    }

    returnComPtr(outLayout, layout);
    return SLANG_OK;
}

} // namespace rhi::webgpu

#include "wgpu-input-layout.h"
#include "wgpu-device.h"
#include "wgpu-util.h"

namespace rhi::wgpu {

Result DeviceImpl::createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout)
{
    RefPtr<InputLayoutImpl> layout = new InputLayoutImpl();
    layout->m_device = this;
    layout->m_vertexBufferLayouts.resize(desc.vertexStreamCount);
    layout->m_vertexAttributes.resize(desc.vertexStreamCount);

    for (GfxIndex i = 0; i < desc.inputElementCount; ++i)
    {
        const InputElementDesc& elementDesc = desc.inputElements[i];
        if (elementDesc.bufferSlotIndex >= desc.vertexStreamCount)
        {
            return SLANG_FAIL;
        }

        WGPUVertexAttribute vertexAttribute = {};
        vertexAttribute.format = translateVertexFormat(elementDesc.format);
        vertexAttribute.offset = elementDesc.offset;
        // TODO determine shader location from name
        vertexAttribute.shaderLocation = 0;

        layout->m_vertexAttributes[elementDesc.bufferSlotIndex].push_back(vertexAttribute);
    }

    size_t attributeIndex = 0;

    for (GfxIndex i = 0; i < desc.vertexStreamCount; ++i)
    {
        const VertexStreamDesc& streamDesc = desc.vertexStreams[i];
        WGPUVertexBufferLayout& bufferLayout = layout->m_vertexBufferLayouts[i];

        bufferLayout.arrayStride = streamDesc.stride;
        bufferLayout.stepMode =
            streamDesc.slotClass == InputSlotClass::PerVertex ? WGPUVertexStepMode_Vertex : WGPUVertexStepMode_Instance;
        bufferLayout.attributes = layout->m_vertexAttributes[i].data();
        bufferLayout.attributeCount = layout->m_vertexAttributes[i].size();
    }

    returnComPtr(outLayout, layout);
    return SLANG_OK;
}

} // namespace rhi::wgpu

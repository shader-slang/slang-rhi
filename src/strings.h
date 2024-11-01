#pragma once

#define S_INVALID "invalid"

// ----------------------------------------------------------------------------
// Enums
// ----------------------------------------------------------------------------

// DeviceType
#define S_DeviceType_Default "Default"
#define S_DeviceType_D3D11 "D3D11"
#define S_DeviceType_D3D12 "D3D12"
#define S_DeviceType_Vulkan "Vulkan"
#define S_DeviceType_Metal "Metal"
#define S_DeviceType_CPU "CPU"
#define S_DeviceType_CUDA "CUDA"

// FormatSupport
#define S_FormatSupport_None "None"
#define S_FormatSupport_Buffer "Buffer"
#define S_FormatSupport_IndexBuffer "IndexBuffer"
#define S_FormatSupport_VertexBuffer "VertexBuffer"
#define S_FormatSupport_Texture "Texture"
#define S_FormatSupport_DepthStencil "DepthStencil"
#define S_FormatSupport_RenderTarget "RenderTarget"
#define S_FormatSupport_Blendable "Blendable"
#define S_FormatSupport_ShaderLoad "ShaderLoad"
#define S_FormatSupport_ShaderSample "ShaderSample"
#define S_FormatSupport_ShaderUavLoad "ShaderUavLoad"
#define S_FormatSupport_ShaderUavStore "ShaderUavStore"
#define S_FormatSupport_ShaderAtomic "ShaderAtomic"

// MemoryType
#define S_MemoryType_DeviceLocal "DeviceLocal"
#define S_MemoryType_Upload "Upload"
#define S_MemoryType_ReadBack "ReadBack"

// BufferUsage
#define S_BufferUsage_None "None"
#define S_BufferUsage_VertexBuffer "VertexBuffer"
#define S_BufferUsage_IndexBuffer "IndexBuffer"
#define S_BufferUsage_ConstantBuffer "ConstantBuffer"
#define S_BufferUsage_ShaderResource "ShaderResource"
#define S_BufferUsage_UnorderedAccess "UnorderedAccess"
#define S_BufferUsage_IndirectArgument "IndirectArgument"
#define S_BufferUsage_CopySource "CopySource"
#define S_BufferUsage_CopyDestination "CopyDestination"
#define S_BufferUsage_AccelerationStructure "AccelerationStructure"
#define S_BufferUsage_AccelerationStructureBuildInput "AccelerationStructureBuildInput"
#define S_BufferUsage_ShaderTable "ShaderTable"

// TextureType
#define S_TextureType_Texture1D "Texture1D"
#define S_TextureType_Texture2D "Texture2D"
#define S_TextureType_Texture3D "Texture3D"
#define S_TextureType_TextureCube "TextureCube"

// TextureUsage
#define S_TextureUsage_None "None"
#define S_TextureUsage_ShaderResource "ShaderResource"
#define S_TextureUsage_UnorderedAccess "UnorderedAccess"
#define S_TextureUsage_RenderTarget "RenderTarget"
#define S_TextureUsage_DepthRead "DepthRead"
#define S_TextureUsage_DepthWrite "DepthWrite"
#define S_TextureUsage_Present "Present"
#define S_TextureUsage_CopySource "CopySource"
#define S_TextureUsage_CopyDestination "CopyDestination"
#define S_TextureUsage_ResolveSource "ResolveSource"
#define S_TextureUsage_ResolveDestination "ResolveDestination"

// ResourceState
#define S_ResourceState_Undefined "Undefined"
#define S_ResourceState_General "General"
#define S_ResourceState_VertexBuffer "VertexBuffer"
#define S_ResourceState_IndexBuffer "IndexBuffer"
#define S_ResourceState_ConstantBuffer "ConstantBuffer"
#define S_ResourceState_StreamOutput "StreamOutput"
#define S_ResourceState_ShaderResource "ShaderResource"
#define S_ResourceState_UnorderedAccess "UnorderedAccess"
#define S_ResourceState_RenderTarget "RenderTarget"
#define S_ResourceState_DepthRead "DepthRead"
#define S_ResourceState_DepthWrite "DepthWrite"
#define S_ResourceState_Present "Present"
#define S_ResourceState_IndirectArgument "IndirectArgument"
#define S_ResourceState_CopySource "CopySource"
#define S_ResourceState_CopyDestination "CopyDestination"
#define S_ResourceState_ResolveSource "ResolveSource"
#define S_ResourceState_ResolveDestination "ResolveDestination"
#define S_ResourceState_AccelerationStructure "AccelerationStructure"
#define S_ResourceState_AccelerationStructureBuildInput "AccelerationStructureBuildInput"

// TextureFilteringMode
#define S_TextureFilteringMode_Point "Point"
#define S_TextureFilteringMode_Linear "Linear"

// TextureAddressingMode
#define S_TextureAddressingMode_Wrap "Wrap"
#define S_TextureAddressingMode_ClampToEdge "ClampToEdge"
#define S_TextureAddressingMode_ClampToBorder "ClampToBorder"
#define S_TextureAddressingMode_MirrorRepeat "MirrorRepeat"
#define S_TextureAddressingMode_MirrorOnce "MirrorOnce"

// ComparisonFunc
#define S_ComparisonFunc_Never "Never"
#define S_ComparisonFunc_Less "Less"
#define S_ComparisonFunc_Equal "Equal"
#define S_ComparisonFunc_LessEqual "LessEqual"
#define S_ComparisonFunc_Greater "Greater"
#define S_ComparisonFunc_NotEqual "NotEqual"
#define S_ComparisonFunc_GreaterEqual "GreaterEqual"
#define S_ComparisonFunc_Always "Always"

// TextureReductionOp
#define S_TextureReductionOp_Average "Average"
#define S_TextureReductionOp_Comparison "Comparison"
#define S_TextureReductionOp_Minimum "Minimum"
#define S_TextureReductionOp_Maximum "Maximum"

// InputSlotClass
#define S_InputSlotClass_PerVertex "PerVertex"
#define S_InputSlotClass_PerInstance "PerInstance"

// PrimitiveTopology
#define S_PrimitiveTopology_PointList "PointList"
#define S_PrimitiveTopology_LineList "LineList"
#define S_PrimitiveTopology_LineStrip "LineStrip"
#define S_PrimitiveTopology_TriangleList "TriangleList"
#define S_PrimitiveTopology_TriangleStrip "TriangleStrip"
#define S_PrimitiveTopology_PatchList "PatchList"

// ----------------------------------------------------------------------------
// Functions
// ----------------------------------------------------------------------------

// CommandEncoder
#define S_CommandEncoder "CommandEncoder"
#define S_CommandEncoder_copyBuffer "copyBuffer"
#define S_CommandEncoder_copyTexture "copyTexture"
#define S_CommandEncoder_copyTextureToBuffer "copyTextureToBuffer"
#define S_CommandEncoder_clearBuffer "clearBuffer"
#define S_CommandEncoder_clearTexture "clearTexture"
#define S_CommandEncoder_uploadTextureData "uploadTextureData"
#define S_CommandEncoder_uploadBufferData "uploadBufferData"
#define S_CommandEncoder_resolveQuery "resolveQuery"
#define S_CommandEncoder_beginRenderPass "beginRenderPass"
#define S_CommandEncoder_endRenderPass "endRenderPass"
#define S_CommandEncoder_setRenderState "setRenderState"
#define S_CommandEncoder_draw "draw"
#define S_CommandEncoder_drawIndexed "drawIndexed"
#define S_CommandEncoder_drawIndirect "drawIndirect"
#define S_CommandEncoder_drawIndexedIndirect "drawIndexedIndirect"
#define S_CommandEncoder_drawMeshTasks "drawMeshTasks"
#define S_CommandEncoder_setComputeState "setComputeState"
#define S_CommandEncoder_dispatchCompute "dispatchCompute"
#define S_CommandEncoder_dispatchComputeIndirect "dispatchComputeIndirect"
#define S_CommandEncoder_setRayTracingState "setRayTracingState"
#define S_CommandEncoder_dispatchRays "dispatchRays"
#define S_CommandEncoder_buildAccelerationStructure "buildAccelerationStructure"
#define S_CommandEncoder_copyAccelerationStructure "copyAccelerationStructure"
#define S_CommandEncoder_queryAccelerationStructureProperties "queryAccelerationStructureProperties"
#define S_CommandEncoder_serializeAccelerationStructure "serializeAccelerationStructure"
#define S_CommandEncoder_deserializeAccelerationStructure "deserializeAccelerationStructure"
#define S_CommandEncoder_setBufferState "setBufferState"
#define S_CommandEncoder_setTextureState "setTextureState"
#define S_CommandEncoder_beginDebugEvent "beginDebugEvent"
#define S_CommandEncoder_endDebugEvent "endDebugEvent"
#define S_CommandEncoder_writeTimestamp "writeTimestamp"
#define S_CommandEncoder_executeCallback "executeCallback"

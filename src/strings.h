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
#define S_DeviceType_WGPU "WGPU"

// FormatSupport
#define S_FormatSupport_None "None"
#define S_FormatSupport_CopySource "CopySource"
#define S_FormatSupport_CopyDestination "CopyDestination"
#define S_FormatSupport_Texture "Texture"
#define S_FormatSupport_DepthStencil "DepthStencil"
#define S_FormatSupport_RenderTarget "RenderTarget"
#define S_FormatSupport_Blendable "Blendable"
#define S_FormatSupport_Multisampling "Multisampling"
#define S_FormatSupport_Resolvable "Resolvable"
#define S_FormatSupport_ShaderLoad "ShaderLoad"
#define S_FormatSupport_ShaderSample "ShaderSample"
#define S_FormatSupport_ShaderUavLoad "ShaderUavLoad"
#define S_FormatSupport_ShaderUavStore "ShaderUavStore"
#define S_FormatSupport_ShaderAtomic "ShaderAtomic"
#define S_FormatSupport_Buffer "Buffer"
#define S_FormatSupport_IndexBuffer "IndexBuffer"
#define S_FormatSupport_VertexBuffer "VertexBuffer"

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
#define S_BufferUsage_Shared "Shared"

// TextureType
#define S_TextureType_Texture1D "Texture1D"
#define S_TextureType_Texture1DArray "Texture1DArray"
#define S_TextureType_Texture2D "Texture2D"
#define S_TextureType_Texture2DArray "Texture2DArray"
#define S_TextureType_Texture2DMS "Texture2DMS"
#define S_TextureType_Texture2DMSArray "Texture2DMSArray"
#define S_TextureType_Texture3D "Texture3D"
#define S_TextureType_TextureCube "TextureCube"
#define S_TextureType_TextureCubeArray "TextureCubeArray"

// TextureAspect
#define S_TextureAspect_All "All"
#define S_TextureAspect_DepthOnly "DepthOnly"
#define S_TextureAspect_StencilOnly "StencilOnly"

// TextureUsage
#define S_TextureUsage_None "None"
#define S_TextureUsage_ShaderResource "ShaderResource"
#define S_TextureUsage_UnorderedAccess "UnorderedAccess"
#define S_TextureUsage_RenderTarget "RenderTarget"
#define S_TextureUsage_DepthStencil "DepthStencil"
#define S_TextureUsage_Present "Present"
#define S_TextureUsage_CopySource "CopySource"
#define S_TextureUsage_CopyDestination "CopyDestination"
#define S_TextureUsage_ResolveSource "ResolveSource"
#define S_TextureUsage_ResolveDestination "ResolveDestination"
#define S_TextureUsage_Typeless "Typeless"
#define S_TextureUsage_Shared "Shared"

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
#define S_ResourceState_AccelerationStructureRead "AccelerationStructureRead"
#define S_ResourceState_AccelerationStructureWrite "AccelerationStructureWrite"
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

// QueryType
#define S_QueryType_Timestamp "Timestamp"
#define S_QueryType_AccelerationStructureCompactedSize "AccelerationStructureCompactedSize"
#define S_QueryType_AccelerationStructureSerializedSize "AccelerationStructureSerializedSize"
#define S_QueryType_AccelerationStructureCurrentSize "AccelerationStructureCurrentSize"

// CooperativeVectorComponentType
#define S_CooperativeVectorComponentType_Float16 "Float16"
#define S_CooperativeVectorComponentType_Float32 "Float32"
#define S_CooperativeVectorComponentType_Float64 "Float64"
#define S_CooperativeVectorComponentType_Sint8 "Sint8"
#define S_CooperativeVectorComponentType_Sint16 "Sint16"
#define S_CooperativeVectorComponentType_Sint32 "Sint32"
#define S_CooperativeVectorComponentType_Sint64 "Sint64"
#define S_CooperativeVectorComponentType_Uint8 "Uint8"
#define S_CooperativeVectorComponentType_Uint16 "Uint16"
#define S_CooperativeVectorComponentType_Uint32 "Uint32"
#define S_CooperativeVectorComponentType_Uint64 "Uint64"
#define S_CooperativeVectorComponentType_Sint8Packed "Sint8Packed"
#define S_CooperativeVectorComponentType_Uint8Packed "Uint8Packed"
#define S_CooperativeVectorComponentType_FloatE4M3 "FloatE4M3"
#define S_CooperativeVectorComponentType_FloatE5M2 "FloatE5M2"

// CooperativeVectorMatrixLayout
#define S_CooperativeVectorMatrixLayout_RowMajor "RowMajor"
#define S_CooperativeVectorMatrixLayout_ColumnMajor "ColumnMajor"
#define S_CooperativeVectorMatrixLayout_InferencingOptimal "InferencingOptimal"
#define S_CooperativeVectorMatrixLayout_TrainingOptimal "TrainingOptimal"

// DebugMessageType
#define S_DebugMessageType_Info "Info"
#define S_DebugMessageType_Warning "Warning"
#define S_DebugMessageType_Error "Error"

// DebugMessageSource
#define S_DebugMessageSource_Layer "Layer"
#define S_DebugMessageSource_Driver "Driver"
#define S_DebugMessageSource_Slang "Slang"

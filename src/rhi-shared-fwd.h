#pragma once

#include <cstdint>

namespace rhi {

class DeviceChild;
class AccelerationStructure;
class Buffer;
class Resource;
class Texture;
class QueryPool;
class InputLayout;
class ShaderTable;
class Heap;

class CommandBuffer;
class CommandEncoder;
class ComputePassEncoder;
class RayTracingPassEncoder;

class CommandList;

class Device;

class ShaderObject;
class RootShaderObject;
struct ShaderObjectID;
struct ResourceSlot;
struct ExtendedShaderObjectType;
struct ExtendedShaderObjectTypeList;
class ExtendedShaderObjectTypeListObject;
class ShaderObjectLayout;

class Pipeline;
class RenderPipeline;
class VirtualRenderPipeline;
class ComputePipeline;
class VirtualComputePipeline;
class RayTracingPipeline;
class VirtualRayTracingPipeline;

struct SpecializationKey;
class ShaderProgram;
class ShaderCompilationReporter;

typedef uint32_t ShaderComponentID;

} // namespace rhi

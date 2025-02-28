#pragma once

namespace rhi {

class AccelerationStructure;
class Buffer;
class Resource;
class Texture;

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

class ShaderProgram;

class QueryPool;
class Pipeline;
class RenderPipeline;
class ComputePipeline;
class RayTracingPipeline;

typedef uint32_t ShaderComponentID;
} // namespace rhi

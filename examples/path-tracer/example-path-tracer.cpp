#include "example-base.h"
#include "camera.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;

#define USE_RAYTRACING_PIPELINE 0

inline float3 randomFloat3()
{
    return float3(std::rand(), std::rand(), std::rand()) * (1.f / float(RAND_MAX));
}

struct Material
{
    float3 baseColor;

    Material(float3 baseColor = float3(0.5))
        : baseColor(baseColor)
    {
    }
};

struct Mesh
{
    struct Vertex
    {
        float3 position;
        float3 normal;
        float2 uv;
    };
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices)
        : vertices(std::move(vertices))
        , indices(std::move(indices))
    {
    }

    uint32_t vertexCount() const { return (uint32_t)(vertices.size()); }
    uint32_t indexCount() const { return (uint32_t)(indices.size()); }

    static Mesh createQuad(float2 size = float2(1))
    {
        std::vector<Vertex> vertices{
            // position, normal, uv
            {{-0.5, 0, -0.5}, {0, 1, 0}, {0, 0}},
            {{+0.5, 0, -0.5}, {0, 1, 0}, {1, 0}},
            {{-0.5, 0, +0.5}, {0, 1, 0}, {0, 1}},
            {{+0.5, 0, +0.5}, {0, 1, 0}, {1, 1}},
        };
        for (auto& vertex : vertices)
        {
            vertex.position *= float3(size.x, 0.f, size.y);
        }
        std::vector<uint32_t> indices{
            2,
            1,
            0,
            1,
            2,
            3,
        };
        return Mesh(vertices, indices);
    }

    static Mesh createCube(float3 size = float3(1))
    {
        std::vector<Vertex> vertices{
            // position, normal, uv
            // left
            {{-0.5, -0.5, -0.5}, {0, -1, 0}, {0, 0}},
            {{-0.5, -0.5, +0.5}, {0, -1, 0}, {1, 0}},
            {{+0.5, -0.5, +0.5}, {0, -1, 0}, {1, 1}},
            {{+0.5, -0.5, -0.5}, {0, -1, 0}, {0, 1}},
            // right
            {{-0.5, +0.5, +0.5}, {0, +1, 0}, {0, 0}},
            {{-0.5, +0.5, -0.5}, {0, +1, 0}, {1, 0}},
            {{+0.5, +0.5, -0.5}, {0, +1, 0}, {1, 1}},
            {{+0.5, +0.5, +0.5}, {0, +1, 0}, {0, 1}},
            // back
            {{-0.5, +0.5, -0.5}, {0, 0, -1}, {0, 0}},
            {{-0.5, -0.5, -0.5}, {0, 0, -1}, {1, 0}},
            {{+0.5, -0.5, -0.5}, {0, 0, -1}, {1, 1}},
            {{+0.5, +0.5, -0.5}, {0, 0, -1}, {0, 1}},
            // front
            {{+0.5, +0.5, +0.5}, {0, 0, +1}, {0, 0}},
            {{+0.5, -0.5, +0.5}, {0, 0, +1}, {1, 0}},
            {{-0.5, -0.5, +0.5}, {0, 0, +1}, {1, 1}},
            {{-0.5, +0.5, +0.5}, {0, 0, +1}, {0, 1}},
            // bottom
            {{-0.5, +0.5, +0.5}, {-1, 0, 0}, {0, 0}},
            {{-0.5, -0.5, +0.5}, {-1, 0, 0}, {1, 0}},
            {{-0.5, -0.5, -0.5}, {-1, 0, 0}, {1, 1}},
            {{-0.5, +0.5, -0.5}, {-1, 0, 0}, {0, 1}},
            // top
            {{+0.5, +0.5, -0.5}, {+1, 0, 0}, {0, 0}},
            {{+0.5, -0.5, -0.5}, {+1, 0, 0}, {1, 0}},
            {{+0.5, -0.5, +0.5}, {+1, 0, 0}, {1, 1}},
            {{+0.5, +0.5, +0.5}, {+1, 0, 0}, {0, 1}},
        };
        for (auto& vertex : vertices)
        {
            vertex.position *= size;
        }
        std::vector<uint32_t> indices{
            0,  2,  1,  0,  3,  2,  4,  6,  5,  4,  7,  6,  8,  10, 9,  8,  11, 10,
            12, 14, 13, 12, 15, 14, 16, 18, 17, 16, 19, 18, 20, 22, 21, 20, 23, 22,
        };
        return Mesh(vertices, indices);
    }
};

struct Transform
{
    float3 translation{0.f};
    float3 scaling{1.f};
    float3 rotation{0.f};
    float4x4 matrix;

    void updateMatrix()
    {
        float4x4 T = translation_matrix(translation);
        float4x4 S = scaling_matrix(scaling);
        float4x4 R = linalg::identity; // TODO support rotation
        matrix = mul(mul(T, R), S);
    }
};

struct Stage
{
    Camera camera;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Transform> transforms;
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> instances;

    uint32_t addMaterial(const Material& material)
    {
        uint32_t materialID = uint32_t(materials.size());
        materials.push_back(material);
        return materialID;
    }

    uint32_t addMesh(const Mesh& mesh)
    {
        uint32_t meshID = uint32_t(meshes.size());
        meshes.push_back(mesh);
        return meshID;
    }

    uint32_t addTransform(const Transform& transform)
    {
        uint32_t transformID = uint32_t(transforms.size());
        transforms.push_back(transform);
        return transformID;
    }

    uint32_t addInstance(uint32_t meshID, uint32_t materialID, uint32_t transformID)
    {
        uint32_t instanceID = uint32_t(instances.size());
        instances.push_back(std::make_tuple(meshID, materialID, transformID));
        return instanceID;
    }

    void initialize()
    {
        camera.target = float3(0, 1, 0);
        camera.position = float3(2, 1, 2);

        uint32_t floorMaterial = addMaterial(Material(float3(0.5)));
        uint32_t floorMesh = addMesh(Mesh::createQuad(float2(5, 5)));
        uint32_t floorTransform = addTransform(Transform());
        addInstance(floorMesh, floorMaterial, floorTransform);

        std::vector<uint32_t> cubeMaterials;
        for (uint32_t i = 0; i < 10; ++i)
            cubeMaterials.push_back(addMaterial(Material(randomFloat3())));
        uint32_t cubeMesh = addMesh(Mesh::createCube(float3(0.1)));
        for (uint32_t i = 0; i < 1000; ++i)
        {
            Transform transform;
            transform.translation = randomFloat3() * 2.f - 1.f;
            transform.translation.y += 1;
            transform.scaling = randomFloat3() + 0.5f;
            transform.rotation = randomFloat3() * 10.f;
            transform.updateMatrix();
            uint32_t cubeTransform = addTransform(transform);
            addInstance(cubeMesh, cubeMaterials[i % cubeMaterials.size()], cubeTransform);
        }
    }
};

struct Scene
{
    struct MaterialDesc
    {
        float3 baseColor;
    };

    struct MeshDesc
    {
        uint32_t vertexCount;
        uint32_t indexCount;
        uint32_t vertexOffset;
        uint32_t indexOffset;
    };

    struct InstanceDesc
    {
        uint32_t meshID;
        uint32_t materialID;
        uint32_t transformID;
    };

    ComPtr<IDevice> device;
    // const Stage& stage;
    // const Camera& camera;

    std::vector<MaterialDesc> materialDescs;
    ComPtr<IBuffer> materialDescsBuffer;
    std::vector<MeshDesc> meshDescs;
    ComPtr<IBuffer> meshDescsBuffer;
    std::vector<InstanceDesc> instanceDescs;
    ComPtr<IBuffer> instanceDescsBuffer;
    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IBuffer> indexBuffer;
    std::vector<float4x4> transforms;
    std::vector<float4x4> inverseTransposeTransforms;
    ComPtr<IBuffer> transformsBuffer;
    ComPtr<IBuffer> inverseTransposeTransformsBuffer;
    std::vector<ComPtr<IAccelerationStructure>> blases;
    ComPtr<IAccelerationStructure> tlas;

    Result initialize(IDevice* device, const Stage& stage)
    {
        this->device = device;

        // Prepare material descriptors
        materialDescs.resize(stage.materials.size());
        for (size_t i = 0; i < stage.materials.size(); ++i)
        {
            materialDescs[i].baseColor = stage.materials[i].baseColor;
        }
        {
            BufferDesc desc = {};
            desc.size = materialDescs.size() * sizeof(MaterialDesc);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "materialDescsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, materialDescs.data(), materialDescsBuffer.writeRef()));
        }

        // Prepare mesh descriptors
        meshDescs.reserve(stage.meshes.size());
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        for (const Mesh& mesh : stage.meshes)
        {
            MeshDesc desc = {};
            desc.vertexCount = mesh.vertexCount();
            desc.indexCount = mesh.indexCount();
            desc.vertexOffset = vertexCount;
            desc.indexOffset = indexCount;
            meshDescs.push_back(desc);
            vertexCount += mesh.vertexCount();
            indexCount += mesh.indexCount();
        }

        // Prepare instance descriptors
        instanceDescs.reserve(stage.instances.size());
        for (const auto& [meshID, materialID, transformID] : stage.instances)
        {
            InstanceDesc desc = {};
            desc.meshID = meshID;
            desc.materialID = materialID;
            desc.transformID = transformID;
            instanceDescs.push_back(desc);
        }

        // Create vertex and index buffers
        std::vector<Mesh::Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(vertexCount);
        indices.reserve(indexCount);
        for (const Mesh& mesh : stage.meshes)
        {
            vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
        }
        {
            BufferDesc desc = {};
            desc.size = vertices.size() * sizeof(Mesh::Vertex);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "vertexBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, vertices.data(), vertexBuffer.writeRef()));
        }
        {
            BufferDesc desc = {};
            desc.size = indices.size() * sizeof(uint32_t);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "indexBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, indices.data(), indexBuffer.writeRef()));
        }
        {
            BufferDesc desc = {};
            desc.size = meshDescs.size() * sizeof(MeshDesc);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "meshDescsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, meshDescs.data(), meshDescsBuffer.writeRef()));
        }
        {
            BufferDesc desc = {};
            desc.size = instanceDescs.size() * sizeof(InstanceDesc);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "instanceDescsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, instanceDescs.data(), instanceDescsBuffer.writeRef()));
        }

        // Prepare transforms
        transforms.resize(stage.transforms.size());
        for (size_t i = 0; i < stage.transforms.size(); ++i)
        {
            transforms[i] = stage.transforms[i].matrix;
        }
        inverseTransposeTransforms.resize(transforms.size());
        for (size_t i = 0; i < transforms.size(); ++i)
        {
            inverseTransposeTransforms[i] = transpose(inverse(transforms[i]));
        }
        {
            BufferDesc desc = {};
            desc.size = transforms.size() * sizeof(float4x4);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "transformsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, transforms.data(), transformsBuffer.writeRef()));
        }
        {
            BufferDesc desc = {};
            desc.size = inverseTransposeTransforms.size() * sizeof(float4x4);
            desc.usage = BufferUsage::ShaderResource;
            desc.label = "inverseTransposeTransformsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(
                desc,
                inverseTransposeTransforms.data(),
                inverseTransposeTransformsBuffer.writeRef()
            ));
        }

        // Build BLASes
        for (const MeshDesc& meshDesc : meshDescs)
        {
            ComPtr<IAccelerationStructure> blas;
            SLANG_RETURN_ON_FAIL(buildBLAS(meshDesc, blas.writeRef()));
            blases.push_back(blas);
        }

        // Build TLAS
        SLANG_RETURN_ON_FAIL(buildTLAS(tlas.writeRef()));

        return SLANG_OK;
    }

    Result buildBLAS(const MeshDesc& meshDesc, IAccelerationStructure** outBLAS)
    {
        AccelerationStructureBuildInput buildInput = {};
        buildInput.type = AccelerationStructureBuildInputType::Triangles;
        AccelerationStructureBuildInputTriangles& triangles = buildInput.triangles;
        triangles.vertexBuffers[0] = BufferOffsetPair(vertexBuffer, meshDesc.vertexOffset * sizeof(Mesh::Vertex));
        triangles.vertexBufferCount = 1;
        triangles.vertexFormat = Format::RGB32Float;
        triangles.vertexCount = meshDesc.vertexCount;
        triangles.vertexStride = sizeof(Mesh::Vertex);
        triangles.indexBuffer = BufferOffsetPair(indexBuffer, meshDesc.indexOffset * sizeof(uint32_t));
        triangles.indexFormat = IndexFormat::Uint32;
        triangles.indexCount = meshDesc.indexCount;
        triangles.flags = AccelerationStructureGeometryFlags::Opaque;

        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = &buildInput;
        buildDesc.inputCount = 1;

        AccelerationStructureSizes sizes = {};
        SLANG_RETURN_ON_FAIL(device->getAccelerationStructureSizes(buildDesc, &sizes));

        AccelerationStructureDesc asDesc = {};
        asDesc.size = sizes.accelerationStructureSize;
        asDesc.label = "blas";
        SLANG_RETURN_ON_FAIL(device->createAccelerationStructure(asDesc, outBLAS));

        ComPtr<IBuffer> blasScratchBuffer;
        {
            BufferDesc desc = {};
            desc.size = sizes.scratchSize;
            desc.usage = BufferUsage::UnorderedAccess;
            desc.label = "blasScratchBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, nullptr, blasScratchBuffer.writeRef()));
        }

        ComPtr<ICommandQueue> queue;
        SLANG_RETURN_ON_FAIL(device->getQueue(QueueType::Graphics, queue.writeRef()));

        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(commandEncoder.writeRef()));
        commandEncoder->buildAccelerationStructure(buildDesc, *outBLAS, nullptr, blasScratchBuffer, 0, nullptr);
        ComPtr<ICommandBuffer> commandBuffer;
        SLANG_RETURN_ON_FAIL(commandEncoder->finish(commandBuffer.writeRef()));
        queue->submit(commandBuffer);

        return SLANG_OK;
    }

    Result buildTLAS(IAccelerationStructure** outTLAS)
    {
        std::vector<AccelerationStructureInstanceDescGeneric> instanceDescsGeneric(instanceDescs.size());
        for (size_t instanceID = 0; instanceID < instanceDescs.size(); ++instanceID)
        {
            const InstanceDesc& instanceDesc = instanceDescs[instanceID];
            AccelerationStructureInstanceDescGeneric& instanceDescGeneric = instanceDescsGeneric[instanceID];
            // instanceDescGeneric.transform = float3x4(transforms[instanceDesc.transformID]);
            instanceDescGeneric.instanceID = (uint32_t)instanceID;
            instanceDescGeneric.instanceMask = 0xFF;
            instanceDescGeneric.instanceContributionToHitGroupIndex = 0;
            instanceDescGeneric.flags = AccelerationStructureInstanceFlags::None;
            instanceDescGeneric.accelerationStructure = blases[instanceDesc.meshID]->getHandle();
        }

        AccelerationStructureInstanceDescType instanceDescType = getAccelerationStructureInstanceDescType(device);
        size_t instanceDescSize = getAccelerationStructureInstanceDescSize(instanceDescType);
        std::vector<uint8_t> instanceDescsData(instanceDescsGeneric.size() * instanceDescSize);
        convertAccelerationStructureInstanceDescs(
            instanceDescsGeneric.size(),
            instanceDescType,
            instanceDescsData.data(),
            instanceDescSize,
            instanceDescsGeneric.data(),
            getAccelerationStructureInstanceDescSize(AccelerationStructureInstanceDescType::Generic)
        );

        ComPtr<IBuffer> instanceDescsBuffer;
        {
            BufferDesc desc = {};
            desc.size = (uint32_t)instanceDescsData.size();
            desc.usage = BufferUsage::AccelerationStructureBuildInput;
            desc.label = "tlasInstanceDescsBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, instanceDescsData.data(), instanceDescsBuffer.writeRef()));
        }

        AccelerationStructureBuildInput buildInput = {};
        buildInput.type = AccelerationStructureBuildInputType::Instances;
        AccelerationStructureBuildInputInstances& instances = buildInput.instances;
        instances.instanceBuffer = instanceDescsBuffer;
        instances.instanceCount = (uint32_t)instanceDescsGeneric.size();
        instances.instanceStride = (uint32_t)instanceDescSize;

        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = &buildInput;
        buildDesc.inputCount = 1;

        AccelerationStructureSizes sizes = {};
        SLANG_RETURN_ON_FAIL(device->getAccelerationStructureSizes(buildDesc, &sizes));

        AccelerationStructureDesc asDesc = {};
        asDesc.size = sizes.accelerationStructureSize;
        asDesc.label = "tlas";
        SLANG_RETURN_ON_FAIL(device->createAccelerationStructure(asDesc, outTLAS));

        ComPtr<IBuffer> tlasScratchBuffer;
        {
            BufferDesc desc = {};
            desc.size = sizes.scratchSize;
            desc.usage = BufferUsage::UnorderedAccess;
            desc.label = "tlasScratchBuffer";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, nullptr, tlasScratchBuffer.writeRef()));
        }

        ComPtr<ICommandQueue> queue;
        SLANG_RETURN_ON_FAIL(device->getQueue(QueueType::Graphics, queue.writeRef()));

        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(commandEncoder.writeRef()));
        commandEncoder->buildAccelerationStructure(buildDesc, *outTLAS, nullptr, tlasScratchBuffer, 0, nullptr);
        ComPtr<ICommandBuffer> commandBuffer;
        SLANG_RETURN_ON_FAIL(commandEncoder->finish(commandBuffer.writeRef()));
        queue->submit(commandBuffer);

        return SLANG_OK;
    }

    void bind(ShaderCursor cursor) const
    {
        cursor["tlas"].setBinding(tlas);
        cursor["materialDescs"].setBinding(materialDescsBuffer);
        cursor["meshDescs"].setBinding(meshDescsBuffer);
        cursor["instanceDescs"].setBinding(instanceDescsBuffer);
        cursor["vertices"].setBinding(vertexBuffer);
        cursor["indices"].setBinding(indexBuffer);
        cursor["transforms"].setBinding(transformsBuffer);
        cursor["inverseTransposeTransforms"].setBinding(inverseTransposeTransformsBuffer);
        // camera.bind(cursor["camera"]);
    }
};

struct PathTracer
{
    ComPtr<IDevice> device;
    Scene* scene;
    ComPtr<IShaderProgram> program;
    ComPtr<IComputePipeline> computePipeline;
    ComPtr<IRayTracingPipeline> rayTracingPipeline;
    ComPtr<IShaderTable> shaderTable;

    Result initialize(IDevice* device, Scene* scene)
    {
        this->device = device;
        this->scene = scene;

        if (USE_RAYTRACING_PIPELINE)
        {
            // program = device->load_program("pathtracer.slang", {"rt_ray_gen", "rt_closest_hit", "rt_miss"});
            // rayTracingPipeline = device->create_ray_tracing_pipeline({
            //     .program = program,
            //     .hit_groups =
            //         {
            //             {
            //                 .hit_group_name = "default",
            //                 .closest_hit_entry_point = "rt_closest_hit",
            //             },
            //         },
            //     .max_recursion = 6,
            //     .max_ray_payload_size = 128,
            // });
            // shaderTable = device->create_shader_table({
            //     .program = program,
            //     .ray_gen_entry_points = {"rt_ray_gen"},
            //     .miss_entry_points = {"rt_miss"},
            //     .hit_group_names = {"default"},
            // });
        }
        else
        {
            SLANG_RETURN_ON_FAIL(
                createComputePipeline(device, "path-tracer.slang", "mainCompute", computePipeline.writeRef())
            );
        }

        return SLANG_OK;
    }

    Result execute(ICommandEncoder* commandEncoder, ITexture* output, uint32_t frame)
    {
        if (USE_RAYTRACING_PIPELINE)
        {
            // ref<RayTracingPassEncoder> passEncoder = commandEncoder->begin_ray_tracing_pass();
            // ShaderObject* shaderObject = passEncoder->bind_pipeline(rayTracingPipeline, shaderTable);
            // ShaderCursor cursor = ShaderCursor(shaderObject);
            // cursor["g_output"] = output;
            // cursor["g_frame"] = frame;
            // scene.bind(cursor["g_scene"]);
            // passEncoder->dispatch_rays(0, {output->width(), output->height(), 1});
            // passEncoder->end();
        }
        else
        {
            IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
            IShaderObject* shaderObject = passEncoder->bindPipeline(computePipeline);
            ShaderCursor cursor = ShaderCursor(shaderObject);
            scene->bind(cursor["g_scene"]);
            cursor = ShaderCursor(shaderObject->getEntryPoint(0));
            cursor["output"].setBinding(output);
            cursor["frame"].setData(frame);
            Extent3D size = output->getDesc().size;
            passEncoder->dispatchCompute(divRoundUp(size.width, 8u), divRoundUp(size.height, 8u), 1);
            passEncoder->end();
        }

        return SLANG_OK;
    }
};

struct Accumulator
{
    ComPtr<IDevice> device;
    ComPtr<IComputePipeline> pipeline;
    ComPtr<ITexture> accumulator;

    Result initialize(IDevice* device)
    {
        this->device = device;
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, "accumulator.slang", "mainCompute", pipeline.writeRef()));
        return SLANG_OK;
    }

    Result execute(ICommandEncoder* commandEncoder, ITexture* input, ITexture* output, bool reset = false)
    {
        if (!accumulator || accumulator->getDesc().size.width != input->getDesc().size.width ||
            accumulator->getDesc().size.height != input->getDesc().size.height)
        {
            TextureDesc desc = {};
            desc.type = TextureType::Texture2D;
            desc.size = input->getDesc().size;
            desc.format = Format::RGBA32Float;
            desc.usage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess;
            desc.label = "accumulator";
            SLANG_RETURN_ON_FAIL(device->createTexture(desc, nullptr, accumulator.writeRef()));
        }
        IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
        IShaderObject* shaderObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor = ShaderCursor(shaderObject->getEntryPoint(0));
        cursor["input"].setBinding(input);
        cursor["output"].setBinding(output);
        cursor["accumulator"].setBinding(accumulator);
        cursor["reset"].setData(reset ? 1u : 0u);
        Extent3D size = input->getDesc().size;
        passEncoder->dispatchCompute(divRoundUp(size.width, 8u), divRoundUp(size.height, 8u), 1);
        passEncoder->end();
        return SLANG_OK;
    }
};

struct ToneMapper
{
    ComPtr<IComputePipeline> pipeline;

    Result initialize(IDevice* device)
    {
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, "tone-mapper.slang", "mainCompute", pipeline.writeRef()));
        return SLANG_OK;
    }

    Result execute(ICommandEncoder* commandEncoder, ITexture* input, ITexture* output)
    {
        IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
        IShaderObject* shaderObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor = ShaderCursor(shaderObject->getEntryPoint(0));
        cursor["input"].setBinding(input);
        cursor["output"].setBinding(output);
        Extent3D size = input->getDesc().size;
        passEncoder->dispatchCompute(divRoundUp(size.width, 8u), divRoundUp(size.height, 8u), 1);
        passEncoder->end();
        return SLANG_OK;
    }
};

class ExamplePathTracer : public ExampleBase
{
public:
    static ExampleDesc getDesc()
    {
        ExampleDesc desc;
        desc.name = "PathTracer";
        desc.requireFeatures = {
            Feature::Surface,
            Feature::AccelerationStructure,
#if USE_RAYTRACING_PIPELINE
            Feature::RayTracing,
#else
            Feature::RayQuery,
#endif
        };
        return desc;
    }

    Result init() override
    {
        m_stage.initialize();
        SLANG_RETURN_ON_FAIL(m_scene.initialize(m_device, m_stage));
        m_cameraController.setCamera(&m_stage.camera);
        SLANG_RETURN_ON_FAIL(m_pathTracer.initialize(m_device, &m_scene));
        SLANG_RETURN_ON_FAIL(m_accumulator.initialize(m_device));
        SLANG_RETURN_ON_FAIL(m_toneMapper.initialize(m_device));
        return SLANG_OK;
    }

    virtual void shutdown() override {}

    virtual Result update() override
    {
        if (m_cameraController.update(m_timeDelta))
        {
            m_frame = 0;
        }
        return SLANG_OK;
    }

    virtual Result draw(ITexture* image) override
    {
        if (!m_renderTexture || m_renderTexture->getDesc().size.width != image->getDesc().size.width ||
            m_renderTexture->getDesc().size.height != image->getDesc().size.height)
        {
            TextureDesc desc = {};
            desc.type = TextureType::Texture2D;
            desc.size = image->getDesc().size;
            desc.format = Format::RGBA32Float;
            desc.usage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess;
            desc.label = "renderTexture";
            SLANG_RETURN_ON_FAIL(m_device->createTexture(desc, nullptr, m_renderTexture.writeRef()));
            desc.label = "accumTexture";
            SLANG_RETURN_ON_FAIL(m_device->createTexture(desc, nullptr, m_accumTexture.writeRef()));
            desc.label = "outputTexture";
            SLANG_RETURN_ON_FAIL(m_device->createTexture(desc, nullptr, m_outputTexture.writeRef()));
            m_frame = 0;
        }

        m_stage.camera.width = image->getDesc().size.width;
        m_stage.camera.height = image->getDesc().size.height;
        m_stage.camera.recompute();

        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(m_queue->createCommandEncoder(commandEncoder.writeRef()));

        SLANG_RETURN_ON_FAIL(m_pathTracer.execute(commandEncoder, m_renderTexture, m_frame));
        SLANG_RETURN_ON_FAIL(m_accumulator.execute(commandEncoder, m_renderTexture, m_accumTexture, m_frame == 0));
        SLANG_RETURN_ON_FAIL(m_toneMapper.execute(commandEncoder, m_accumTexture, m_outputTexture));
        SLANG_RETURN_ON_FAIL(blit(image, m_outputTexture, commandEncoder));

        ComPtr<ICommandBuffer> commandBuffer;
        SLANG_RETURN_ON_FAIL(commandEncoder->finish(commandBuffer.writeRef()));
        m_queue->submit(commandBuffer);

        return SLANG_OK;
    }

    virtual void onMousePosition(float x, float y) override { m_cameraController.onMousePosition(x, y); }

    virtual void onMouseButton(int button, int action, int mods) override
    {
        m_cameraController.onMouseButton(button, action, mods);
    }

    virtual void onKey(int key, int scancode, int action, int mods) override
    {
        m_cameraController.onKey(key, action, mods);
    }

    Stage m_stage;
    Scene m_scene;
    CameraController m_cameraController;
    PathTracer m_pathTracer;
    Accumulator m_accumulator;
    ToneMapper m_toneMapper;
    ComPtr<ITexture> m_renderTexture;
    ComPtr<ITexture> m_accumTexture;
    ComPtr<ITexture> m_outputTexture;
};

EXAMPLE_MAIN(ExamplePathTracer)

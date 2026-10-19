#include "../base/example.h"
#include "../base/camera.h"
#include "../base/geometry.h"
#include "settings.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;
using namespace rhi::math;

// These layouts match path-tracer.slang. The same records are used for
// authoring and upload; instance IDs index both shading data and TLAS instances.
struct Material
{
    float3 baseColor;
    uint32_t mirror = 0;
};

struct Instance
{
    float4x4 transform;
    float4x4 normalTransform;
    uint32_t meshID;
    uint32_t materialID;
    uint32_t padding[2] = {};
};

struct MeshDesc
{
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t vertexOffset;
    uint32_t indexOffset;
};

static_assert(sizeof(Material) == 16 && sizeof(Instance) == 144 && sizeof(MeshDesc) == 16);

struct SceneData
{
    std::vector<Material> materials;
    std::vector<geometry::Mesh> meshes;
    std::vector<Instance> instances;

    // Scene objects use positive scales and a rotation around the vertical axis.
    void addInstance(uint32_t mesh, uint32_t material, float3 position, float3 scale, float yaw = 0)
    {
        float4x4 rotation = rotationY(yaw);
        float4x4 transform = mul(translationMatrix(position), mul(rotation, scalingMatrix(scale)));
        float4x4 normalTransform = mul(rotation, scalingMatrix({1 / scale.x, 1 / scale.y, 1 / scale.z}));
        instances.push_back({transform, normalTransform, mesh, material});
    }

    static SceneData create()
    {
        SceneData scene;
        scene.materials = {
            {{0.55f, 0.52f, 0.46f}},    // Warm ground.
            {{0.75f, 0.16f, 0.045f}},   // Terracotta.
            {{0.045f, 0.38f, 0.42f}},   // Teal.
            {{0.72f, 0.65f, 0.48f}},    // Sand.
            {{0.94f, 0.96f, 0.98f}, 1}, // Silver mirror.
        };
        scene.meshes = {geometry::Mesh::createQuad(), geometry::Mesh::createCube()};
        scene.addInstance(0, 0, {0, 0, 0}, {500, 1, 500});

        // A small sculpture: large blocks make reflections and color bleeding easy to see.
        scene.addInstance(1, 4, {0, 0.9f, 0}, {1.1f, 1.8f, 1.1f}, 0.25f);
        scene.addInstance(1, 1, {-0.65f, 0.55f, 1.65f}, {1.1f, 1.1f, 1.1f}, -0.3f);
        scene.addInstance(1, 2, {1.45f, 0.4f, 0.9f}, {0.8f, 0.8f, 0.8f}, 0.35f);
        scene.addInstance(1, 3, {0.8f, 0.8f, -1.65f}, {0.75f, 1.6f, 0.75f}, -0.15f);
        scene.addInstance(1, 2, {-1.35f, 0.25f, -1.35f}, {1.2f, 0.5f, 0.75f}, 0.2f);
        scene.addInstance(1, 3, {-1.35f, 0.7f, -1.35f}, {0.65f, 0.4f, 0.65f}, -0.2f);

        // Reuse the cube BLAS for an arc of smaller blocks. A fixed seed keeps
        // the subtle size/rotation variations identical across backends.
        RNG rng;
        for (uint32_t i = 0; i < 13; ++i)
        {
            float angle = radians(15.f + 15.f * i);
            float size = 0.22f + 0.16f * rng.nextFloat();
            float yaw = rng.nextFloat() * kPI;
            scene.addInstance(
                1,
                1 + i % 4,
                {3.f * std::cos(angle), size * 0.5f, -3.f * std::sin(angle)},
                float3(size),
                yaw
            );
        }
        return scene;
    }
};

struct Scene
{
    ComPtr<IBuffer> materials;
    ComPtr<IBuffer> meshes;
    ComPtr<IBuffer> instances;
    ComPtr<IBuffer> vertices;
    ComPtr<IBuffer> indices;
    std::vector<ComPtr<IAccelerationStructure>> blases;
    ComPtr<IAccelerationStructure> tlas;

    // Both BLAS and TLAS builds follow the same allocate / record / submit sequence.
    static Result buildAccelerationStructure(
        IDevice* device,
        ICommandQueue* queue,
        const AccelerationStructureBuildInput& input,
        AccelerationStructureKind kind,
        IAccelerationStructure** result
    )
    {
        AccelerationStructureBuildDesc build = {};
        build.inputs = &input;
        build.inputCount = 1;
        AccelerationStructureSizes sizes = {};
        SLANG_RETURN_ON_FAIL(device->getAccelerationStructureSizes(build, &sizes));

        AccelerationStructureDesc desc = {};
        desc.kind = kind;
        desc.size = sizes.accelerationStructureSize;
        desc.label = kind == AccelerationStructureKind::BottomLevel ? "blas" : "tlas";
        SLANG_RETURN_ON_FAIL(device->createAccelerationStructure(desc, result));

        BufferDesc scratchDesc = {};
        scratchDesc.size = sizes.scratchSize;
        scratchDesc.usage = BufferUsage::UnorderedAccess;
        scratchDesc.label = "buildScratch";
        ComPtr<IBuffer> scratch;
        SLANG_RETURN_ON_FAIL(device->createBuffer(scratchDesc, nullptr, scratch.writeRef()));
        ComPtr<ICommandEncoder> encoder;
        SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(encoder.writeRef()));
        encoder->buildAccelerationStructure(build, *result, nullptr, scratch, 0, nullptr);
        ComPtr<ICommandBuffer> commands;
        SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
        return queue->submit(commands);
    }

    Result initialize(IDevice* device, ICommandQueue* queue, const SceneData& scene)
    {
        std::vector<MeshDesc> meshDescs;
        std::vector<geometry::Vertex> vertexData;
        std::vector<uint32_t> indexData;
        for (const auto& mesh : scene.meshes)
        {
            meshDescs.push_back(
                {mesh.vertexCount(), mesh.indexCount(), uint32_t(vertexData.size()), uint32_t(indexData.size())}
            );
            vertexData.insert(vertexData.end(), mesh.vertices.begin(), mesh.vertices.end());
            indexData.insert(indexData.end(), mesh.indices.begin(), mesh.indices.end());
        }
        BufferUsage geometryUsage = BufferUsage::ShaderResource | BufferUsage::AccelerationStructureBuildInput;
        BufferUsage shaderUsage = BufferUsage::ShaderResource;
        SLANG_RETURN_ON_FAIL(
            uploadBuffer<geometry::Vertex>(device, vertexData, geometryUsage, "vertices", vertices.writeRef())
        );
        SLANG_RETURN_ON_FAIL(uploadBuffer<uint32_t>(device, indexData, geometryUsage, "indices", indices.writeRef()));
        SLANG_RETURN_ON_FAIL(
            uploadBuffer<Material>(device, scene.materials, shaderUsage, "materials", materials.writeRef())
        );
        SLANG_RETURN_ON_FAIL(uploadBuffer<MeshDesc>(device, meshDescs, shaderUsage, "meshes", meshes.writeRef()));
        SLANG_RETURN_ON_FAIL(
            uploadBuffer<Instance>(device, scene.instances, shaderUsage, "instances", instances.writeRef())
        );

        // One BLAS per mesh, shared by every instance of that mesh.
        for (const MeshDesc& mesh : meshDescs)
        {
            AccelerationStructureBuildInput input = {};
            input.type = AccelerationStructureBuildInputType::Triangles;
            auto& triangles = input.triangles;
            triangles.vertexBuffers[0] = BufferOffsetPair(vertices, mesh.vertexOffset * sizeof(geometry::Vertex));
            triangles.vertexBufferCount = 1;
            triangles.vertexFormat = Format::RGB32Float;
            triangles.vertexCount = mesh.vertexCount;
            triangles.vertexStride = sizeof(geometry::Vertex);
            triangles.indexBuffer = BufferOffsetPair(indices, mesh.indexOffset * sizeof(uint32_t));
            triangles.indexFormat = IndexFormat::Uint32;
            triangles.indexCount = mesh.indexCount;
            triangles.flags = AccelerationStructureGeometryFlags::Opaque;
            ComPtr<IAccelerationStructure> blas;
            SLANG_RETURN_ON_FAIL(buildAccelerationStructure(
                device,
                queue,
                input,
                AccelerationStructureKind::BottomLevel,
                blas.writeRef()
            ));
            blases.push_back(blas);
        }

        // Translate portable instance descriptors into this backend's native layout.
        std::vector<AccelerationStructureInstanceDescGeneric> nativeInstances(scene.instances.size());
        for (size_t i = 0; i < scene.instances.size(); ++i)
        {
            const Instance& instance = scene.instances[i];
            auto& native = nativeInstances[i];
            for (size_t row = 0; row < 3; ++row)
                for (size_t column = 0; column < 4; ++column)
                    native.transform[row][column] = instance.transform[row][column];
            native.instanceID = uint32_t(i);
            native.instanceMask = 0xFF;
            native.accelerationStructure = blases[instance.meshID]->getHandle();
        }
        auto instanceType = getAccelerationStructureInstanceDescType(device);
        size_t instanceStride = getAccelerationStructureInstanceDescSize(instanceType);
        std::vector<uint8_t> nativeData(nativeInstances.size() * instanceStride);
        convertAccelerationStructureInstanceDescs(
            nativeInstances.size(),
            instanceType,
            nativeData.data(),
            instanceStride,
            nativeInstances.data(),
            sizeof(AccelerationStructureInstanceDescGeneric)
        );
        ComPtr<IBuffer> instanceBuffer;
        SLANG_RETURN_ON_FAIL(
            uploadBuffer<uint8_t>(
                device,
                nativeData,
                BufferUsage::AccelerationStructureBuildInput,
                "tlasInstances",
                instanceBuffer.writeRef()
            )
        );
        AccelerationStructureBuildInput input = {};
        input.type = AccelerationStructureBuildInputType::Instances;
        input.instances.instanceBuffer = instanceBuffer;
        input.instances.instanceCount = uint32_t(nativeInstances.size());
        input.instances.instanceStride = uint32_t(instanceStride);
        return buildAccelerationStructure(device, queue, input, AccelerationStructureKind::TopLevel, tlas.writeRef());
    }

    Result bind(ShaderCursor cursor) const
    {
        SLANG_RETURN_ON_FAIL(cursor["tlas"].setBinding(tlas));
        SLANG_RETURN_ON_FAIL(cursor["materials"].setBinding(materials));
        SLANG_RETURN_ON_FAIL(cursor["meshes"].setBinding(meshes));
        SLANG_RETURN_ON_FAIL(cursor["instances"].setBinding(instances));
        SLANG_RETURN_ON_FAIL(cursor["vertices"].setBinding(vertices));
        return cursor["indices"].setBinding(indices);
    }
};

struct PathTracer
{
    enum class Mode
    {
        RayPipeline,
        RayQuery
    };
    Mode mode = Mode::RayPipeline;
    ComPtr<IComputePipeline> computePipeline;
    ComPtr<IRayTracingPipeline> rayTracingPipeline;
    ComPtr<IShaderTable> shaderTable;

    bool canSwitchMode() const { return computePipeline && rayTracingPipeline; }
    const char* modeName() const { return mode == Mode::RayPipeline ? "Ray pipeline" : "Ray query"; }
    void toggleMode()
    {
        if (canSwitchMode())
            mode = mode == Mode::RayPipeline ? Mode::RayQuery : Mode::RayPipeline;
    }

    Result initialize(IDevice* device)
    {
        if (device->hasFeature(Feature::RayTracing))
        {
            ComPtr<IShaderProgram> program;
            SLANG_RETURN_ON_FAIL(createProgram(
                device,
                "path-tracer.slang",
                {"renderRaygen", "mainClosestHit", "mainMiss"},
                program.writeRef()
            ));
            HitGroupDesc hitGroup = {};
            hitGroup.hitGroupName = "default";
            hitGroup.closestHitEntryPoint = "mainClosestHit";
            RayTracingPipelineDesc desc = {};
            desc.program = program;
            desc.hitGroupCount = 1;
            desc.hitGroups = &hitGroup;
            // Raygen iterates over bounces; hit shaders only return a 16-byte hit record.
            desc.maxRecursion = 1;
            desc.maxRayPayloadSize = 16;
            desc.maxAttributeSizeInBytes = 8;
            SLANG_RETURN_ON_FAIL(device->createRayTracingPipeline(desc, rayTracingPipeline.writeRef()));
            const char* raygen = "renderRaygen";
            const char* miss = "mainMiss";
            const char* hit = "default";
            ShaderTableDesc table = {};
            table.rayGenShaderCount = 1;
            table.rayGenShaderEntryPointNames = &raygen;
            table.missShaderCount = 1;
            table.missShaderEntryPointNames = &miss;
            table.hitGroupCount = 1;
            table.hitGroupNames = &hit;
            table.program = program;
            SLANG_RETURN_ON_FAIL(device->createShaderTable(table, shaderTable.writeRef()));
        }
        if (device->hasFeature(Feature::RayQuery))
            SLANG_RETURN_ON_FAIL(
                createComputePipeline(device, "path-tracer.slang", "renderCompute", computePipeline.writeRef())
            );
        if (!rayTracingPipeline && !computePipeline)
            return SLANG_E_NOT_AVAILABLE;
        mode = rayTracingPipeline ? Mode::RayPipeline : Mode::RayQuery;
        return SLANG_OK;
    }

    Result execute(
        ICommandEncoder* encoder,
        const Scene& scene,
        const Camera& camera,
        ITexture* output,
        uint32_t frame,
        uint32_t maxBounces
    )
    {
        IRayTracingPassEncoder* rayPass = nullptr;
        IComputePassEncoder* computePass = nullptr;
        IShaderObject* root;
        if (mode == Mode::RayPipeline)
        {
            rayPass = encoder->beginRayTracingPass();
            root = rayPass->bindPipeline(rayTracingPipeline, shaderTable);
        }
        else
        {
            computePass = encoder->beginComputePass();
            root = computePass->bindPipeline(computePipeline);
        }
        IPassEncoder* timingPass = rayPass ? static_cast<IPassEncoder*>(rayPass) : computePass;
        Profiler::pushRegion(timingPass, modeName());
        SLANG_RETURN_ON_FAIL(scene.bind(ShaderCursor(root)["g_scene"]));
        ShaderCursor cursor(root->getEntryPoint(0));
        SLANG_RETURN_ON_FAIL(cursor["camera"]["position"].setData(camera.position));
        SLANG_RETURN_ON_FAIL(cursor["camera"]["imageU"].setData(camera.imageU));
        SLANG_RETURN_ON_FAIL(cursor["camera"]["imageV"].setData(camera.imageV));
        SLANG_RETURN_ON_FAIL(cursor["camera"]["imageW"].setData(camera.imageW));
        SLANG_RETURN_ON_FAIL(cursor["output"].setBinding(output));
        SLANG_RETURN_ON_FAIL(cursor["frame"].setData(frame));
        SLANG_RETURN_ON_FAIL(cursor["maxBounces"].setData(maxBounces));
        auto size = output->getDesc().size;
        if (rayPass)
        {
            rayPass->dispatchRays(0, size.width, size.height, 1);
        }
        else
        {
            computePass->dispatchCompute(divRoundUp(size.width, 8u), divRoundUp(size.height, 8u), 1);
        }
        Profiler::popRegion(timingPass);
        timingPass->end();
        return SLANG_OK;
    }
};

struct Accumulator
{
    ComPtr<IDevice> device;
    ComPtr<IComputePipeline> pipeline;
    ComPtr<ITexture> accumulator;

    Result initialize(IDevice* device_)
    {
        device = device_;
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, "path-tracer.slang", "accumulate", pipeline.writeRef()));
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
            reset = true;
        }
        IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
        Profiler::pushRegion(passEncoder, "Accumulation");
        IShaderObject* shaderObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor = ShaderCursor(shaderObject->getEntryPoint(0));
        SLANG_RETURN_ON_FAIL(cursor["input"].setBinding(input));
        SLANG_RETURN_ON_FAIL(cursor["output"].setBinding(output));
        SLANG_RETURN_ON_FAIL(cursor["accumulator"].setBinding(accumulator));
        SLANG_RETURN_ON_FAIL(cursor["reset"].setData(reset ? 1u : 0u));
        Extent3D size = input->getDesc().size;
        passEncoder->dispatchCompute(divRoundUp(size.width, 8u), divRoundUp(size.height, 8u), 1);
        Profiler::popRegion(passEncoder);
        passEncoder->end();
        return SLANG_OK;
    }
};

struct ToneMapper
{
    ComPtr<IComputePipeline> pipeline;

    Result initialize(IDevice* device)
    {
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, "path-tracer.slang", "tonemap", pipeline.writeRef()));
        return SLANG_OK;
    }

    Result execute(ICommandEncoder* commandEncoder, ITexture* input, ITexture* output)
    {
        IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
        Profiler::pushRegion(passEncoder, "Tone map");
        IShaderObject* shaderObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor = ShaderCursor(shaderObject->getEntryPoint(0));
        SLANG_RETURN_ON_FAIL(cursor["input"].setBinding(input));
        SLANG_RETURN_ON_FAIL(cursor["output"].setBinding(output));
        Extent3D size = input->getDesc().size;
        passEncoder->dispatchCompute(divRoundUp(size.width, 8u), divRoundUp(size.height, 8u), 1);
        Profiler::popRegion(passEncoder);
        passEncoder->end();
        return SLANG_OK;
    }
};

class ExamplePathTracer : public ExampleBase
{
public:
    Result init(DeviceType deviceType) override
    {
        SLANG_RETURN_ON_FAIL(
            createDevice(deviceType, {Feature::Surface, Feature::AccelerationStructure}, {}, m_device.writeRef())
        );
        if (!m_device->hasFeature(Feature::RayTracing) && !m_device->hasFeature(Feature::RayQuery))
        {
            m_missingFeatures = {Feature::RayTracing, Feature::RayQuery};
            return SLANG_E_NOT_AVAILABLE;
        }
        SLANG_RETURN_ON_FAIL(createWindow(m_device, "Path tracer"));
        SLANG_RETURN_ON_FAIL(createSurface(m_device, Format::Undefined, m_surface.writeRef()));
        SLANG_RETURN_ON_FAIL(initOverlay(m_device, m_surface->getConfig()->format));

        SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));

        m_blitter = std::make_unique<Blitter>(m_device);

        SLANG_RETURN_ON_FAIL(m_scene.initialize(m_device, m_queue, SceneData::create()));
        resetCamera();
        m_cameraController.setCamera(&m_camera);
        SLANG_RETURN_ON_FAIL(m_pathTracer.initialize(m_device));
        SLANG_RETURN_ON_FAIL(m_accumulator.initialize(m_device));
        SLANG_RETURN_ON_FAIL(m_toneMapper.initialize(m_device));
        SLANG_RETURN_ON_FAIL(initProfiler(m_device));
        return SLANG_OK;
    }

    virtual void shutdown() override
    {
        if (m_queue)
            m_queue->waitOnHost();
        m_queue.setNull();
        m_blitter.reset();
        m_surface.setNull();
        m_device.setNull();
    }

    virtual Result update(double time) override
    {
        float dt = m_time < 0 ? 0.f : float(std::clamp(time - m_time, 0.0, 0.1));
        m_time = time;
        if (m_cameraController.update(dt))
        {
            m_frame = 0;
        }
        return SLANG_OK;
    }

    virtual Result draw() override
    {
        // Skip rendering if surface is not configured (eg. when window is minimized)
        if (!m_surface->getConfig())
        {
            return SLANG_OK;
        }

        // Acquire next image from the surface
        ComPtr<ITexture> image;
        // A native resize can invalidate the swapchain before its queued callback.
        m_surface->acquireNextImage(image.writeRef());
        if (!image)
        {
            return SLANG_OK;
        }

        uint32_t width = image->getDesc().size.width;
        uint32_t height = image->getDesc().size.height;

        // Create or resize auxiliary textures if needed
        if (!m_renderTexture || m_renderTexture->getDesc().size.width != width ||
            m_renderTexture->getDesc().size.height != height)
        {
            TextureDesc desc = {};
            desc.type = TextureType::Texture2D;
            desc.size = {width, height, 1};
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

        m_camera.width = width;
        m_camera.height = height;
        m_camera.recompute();

        // Start command encoding
        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(m_queue->createCommandEncoder(commandEncoder.writeRef()));
        auto profile = getProfiler().frame();
        SLANG_RETURN_ON_FAIL(profile.begin());

        SLANG_RETURN_ON_FAIL(
            m_pathTracer.execute(commandEncoder, m_scene, m_camera, m_renderTexture, m_frame, m_maxBounces)
        );
        SLANG_RETURN_ON_FAIL(
            m_accumulator.execute(commandEncoder, m_renderTexture, m_accumTexture, m_frame == 0 || !m_accumulate)
        );
        SLANG_RETURN_ON_FAIL(m_toneMapper.execute(commandEncoder, m_accumTexture, m_outputTexture));

        // Blit result to the surface image
        SLANG_RETURN_ON_FAIL(m_blitter->blit(image, m_outputTexture, commandEncoder));

        clearOverlay();
        printOverlayHeader();
        printCameraControls();
        printOverlay("T: Tracing mode | [/]: Bounces | Space: Accumulation | R: Reset view\n\n");
        printOverlay(
            "%s%s | Bounces: %u | Accumulation: %s\n",
            m_pathTracer.modeName(),
            m_pathTracer.canSwitchMode() ? "" : " (only supported mode)",
            m_maxBounces,
            m_accumulate ? "on" : "off"
        );
        printOverlay(
            "Samples/pixel: %llu",
            static_cast<unsigned long long>(m_accumulate ? uint64_t(m_frame) + 1 : 1) * PATH_TRACER_SAMPLES_PER_PIXEL
        );
        SLANG_RETURN_ON_FAIL(drawOverlay(commandEncoder, image));

        // Submit command buffer
        ComPtr<ICommandBuffer> commandBuffer;
        SLANG_RETURN_ON_FAIL(commandEncoder->finish(commandBuffer.writeRef()));
        SLANG_RETURN_ON_FAIL(profile.end());
        SLANG_RETURN_ON_FAIL(m_queue->submit(commandBuffer));
        profile.markSubmitted();

        m_frame += 1;

        // Present the surface
        return presentSurface();
    }

    virtual Result onResize(int width, int height, int framebufferWidth, int framebufferHeight) override
    {
        // Wait for GPU to be idle before resizing
        SLANG_RETURN_ON_FAIL(m_queue->waitOnHost());
        // Configure or unconfigure the surface based on the new framebuffer size
        if (framebufferWidth > 0 && framebufferHeight > 0)
        {
            SurfaceConfig surfaceConfig;
            surfaceConfig.width = framebufferWidth;
            surfaceConfig.height = framebufferHeight;
            return configureSurface(m_surface, surfaceConfig);
        }
        else
        {
            return m_surface->unconfigure();
        }
    }

    virtual void onMousePosition(float x, float y) override { m_cameraController.onMousePosition(x, y); }

    virtual void onMouseButton(int button, int action, int mods) override
    {
        m_cameraController.onMouseButton(button, action);
    }

    virtual void onKey(int key, int scancode, int action, int mods) override
    {
        if (m_cameraController.onKey(key, action))
            return;
        if (action == GLFW_PRESS)
        {
            switch (key)
            {
            case GLFW_KEY_T:
                m_pathTracer.toggleMode();
                m_frame = 0;
                break;
            case GLFW_KEY_LEFT_BRACKET:
                m_maxBounces = std::max(1u, m_maxBounces - 1);
                m_frame = 0;
                break;
            case GLFW_KEY_RIGHT_BRACKET:
                m_maxBounces = std::min(uint32_t(PATH_TRACER_MAX_BOUNCES), m_maxBounces + 1);
                m_frame = 0;
                break;
            case GLFW_KEY_SPACE:
                m_accumulate = !m_accumulate;
                m_frame = 0;
                break;
            case GLFW_KEY_R:
                resetCamera();
                break;
            default:
                break;
            }
        }
    }

    void resetCamera()
    {
        m_camera.position = {5.f, 3.3f, 6.f};
        m_camera.target = {0, 0.65f, -0.4f};
        m_camera.up = {0, 1, 0};
        m_camera.fov = 45.f;
        m_camera.recompute();
        m_cameraController.reset();
        m_frame = 0;
    }

    ComPtr<IDevice> m_device;
    ComPtr<ISurface> m_surface;
    ComPtr<ICommandQueue> m_queue;
    std::unique_ptr<Blitter> m_blitter;

    Camera m_camera;
    Scene m_scene;
    CameraController m_cameraController;
    PathTracer m_pathTracer;
    Accumulator m_accumulator;
    ToneMapper m_toneMapper;
    ComPtr<ITexture> m_renderTexture;
    ComPtr<ITexture> m_accumTexture;
    ComPtr<ITexture> m_outputTexture;

    double m_time = -1.0;
    uint32_t m_maxBounces = PATH_TRACER_DEFAULT_BOUNCES;
    bool m_accumulate = true;
    uint32_t m_frame = 0;
};

EXAMPLE_MAIN(ExamplePathTracer)

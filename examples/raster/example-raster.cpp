// Rasterization example: a Slang logo and instanced material spheres lit by a
// directional light and procedural environments. The frame records a shadow pass,
// an HDR scene with optional 4x MSAA, compute bloom, and fullscreen tone mapping.
// Shader cursors bind resources and parameters; debug groups label the GPU passes.
// Fly the camera with mouse and keyboard, or drag to move the light. The overlay lists
// the remaining controls; F1 toggles it.

#include "../base/example.h"
#include "../base/logo.h"
#include "../base/camera.h"
#include "../base/geometry.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <vector>

using namespace rhi;

struct Material
{
    math::float3 baseColor;
    float metallic;
    float roughness;
    uint32_t groundPattern = 0;
};

struct Scene
{
    std::vector<geometry::Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<logo::MeshRange> meshes;
    std::vector<Material> materials;
};

Scene createScene();

// Startup-only preprocessing. Compute writes ordinary array textures, then copies
// the faces to sampled cubemaps, including on backends without cube UAV views.
class Environment
{
public:
    static constexpr uint32_t kSpecularSize = 256;
    static constexpr uint32_t kSpecularMips = 9;
    static constexpr uint32_t kDiffuseSize = 16;
    static constexpr uint32_t kBrdfSize = 128;

    Result init(IDevice* device);

    ComPtr<ITexture> specular[2], diffuse[2], brdf;
};


// Three float4s give StructuredBuffer elements the same 48-byte layout on every
// backend. Uniform positive scale lets the unit-sphere normal remain unchanged.
struct SphereInstance
{
    math::float4 translationScale;
    math::float4 baseColorMetallic;
    math::float4 materialParameters; // x: roughness; yzw: unused.
};
static_assert(sizeof(SphereInstance) == 48);

inline constexpr uint32_t kSphereColumns = 6;
inline constexpr uint32_t kSphereRows = 5;
inline constexpr uint32_t kSphereCount = kSphereColumns * kSphereRows;

std::vector<SphereInstance> createSphereInstances();


struct Settings
{
    float lightYaw = -0.65f;
    float lightPitch = 0.85f;
    float roughness = 0.28f;
    float exposure = 0.0f;
    bool bloom = true;
    uint32_t environmentPreset = 0; // Studio / outdoor.
    float environmentRotation = 0;
    bool environmentLighting = true;
    bool msaa = true;
    bool spheres = true;
};

// Records the rendering passes into the supplied encoder and output texture.
class Renderer
{
public:
    static constexpr uint32_t kShadowSize = 1024;

    Result init(IDevice* device, Format outputFormat);

    // The caller must wait for previous submissions before replacing these targets.
    Result resize(uint32_t width, uint32_t height);

    Result render(ICommandEncoder* encoder, ITexture* output, const Camera& camera, const Settings& settings);

    bool supportsMsaa() const;

private:
    Result initSpheres();

    Result createMsaaTargets(uint32_t width, uint32_t height, ITexture** color, ITexture** depth);

    Result createTexture(
        uint32_t width,
        uint32_t height,
        Format format,
        TextureUsage usage,
        const char* label,
        ITexture** out,
        uint32_t sampleCount = 1
    );

    RenderState renderState(uint32_t width, uint32_t height, bool spheres = false) const;

    ComPtr<IDevice> m_device;
    Format m_outputFormat = Format::Undefined;
    uint32_t m_width = 0, m_height = 0;
    std::vector<logo::MeshRange> m_meshes;
    std::vector<Material> m_materials;
    uint32_t m_indexCount = 0;
    ComPtr<IBuffer> m_vertices, m_indices;
    ComPtr<IInputLayout> m_inputLayout, m_shadowInputLayout;
    ComPtr<IRenderPipeline> m_shadowPipeline, m_scenePipeline, m_toneMapPipeline;
    ComPtr<IRenderPipeline> m_backgroundPipeline;
    ComPtr<IRenderPipeline> m_msaaScenePipeline, m_msaaBackgroundPipeline;
    ComPtr<IComputePipeline> m_bloomPipeline;
    ComPtr<ISampler> m_linearSampler, m_shadowSampler;
    ComPtr<ITexture> m_shadow, m_hdr, m_depth, m_bloom[2];
    Environment m_environment;
    ComPtr<ITexture> m_msaaColor, m_msaaDepth;
    bool m_msaaSupported = false;
    ComPtr<IBuffer> m_sphereVertices, m_sphereIndices, m_sphereInstances;
    uint32_t m_sphereIndexCount = 0;
    ComPtr<IRenderPipeline> m_spherePipeline, m_msaaSpherePipeline, m_sphereShadowPipeline;
};

Scene createScene()
{
    Scene scene;
    scene.vertices.assign(std::begin(logo::kVertices), std::end(logo::kVertices));
    scene.indices.assign(std::begin(logo::kIndices), std::end(logo::kIndices));
    scene.meshes.assign(std::begin(logo::kMeshRanges), std::end(logo::kMeshRanges));
    // Scene-specific surface properties for the logo and ground plane.
    scene.materials = {
        {logo::kColors[0], 0.55f, 0.28f},
        {logo::kColors[1], 0.55f, 0.28f},
        {{0.075f, 0.09f, 0.12f}, 0.0f, 0.65f, 1},
    };
    uint32_t base = uint32_t(scene.vertices.size());
    scene.vertices.insert(
        scene.vertices.end(),
        {
            {{-100, -0.03f, -100}, {0, 1, 0}},
            {{-100, -0.03f, +100}, {0, 1, 0}},
            {{+100, -0.03f, +100}, {0, 1, 0}},
            {{+100, -0.03f, -100}, {0, 1, 0}},
        }
    );
    scene.meshes.push_back({uint32_t(scene.indices.size()), 6, 2});
    for (uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u})
        scene.indices.push_back(base + index);
    return scene;
}

Result Environment::init(IDevice* device)
{
    ComPtr<IShaderProgram> filterProgram, brdfProgram;
    SLANG_RETURN_ON_FAIL(createProgram(device, "environment.slang", {"environmentMain"}, filterProgram.writeRef()));
    SLANG_RETURN_ON_FAIL(createProgram(device, "environment.slang", {"brdfMain"}, brdfProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = filterProgram;
    ComPtr<IComputePipeline> filterPipeline;
    SLANG_RETURN_ON_FAIL(device->createComputePipeline(pipelineDesc, filterPipeline.writeRef()));
    pipelineDesc.program = brdfProgram;
    ComPtr<IComputePipeline> brdfPipeline;
    SLANG_RETURN_ON_FAIL(device->createComputePipeline(pipelineDesc, brdfPipeline.writeRef()));

    TextureDesc desc = {};
    desc.format = Format::RGBA16Float;
    desc.size = {kBrdfSize, kBrdfSize, 1};
    desc.usage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess;
    desc.label = "Environment BRDF LUT";
    SLANG_RETURN_ON_FAIL(device->createTexture(desc, nullptr, brdf.writeRef()));

    ComPtr<ICommandQueue> queue;
    SLANG_RETURN_ON_FAIL(device->getQueue(QueueType::Graphics, queue.writeRef()));
    ComPtr<ICommandEncoder> encoder;
    SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(encoder.writeRef()));
    auto pass = encoder->beginComputePass();
    pass->pushDebugGroup("Integrate environment BRDF", {});
    ShaderCursor cursor(pass->bindPipeline(brdfPipeline));
    SLANG_RETURN_ON_FAIL(cursor["brdfOutput"].setBinding(brdf));
    pass->dispatchCompute(math::divRoundUp(kBrdfSize, 8u), math::divRoundUp(kBrdfSize, 8u), 1);
    pass->popDebugGroup();
    pass->end();

    // Keep temporary textures/views alive until the initialization submission finishes.
    ComPtr<ITexture> staging[2];
    ComPtr<ITextureView> views[2][kSpecularMips];
    for (uint32_t diffuseFilter = 0; diffuseFilter < 2; ++diffuseFilter)
    {
        uint32_t size = diffuseFilter ? kDiffuseSize : kSpecularSize;
        uint32_t mipCount = diffuseFilter ? 1 : kSpecularMips;
        desc.type = TextureType::Texture2DArray;
        desc.size = {size, size, 1};
        desc.arrayLength = 6;
        desc.mipCount = mipCount;
        desc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        desc.label = "Environment filter staging";
        SLANG_RETURN_ON_FAIL(device->createTexture(desc, nullptr, staging[diffuseFilter].writeRef()));
        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            TextureViewDesc view = {};
            view.subresourceRange = {0, 6, mip, 1};
            SLANG_RETURN_ON_FAIL(
                device->createTextureView(staging[diffuseFilter], view, views[diffuseFilter][mip].writeRef())
            );
        }

        desc.type = TextureType::TextureCube;
        desc.arrayLength = 1;
        desc.usage = TextureUsage::ShaderResource | TextureUsage::CopyDestination;
        for (uint32_t preset = 0; preset < 2; ++preset)
        {
            auto& texture = diffuseFilter ? diffuse[preset] : specular[preset];
            desc.label = diffuseFilter ? "Environment diffuse irradiance" : "Environment GGX reflection";
            SLANG_RETURN_ON_FAIL(device->createTexture(desc, nullptr, texture.writeRef()));
            for (uint32_t mip = 0; mip < mipCount; ++mip)
            {
                uint32_t mipSize = std::max(size >> mip, 1u);
                float roughness = diffuseFilter ? 0 : float(mip) / (kSpecularMips - 1);
                pass = encoder->beginComputePass();
                pass->pushDebugGroup(diffuseFilter ? "Filter diffuse environment" : "Filter GGX environment", {});
                cursor = ShaderCursor(pass->bindPipeline(filterPipeline));
                SLANG_RETURN_ON_FAIL(cursor["environmentOutput"].setBinding(views[diffuseFilter][mip]));
                SLANG_RETURN_ON_FAIL(cursor["environmentPreset"].setData(preset));
                SLANG_RETURN_ON_FAIL(cursor["diffuseFilter"].setData(diffuseFilter));
                SLANG_RETURN_ON_FAIL(cursor["filterRoughness"].setData(roughness));
                pass->dispatchCompute(math::divRoundUp(mipSize, 8u), math::divRoundUp(mipSize, 8u), 6);
                pass->popDebugGroup();
                pass->end();
                encoder->copyTexture(
                    texture,
                    {0, 6, mip, 1},
                    {},
                    staging[diffuseFilter],
                    {0, 6, mip, 1},
                    {},
                    {mipSize, mipSize, 1}
                );
            }
        }
    }
    ComPtr<ICommandBuffer> commands;
    SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
    SLANG_RETURN_ON_FAIL(queue->submit(commands));
    return queue->waitOnHost();
}

std::vector<SphereInstance> createSphereInstances()
{
    std::vector<SphereInstance> instances;
    for (uint32_t row = 0; row < kSphereRows; ++row)
    {
        for (uint32_t column = 0; column < kSphereColumns; ++column)
        {
            float t = float(column) / (kSphereColumns - 1);
            float metallic = float(row) / (kSphereRows - 1);
            float x = column < 3 ? -4.6f + 1.05f * column : 2.5f + 1.05f * (column - 3);
            constexpr float radius = 0.38f;
            math::float3 color = math::float3(1.0f, 0.18f, 0.05f) * (1 - t) + math::float3(0.02f, 0.5f, 0.6f) * t;
            instances.push_back({
                {x, radius - 0.03f, 2.6f - 1.3f * row, radius},
                math::float4(color, metallic),
                {0.08f + 0.92f * t, 0, 0, 0},
            });
        }
    }
    return instances;
}


Result Renderer::init(IDevice* device, Format outputFormat)
{
    m_device = device;
    m_outputFormat = outputFormat;
    auto scene = createScene();
    m_meshes = std::move(scene.meshes);
    m_materials = std::move(scene.materials);
    m_indexCount = uint32_t(scene.indices.size());

    // The bloom shader declares rgba16f storage explicitly.
    for (const auto& requirement : {
             std::pair{
                 Format::RGBA16Float,
                 FormatSupport::RenderTarget | FormatSupport::ShaderSample | FormatSupport::ShaderUavStore
             },
             // D3D's format table queries the depth format's SRV mapping,
             // so depth attachment support is verified by texture creation.
             std::pair{Format::D32Float, FormatSupport::ShaderSample},
         })
    {
        FormatSupport support;
        SLANG_RETURN_ON_FAIL(device->getFormatSupport(requirement.first, &support));
        if ((support & requirement.second) != requirement.second)
            return SLANG_E_NOT_AVAILABLE;
    }

    BufferDesc buffer = {};
    buffer.size = scene.vertices.size() * sizeof(geometry::Vertex);
    buffer.usage = BufferUsage::VertexBuffer;
    buffer.defaultState = ResourceState::VertexBuffer;
    buffer.label = "Logo vertices";
    SLANG_RETURN_ON_FAIL(device->createBuffer(buffer, scene.vertices.data(), m_vertices.writeRef()));
    buffer.size = scene.indices.size() * sizeof(uint32_t);
    buffer.usage = BufferUsage::IndexBuffer;
    buffer.defaultState = ResourceState::IndexBuffer;
    buffer.label = "Logo indices";
    SLANG_RETURN_ON_FAIL(device->createBuffer(buffer, scene.indices.data(), m_indices.writeRef()));
    SLANG_RETURN_ON_FAIL(initSpheres());

    VertexStreamDesc stream = {sizeof(geometry::Vertex), InputSlotClass::PerVertex, 0};
    InputElementDesc elements[] = {
        {"POSITION", 0, Format::RGB32Float, offsetof(geometry::Vertex, position), 0},
        {"NORMAL", 0, Format::RGB32Float, offsetof(geometry::Vertex, normal), 0},
    };
    InputLayoutDesc input = {};
    input.vertexStreams = &stream;
    input.vertexStreamCount = 1;
    input.inputElements = elements;
    input.inputElementCount = 2;
    SLANG_RETURN_ON_FAIL(device->createInputLayout(input, m_inputLayout.writeRef()));
    input.inputElementCount = 1;
    SLANG_RETURN_ON_FAIL(device->createInputLayout(input, m_shadowInputLayout.writeRef()));

    RenderPipelineDesc pipeline = {};
    pipeline.inputLayout = m_shadowInputLayout;
    pipeline.depthStencil.format = Format::D32Float;
    pipeline.depthStencil.depthTestEnable = true;
    pipeline.depthStencil.depthWriteEnable = true;
    ComPtr<IShaderProgram> program;
    SLANG_RETURN_ON_FAIL(createProgram(device, "raster.slang", {"shadowVertex", "shadowFragment"}, program.writeRef()));
    pipeline.program = program;
    pipeline.rasterizer.depthBias = 100;
    pipeline.rasterizer.slopeScaledDepthBias = 1.5f;
    SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_shadowPipeline.writeRef()));
    SLANG_RETURN_ON_FAIL(
        createProgram(device, "raster.slang", {"sphereShadowVertex", "shadowFragment"}, program.writeRef())
    );
    pipeline.program = program;
    SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_sphereShadowPipeline.writeRef()));

    ComPtr<IShaderProgram> sceneProgram, sphereProgram, backgroundProgram;
    SLANG_RETURN_ON_FAIL(
        createProgram(device, "raster.slang", {"sceneVertex", "sceneFragment"}, sceneProgram.writeRef())
    );
    SLANG_RETURN_ON_FAIL(
        createProgram(device, "raster.slang", {"sphereVertex", "sceneFragment"}, sphereProgram.writeRef())
    );
    SLANG_RETURN_ON_FAIL(
        createProgram(device, "raster.slang", {"backgroundVertex", "backgroundFragment"}, backgroundProgram.writeRef())
    );
    auto createScenePipelines = [&](uint32_t sampleCount) -> Result
    {
        bool msaa = sampleCount > 1;
        if (msaa)
        {
            // There is no per-format sample-count query. Probe the exact 4x attachments.
            FormatSupport support = FormatSupport::None;
            SLANG_RETURN_ON_FAIL(device->getFormatSupport(Format::RGBA16Float, &support));
            if ((support & FormatSupport::Resolvable) == FormatSupport::None)
                return SLANG_E_NOT_AVAILABLE;
            ComPtr<ITexture> colorProbe, depthProbe;
            SLANG_RETURN_ON_FAIL(createMsaaTargets(1, 1, colorProbe.writeRef(), depthProbe.writeRef()));
        }
        ColorTargetDesc color = {};
        color.format = Format::RGBA16Float;
        RenderPipelineDesc desc = {};
        desc.targets = &color;
        desc.targetCount = 1;
        desc.multisample.sampleCount = sampleCount;
        desc.rasterizer.multisampleEnable = msaa;
        desc.depthStencil.depthWriteEnable = false;
        desc.program = backgroundProgram;
        SLANG_RETURN_ON_FAIL(
            device->createRenderPipeline(desc, (msaa ? m_msaaBackgroundPipeline : m_backgroundPipeline).writeRef())
        );
        desc.inputLayout = m_inputLayout;
        desc.depthStencil.format = Format::D32Float;
        desc.depthStencil.depthTestEnable = true;
        desc.depthStencil.depthWriteEnable = true;
        desc.program = sceneProgram;
        SLANG_RETURN_ON_FAIL(
            device->createRenderPipeline(desc, (msaa ? m_msaaScenePipeline : m_scenePipeline).writeRef())
        );
        desc.program = sphereProgram;
        return device->createRenderPipeline(desc, (msaa ? m_msaaSpherePipeline : m_spherePipeline).writeRef());
    };
    SLANG_RETURN_ON_FAIL(createScenePipelines(1));
    m_msaaSupported = SLANG_SUCCEEDED(createScenePipelines(4));

    pipeline = {};
    ColorTargetDesc color = {};
    color.format = outputFormat;
    pipeline.targets = &color;
    pipeline.targetCount = 1;
    pipeline.depthStencil.depthWriteEnable = false;
    SLANG_RETURN_ON_FAIL(
        createProgram(device, "../base/post.slang", {"fullscreenVertex", "toneMapFragment"}, program.writeRef())
    );
    pipeline.program = program;
    SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_toneMapPipeline.writeRef()));

    ComputePipelineDesc compute = {};
    SLANG_RETURN_ON_FAIL(createProgram(device, "../base/post.slang", {"bloomMain"}, program.writeRef()));
    compute.program = program;
    SLANG_RETURN_ON_FAIL(device->createComputePipeline(compute, m_bloomPipeline.writeRef()));

    SamplerDesc sampler = {};
    sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressingMode::ClampToEdge;
    SLANG_RETURN_ON_FAIL(device->createSampler(sampler, m_linearSampler.writeRef()));
    sampler.reductionOp = TextureReductionOp::Comparison;
    sampler.comparisonFunc = ComparisonFunc::LessEqual;
    SLANG_RETURN_ON_FAIL(device->createSampler(sampler, m_shadowSampler.writeRef()));
    SLANG_RETURN_ON_FAIL(m_environment.init(device));
    return createTexture(
        kShadowSize,
        kShadowSize,
        Format::D32Float,
        TextureUsage::DepthStencil | TextureUsage::ShaderResource,
        "Shadow map",
        m_shadow.writeRef()
    );
}

Result Renderer::resize(uint32_t width, uint32_t height)
{
    if (!width || !height)
        return SLANG_E_INVALID_ARG;
    m_width = width;
    m_height = height;
    SLANG_RETURN_ON_FAIL(createTexture(
        width,
        height,
        Format::RGBA16Float,
        TextureUsage::RenderTarget | TextureUsage::ShaderResource |
            (m_msaaSupported ? TextureUsage::ResolveDestination : TextureUsage::None),
        "HDR scene",
        m_hdr.writeRef()
    ));
    SLANG_RETURN_ON_FAIL(
        createTexture(width, height, Format::D32Float, TextureUsage::DepthStencil, "Scene depth", m_depth.writeRef())
    );
    if (m_msaaSupported)
        SLANG_RETURN_ON_FAIL(createMsaaTargets(width, height, m_msaaColor.writeRef(), m_msaaDepth.writeRef()));
    for (auto& target : m_bloom)
        SLANG_RETURN_ON_FAIL(createTexture(
            math::divRoundUp(width, 2u),
            math::divRoundUp(height, 2u),
            Format::RGBA16Float,
            TextureUsage::UnorderedAccess | TextureUsage::ShaderResource,
            "Bloom",
            target.writeRef()
        ));
    return SLANG_OK;
}

Result Renderer::render(ICommandEncoder* encoder, ITexture* output, const Camera& camera, const Settings& settings)
{
    if (!m_width || !m_height || output->getDesc().size.width != m_width || output->getDesc().size.height != m_height ||
        output->getDesc().format != m_outputFormat || settings.environmentPreset > 1)
        return SLANG_E_INVALID_ARG;
    bool msaa = settings.msaa && m_msaaSupported;
    for (const auto& mesh : logo::kMeshRanges)
        m_materials[mesh.materialIndex].roughness = settings.roughness;
    math::float3 light = {
        std::sin(settings.lightYaw) * std::cos(settings.lightPitch),
        std::sin(settings.lightPitch),
        std::cos(settings.lightYaw) * std::cos(settings.lightPitch),
    };
    math::float4x4 viewProjection = math::mul(
        math::perspective(camera.aspectRatio, math::radians(camera.fov), 0.1f, 60.0f),
        math::lookAt(camera.position, camera.target)
    );
    math::float4x4 orthographic = math::identityMatrix<4>();
    orthographic[0][0] = orthographic[1][1] = 1.0f / 7.0f;
    orthographic[2][2] = 1.0f / (0.1f - 24.0f);
    orthographic[2][3] = 0.1f / (0.1f - 24.0f);
    // Keep the shadow map centered on the scene as the camera moves freely.
    math::float3 lightTarget(0, 1.58f, 0);
    math::float4x4 lightViewProjection =
        math::mul(orthographic, math::lookAt(lightTarget + light * 12.0f, lightTarget));

    RenderPassDepthStencilAttachment depth = {};
    depth.view = m_shadow->getDefaultView();
    RenderPassDesc pass = {};
    pass.depthStencilAttachment = &depth;
    auto renderPass = encoder->beginRenderPass(pass);
    Profiler::pushRegion(renderPass, "Raster frame");
    Profiler::pushRegion(renderPass, "Shadows");
    renderPass->pushDebugGroup("Shadow map", {});
    ShaderCursor shadowCursor(renderPass->bindPipeline(m_shadowPipeline));
    SLANG_RETURN_ON_FAIL(shadowCursor["frame"]["lightViewProjection"].setData(lightViewProjection));
    renderPass->setRenderState(renderState(kShadowSize, kShadowSize));
    DrawArguments shadowDraw = {};
    shadowDraw.vertexCount = m_indexCount;
    renderPass->drawIndexed(shadowDraw);
    if (settings.spheres)
    {
        ShaderCursor cursor(renderPass->bindPipeline(m_sphereShadowPipeline));
        SLANG_RETURN_ON_FAIL(cursor["frame"]["lightViewProjection"].setData(lightViewProjection));
        SLANG_RETURN_ON_FAIL(cursor["sphereInstances"].setBinding(m_sphereInstances));
        renderPass->setRenderState(renderState(kShadowSize, kShadowSize, true));
        DrawArguments sphereDraw = {};
        sphereDraw.vertexCount = m_sphereIndexCount;
        sphereDraw.instanceCount = kSphereCount;
        renderPass->drawIndexed(sphereDraw);
    }
    renderPass->popDebugGroup();
    Profiler::popRegion(renderPass);
    renderPass->end();

    RenderPassColorAttachment color = {};
    color.view = (msaa ? m_msaaColor : m_hdr)->getDefaultView();
    depth.view = (msaa ? m_msaaDepth : m_depth)->getDefaultView();
    pass.colorAttachments = &color;
    pass.colorAttachmentCount = 1;
    pass.depthStencilAttachment = nullptr;
    renderPass = encoder->beginRenderPass(pass);
    Profiler::pushRegion(renderPass, "Scene");
    renderPass->pushDebugGroup("Procedural environment background", {});
    ShaderCursor background(renderPass->bindPipeline(msaa ? m_msaaBackgroundPipeline : m_backgroundPipeline));
    SLANG_RETURN_ON_FAIL(background["frame"]["cameraRight"].setData(camera.imageU));
    SLANG_RETURN_ON_FAIL(background["frame"]["cameraUp"].setData(camera.imageV));
    SLANG_RETURN_ON_FAIL(background["frame"]["cameraForward"].setData(camera.imageW));
    SLANG_RETURN_ON_FAIL(background["frame"]["lightDirection"].setData(light));
    SLANG_RETURN_ON_FAIL(background["frame"]["environmentRotation"].setData(settings.environmentRotation));
    SLANG_RETURN_ON_FAIL(background["frame"]["environmentPreset"].setData(settings.environmentPreset));
    RenderState backgroundState = {};
    backgroundState.viewports[0] = Viewport::fromSize(m_width, m_height);
    backgroundState.viewportCount = 1;
    backgroundState.scissorRects[0] = ScissorRect::fromSize(m_width, m_height);
    backgroundState.scissorRectCount = 1;
    renderPass->setRenderState(backgroundState);
    DrawArguments backgroundDraw = {};
    backgroundDraw.vertexCount = 3;
    renderPass->draw(backgroundDraw);
    renderPass->popDebugGroup();
    renderPass->end();

    color.loadOp = LoadOp::Load;
    // Preserve the multisampled background until geometry is complete, then
    // resolve linear HDR color before the single-sample post-processing passes.
    color.resolveTarget = msaa ? m_hdr->getDefaultView() : nullptr;
    pass.depthStencilAttachment = &depth;
    renderPass = encoder->beginRenderPass(pass);
    renderPass->pushDebugGroup("Lit HDR scene", {});
    renderPass->setRenderState(renderState(m_width, m_height));
    auto bindSceneFrame = [&](ShaderCursor cursor) -> Result
    {
        SLANG_RETURN_ON_FAIL(cursor["frame"]["viewProjection"].setData(viewProjection));
        SLANG_RETURN_ON_FAIL(cursor["frame"]["lightViewProjection"].setData(lightViewProjection));
        SLANG_RETURN_ON_FAIL(cursor["frame"]["eye"].setData(camera.position));
        SLANG_RETURN_ON_FAIL(cursor["frame"]["lightDirection"].setData(light));
        SLANG_RETURN_ON_FAIL(cursor["frame"]["environmentRotation"].setData(settings.environmentRotation));
        float environmentIntensity = settings.environmentLighting ? 1.0f : 0.0f;
        SLANG_RETURN_ON_FAIL(cursor["frame"]["environmentIntensity"].setData(environmentIntensity));
        SLANG_RETURN_ON_FAIL(
            cursor["specularEnvironment"].setBinding(m_environment.specular[settings.environmentPreset])
        );
        SLANG_RETURN_ON_FAIL(
            cursor["diffuseEnvironment"].setBinding(m_environment.diffuse[settings.environmentPreset])
        );
        SLANG_RETURN_ON_FAIL(cursor["environmentBrdf"].setBinding(m_environment.brdf));
        SLANG_RETURN_ON_FAIL(cursor["environmentSampler"].setBinding(m_linearSampler));
        SLANG_RETURN_ON_FAIL(cursor["shadowMap"].setBinding(m_shadow));
        SLANG_RETURN_ON_FAIL(cursor["shadowSampler"].setBinding(m_shadowSampler));
        return SLANG_OK;
    };
    for (const auto& mesh : m_meshes)
    {
        ShaderCursor cursor(renderPass->bindPipeline(msaa ? m_msaaScenePipeline : m_scenePipeline));
        SLANG_RETURN_ON_FAIL(bindSceneFrame(cursor));
        const auto& material = m_materials[mesh.materialIndex];
        SLANG_RETURN_ON_FAIL(cursor["material"]["baseColor"].setData(material.baseColor));
        SLANG_RETURN_ON_FAIL(cursor["material"]["metallic"].setData(material.metallic));
        SLANG_RETURN_ON_FAIL(cursor["material"]["roughness"].setData(material.roughness));
        SLANG_RETURN_ON_FAIL(cursor["material"]["groundPattern"].setData(material.groundPattern));
        DrawArguments draw = {};
        draw.startIndexLocation = mesh.firstIndex;
        draw.vertexCount = mesh.indexCount;
        renderPass->drawIndexed(draw);
    }
    if (settings.spheres)
    {
        renderPass->pushDebugGroup("Instanced spheres", {});
        ShaderCursor cursor(renderPass->bindPipeline(msaa ? m_msaaSpherePipeline : m_spherePipeline));
        SLANG_RETURN_ON_FAIL(bindSceneFrame(cursor));
        SLANG_RETURN_ON_FAIL(cursor["sphereInstances"].setBinding(m_sphereInstances));
        renderPass->setRenderState(renderState(m_width, m_height, true));
        DrawArguments sphereDraw = {};
        sphereDraw.vertexCount = m_sphereIndexCount;
        sphereDraw.instanceCount = kSphereCount;
        renderPass->drawIndexed(sphereDraw);
        renderPass->popDebugGroup();
    }
    renderPass->popDebugGroup();
    Profiler::popRegion(renderPass);
    renderPass->end();

    if (settings.bloom)
    {
        for (uint32_t i = 0; i < 3; ++i)
        {
            auto computePass = encoder->beginComputePass();
            if (i == 0)
                Profiler::pushRegion(computePass, "Bloom");
            computePass->pushDebugGroup(
                i == 0   ? "Bloom highlights"
                : i == 1 ? "Bloom horizontal"
                         : "Bloom vertical",
                {}
            );
            ShaderCursor cursor(computePass->bindPipeline(m_bloomPipeline));
            SLANG_RETURN_ON_FAIL(cursor["source"].setBinding(i == 0 ? m_hdr.get() : m_bloom[(i - 1) % 2].get()));
            SLANG_RETURN_ON_FAIL(cursor["destination"].setBinding(m_bloom[i % 2]));
            SLANG_RETURN_ON_FAIL(cursor["linearSampler"].setBinding(m_linearSampler));
            SLANG_RETURN_ON_FAIL(cursor["bloomPass"].setData(i));
            auto size = m_bloom[i % 2]->getDesc().size;
            computePass->dispatchCompute(math::divRoundUp(size.width, 8u), math::divRoundUp(size.height, 8u), 1);
            computePass->popDebugGroup();
            if (i == 2)
                Profiler::popRegion(computePass);
            computePass->end();
        }
    }

    color.view = output->getDefaultView();
    color.resolveTarget = nullptr;
    color.loadOp = LoadOp::Clear;
    pass.depthStencilAttachment = nullptr;
    renderPass = encoder->beginRenderPass(pass);
    Profiler::pushRegion(renderPass, "Tone map");
    renderPass->pushDebugGroup("Tone mapping", {});
    ShaderCursor cursor(renderPass->bindPipeline(m_toneMapPipeline));
    SLANG_RETURN_ON_FAIL(cursor["source"].setBinding(m_hdr));
    SLANG_RETURN_ON_FAIL(cursor["bloom"].setBinding(settings.bloom ? m_bloom[0].get() : m_hdr.get()));
    SLANG_RETURN_ON_FAIL(cursor["linearSampler"].setBinding(m_linearSampler));
    SLANG_RETURN_ON_FAIL(cursor["exposure"].setData(settings.exposure));
    float strength = settings.bloom ? 0.18f : 0.0f;
    SLANG_RETURN_ON_FAIL(cursor["bloomStrength"].setData(strength));
    uint32_t encodeSrgb = getFormatInfo(m_outputFormat).isSrgb ? 0 : 1;
    SLANG_RETURN_ON_FAIL(cursor["encodeSrgb"].setData(encodeSrgb));
    RenderState state = {};
    state.viewports[0] = Viewport::fromSize(m_width, m_height);
    state.viewportCount = 1;
    state.scissorRects[0] = ScissorRect::fromSize(m_width, m_height);
    state.scissorRectCount = 1;
    renderPass->setRenderState(state);
    DrawArguments draw = {};
    draw.vertexCount = 3;
    renderPass->draw(draw);
    renderPass->popDebugGroup();
    Profiler::popRegion(renderPass); // Tone map
    Profiler::popRegion(renderPass); // Raster frame
    renderPass->end();
    return SLANG_OK;
}

bool Renderer::supportsMsaa() const
{
    return m_msaaSupported;
}

Result Renderer::initSpheres()
{
    // The logo and sphere meshes share the position/normal input layout.
    auto mesh = geometry::Mesh::createSphere();
    m_sphereIndexCount = uint32_t(mesh.indices.size());
    BufferDesc desc = {};
    desc.size = mesh.vertices.size() * sizeof(geometry::Vertex);
    desc.usage = BufferUsage::VertexBuffer;
    desc.defaultState = ResourceState::VertexBuffer;
    desc.label = "Shared sphere vertices";
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(desc, mesh.vertices.data(), m_sphereVertices.writeRef()));
    desc.size = mesh.indices.size() * sizeof(uint32_t);
    desc.usage = BufferUsage::IndexBuffer;
    desc.defaultState = ResourceState::IndexBuffer;
    desc.label = "Shared sphere indices";
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(desc, mesh.indices.data(), m_sphereIndices.writeRef()));
    auto instances = createSphereInstances();
    desc.size = instances.size() * sizeof(SphereInstance);
    desc.elementSize = sizeof(SphereInstance);
    desc.usage = BufferUsage::ShaderResource;
    desc.defaultState = ResourceState::ShaderResource;
    desc.label = "Sphere transforms and materials";
    return m_device->createBuffer(desc, instances.data(), m_sphereInstances.writeRef());
}

Result Renderer::createMsaaTargets(uint32_t width, uint32_t height, ITexture** color, ITexture** depth)
{
    SLANG_RETURN_ON_FAIL(createTexture(
        width,
        height,
        Format::RGBA16Float,
        TextureUsage::RenderTarget | TextureUsage::ResolveSource,
        "4x MSAA HDR scene",
        color,
        4
    ));
    return createTexture(width, height, Format::D32Float, TextureUsage::DepthStencil, "4x MSAA depth", depth, 4);
}

Result Renderer::createTexture(
    uint32_t width,
    uint32_t height,
    Format format,
    TextureUsage usage,
    const char* label,
    ITexture** out,
    uint32_t sampleCount
)
{
    TextureDesc desc = {};
    desc.size = {width, height, 1};
    desc.format = format;
    desc.usage = usage;
    desc.label = label;
    desc.type = sampleCount > 1 ? TextureType::Texture2DMS : TextureType::Texture2D;
    desc.sampleCount = sampleCount;
    return m_device->createTexture(desc, nullptr, out);
}

RenderState Renderer::renderState(uint32_t width, uint32_t height, bool spheres) const
{
    RenderState state = {};
    state.viewports[0] = Viewport::fromSize(width, height);
    state.viewportCount = 1;
    state.scissorRects[0] = ScissorRect::fromSize(width, height);
    state.scissorRectCount = 1;
    state.vertexBuffers[0] = {spheres ? m_sphereVertices : m_vertices, 0};
    state.vertexBufferCount = 1;
    state.indexBuffer = {spheres ? m_sphereIndices : m_indices, 0};
    state.indexFormat = IndexFormat::Uint32;
    return state;
}

class ExampleRaster : public ExampleBase
{
public:
    Result init(DeviceType deviceType) override
    {
        SLANG_RETURN_ON_FAIL(
            createDevice(deviceType, {Feature::Surface, Feature::Rasterization}, {}, m_device.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createWindow(m_device, "Slang logo", 960, 640));
        SLANG_RETURN_ON_FAIL(createSurface(m_device, Format::Undefined, m_surface.writeRef()));
        SLANG_RETURN_ON_FAIL(initOverlay(m_device, m_surface->getConfig()->format));
        SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));

        m_renderer = std::make_unique<Renderer>();
        SLANG_RETURN_ON_FAIL(m_renderer->init(m_device, m_surface->getConfig()->format));
        SLANG_RETURN_ON_FAIL(m_renderer->resize(m_surface->getConfig()->width, m_surface->getConfig()->height));

        SLANG_RETURN_ON_FAIL(initProfiler(m_device));

        m_cameraController.setCamera(&m_camera);
        resetCamera();
        return SLANG_OK;
    }

    virtual void shutdown() override
    {
        if (m_queue)
            m_queue->waitOnHost();
        m_renderer.reset();
        m_surface.setNull();
        m_queue.setNull();
        m_device.setNull();
    }

    virtual Result update(double time) override
    {
        float dt = m_time < 0 ? 0.f : float(std::clamp(time - m_time, 0.0, 0.1));
        m_time = time;
        m_cameraController.update(dt);
        return SLANG_OK;
    }

    virtual Result draw() override
    {
        if (!m_surface->getConfig())
            return SLANG_OK;
        ComPtr<ITexture> image;
        // A native resize can invalidate the swapchain before its queued callback.
        m_surface->acquireNextImage(image.writeRef());
        if (!image)
            return SLANG_OK;
        m_camera.width = image->getDesc().size.width;
        m_camera.height = image->getDesc().size.height;
        m_camera.recompute();
        auto profile = getProfiler().frame();
        SLANG_RETURN_ON_FAIL(profile.begin());
        ComPtr<ICommandEncoder> encoder;
        SLANG_RETURN_ON_FAIL(m_queue->createCommandEncoder(encoder.writeRef()));
        SLANG_RETURN_ON_FAIL(m_renderer->render(encoder, image, m_camera, m_settings));
        SLANG_RETURN_ON_FAIL(drawStatusOverlay(encoder, image));
        ComPtr<ICommandBuffer> commands;
        SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
        SLANG_RETURN_ON_FAIL(m_queue->submit(commands));
        SLANG_RETURN_ON_FAIL(profile.end());
        return presentSurface();
    }

    virtual Result onResize(int width, int height, int framebufferWidth, int framebufferHeight) override
    {
        if (!m_queue || !m_renderer)
            return SLANG_OK;
        SLANG_RETURN_ON_FAIL(m_queue->waitOnHost());
        if (framebufferWidth <= 0 || framebufferHeight <= 0)
        {
            return m_surface->unconfigure();
        }
        SurfaceConfig config = {};
        config.format = m_surface->getInfo().preferredFormat;
        config.width = framebufferWidth;
        config.height = framebufferHeight;
        SLANG_RETURN_ON_FAIL(configureSurface(m_surface, config));
        return m_renderer->resize(framebufferWidth, framebufferHeight);
    }

    virtual void onMouseButton(int button, int action, int mods) override
    {
        m_lastMouse = {getMouseX(), getMouseY()};
        m_cameraController.onMouseButton(button, action);
    }

    virtual void onMousePosition(float x, float y) override
    {
        m_cameraController.onMousePosition(x, y);
        // The framework mirrors absolute positions to every backend window.
        // Use a fixed sensitivity so those shared events produce identical views.
        math::float2 delta = math::float2(x, y) - m_lastMouse;
        m_lastMouse = {x, y};
        if (isMouseDown(GLFW_MOUSE_BUTTON_RIGHT))
        {
            m_settings.lightYaw -= delta.x * 0.006f;
            m_settings.lightPitch = std::clamp(m_settings.lightPitch + delta.y * 0.006f, 0.15f, 1.45f);
        }
    }

    virtual void onKey(int key, int scancode, int action, int mods) override
    {
        if (m_cameraController.onKey(key, action))
            return;
        if (action != GLFW_PRESS && action != GLFW_REPEAT)
            return;
        if (key == GLFW_KEY_B && action == GLFW_PRESS)
            m_settings.bloom = !m_settings.bloom;
        if (key == GLFW_KEY_N && action == GLFW_PRESS)
            m_settings.environmentPreset = 1 - m_settings.environmentPreset;
        if (key == GLFW_KEY_I && action == GLFW_PRESS)
            m_settings.environmentLighting = !m_settings.environmentLighting;
        if (key == GLFW_KEY_M && action == GLFW_PRESS && m_renderer->supportsMsaa())
            m_settings.msaa = !m_settings.msaa;
        if (key == GLFW_KEY_G && action == GLFW_PRESS)
            m_settings.spheres = !m_settings.spheres;
        if (key == GLFW_KEY_COMMA || key == GLFW_KEY_PERIOD)
            m_settings.environmentRotation =
                std::remainder(m_settings.environmentRotation + (key == GLFW_KEY_PERIOD ? 0.1f : -0.1f), 2 * math::kPI);
        if (key == GLFW_KEY_R)
        {
            m_settings = {};
            resetCamera();
        }
        if (key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_RIGHT_BRACKET)
            m_settings.roughness =
                std::clamp(m_settings.roughness + (key == GLFW_KEY_RIGHT_BRACKET ? 0.05f : -0.05f), 0.08f, 1.0f);
        if (key == GLFW_KEY_MINUS || key == GLFW_KEY_EQUAL)
            m_settings.exposure =
                std::clamp(m_settings.exposure + (key == GLFW_KEY_EQUAL ? 0.25f : -0.25f), -4.0f, 4.0f);
    }

private:
    void resetCamera()
    {
        constexpr float yaw = 0.22f, pitch = 0.5f;
        m_camera.target = {0, 1.58f, 0};
        m_camera.position =
            m_camera.target +
            math::float3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)) * 14.0f;
        m_camera.up = {0, 1, 0};
        m_camera.fov = math::degrees(0.65f);
        m_camera.recompute();
        m_cameraController.reset();
    }

    const char* msaaStatus() const
    {
        return !m_renderer->supportsMsaa() ? "UNAVAILABLE" : m_settings.msaa ? "4X" : "OFF";
    }

    Result drawStatusOverlay(ICommandEncoder* encoder, ITexture* image)
    {
        clearOverlay();
        printOverlayHeader();
        printCameraControls();
        printOverlay("Right drag: Light | [ / ]: Roughness | - / =: Exposure | B: Bloom\n");
        printOverlay("N: Environment | , / .: Rotate environment | I: Lighting\n");
        printOverlay("M: MSAA | G: Sphere grid | R: Reset\n\n");
        printOverlay(
            "Roughness %.2f | Exposure %+.2f | Bloom %s\n",
            m_settings.roughness,
            m_settings.exposure,
            m_settings.bloom ? "on" : "off"
        );
        printOverlay(
            "MSAA %s | Spheres %u | Environment %s\n",
            msaaStatus(),
            m_settings.spheres ? kSphereCount : 0u,
            m_settings.environmentPreset == 0 ? "studio" : "outdoor"
        );
        printOverlay(
            "Environment angle %+.0f deg | Lighting %s",
            math::degrees(m_settings.environmentRotation),
            m_settings.environmentLighting ? "on" : "off"
        );
        return drawOverlay(encoder, image);
    }

    ComPtr<IDevice> m_device;
    ComPtr<ISurface> m_surface;
    ComPtr<ICommandQueue> m_queue;
    std::unique_ptr<Renderer> m_renderer;
    Settings m_settings;
    math::float2 m_lastMouse;
    Camera m_camera;
    CameraController m_cameraController;
    double m_time = -1.0;
};

EXAMPLE_MAIN(ExampleRaster)

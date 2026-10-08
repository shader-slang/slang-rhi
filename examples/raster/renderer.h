#pragma once

#include "../base/logo-scene.h"
#include "../base/gpu-profiler.h"
#include "environment.h"
#include "spheres.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <algorithm>

namespace rhi::raster {

using namespace math;

struct Settings
{
    float yaw = 0.22f;
    float pitch = 0.5f;
    float distance = 14.0f;
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

struct Programs
{
    ComPtr<IShaderProgram> shadow;
    ComPtr<IShaderProgram> scene;
    ComPtr<IShaderProgram> bloom;
    ComPtr<IShaderProgram> toneMap;
    ComPtr<IShaderProgram> background;
    ComPtr<IShaderProgram> environment;
    ComPtr<IShaderProgram> environmentBrdf;
    ComPtr<IShaderProgram> spheres;
    ComPtr<IShaderProgram> sphereShadow;
};

inline float3 orbitDirection(float yaw, float pitch)
{
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

// Right-handed view and projection matrices, with a [0, 1] depth range.
inline float4x4 lookAt(float3 eye, float3 target)
{
    float3 z = normalize(eye - target);
    float3 x = normalize(cross(float3(0, 1, 0), z));
    float3 y = cross(z, x);
    float4x4 result = identityMatrix<4>();
    result[0] = float4(x, -dot(x, eye));
    result[1] = float4(y, -dot(y, eye));
    result[2] = float4(z, -dot(z, eye));
    return result;
}

inline float4x4 perspective(float aspect)
{
    constexpr float nearPlane = 0.1f, farPlane = 60.0f;
    float scale = 1.0f / std::tan(0.65f * 0.5f);
    float4x4 result;
    result[0][0] = scale / aspect;
    result[1][1] = scale;
    result[2][2] = farPlane / (nearPlane - farPlane);
    result[2][3] = nearPlane * farPlane / (nearPlane - farPlane);
    result[3][2] = -1;
    return result;
}

// The renderer records into a caller-owned encoder and output texture. Keeping
// window management outside it also lets tests render the complete scene offscreen.
class Renderer
{
public:
    static constexpr uint32_t kShadowSize = 1024;

    Result init(IDevice* device, Format outputFormat, const Programs& programs)
    {
        m_device = device;
        m_outputFormat = outputFormat;
        m_scene = logo::createScene();

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
        buffer.size = m_scene.vertices.size() * sizeof(logo::Vertex);
        buffer.usage = BufferUsage::VertexBuffer;
        buffer.defaultState = ResourceState::VertexBuffer;
        buffer.label = "Logo vertices";
        SLANG_RETURN_ON_FAIL(device->createBuffer(buffer, m_scene.vertices.data(), m_vertices.writeRef()));
        buffer.size = m_scene.indices.size() * sizeof(uint32_t);
        buffer.usage = BufferUsage::IndexBuffer;
        buffer.defaultState = ResourceState::IndexBuffer;
        buffer.label = "Logo indices";
        SLANG_RETURN_ON_FAIL(device->createBuffer(buffer, m_scene.indices.data(), m_indices.writeRef()));
        SLANG_RETURN_ON_FAIL(initSpheres());

        VertexStreamDesc stream = {sizeof(logo::Vertex), InputSlotClass::PerVertex, 0};
        InputElementDesc elements[] = {
            {"POSITION", 0, Format::RGB32Float, offsetof(logo::Vertex, position), 0},
            {"NORMAL", 0, Format::RGB32Float, offsetof(logo::Vertex, normal), 0},
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
        pipeline.program = programs.shadow;
        pipeline.rasterizer.depthBias = 100;
        pipeline.rasterizer.slopeScaledDepthBias = 1.5f;
        SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_shadowPipeline.writeRef()));
        pipeline.program = programs.sphereShadow;
        SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_sphereShadowPipeline.writeRef()));

        ColorTargetDesc color = {};
        color.format = Format::RGBA16Float;
        pipeline.targets = &color;
        pipeline.targetCount = 1;
        pipeline.inputLayout = m_inputLayout;
        pipeline.program = programs.scene;
        pipeline.rasterizer.depthBias = 0;
        pipeline.rasterizer.slopeScaledDepthBias = 0;
        SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_scenePipeline.writeRef()));
        pipeline.program = programs.spheres;
        SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_spherePipeline.writeRef()));

        pipeline = {};
        pipeline.targets = &color;
        pipeline.targetCount = 1;
        pipeline.depthStencil.depthWriteEnable = false;
        pipeline.program = programs.background;
        SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_backgroundPipeline.writeRef()));

        // There is no per-format sample-count query in the RHI. Probe the exact
        // 4x color/depth attachments and pipelines, retaining the 1x path if unavailable.
        m_msaaSupported = SLANG_SUCCEEDED(initMsaa(programs));

        pipeline = {};
        color.format = outputFormat;
        pipeline.targets = &color;
        pipeline.targetCount = 1;
        pipeline.depthStencil.depthWriteEnable = false;
        pipeline.program = programs.toneMap;
        SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_toneMapPipeline.writeRef()));

        ComputePipelineDesc compute = {};
        compute.program = programs.bloom;
        SLANG_RETURN_ON_FAIL(device->createComputePipeline(compute, m_bloomPipeline.writeRef()));

        SamplerDesc sampler = {};
        sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressingMode::ClampToEdge;
        SLANG_RETURN_ON_FAIL(device->createSampler(sampler, m_linearSampler.writeRef()));
        sampler.reductionOp = TextureReductionOp::Comparison;
        sampler.comparisonFunc = ComparisonFunc::LessEqual;
        SLANG_RETURN_ON_FAIL(device->createSampler(sampler, m_shadowSampler.writeRef()));
        SLANG_RETURN_ON_FAIL(m_environment.init(device, programs.environment, programs.environmentBrdf));
        return createTexture(
            kShadowSize,
            kShadowSize,
            Format::D32Float,
            TextureUsage::DepthStencil | TextureUsage::ShaderResource,
            "Shadow map",
            m_shadow.writeRef()
        );
    }

    // The caller must wait for previous submissions before replacing these targets.
    Result resize(uint32_t width, uint32_t height)
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
        SLANG_RETURN_ON_FAIL(createTexture(
            width,
            height,
            Format::D32Float,
            TextureUsage::DepthStencil,
            "Scene depth",
            m_depth.writeRef()
        ));
        if (m_msaaSupported)
            SLANG_RETURN_ON_FAIL(createMsaaTargets(width, height, m_msaaColor.writeRef(), m_msaaDepth.writeRef()));
        for (auto& target : m_bloom)
            SLANG_RETURN_ON_FAIL(createTexture(
                (width + 1) / 2,
                (height + 1) / 2,
                Format::RGBA16Float,
                TextureUsage::UnorderedAccess | TextureUsage::ShaderResource,
                "Bloom",
                target.writeRef()
            ));
        return SLANG_OK;
    }

    Result render(ICommandEncoder* encoder, ITexture* output, const Settings& settings, GpuProfiler* profiler = nullptr)
    {
        if (!m_width || !m_height || output->getDesc().size.width != m_width ||
            output->getDesc().size.height != m_height || output->getDesc().format != m_outputFormat ||
            settings.environmentPreset > 1)
            return SLANG_E_INVALID_ARG;
        bool msaa = settings.msaa && m_msaaSupported;
        float3 target(0, 1.58f, 0);
        float3 eye = target + orbitDirection(settings.yaw, settings.pitch) * settings.distance;
        float3 light = orbitDirection(settings.lightYaw, settings.lightPitch);
        float4x4 viewProjection = mul(perspective(float(m_width) / m_height), lookAt(eye, target));
        float4x4 orthographic = identityMatrix<4>();
        orthographic[0][0] = orthographic[1][1] = 1.0f / 7.0f;
        orthographic[2][2] = 1.0f / (0.1f - 24.0f);
        orthographic[2][3] = 0.1f / (0.1f - 24.0f);
        float4x4 lightViewProjection = mul(orthographic, lookAt(target + light * 12.0f, target));

        RenderPassDepthStencilAttachment depth = {};
        depth.view = m_shadow->getDefaultView();
        RenderPassDesc pass = {};
        pass.depthStencilAttachment = &depth;
        auto renderPass = encoder->beginRenderPass(pass);
        uint32_t profileRegion =
            profiler ? profiler->beginRegion(renderPass, "Raster frame") : GpuProfiler::kInvalidRegion;
        renderPass->pushDebugGroup("Shadow map", {});
        ShaderCursor shadowCursor(renderPass->bindPipeline(m_shadowPipeline));
        SLANG_RETURN_ON_FAIL(shadowCursor["frame"]["lightViewProjection"].setData(lightViewProjection));
        renderPass->setRenderState(renderState(kShadowSize, kShadowSize));
        DrawArguments draw = {};
        draw.vertexCount = uint32_t(m_scene.indices.size());
        renderPass->drawIndexed(draw);
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
        renderPass->end();

        RenderPassColorAttachment color = {};
        color.view = (msaa ? m_msaaColor : m_hdr)->getDefaultView();
        color.clearValue[0] = 0.018f;
        color.clearValue[1] = 0.025f;
        color.clearValue[2] = 0.04f;
        color.clearValue[3] = 1.0f;
        depth.view = (msaa ? m_msaaDepth : m_depth)->getDefaultView();
        pass.colorAttachments = &color;
        pass.colorAttachmentCount = 1;
        pass.depthStencilAttachment = nullptr;
        renderPass = encoder->beginRenderPass(pass);
        renderPass->pushDebugGroup("Procedural environment background", {});
        ShaderCursor background(renderPass->bindPipeline(msaa ? m_msaaBackgroundPipeline : m_backgroundPipeline));
        float3 forward = normalize(target - eye);
        float3 right = normalize(cross(forward, float3(0, 1, 0)));
        float scale = std::tan(0.65f * 0.5f);
        float3 cameraRight = right * (scale * float(m_width) / m_height);
        float3 cameraUp = cross(right, forward) * scale;
        SLANG_RETURN_ON_FAIL(background["frame"]["cameraRight"].setData(cameraRight));
        SLANG_RETURN_ON_FAIL(background["frame"]["cameraUp"].setData(cameraUp));
        SLANG_RETURN_ON_FAIL(background["frame"]["cameraForward"].setData(forward));
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
            SLANG_RETURN_ON_FAIL(cursor["frame"]["eye"].setData(eye));
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
        for (const auto& mesh : m_scene.meshes)
        {
            ShaderCursor cursor(renderPass->bindPipeline(msaa ? m_msaaScenePipeline : m_scenePipeline));
            SLANG_RETURN_ON_FAIL(bindSceneFrame(cursor));
            const auto& material = m_scene.materials[mesh.materialIndex];
            float roughness = mesh.materialIndex < 2 ? settings.roughness : material.roughness;
            SLANG_RETURN_ON_FAIL(cursor["material"]["baseColor"].setData(material.baseColor));
            SLANG_RETURN_ON_FAIL(cursor["material"]["metallic"].setData(material.metallic));
            SLANG_RETURN_ON_FAIL(cursor["material"]["roughness"].setData(roughness));
            uint32_t groundPattern = mesh.materialIndex == 2 ? 1 : 0;
            SLANG_RETURN_ON_FAIL(cursor["material"]["groundPattern"].setData(groundPattern));
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
        renderPass->end();

        if (settings.bloom)
        {
            for (uint32_t i = 0; i < 3; ++i)
            {
                auto computePass = encoder->beginComputePass();
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
                computePass->dispatchCompute(((m_width + 1) / 2 + 7) / 8, ((m_height + 1) / 2 + 7) / 8, 1);
                computePass->popDebugGroup();
                computePass->end();
            }
        }

        color.view = output->getDefaultView();
        color.resolveTarget = nullptr;
        color.loadOp = LoadOp::Clear;
        pass.depthStencilAttachment = nullptr;
        renderPass = encoder->beginRenderPass(pass);
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
        draw = {};
        draw.vertexCount = 3;
        renderPass->draw(draw);
        if (profiler)
            profiler->endRegion(renderPass, profileRegion);
        renderPass->popDebugGroup();
        renderPass->end();
        return SLANG_OK;
    }

    bool supportsMsaa() const { return m_msaaSupported; }

private:
    Result initSpheres()
    {
        // The logo and sphere meshes share the position/normal input layout.
        static_assert(sizeof(SphereVertex) == sizeof(logo::Vertex));
        static_assert(offsetof(SphereVertex, normal) == offsetof(logo::Vertex, normal));
        auto mesh = createSphereMesh();
        m_sphereIndexCount = uint32_t(mesh.indices.size());
        BufferDesc desc = {};
        desc.size = mesh.vertices.size() * sizeof(SphereVertex);
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

    Result createMsaaTargets(uint32_t width, uint32_t height, ITexture** color, ITexture** depth)
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

    Result initMsaa(const Programs& programs)
    {
        FormatSupport support = FormatSupport::None;
        SLANG_RETURN_ON_FAIL(m_device->getFormatSupport(Format::RGBA16Float, &support));
        if ((support & FormatSupport::Resolvable) == FormatSupport::None)
            return SLANG_E_NOT_AVAILABLE;
        ComPtr<ITexture> colorProbe, depthProbe;
        SLANG_RETURN_ON_FAIL(createMsaaTargets(1, 1, colorProbe.writeRef(), depthProbe.writeRef()));
        ColorTargetDesc color = {};
        color.format = Format::RGBA16Float;
        RenderPipelineDesc pipeline = {};
        pipeline.targets = &color;
        pipeline.targetCount = 1;
        pipeline.multisample.sampleCount = 4;
        pipeline.rasterizer.multisampleEnable = true;
        pipeline.depthStencil.depthWriteEnable = false;
        pipeline.program = programs.background;
        SLANG_RETURN_ON_FAIL(m_device->createRenderPipeline(pipeline, m_msaaBackgroundPipeline.writeRef()));
        pipeline.inputLayout = m_inputLayout;
        pipeline.depthStencil.format = Format::D32Float;
        pipeline.depthStencil.depthTestEnable = true;
        pipeline.depthStencil.depthWriteEnable = true;
        pipeline.program = programs.scene;
        SLANG_RETURN_ON_FAIL(m_device->createRenderPipeline(pipeline, m_msaaScenePipeline.writeRef()));
        pipeline.program = programs.spheres;
        return m_device->createRenderPipeline(pipeline, m_msaaSpherePipeline.writeRef());
    }

    Result createTexture(
        uint32_t width,
        uint32_t height,
        Format format,
        TextureUsage usage,
        const char* label,
        ITexture** out,
        uint32_t sampleCount = 1
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

    RenderState renderState(uint32_t width, uint32_t height, bool spheres = false) const
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

    ComPtr<IDevice> m_device;
    Format m_outputFormat = Format::Undefined;
    uint32_t m_width = 0, m_height = 0;
    logo::Scene m_scene;
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

} // namespace rhi::raster

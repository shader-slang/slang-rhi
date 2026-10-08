#pragma once

#include "../base/gpu-profiler.h"
#include "../base/logo-scene.h"
#include <slang-rhi/shader-cursor.h>

namespace rhi::particles {

struct Programs
{
    ComPtr<IShaderProgram> simulate, spawn, finalize, particles, logo, bloom, toneMap;
};

struct Particle
{
    math::float4 positionAge, velocityLife, colorSize;
};
static_assert(sizeof(Particle) == 48);

struct SimulationParams
{
    uint32_t capacity = 0, spawnCount = 400, tick = 0;
    float dt = 1.0f / 120.0f;
    math::float2 mouse = {};
    float mouseForce = 0, padding = 0;
};
static_assert(sizeof(SimulationParams) == 32);

class Renderer
{
public:
    static constexpr uint32_t kDefaultCapacity = 262144;

    Result init(IDevice* device, Format outputFormat, const Programs& programs, uint32_t capacity = kDefaultCapacity)
    {
        if (!capacity || capacity > kDefaultCapacity)
            return SLANG_E_INVALID_ARG;
        m_device = device;
        m_outputFormat = outputFormat;
        m_capacity = capacity;
        // There is no indirect-draw feature bit. This is an implementation gap
        // in the Metal backend, rather than a hardware capability restriction.
        m_indirect = device->getDeviceType() != DeviceType::Metal;
        FormatSupport support;
        SLANG_RETURN_ON_FAIL(device->getFormatSupport(Format::RGBA16Float, &support));
        if (!is_set(support, FormatSupport::RenderTarget | FormatSupport::ShaderSample | FormatSupport::ShaderUavStore))
            return SLANG_E_NOT_AVAILABLE;

        for (uint32_t i = 0; i < 2; ++i)
        {
            BufferDesc desc = {};
            desc.size = Size(capacity) * sizeof(Particle);
            desc.elementSize = sizeof(Particle);
            desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
            desc.label = "Particle state";
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, nullptr, m_particles[i].writeRef()));
            desc.size = sizeof(uint32_t);
            desc.elementSize = sizeof(uint32_t);
            desc.usage |= BufferUsage::CopyDestination;
            desc.label = "Particle live count";
            uint32_t zero = 0;
            SLANG_RETURN_ON_FAIL(device->createBuffer(desc, &zero, m_counts[i].writeRef()));
        }
        BufferDesc args = {};
        args.size = sizeof(IndirectDrawArguments);
        args.elementSize = 0;
        args.usage = BufferUsage::UnorderedAccess | BufferUsage::IndirectArgument | BufferUsage::CopySource |
                     BufferUsage::CopyDestination;
        args.label = "GPU particle draw arguments";
        uint32_t initialArgs[4] = {6, 0, 0, 0};
        SLANG_RETURN_ON_FAIL(device->createBuffer(args, initialArgs, m_arguments.writeRef()));

        ComputePipelineDesc compute = {};
        compute.program = programs.simulate;
        SLANG_RETURN_ON_FAIL(device->createComputePipeline(compute, m_simulate.writeRef()));
        compute.program = programs.spawn;
        SLANG_RETURN_ON_FAIL(device->createComputePipeline(compute, m_spawn.writeRef()));
        compute.program = programs.finalize;
        SLANG_RETURN_ON_FAIL(device->createComputePipeline(compute, m_finalize.writeRef()));
        compute.program = programs.bloom;
        SLANG_RETURN_ON_FAIL(device->createComputePipeline(compute, m_bloomPipeline.writeRef()));

        ColorTargetDesc target = {};
        target.format = Format::RGBA16Float;
        target.enableBlend = true;
        target.color.srcFactor = target.color.dstFactor = BlendFactor::One;
        target.alpha.srcFactor = BlendFactor::Zero;
        target.alpha.dstFactor = BlendFactor::One;
        RenderPipelineDesc pipeline = {};
        pipeline.program = programs.particles;
        pipeline.targets = &target;
        pipeline.targetCount = 1;
        pipeline.depthStencil.depthTestEnable = pipeline.depthStencil.depthWriteEnable = false;
        pipeline.rasterizer.cullMode = CullMode::None;
        SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_particlePipeline.writeRef()));

        BufferDesc mesh = {};
        mesh.size = sizeof(logo::kVertices);
        mesh.usage = BufferUsage::VertexBuffer;
        SLANG_RETURN_ON_FAIL(device->createBuffer(mesh, logo::kVertices, m_logoVertices.writeRef()));
        mesh.size = sizeof(logo::kIndices);
        mesh.usage = BufferUsage::IndexBuffer;
        SLANG_RETURN_ON_FAIL(device->createBuffer(mesh, logo::kIndices, m_logoIndices.writeRef()));
        VertexStreamDesc stream = {sizeof(logo::Vertex), InputSlotClass::PerVertex, 0};
        InputElementDesc element = {"POSITION", 0, Format::RGB32Float, 0, 0};
        InputLayoutDesc layout = {};
        layout.vertexStreams = &stream;
        layout.vertexStreamCount = 1;
        layout.inputElements = &element;
        layout.inputElementCount = 1;
        ComPtr<IInputLayout> inputLayout;
        SLANG_RETURN_ON_FAIL(device->createInputLayout(layout, inputLayout.writeRef()));
        target.enableBlend = false;
        pipeline.program = programs.logo;
        pipeline.inputLayout = inputLayout;
        SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_logoPipeline.writeRef()));
        target.format = outputFormat;
        pipeline.program = programs.toneMap;
        pipeline.inputLayout = nullptr;
        SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_toneMapPipeline.writeRef()));
        SamplerDesc sampler = {};
        sampler.minFilter = sampler.magFilter = TextureFilteringMode::Linear;
        sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressingMode::ClampToEdge;
        return device->createSampler(sampler, m_sampler.writeRef());
    }

    Result resize(uint32_t width, uint32_t height)
    {
        if (!width || !height)
            return SLANG_E_INVALID_ARG;
        TextureDesc desc = {};
        desc.size = {width, height, 1};
        desc.format = Format::RGBA16Float;
        desc.usage = TextureUsage::RenderTarget | TextureUsage::ShaderResource | TextureUsage::UnorderedAccess;
        desc.label = "Particle HDR";
        SLANG_RETURN_ON_FAIL(m_device->createTexture(desc, nullptr, m_hdr.writeRef()));
        desc.size = {(width + 1) / 2, (height + 1) / 2, 1};
        desc.label = "Particle bloom";
        for (auto& texture : m_bloom)
            SLANG_RETURN_ON_FAIL(m_device->createTexture(desc, nullptr, texture.writeRef()));
        m_width = width;
        m_height = height;
        return SLANG_OK;
    }

    void reset(ICommandEncoder* encoder)
    {
        for (auto& count : m_counts)
            encoder->clearBuffer(count);
        encoder->clearBuffer(m_arguments);
        m_current = 0;
        m_tick = 0;
    }

    Result step(ICommandEncoder* encoder, SimulationParams params, GpuProfiler* profiler = nullptr)
    {
        params.capacity = m_capacity;
        params.spawnCount = std::min(params.spawnCount, m_capacity);
        params.tick = m_tick++;
        uint32_t next = 1 - m_current;
        encoder->clearBuffer(m_counts[next]);
        auto pass = encoder->beginComputePass();
        uint32_t region = profiler ? profiler->beginRegion(pass, "Simulation step") : GpuProfiler::kInvalidRegion;
        pass->pushDebugGroup("Simulate and compact particles", {});
        ShaderCursor cursor(pass->bindPipeline(m_simulate));
        SLANG_RETURN_ON_FAIL(cursor["sim"].setData(params));
        SLANG_RETURN_ON_FAIL(cursor["inputParticles"].setBinding(m_particles[m_current]));
        SLANG_RETURN_ON_FAIL(cursor["inputCount"].setBinding(m_counts[m_current]));
        SLANG_RETURN_ON_FAIL(cursor["outputParticles"].setBinding(m_particles[next]));
        SLANG_RETURN_ON_FAIL(cursor["outputCount"].setBinding(m_counts[next]));
        pass->dispatchCompute((m_capacity + 127) / 128, 1, 1);
        pass->popDebugGroup();
        pass->end();

        pass = encoder->beginComputePass();
        pass->pushDebugGroup("Spawn particles", {});
        cursor = ShaderCursor(pass->bindPipeline(m_spawn));
        SLANG_RETURN_ON_FAIL(cursor["sim"].setData(params));
        SLANG_RETURN_ON_FAIL(cursor["outputParticles"].setBinding(m_particles[next]));
        SLANG_RETURN_ON_FAIL(cursor["outputCount"].setBinding(m_counts[next]));
        pass->dispatchCompute(std::max(1u, (params.spawnCount + 127) / 128), 1, 1);
        pass->popDebugGroup();
        pass->end();

        pass = encoder->beginComputePass();
        pass->pushDebugGroup("Generate particle draw arguments", {});
        cursor = ShaderCursor(pass->bindPipeline(m_finalize));
        SLANG_RETURN_ON_FAIL(cursor["sim"].setData(params));
        SLANG_RETURN_ON_FAIL(cursor["outputCount"].setBinding(m_counts[next]));
        SLANG_RETURN_ON_FAIL(cursor["drawArguments"].setBinding(m_arguments));
        pass->dispatchCompute(1, 1, 1);
        if (profiler)
            profiler->endRegion(pass, region);
        pass->popDebugGroup();
        pass->end();
        m_current = next;
        return SLANG_OK;
    }

    Result render(ICommandEncoder* encoder, ITexture* output, bool bloom = true, GpuProfiler* profiler = nullptr)
    {
        if (!m_hdr || output->getDesc().size.width != m_width || output->getDesc().size.height != m_height ||
            output->getDesc().format != m_outputFormat)
            return SLANG_E_INVALID_ARG;
        RenderPassColorAttachment color = {};
        color.view = m_hdr->getDefaultView();
        color.clearValue[0] = 0.001f;
        color.clearValue[1] = 0.002f;
        color.clearValue[2] = 0.005f;
        color.clearValue[3] = 1;
        RenderPassDesc desc = {};
        desc.colorAttachments = &color;
        desc.colorAttachmentCount = 1;
        auto pass = encoder->beginRenderPass(desc);
        uint32_t region = profiler ? profiler->beginRegion(pass, "Particles + logo") : GpuProfiler::kInvalidRegion;
        pass->pushDebugGroup("Particle HDR scene", {});
        float aspect = float(m_width) / m_height;
        auto state = renderState();
        state.vertexBuffers[0] = {m_logoVertices, 0};
        state.vertexBufferCount = 1;
        state.indexBuffer = {m_logoIndices, 0};
        state.indexFormat = IndexFormat::Uint32;
        pass->setRenderState(state);
        for (auto& mesh : logo::kMeshRanges)
        {
            ShaderCursor cursor(pass->bindPipeline(m_logoPipeline));
            SLANG_RETURN_ON_FAIL(cursor["aspect"].setData(aspect));
            math::float3 tint =
                mesh.materialIndex == 0 ? math::float3(1, 0.133f, 0.033f) : math::float3(0.014f, 0.515f, 0.584f);
            SLANG_RETURN_ON_FAIL(cursor["logoColor"].setData(tint));
            DrawArguments draw = {};
            draw.vertexCount = mesh.indexCount;
            draw.startIndexLocation = mesh.firstIndex;
            pass->drawIndexed(draw);
        }
        ShaderCursor cursor(pass->bindPipeline(m_particlePipeline));
        SLANG_RETURN_ON_FAIL(cursor["aspect"].setData(aspect));
        SLANG_RETURN_ON_FAIL(cursor["particles"].setBinding(m_particles[m_current]));
        SLANG_RETURN_ON_FAIL(cursor["liveCount"].setBinding(m_counts[m_current]));
        pass->setRenderState(renderState());
        if (m_indirect)
            pass->drawIndirect(1, {m_arguments, 0});
        else
        {
            DrawArguments draw = {};
            draw.vertexCount = 6;
            draw.instanceCount = m_capacity;
            pass->draw(draw);
        }
        if (profiler)
            profiler->endRegion(pass, region);
        pass->popDebugGroup();
        pass->end();

        if (bloom)
        {
            uint32_t bloomRegion = GpuProfiler::kInvalidRegion;
            for (uint32_t i = 0; i < 3; ++i)
            {
                auto compute = encoder->beginComputePass();
                if (profiler && i == 0)
                    bloomRegion = profiler->beginRegion(compute, "Bloom");
                compute->pushDebugGroup("Particle bloom", {});
                ShaderCursor filter(compute->bindPipeline(m_bloomPipeline));
                SLANG_RETURN_ON_FAIL(filter["source"].setBinding(i == 0 ? m_hdr.get() : m_bloom[(i - 1) % 2].get()));
                SLANG_RETURN_ON_FAIL(filter["destination"].setBinding(m_bloom[i % 2]));
                SLANG_RETURN_ON_FAIL(filter["linearSampler"].setBinding(m_sampler));
                SLANG_RETURN_ON_FAIL(filter["bloomPass"].setData(i));
                compute->dispatchCompute(((m_width + 1) / 2 + 7) / 8, ((m_height + 1) / 2 + 7) / 8, 1);
                if (profiler && i == 2)
                    profiler->endRegion(compute, bloomRegion);
                compute->popDebugGroup();
                compute->end();
            }
        }
        color.view = output->getDefaultView();
        pass = encoder->beginRenderPass(desc);
        region = profiler ? profiler->beginRegion(pass, "Tone map") : GpuProfiler::kInvalidRegion;
        cursor = ShaderCursor(pass->bindPipeline(m_toneMapPipeline));
        SLANG_RETURN_ON_FAIL(cursor["source"].setBinding(m_hdr));
        SLANG_RETURN_ON_FAIL(cursor["bloom"].setBinding(bloom ? m_bloom[0].get() : m_hdr.get()));
        SLANG_RETURN_ON_FAIL(cursor["linearSampler"].setBinding(m_sampler));
        SLANG_RETURN_ON_FAIL(cursor["exposure"].setData(0.0f));
        SLANG_RETURN_ON_FAIL(cursor["bloomStrength"].setData(bloom ? 0.3f : 0.0f));
        SLANG_RETURN_ON_FAIL(cursor["encodeSrgb"].setData(getFormatInfo(m_outputFormat).isSrgb ? 0u : 1u));
        pass->setRenderState(renderState());
        DrawArguments draw = {};
        draw.vertexCount = 3;
        pass->draw(draw);
        if (profiler)
            profiler->endRegion(pass, region);
        pass->end();
        return SLANG_OK;
    }

    IBuffer* countBuffer() const { return m_counts[m_current]; }
    IBuffer* particleBuffer() const { return m_particles[m_current]; }
    IBuffer* argumentBuffer() const { return m_arguments; }
    bool indirect() const { return m_indirect; }
    void setIndirect(bool enabled) { m_indirect = enabled && m_device->getDeviceType() != DeviceType::Metal; }
    uint32_t capacity() const { return m_capacity; }

private:
    RenderState renderState() const
    {
        RenderState state = {};
        state.viewports[0] = Viewport::fromSize(m_width, m_height);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(m_width, m_height);
        state.scissorRectCount = 1;
        return state;
    }

    ComPtr<IDevice> m_device;
    ComPtr<IBuffer> m_particles[2], m_counts[2], m_arguments, m_logoVertices, m_logoIndices;
    ComPtr<IComputePipeline> m_simulate, m_spawn, m_finalize, m_bloomPipeline;
    ComPtr<IRenderPipeline> m_particlePipeline, m_logoPipeline, m_toneMapPipeline;
    ComPtr<ISampler> m_sampler;
    ComPtr<ITexture> m_hdr, m_bloom[2];
    Format m_outputFormat = Format::Undefined;
    uint32_t m_capacity = 0, m_current = 0, m_tick = 0, m_width = 0, m_height = 0;
    bool m_indirect = true;
};

} // namespace rhi::particles

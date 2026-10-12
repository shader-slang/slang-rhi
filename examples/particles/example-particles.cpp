// GPU particle example: compute shaders simulate, compact, and spawn particles
// that gather into the Slang logo and burst apart. GPU-generated draw arguments
// drive indirect rendering, followed by bloom and tone mapping. Move the mouse
// to stir particles; the overlay lists the remaining controls.

#include "../base/example.h"
#include "../base/logo.h"
#include "settings.h"
#include <slang-rhi/shader-cursor.h>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace rhi;

// XY: center in logo coordinates; Z: scale into particle world space.
// Used for both target sampling and the rendered silhouette.
static constexpr math::float3 kLogoTransform = {0, 1.58f, 0.8f};

std::vector<math::float4> createLogoTargets();

struct Particle
{
    math::float4 positionAge, velocityLife, colorSize, targetStyle;
};
static_assert(sizeof(Particle) == 64);

struct SimulationParams
{
    uint32_t capacity = 0, spawnCount = 400, tick = 0;
    float dt = 1.0f / 120.0f;
    math::float2 mouse = {};
    float mouseForce = 0;
    uint32_t cycleTick = 0;
    math::float2 mousePrevious = {}, mouseVelocity = {};
};
static_assert(sizeof(SimulationParams) == 48);

class Renderer
{
public:
    static constexpr uint32_t kDefaultCapacity = 262144;

    Result init(IDevice* device, Format outputFormat, uint32_t capacity = kDefaultCapacity);

    Result resize(uint32_t width, uint32_t height);

    void reset(ICommandEncoder* encoder);

    Result step(ICommandEncoder* encoder, SimulationParams params);

    Result render(ICommandEncoder* encoder, ITexture* output, bool bloom = true);

    IBuffer* countBuffer() const;
    bool indirect() const;
    void setIndirect(bool enabled);
    uint32_t capacity() const;
    void burst();
    const char* phaseName() const;

private:
    RenderState renderState() const;

    ComPtr<IDevice> m_device;
    ComPtr<IBuffer> m_particles[2], m_counts[2], m_arguments, m_logoVertices, m_logoIndices;
    ComPtr<IBuffer> m_logoTargets;
    ComPtr<IComputePipeline> m_simulate, m_spawn, m_finalize, m_bloomPipeline;
    ComPtr<IRenderPipeline> m_particlePipeline, m_logoPipeline, m_toneMapPipeline;
    ComPtr<ISampler> m_sampler;
    ComPtr<ITexture> m_hdr, m_bloom[2];
    Format m_outputFormat = Format::Undefined;
    uint32_t m_capacity = 0, m_current = 0, m_tick = 0, m_width = 0, m_height = 0;
    uint32_t m_cycleTick = 0;
    float m_renderCycleTime = 0;
    bool m_indirect = true;
};

// Area-weighted samples of the visible logo surface, in particle world space.
// Upload once; spawning assigns a persistent target independently of compaction.
std::vector<math::float4> createLogoTargets()
{
    struct Triangle
    {
        math::float3 a, b, c;
        float cumulativeArea;
        uint32_t material;
    };
    std::vector<Triangle> triangles;
    float area = 0;
    for (const auto& mesh : logo::kMeshRanges)
        for (uint32_t i = mesh.firstIndex; i < mesh.firstIndex + mesh.indexCount; i += 3)
        {
            auto a = logo::kVertices[logo::kIndices[i]].position;
            auto b = logo::kVertices[logo::kIndices[i + 1]].position;
            auto c = logo::kVertices[logo::kIndices[i + 2]].position;
            float projectedArea = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (projectedArea <= 1e-8f || a.z + b.z + c.z <= 0)
                continue;
            area += projectedArea;
            triangles.push_back({a, b, c, area, mesh.materialIndex});
        }
    if (triangles.empty())
        return {};
    uint32_t seed = 0x51a9u;
    auto random = [&]()
    {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / 16777216.0f;
    };
    std::vector<math::float4> targets;
    targets.reserve(PARTICLE_LOGO_TARGET_COUNT);
    for (uint32_t i = 0; i < PARTICLE_LOGO_TARGET_COUNT; ++i)
    {
        // Stratify triangle selection to preserve even the smaller logo regions.
        float sampleArea = (float(i) + random()) / PARTICLE_LOGO_TARGET_COUNT * area;
        const auto& triangle = *std::lower_bound(
            triangles.begin(),
            triangles.end(),
            sampleArea,
            [](const Triangle& t, float value)
            {
                return t.cumulativeArea < value;
            }
        );
        float u = std::sqrt(random()), v = random();
        auto p = triangle.a * (1 - u) + triangle.b * (u * (1 - v)) + triangle.c * (u * v);
        targets.push_back(
            {(p.x - kLogoTransform.x) * kLogoTransform.z,
             (p.y - kLogoTransform.y) * kLogoTransform.z,
             float(triangle.material),
             0}
        );
    }
    return targets;
}


Result Renderer::init(IDevice* device, Format outputFormat, uint32_t capacity)
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
    const FormatSupport required =
        FormatSupport::RenderTarget | FormatSupport::ShaderSample | FormatSupport::ShaderUavStore;
    if ((support & required) != required)
        return SLANG_E_NOT_AVAILABLE;

    for (uint32_t i = 0; i < 2; ++i)
    {
        BufferDesc desc = {};
        desc.size = rhi::Size(capacity) * sizeof(Particle);
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

    auto targets = createLogoTargets();
    if (targets.size() != PARTICLE_LOGO_TARGET_COUNT)
        return SLANG_FAIL;
    BufferDesc targetBuffer = {};
    targetBuffer.size = targets.size() * sizeof(targets[0]);
    targetBuffer.elementSize = sizeof(targets[0]);
    targetBuffer.usage = BufferUsage::ShaderResource;
    targetBuffer.label = "Particle logo targets";
    SLANG_RETURN_ON_FAIL(device->createBuffer(targetBuffer, targets.data(), m_logoTargets.writeRef()));

    SLANG_RETURN_ON_FAIL(createComputePipeline(device, "particles.slang", "simulateMain", m_simulate.writeRef()));
    SLANG_RETURN_ON_FAIL(createComputePipeline(device, "particles.slang", "spawnMain", m_spawn.writeRef()));
    SLANG_RETURN_ON_FAIL(createComputePipeline(device, "particles.slang", "finalizeMain", m_finalize.writeRef()));
    SLANG_RETURN_ON_FAIL(createComputePipeline(device, "../base/post.slang", "bloomMain", m_bloomPipeline.writeRef()));

    ComPtr<IShaderProgram> program;
    SLANG_RETURN_ON_FAIL(
        createProgram(device, "particles.slang", {"particleVertex", "particleFragment"}, program.writeRef())
    );
    ColorTargetDesc target = {};
    target.format = Format::RGBA16Float;
    target.enableBlend = true;
    target.color.srcFactor = target.color.dstFactor = BlendFactor::One;
    target.alpha.srcFactor = BlendFactor::Zero;
    target.alpha.dstFactor = BlendFactor::One;
    RenderPipelineDesc pipeline = {};
    pipeline.program = program;
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
    VertexStreamDesc stream = {sizeof(geometry::Vertex), InputSlotClass::PerVertex, 0};
    InputElementDesc element = {"POSITION", 0, Format::RGB32Float, 0, 0};
    InputLayoutDesc layout = {};
    layout.vertexStreams = &stream;
    layout.vertexStreamCount = 1;
    layout.inputElements = &element;
    layout.inputElementCount = 1;
    ComPtr<IInputLayout> inputLayout;
    SLANG_RETURN_ON_FAIL(device->createInputLayout(layout, inputLayout.writeRef()));
    target.enableBlend = false;
    SLANG_RETURN_ON_FAIL(createProgram(device, "particles.slang", {"logoVertex", "logoFragment"}, program.writeRef()));
    pipeline.program = program;
    pipeline.inputLayout = inputLayout;
    SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_logoPipeline.writeRef()));
    target.format = outputFormat;
    SLANG_RETURN_ON_FAIL(
        createProgram(device, "../base/post.slang", {"fullscreenVertex", "toneMapFragment"}, program.writeRef())
    );
    pipeline.program = program;
    pipeline.inputLayout = nullptr;
    SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipeline, m_toneMapPipeline.writeRef()));
    SamplerDesc sampler = {};
    sampler.minFilter = sampler.magFilter = TextureFilteringMode::Linear;
    sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressingMode::ClampToEdge;
    return device->createSampler(sampler, m_sampler.writeRef());
}

Result Renderer::resize(uint32_t width, uint32_t height)
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

void Renderer::reset(ICommandEncoder* encoder)
{
    for (auto& count : m_counts)
        encoder->clearBuffer(count);
    encoder->clearBuffer(m_arguments);
    m_current = 0;
    m_tick = 0;
    m_cycleTick = 0;
    m_renderCycleTime = 0;
}

Result Renderer::step(ICommandEncoder* encoder, SimulationParams params)
{
    params.capacity = m_capacity;
    params.spawnCount = std::min(params.spawnCount, m_capacity);
    params.tick = m_tick++;
    params.cycleTick = m_cycleTick;
    m_renderCycleTime = float(m_cycleTick) / 120.0f;
    m_cycleTick = (m_cycleTick + 1) % PARTICLE_CYCLE_TICKS;
    uint32_t next = 1 - m_current;
    encoder->clearBuffer(m_counts[next]);
    auto pass = encoder->beginComputePass();
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
    SLANG_RETURN_ON_FAIL(cursor["logoTargets"].setBinding(m_logoTargets));
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

    pass->popDebugGroup();
    pass->end();
    m_current = next;
    return SLANG_OK;
}

Result Renderer::render(ICommandEncoder* encoder, ITexture* output, bool bloom)
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
        SLANG_RETURN_ON_FAIL(cursor["logoTransform"].setData(kLogoTransform));
        SLANG_RETURN_ON_FAIL(cursor["logoColor"].setData(logo::kColors[mesh.materialIndex]));
        SLANG_RETURN_ON_FAIL(cursor["cycleTime"].setData(m_renderCycleTime));
        DrawArguments draw = {};
        draw.vertexCount = mesh.indexCount;
        draw.startIndexLocation = mesh.firstIndex;
        pass->drawIndexed(draw);
    }
    ShaderCursor cursor(pass->bindPipeline(m_particlePipeline));
    SLANG_RETURN_ON_FAIL(cursor["aspect"].setData(aspect));
    SLANG_RETURN_ON_FAIL(cursor["cycleTime"].setData(m_renderCycleTime));
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

    pass->popDebugGroup();
    pass->end();

    if (bloom)
    {
        for (uint32_t i = 0; i < 3; ++i)
        {
            auto compute = encoder->beginComputePass();

            compute->pushDebugGroup("Particle bloom", {});
            ShaderCursor filter(compute->bindPipeline(m_bloomPipeline));
            SLANG_RETURN_ON_FAIL(filter["source"].setBinding(i == 0 ? m_hdr.get() : m_bloom[(i - 1) % 2].get()));
            SLANG_RETURN_ON_FAIL(filter["destination"].setBinding(m_bloom[i % 2]));
            SLANG_RETURN_ON_FAIL(filter["linearSampler"].setBinding(m_sampler));
            SLANG_RETURN_ON_FAIL(filter["bloomPass"].setData(i));
            compute->dispatchCompute(((m_width + 1) / 2 + 7) / 8, ((m_height + 1) / 2 + 7) / 8, 1);

            compute->popDebugGroup();
            compute->end();
        }
    }
    color.view = output->getDefaultView();
    pass = encoder->beginRenderPass(desc);
    cursor = ShaderCursor(pass->bindPipeline(m_toneMapPipeline));
    SLANG_RETURN_ON_FAIL(cursor["source"].setBinding(m_hdr));
    SLANG_RETURN_ON_FAIL(cursor["bloom"].setBinding(bloom ? m_bloom[0].get() : m_hdr.get()));
    SLANG_RETURN_ON_FAIL(cursor["linearSampler"].setBinding(m_sampler));
    SLANG_RETURN_ON_FAIL(cursor["exposure"].setData(0.0f));
    SLANG_RETURN_ON_FAIL(cursor["bloomStrength"].setData(bloom ? 0.35f : 0.0f));
    SLANG_RETURN_ON_FAIL(cursor["encodeSrgb"].setData(getFormatInfo(m_outputFormat).isSrgb ? 0u : 1u));
    pass->setRenderState(renderState());
    DrawArguments draw = {};
    draw.vertexCount = 3;
    pass->draw(draw);

    pass->end();
    return SLANG_OK;
}

IBuffer* Renderer::countBuffer() const
{
    return m_counts[m_current];
}

bool Renderer::indirect() const
{
    return m_indirect;
}

void Renderer::setIndirect(bool enabled)
{
    m_indirect = enabled && m_device->getDeviceType() != DeviceType::Metal;
}

uint32_t Renderer::capacity() const
{
    return m_capacity;
}

void Renderer::burst()
{
    m_cycleTick = PARTICLE_BURST_TICK;
}

const char* Renderer::phaseName() const
{
    if (m_renderCycleTime < 3)
        return "Flow";
    if (m_renderCycleTime < 7)
        return "Gather";
    if (m_renderCycleTime < 10)
        return "Form";
    if (m_renderCycleTime < 11)
        return "Burst";
    return "Swirl";
}

RenderState Renderer::renderState() const
{
    RenderState state = {};
    state.viewports[0] = Viewport::fromSize(m_width, m_height);
    state.viewportCount = 1;
    state.scissorRects[0] = ScissorRect::fromSize(m_width, m_height);
    state.scissorRectCount = 1;
    return state;
}

class ExampleParticles : public ExampleBase
{
public:
    Result init(DeviceType deviceType) override
    {
        SLANG_RETURN_ON_FAIL(
            createDevice(deviceType, {Feature::Surface, Feature::Rasterization}, {}, m_device.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createWindow(m_device, "Particle energy sculpture", 960, 720));
        SLANG_RETURN_ON_FAIL(createSurface(m_device, Format::Undefined, m_surface.writeRef()));
        SLANG_RETURN_ON_FAIL(initOverlay(m_device, m_surface->getConfig()->format));
        SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));
        m_renderer = std::make_unique<Renderer>();
        SLANG_RETURN_ON_FAIL(m_renderer->init(m_device, m_surface->getConfig()->format));
        SLANG_RETURN_ON_FAIL(m_renderer->resize(m_surface->getConfig()->width, m_surface->getConfig()->height));

        // Live-count telemetry is independent of simulation. D3D11
        // has no RHI fence implementation, so it simply omits this optional HUD value.
        if (deviceType != DeviceType::D3D11)
        {
            FenceDesc fence = {};
            SLANG_RETURN_ON_FAIL(m_device->createFence(fence, m_readbackFence.writeRef()));
            for (auto& slot : m_readbacks)
            {
                BufferDesc desc = {};
                desc.size = sizeof(uint32_t);
                desc.usage = BufferUsage::CopyDestination;
                desc.memoryType = MemoryType::ReadBack;
                SLANG_RETURN_ON_FAIL(m_device->createBuffer(desc, nullptr, slot.buffer.writeRef()));
            }
        }
        return SLANG_OK;
    }

    void shutdown() override
    {
        if (m_queue)
            m_queue->waitOnHost();
        m_renderer.reset();
        m_readbacks = {};
        m_readbackFence.setNull();
        m_surface.setNull();
        m_queue.setNull();
        m_device.setNull();
    }

    Result update(double time) override
    {
        bool visible = m_surface->getConfig() != nullptr;
        m_steps = m_clock.update(time, visible && !m_paused);
        if (visible && m_singleStep && m_paused)
            m_steps = 1;
        m_singleStep = false;
        // Do not replay cursor travel collected while paused or minimized.
        if (!visible || (m_paused && !m_steps))
            m_mouseSimulatedWorld = m_mouseWorld;
        return SLANG_OK;
    }

    Result draw() override
    {
        if (!m_surface->getConfig())
            return SLANG_OK;
        ComPtr<ITexture> image;
        // A native resize can invalidate the swapchain before its queued callback.
        acquireSurfaceImage(image.writeRef());
        if (!image)
            return SLANG_OK;
        SLANG_RETURN_ON_FAIL(pollReadbacks());
        ComPtr<ICommandEncoder> encoder;
        SLANG_RETURN_ON_FAIL(m_queue->createCommandEncoder(encoder.writeRef()));
        if (m_reset)
        {
            m_renderer->reset(encoder);
            m_reset = false;
            m_mouseSimulatedWorld = m_mouseWorld;
        }
        SimulationParams params;
        params.spawnCount = m_emissionPerStep;
        params.mouse = m_mouseWorld;
        params.mouseForce = isMouseDown(GLFW_MOUSE_BUTTON_RIGHT)  ? -18.0f
                            : isMouseDown(GLFW_MOUSE_BUTTON_LEFT) ? 18.0f
                                                                  : 0.0f;
        // Distribute pending motion over the fixed steps, consuming it once.
        // Frames with no simulation step retain it for the next step.
        math::float2 travel = m_mouseWorld - m_mouseSimulatedWorld;
        if (m_mouseValid && m_steps)
        {
            params.mouseVelocity = travel / (float(m_steps) * params.dt);
            float speed = math::length(params.mouseVelocity);
            if (speed > 12)
                params.mouseVelocity = params.mouseVelocity * (12 / speed);
        }
        for (uint32_t i = 0; i < m_steps; ++i)
        {
            params.mousePrevious = m_mouseSimulatedWorld + travel * (float(i) / m_steps);
            params.mouse = m_mouseSimulatedWorld + travel * (float(i + 1) / m_steps);
            SLANG_RETURN_ON_FAIL(m_renderer->step(encoder, params));
        }
        if (m_steps)
            m_mouseSimulatedWorld = m_mouseWorld;
        SLANG_RETURN_ON_FAIL(m_renderer->render(encoder, image, m_bloom));
        SLANG_RETURN_ON_FAIL(drawStatusOverlay(encoder, image));

        Readback* readback = nullptr;
        if (m_readbackFence && m_frameNumber++ % 30 == 0)
        {
            for (auto& slot : m_readbacks)
                if (!slot.value)
                {
                    readback = &slot;
                    encoder->copyBuffer(slot.buffer, 0, m_renderer->countBuffer(), 0, sizeof(uint32_t));
                    break;
                }
        }
        ComPtr<ICommandBuffer> commands;
        SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
        ICommandBuffer* commandPointer = commands;
        IFence* fencePointer = m_readbackFence;
        SubmitDesc submit = {};
        submit.commandBuffers = &commandPointer;
        submit.commandBufferCount = 1;
        if (readback)
        {
            readback->value = ++m_fenceValue;
            readback->generation = m_generation;
            submit.signalFences = &fencePointer;
            submit.signalFenceValues = &readback->value;
            submit.signalFenceCount = 1;
        }
        SLANG_RETURN_ON_FAIL(m_queue->submit(submit));
        return presentSurface();
    }

    Result onResize(int width, int height, int framebufferWidth, int framebufferHeight) override
    {
        if (!m_queue || !m_renderer)
            return SLANG_OK;
        m_clock.reset();
        m_mouseValid = false;
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

    void onMouseEnter(bool entered) override
    {
        // Start a fresh sweep when entering or leaving any backend window.
        m_mouseValid = false;
        m_mouseSimulatedWorld = m_mouseWorld;
    }

    void onMousePosition(float x, float y) override
    {
        // Backend window sizes are synchronized, so use our cached window size.
        int width = getWindowWidth(), height = getWindowHeight();
        if (width > 0 && height > 0)
        {
            m_mouseWorld = {(2 * x / width - 1) * 3.6f * float(width) / height, (1 - 2 * y / height) * 3.6f};
            bool inside = x >= 0 && y >= 0 && x < width && y < height;
            if (!m_mouseValid || !inside)
                m_mouseSimulatedWorld = m_mouseWorld;
            m_mouseValid = inside;
        }
    }

    void onKey(int key, int scancode, int action, int mods) override
    {
        if (action != GLFW_PRESS && action != GLFW_REPEAT)
            return;
        if (key == GLFW_KEY_LEFT_BRACKET)
            m_emissionPerStep = m_emissionPerStep > 100 ? m_emissionPerStep - 100 : 0;
        if (key == GLFW_KEY_RIGHT_BRACKET)
            m_emissionPerStep = std::min(4000u, m_emissionPerStep + 100);
        if (action != GLFW_PRESS)
            return;
        if (key == GLFW_KEY_SPACE)
        {
            m_paused = !m_paused;
            m_mouseSimulatedWorld = m_mouseWorld;
        }
        if (key == GLFW_KEY_N)
            m_singleStep = true;
        if (key == GLFW_KEY_B)
            m_bloom = !m_bloom;
        if (key == GLFW_KEY_D)
            m_renderer->setIndirect(!m_renderer->indirect());
        if (key == GLFW_KEY_F)
            m_renderer->burst();
        if (key == GLFW_KEY_R)
        {
            m_reset = true;
            m_clock.reset();
            m_countValid = false;
            ++m_generation;
        }
    }

private:
    struct Readback
    {
        ComPtr<IBuffer> buffer;
        uint64_t value = 0, generation = 0;
    };

    Result pollReadbacks()
    {
        if (!m_readbackFence)
            return SLANG_OK;
        uint64_t completed;
        SLANG_RETURN_ON_FAIL(m_readbackFence->getCurrentValue(&completed));
        for (auto& slot : m_readbacks)
        {
            if (!slot.value || slot.value > completed)
                continue;
            if (slot.generation == m_generation && slot.value > m_lastReadback)
            {
                void* data;
                SLANG_RETURN_ON_FAIL(m_device->mapBuffer(slot.buffer, CpuAccessMode::Read, &data));
                m_liveCount = *static_cast<const uint32_t*>(data);
                SLANG_RETURN_ON_FAIL(m_device->unmapBuffer(slot.buffer));
                m_countValid = true;
                m_lastReadback = slot.value;
            }
            slot.value = 0;
        }
        return SLANG_OK;
    }

    Result drawStatusOverlay(ICommandEncoder* encoder, ITexture* image)
    {
        clearOverlay();
        printOverlayHeader();
        printOverlay("Move mouse: Stir | Left: Attract | Right: Repel\n");
        printOverlay("[ / ]: Emission | Space: Pause | N: Single step\n");
        printOverlay("F: Burst | R: Reset | B: Bloom | D: Direct/indirect draw\n\n");
        printOverlay(
            "%s | Emission: %u/sec | %s | Bloom: %s\n",
            m_renderer->indirect() ? "Indirect draw" : "Direct draw",
            m_emissionPerStep * 120,
            m_paused ? "Paused" : "Running",
            m_bloom ? "on" : "off"
        );
        if (m_countValid)
            printOverlay("Live particles: %u / %u (delayed)\n", m_liveCount, m_renderer->capacity());
        else
            printOverlay("Live particles: %s\n", m_readbackFence ? "pending" : "readback unavailable");
        printOverlay("Phase: %s", m_renderer->phaseName());
        return drawOverlay(encoder, image);
    }

    ComPtr<IDevice> m_device;
    ComPtr<ISurface> m_surface;
    ComPtr<ICommandQueue> m_queue;
    std::unique_ptr<Renderer> m_renderer;
    ComPtr<IFence> m_readbackFence;
    std::array<Readback, 3> m_readbacks;
    uint64_t m_fenceValue = 0, m_lastReadback = 0, m_generation = 0, m_frameNumber = 0;
    FixedStepClock m_clock;
    math::float2 m_mouseWorld = {}, m_mouseSimulatedWorld = {};
    bool m_mouseValid = false;
    uint32_t m_steps = 0, m_emissionPerStep = 400, m_liveCount = 0;
    bool m_paused = false, m_singleStep = false, m_reset = true, m_bloom = true, m_countValid = false;
};

EXAMPLE_MAIN(ExampleParticles)

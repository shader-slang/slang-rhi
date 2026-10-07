#pragma once

#include "bitmap-font.h"
#include "math.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <string_view>
#include <vector>

namespace rhi {

// Minimal ASCII bitmap text for example HUDs. Coordinates and scale are in
// framebuffer pixels. Call clear(), addText(), then render() after tone mapping.
// A batch uses one atlas, one buffer upload, and one alpha-blended draw.
class TextRenderer
{
public:
    static constexpr size_t kMaxGlyphs = 4096;

    Result init(IDevice* device, Format outputFormat, IShaderProgram* program)
    {
        m_outputFormat = outputFormat;
        m_vertices.reserve(kMaxGlyphs * 6);
        auto pixels = bitmap_font::createAtlas();
        TextureDesc texture = {};
        texture.size = {bitmap_font::kWidth, bitmap_font::kHeight, 1};
        texture.format = Format::R8Unorm;
        texture.usage = TextureUsage::ShaderResource;
        texture.label = "HUD font atlas";
        SubresourceData data = {};
        data.data = pixels.data();
        data.rowPitch = bitmap_font::kWidth;
        data.slicePitch = pixels.size();
        SLANG_RETURN_ON_FAIL(device->createTexture(texture, &data, m_atlas.writeRef()));
        SamplerDesc sampler = {};
        sampler.minFilter = sampler.magFilter = sampler.mipFilter = TextureFilteringMode::Point;
        sampler.addressU = sampler.addressV = TextureAddressingMode::ClampToEdge;
        SLANG_RETURN_ON_FAIL(device->createSampler(sampler, m_sampler.writeRef()));

        BufferDesc buffer = {};
        buffer.size = kMaxGlyphs * 6 * sizeof(Vertex);
        buffer.usage = BufferUsage::VertexBuffer | BufferUsage::CopyDestination;
        buffer.label = "HUD text vertices";
        SLANG_RETURN_ON_FAIL(device->createBuffer(buffer, nullptr, m_buffer.writeRef()));
        VertexStreamDesc stream = {sizeof(Vertex), InputSlotClass::PerVertex, 0};
        InputElementDesc elements[] = {
            {"POSITION", 0, Format::RG32Float, offsetof(Vertex, position), 0},
            {"TEXCOORD", 0, Format::RG32Float, offsetof(Vertex, uv), 0},
            {"COLOR", 0, Format::RGBA32Float, offsetof(Vertex, color), 0},
        };
        InputLayoutDesc input = {};
        input.vertexStreams = &stream;
        input.vertexStreamCount = 1;
        input.inputElements = elements;
        input.inputElementCount = 3;
        SLANG_RETURN_ON_FAIL(device->createInputLayout(input, m_inputLayout.writeRef()));
        ColorTargetDesc color = {};
        color.format = outputFormat;
        color.enableBlend = true;
        color.color.srcFactor = BlendFactor::SrcAlpha;
        color.color.dstFactor = BlendFactor::InvSrcAlpha;
        color.alpha.srcFactor = BlendFactor::One;
        color.alpha.dstFactor = BlendFactor::InvSrcAlpha;
        RenderPipelineDesc pipeline = {};
        pipeline.program = program;
        pipeline.inputLayout = m_inputLayout;
        pipeline.targets = &color;
        pipeline.targetCount = 1;
        pipeline.depthStencil.depthWriteEnable = false;
        return device->createRenderPipeline(pipeline, m_pipeline.writeRef());
    }

    void clear() { m_vertices.clear(); }

    // A failed capacity check leaves the existing batch unchanged. Newlines and
    // spaces advance the pen without consuming glyph capacity. Tabs are 4 spaces.
    Result addText(std::string_view text, math::float2 origin, float scale, math::float4 color = math::float4(1))
    {
        if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(origin.x) || !std::isfinite(origin.y))
            return SLANG_E_INVALID_ARG;
        size_t glyphCount = 0;
        for (unsigned char c : text)
            glyphCount += c != ' ' && c != '\n' && c != '\r' && c != '\t';
        if (glyphCount > kMaxGlyphs - m_vertices.size() / 6)
            return SLANG_E_OUT_OF_MEMORY;
        math::float2 pen = origin;
        for (unsigned char c : text)
        {
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                pen.x = origin.x;
                pen.y += 9 * scale;
                continue;
            }
            if (c == ' ' || c == '\t')
            {
                pen.x += (c == '\t' ? 24 : 6) * scale;
                continue;
            }
            uint32_t index = bitmap_font::glyphCharacter(c) - ' ';
            math::float2 cell(float(index % 16) * 8, float(index / 16) * 8);
            for (math::float2 corner : {math::float2(0, 0), {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}})
            {
                m_vertices.push_back(
                    {pen + corner * (8 * scale),
                     (cell + corner * 8) / math::float2(float(bitmap_font::kWidth), float(bitmap_font::kHeight)),
                     color}
                );
            }
            pen.x += 6 * scale;
        }
        return SLANG_OK;
    }

    Result render(ICommandEncoder* encoder, ITexture* target)
    {
        if (!encoder || !target || !m_pipeline || target->getDesc().format != m_outputFormat)
            return SLANG_E_INVALID_ARG;
        if (m_vertices.empty())
            return SLANG_OK;
        uint32_t width = target->getDesc().size.width, height = target->getDesc().size.height;
        if (!width || !height)
            return SLANG_E_INVALID_ARG;
        // Upload is recorded into the same queue as the draw, so successive
        // frames safely reuse this buffer without a host wait or mapped writes.
        SLANG_RETURN_ON_FAIL(
            encoder->uploadBufferData(m_buffer, 0, m_vertices.size() * sizeof(Vertex), m_vertices.data())
        );
        RenderPassColorAttachment color = {};
        color.view = target->getDefaultView();
        color.loadOp = LoadOp::Load;
        RenderPassDesc desc = {};
        desc.colorAttachments = &color;
        desc.colorAttachmentCount = 1;
        auto pass = encoder->beginRenderPass(desc);
        pass->pushDebugGroup("Text overlay", {});
        ShaderCursor cursor(pass->bindPipeline(m_pipeline));
        SLANG_RETURN_ON_FAIL(cursor["fontAtlas"].setBinding(m_atlas));
        SLANG_RETURN_ON_FAIL(cursor["fontSampler"].setBinding(m_sampler));
        math::float2 inverseSize(1.0f / width, 1.0f / height);
        SLANG_RETURN_ON_FAIL(cursor["inverseTargetSize"].setData(inverseSize));
        uint32_t encodeSrgb = getFormatInfo(m_outputFormat).isSrgb ? 0 : 1;
        SLANG_RETURN_ON_FAIL(cursor["encodeSrgb"].setData(encodeSrgb));
        RenderState state = {};
        state.viewports[0] = Viewport::fromSize(width, height);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(width, height);
        state.scissorRectCount = 1;
        state.vertexBuffers[0] = {m_buffer, 0};
        state.vertexBufferCount = 1;
        pass->setRenderState(state);
        DrawArguments draw = {};
        draw.vertexCount = uint32_t(m_vertices.size());
        pass->draw(draw);
        pass->popDebugGroup();
        pass->end();
        return SLANG_OK;
    }

private:
    struct Vertex
    {
        math::float2 position;
        math::float2 uv;
        math::float4 color;
    };

    Format m_outputFormat = Format::Undefined;
    std::vector<Vertex> m_vertices;
    ComPtr<ITexture> m_atlas;
    ComPtr<ISampler> m_sampler;
    ComPtr<IBuffer> m_buffer;
    ComPtr<IInputLayout> m_inputLayout;
    ComPtr<IRenderPipeline> m_pipeline;
};

} // namespace rhi

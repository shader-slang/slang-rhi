#pragma once

#include "math.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace rhi::bitmap_font {

// Small hand-defined 5x7 glyphs, stored in 8x8 atlas cells. Lowercase ASCII is
// displayed as uppercase. Other characters use '?'; this is an example HUD font.
struct Glyph
{
    char character;
    uint8_t rows[7];
};

// clang-format off
inline constexpr Glyph kGlyphs[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}},
    {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {14, 4, 4, 4, 4, 4, 14}},
    {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}},
    {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'N', {17, 25, 25, 21, 19, 19, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}},
    {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}},
    {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},
    {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'.', {0, 0, 0, 0, 0, 12, 12}},
    {',', {0, 0, 0, 0, 0, 4, 8}},
    {':', {0, 4, 4, 0, 4, 4, 0}},
    {';', {0, 4, 4, 0, 4, 4, 8}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'+', {0, 4, 4, 31, 4, 4, 0}},
    {'=', {0, 0, 31, 0, 31, 0, 0}},
    {'/', {1, 2, 2, 4, 8, 8, 16}},
    {'\\', {16, 8, 8, 4, 2, 2, 1}},
    {'[', {14, 8, 8, 8, 8, 8, 14}},
    {']', {14, 2, 2, 2, 2, 2, 14}},
    {'(', {2, 4, 8, 8, 8, 4, 2}},
    {')', {8, 4, 2, 2, 2, 4, 8}},
    {'<', {1, 2, 4, 8, 4, 2, 1}},
    {'>', {16, 8, 4, 2, 4, 8, 16}},
    {'!', {4, 4, 4, 4, 4, 0, 4}},
    {'?', {14, 17, 1, 2, 4, 0, 4}},
    {'_', {0, 0, 0, 0, 0, 0, 31}},
    {'|', {4, 4, 4, 4, 4, 4, 4}},
    {'%', {25, 25, 2, 4, 8, 19, 19}},
    {'*', {0, 21, 14, 31, 14, 21, 0}},
    {'\'', {4, 4, 8, 0, 0, 0, 0}},
    {'"', {10, 10, 10, 0, 0, 0, 0}},
};
// clang-format on

inline constexpr uint32_t kWidth = 128, kHeight = 48;

inline unsigned char glyphCharacter(unsigned char c)
{
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    for (const auto& glyph : kGlyphs)
        if (glyph.character == c)
            return c;
    return '?';
}

inline std::array<uint8_t, kWidth * kHeight> createAtlas()
{
    std::array<uint8_t, kWidth * kHeight> pixels = {};
    for (const auto& glyph : kGlyphs)
    {
        uint32_t index = uint32_t(glyph.character - ' ');
        uint32_t x = (index % 16) * 8, y = (index / 16) * 8;
        for (uint32_t row = 0; row < 7; ++row)
            for (uint32_t column = 0; column < 5; ++column)
                pixels[(y + row) * kWidth + x + column] = (glyph.rows[row] & (16 >> column)) ? 255 : 0;
    }
    return pixels;
}

} // namespace rhi::bitmap_font

namespace rhi {

// Minimal ASCII bitmap text for example HUDs. Coordinates and scale are in
// framebuffer pixels. Call clear(), addText(), then render() after tone mapping.
// Colors are linear. Non-sRGB normalized targets are assumed to store sRGB-encoded
// SDR output; floating-point targets retain linear values.
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
        // An opaque atlas texel also lets HUDs draw solid, alpha-blended panels.
        for (size_t i = 0; i < pixels.size(); ++i)
            if (pixels[i] == 255)
            {
                m_solidUv = {
                    (float(i % bitmap_font::kWidth) + 0.5f) / bitmap_font::kWidth,
                    (float(i / bitmap_font::kWidth) + 0.5f) / bitmap_font::kHeight
                };
                break;
            }
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

    // Call before addText() to place a translucent panel behind the labels.
    Result addRect(math::float2 origin, math::float2 size, math::float4 color)
    {
        if (m_vertices.size() + 6 > kMaxGlyphs * 6)
            return SLANG_E_OUT_OF_MEMORY;
        for (math::float2 corner : {math::float2(0, 0), {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}})
            m_vertices.push_back({origin + corner * size, m_solidUv, color});
        return SLANG_OK;
    }

    // A failed capacity check leaves the existing batch unchanged. Newlines and
    // spaces advance the pen without consuming glyph capacity. Tabs are 4 spaces.
    Result addText(std::string_view text, math::float2 origin, float scale, math::float4 color = math::float4(1))
    {
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
        const auto& formatInfo = getFormatInfo(m_outputFormat);
        uint32_t encodeSrgb = formatInfo.kind == FormatKind::Normalized && !formatInfo.isSrgb ? 1 : 0;
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
    math::float2 m_solidUv = {};
    std::vector<Vertex> m_vertices;
    ComPtr<ITexture> m_atlas;
    ComPtr<ISampler> m_sampler;
    ComPtr<IBuffer> m_buffer;
    ComPtr<IInputLayout> m_inputLayout;
    ComPtr<IRenderPipeline> m_pipeline;
};

} // namespace rhi

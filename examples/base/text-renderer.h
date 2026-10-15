#pragma once

#include "utils.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <array>
#include <cstdint>
#include <algorithm>
#include <string>
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

// A fixed-width text grid with a translucent background panel. Call clear(),
// append(), then render() after tone mapping. Layout uses the target dimensions
// and clips to the screen; pixelRatio and scale control the font size.
// Raster and compute paths share the same glyph lookup and premultiplied colors.
class TextRenderer
{
public:
    static constexpr uint32_t kMaxCells = 4096;
    math::float4 textColor = {0.82f, 0.88f, 0.96f, 1};
    math::float4 panelColor = {0.003f, 0.006f, 0.012f, 0.85f};
    float margin = 12.f;

    Result init(IDevice* device, Format outputFormat)
    {
        m_device = device;
        m_outputFormat = outputFormat;
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
        BufferDesc buffer = {};
        buffer.size = kMaxCells * sizeof(uint32_t);
        buffer.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
        buffer.label = "HUD character grid";
        return device->createBuffer(buffer, nullptr, m_buffer.writeRef());
    }

    void clear() { m_text.clear(); }

    // Normalize once so wrapping and glyph lookup use the same character widths.
    void append(std::string_view text)
    {
        for (unsigned char c : text)
        {
            if (c == '\t')
                m_text.append(4, ' ');
            else if (c != '\r')
                m_text += c == '\n' ? '\n' : char(bitmap_font::glyphCharacter(c));
        }
    }

    Result render(ICommandEncoder* encoder, ITexture* target, float pixelRatio = 1.f, float scale = 1.5f)
    {
        if (m_text.empty())
            return SLANG_OK;
        if (!encoder || !target || !m_device || target->getDesc().format != m_outputFormat)
            return SLANG_E_INVALID_ARG;
        const auto& desc = target->getDesc();
        uint32_t width = desc.size.width, height = desc.size.height;
        float padding = margin * pixelRatio;
        float fontScale = 1.5f * scale * pixelRatio;
        if (!width || !height || !std::isfinite(padding) || padding < 0 || !std::isfinite(fontScale) || fontScale <= 0)
            return SLANG_E_INVALID_ARG;
        if (padding >= width || padding >= height)
            return SLANG_OK;

        // Bound the grid by both the screen and its fixed upload buffer. Rows
        // below the screen are omitted; long words wrap at the column limit.
        uint32_t maxColumns =
            uint32_t(std::clamp(std::floor((width - 4 * padding) / (6 * fontScale)), 1.f, float(kMaxCells)));
        uint32_t maxRows = uint32_t(
            std::clamp(std::ceil((height - 2 * padding) / (9 * fontScale)), 1.f, float(kMaxCells / maxColumns))
        );
        std::vector<std::string_view> lines;
        std::string_view remaining(m_text);
        uint32_t columns = 1;
        while (lines.size() < maxRows)
        {
            size_t newline = remaining.find('\n');
            auto line = remaining.substr(0, newline);
            size_t count = line.size();
            size_t skip = count + (newline != std::string_view::npos ? 1 : 0);
            if (count > maxColumns)
            {
                count = line.rfind(' ', maxColumns);
                if (count == std::string_view::npos || count == 0)
                    count = maxColumns;
                skip = count + (line[count] == ' ' ? 1 : 0);
            }
            lines.push_back(line.substr(0, count));
            columns = std::max(columns, uint32_t(count));
            if (skip == remaining.size() && newline == std::string_view::npos)
                break;
            remaining.remove_prefix(skip);
        }
        uint32_t rows = uint32_t(lines.size());
        m_grid.assign(columns * rows, 0); // Atlas cell zero is a space.
        for (uint32_t row = 0; row < rows; ++row)
            for (uint32_t column = 0; column < lines[row].size(); ++column)
                m_grid[row * columns + column] = uint32_t(lines[row][column] - ' ');

        math::float2 panelOrigin(padding);
        math::float2 textOrigin(2 * padding);
        math::float2 panelEnd =
            textOrigin + math::float2(columns * 6.f, rows * 9.f) * fontScale + math::float2(padding);
        ScissorRect bounds = {
            uint32_t(std::floor(padding)),
            uint32_t(std::floor(padding)),
            uint32_t(std::min(float(width), std::ceil(panelEnd.x))),
            uint32_t(std::min(float(height), std::ceil(panelEnd.y))),
        };
        bool raster = m_device->hasFeature(Feature::Rasterization) && is_set(desc.usage, TextureUsage::RenderTarget);
        if (!raster && !is_set(desc.usage, TextureUsage::UnorderedAccess))
            return SLANG_E_NOT_AVAILABLE;
        SLANG_RETURN_ON_FAIL(preparePipeline(raster));
        // Upload and consumption share the command stream; no mapped writes or host waits.
        SLANG_RETURN_ON_FAIL(encoder->uploadBufferData(m_buffer, 0, m_grid.size() * sizeof(uint32_t), m_grid.data()));

        IRenderPassEncoder* renderPass = nullptr;
        IComputePassEncoder* computePass = nullptr;
        IShaderObject* root;
        if (raster)
        {
            RenderPassColorAttachment color = {};
            color.view = target->getDefaultView();
            color.loadOp = LoadOp::Load;
            RenderPassDesc pass = {};
            pass.colorAttachments = &color;
            pass.colorAttachmentCount = 1;
            renderPass = encoder->beginRenderPass(pass);
            root = renderPass->bindPipeline(m_renderPipeline);
        }
        else
        {
            computePass = encoder->beginComputePass();
            root = computePass->bindPipeline(m_computePipeline);
        }
        ShaderCursor cursor(root);
        const auto& format = getFormatInfo(m_outputFormat);
        // Match raster blending: sRGB attachments blend in linear space; other
        // normalized presentation images store encoded values and blend as stored.
        uint32_t encodeSrgb = format.kind == FormatKind::Normalized && !format.isSrgb;
        uint32_t gridSize[] = {columns, rows};
        SLANG_RETURN_ON_FAIL(cursor["fontAtlas"].setBinding(m_atlas));
        SLANG_RETURN_ON_FAIL(cursor["characters"].setBinding(m_buffer));
        SLANG_RETURN_ON_FAIL(cursor["gridSize"].setData(gridSize));
        SLANG_RETURN_ON_FAIL(cursor["panelOrigin"].setData(panelOrigin));
        SLANG_RETURN_ON_FAIL(cursor["panelEnd"].setData(panelEnd));
        SLANG_RETURN_ON_FAIL(cursor["textOrigin"].setData(textOrigin));
        SLANG_RETURN_ON_FAIL(cursor["fontScale"].setData(fontScale));
        SLANG_RETURN_ON_FAIL(cursor["textColor"].setData(textColor));
        SLANG_RETURN_ON_FAIL(cursor["panelColor"].setData(panelColor));
        SLANG_RETURN_ON_FAIL(cursor["encodeSrgb"].setData(encodeSrgb));
        if (renderPass)
        {
            RenderState state = {};
            state.viewports[0] = Viewport::fromSize(width, height);
            state.viewportCount = 1;
            state.scissorRects[0] = bounds;
            state.scissorRectCount = 1;
            renderPass->setRenderState(state);
            DrawArguments draw = {};
            draw.vertexCount = 3;
            renderPass->draw(draw);
            renderPass->end();
        }
        else
        {
            uint32_t origin[] = {bounds.minX, bounds.minY};
            uint32_t extent[] = {bounds.maxX - bounds.minX, bounds.maxY - bounds.minY};
            uint32_t targetSrgb = format.isSrgb;
            SLANG_RETURN_ON_FAIL(cursor["target"].setBinding(target));
            SLANG_RETURN_ON_FAIL(cursor["dispatchOrigin"].setData(origin));
            SLANG_RETURN_ON_FAIL(cursor["dispatchExtent"].setData(extent));
            SLANG_RETURN_ON_FAIL(cursor["targetSrgb"].setData(targetSrgb));
            computePass->dispatchCompute(math::divRoundUp(extent[0], 8u), math::divRoundUp(extent[1], 8u), 1);
            computePass->end();
        }
        return SLANG_OK;
    }

private:
    Result preparePipeline(bool raster)
    {
        if (raster && !m_renderPipeline)
        {
            ColorTargetDesc color = {};
            color.format = m_outputFormat;
            color.enableBlend = true;
            color.color.srcFactor = color.alpha.srcFactor = BlendFactor::One;
            color.color.dstFactor = color.alpha.dstFactor = BlendFactor::InvSrcAlpha;
            RenderPipelineDesc desc = {};
            desc.targets = &color;
            desc.targetCount = 1;
            desc.depthStencil.depthWriteEnable = false;
            return createRenderPipeline(
                m_device,
                "../base/text-renderer.slang",
                "textVertex",
                "textFragment",
                desc,
                m_renderPipeline.writeRef()
            );
        }
        if (!raster && !m_computePipeline)
        {
            const char* format = getFormatInfo(m_outputFormat).slangName;
            if (m_outputFormat == Format::RGBA8UnormSrgb)
                format = "rgba8"; // Storage accesses use the underlying unorm representation.
            if (!format)
                return SLANG_E_NOT_AVAILABLE;
            std::string source = std::string("#define TEXT_TARGET_FORMAT \"") + format + "\"\n";
            if (m_outputFormat == Format::RGBA8Unorm || m_outputFormat == Format::RGBA8UnormSrgb)
                source += "#define TEXT_TARGET_RGBA8 1\n";
            source += "#include \"../base/text-renderer.slang\"\n";
            return createComputePipelineFromSource(
                m_device,
                source.c_str(),
                "textCompute",
                m_computePipeline.writeRef()
            );
        }
        return SLANG_OK;
    }

    ComPtr<IDevice> m_device;
    Format m_outputFormat = Format::Undefined;
    std::string m_text;
    std::vector<uint32_t> m_grid;
    ComPtr<ITexture> m_atlas;
    ComPtr<IBuffer> m_buffer;
    ComPtr<IRenderPipeline> m_renderPipeline;
    ComPtr<IComputePipeline> m_computePipeline;
};

} // namespace rhi

Texture2D<float> fontAtlas;
SamplerState fontSampler;
float2 inverseTargetSize;
uint encodeSrgb;

struct TextVertex
{
    float2 position : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct TextVaryings
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

[shader("vertex")]
TextVaryings textVertex(TextVertex input)
{
    TextVaryings output;
    output.position = float4(input.position * inverseTargetSize * float2(2, -2) + float2(-1, 1), 0, 1);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 textFragment(TextVaryings input) : SV_Target
{
    float coverage = fontAtlas.Sample(fontSampler, input.uv);
    float3 color = input.color.rgb;
    if (encodeSrgb != 0)
        color = select(color <= 0.0031308, color * 12.92, 1.055 * pow(max(color, 0), 1.0 / 2.4) - 0.055);
    return float4(color, input.color.a * coverage);
}

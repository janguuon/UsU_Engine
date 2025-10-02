cbuffer PerObjectCB : register(b0)
{
    float4x4 gMVP;
};

Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);

struct VSIn
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

struct VSOut
{
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

VSOut VSMain(VSIn input)
{
    VSOut o;
    o.position = mul(float4(input.position, 1.0f), gMVP);
    o.normal = input.normal;
    o.uv = input.uv;
    return o;
}

float4 PSMain(VSOut input) : SV_Target
{
    // Sample texture with provided UVs
    float4 color = gTex.Sample(gSamp, input.uv);
    return color;
}

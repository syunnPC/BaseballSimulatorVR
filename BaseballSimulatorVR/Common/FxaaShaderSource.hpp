#pragma once

namespace BaseballSimulatorVR::Common::FxaaShaderSource
{
	constexpr char VertexShaderSource[] = R"(
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VertexOutput main(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    if (vertexId == 0U)
    {
        output.position = float4(-1.0, 1.0, 0.0, 1.0);
        output.uv = float2(0.0, 0.0);
    }
    else if (vertexId == 1U)
    {
        output.position = float4(-1.0, -3.0, 0.0, 1.0);
        output.uv = float2(0.0, 2.0);
    }
    else
    {
        output.position = float4(3.0, 1.0, 0.0, 1.0);
        output.uv = float2(2.0, 0.0);
    }
    return output;
}
)";

	constexpr char PixelShaderSource[] = R"(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer FxaaParameters : register(b0)
{
    float2 inverseResolution;
    float edgeThreshold;
    float edgeThresholdMin;
    float blendFactor;
};

float Luma(float3 rgb)
{
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    const float3 rgbM = sceneTexture.Sample(linearSampler, uv).rgb;
    const float3 rgbN = sceneTexture.Sample(linearSampler, uv + float2(0.0, -inverseResolution.y)).rgb;
    const float3 rgbS = sceneTexture.Sample(linearSampler, uv + float2(0.0, inverseResolution.y)).rgb;
    const float3 rgbW = sceneTexture.Sample(linearSampler, uv + float2(-inverseResolution.x, 0.0)).rgb;
    const float3 rgbE = sceneTexture.Sample(linearSampler, uv + float2(inverseResolution.x, 0.0)).rgb;

    const float lumaM = Luma(rgbM);
    const float lumaN = Luma(rgbN);
    const float lumaS = Luma(rgbS);
    const float lumaW = Luma(rgbW);
    const float lumaE = Luma(rgbE);

    const float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    const float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    const float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(edgeThresholdMin, lumaMax * edgeThreshold))
    {
        return float4(rgbM, 1.0);
    }

    float2 direction;
    direction.x = -((lumaN + lumaS) - (2.0 * lumaM));
    direction.y = (lumaW + lumaE) - (2.0 * lumaM);

    const float dirReduce =
        max((lumaN + lumaS + lumaW + lumaE) * (0.25 / 8.0), 1.0 / 128.0);
    const float rcpDirMin = 1.0 / (min(abs(direction.x), abs(direction.y)) + dirReduce);
    direction = clamp(direction * rcpDirMin, -8.0, 8.0) * inverseResolution;

    const float3 rgbA = 0.5 * (
        sceneTexture.Sample(linearSampler, uv + direction * ((1.0 / 3.0) - 0.5)).rgb +
        sceneTexture.Sample(linearSampler, uv + direction * ((2.0 / 3.0) - 0.5)).rgb);
    const float3 rgbB = (rgbA * 0.5) + (0.25 * (
        sceneTexture.Sample(linearSampler, uv + direction * -0.5).rgb +
        sceneTexture.Sample(linearSampler, uv + direction * 0.5).rgb));

    const float lumaB = Luma(rgbB);
    const float3 result = ((lumaB < lumaMin) || (lumaB > lumaMax))
                              ? rgbA
                              : rgbB;
    return float4(lerp(rgbM, result, saturate(blendFactor)), 1.0);
}
)";
} // namespace BaseballSimulatorVR::Common::FxaaShaderSource

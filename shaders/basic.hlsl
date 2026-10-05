// HLSL for the Direct3D 11 backend.
//
// One file, two entry points - the convention the D3D11 backend uses (VSMain for the vertex stage,
// PSMain for the fragment/pixel stage) because IRenderDevice::createShader only carries a stage.
//
// The constant block mirrors the std140 block the other two backends use: one matrix per draw, bound
// to b0. Column-major storage is HLSL's default, so mul(uMvp, float4(position, 1)) matches the
// GLSL/Vulkan v' = M * v convention exactly.

cbuffer Scene : register(b0)
{
    float4x4 uMvp;
};

struct VsInput
{
    // Semantics follow the attribute locations: location 0 -> POSITION, location 1 -> COLOR
    float3 position : POSITION;
    float3 color : COLOR;
};

struct PsInput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

PsInput VSMain(VsInput input)
{
    PsInput output;
    output.position = mul(uMvp, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

float4 PSMain(PsInput input) : SV_TARGET
{
    return float4(input.color, 1.0);
}

// minimal.slang

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color    : COLOR;
};

// 1. FIXED: Correctly map this to Vulkan Push Constants
struct PushConstants
{
    float4 dynamicColor;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> gPushConstants;


// --- MESH SHADER STAGE ---
[shader("mesh")]
[NumThreads(3, 1, 1)]
[outputtopology("triangle")]
void meshMain(
    uint3 gtid : SV_GroupThreadID,
    out vertices VertexOutput outVertices[3],
    out indices uint3 outIndices[1])
{
    // 2. FIXED: The correct intrinsic name
    SetMeshOutputCounts(3, 1);

    // Vulkan Y is down, so -0.5 is the top of the screen
    float3 positions[3] = {
        float3( 0.5f,  0.5f, 0.0f), // Bottom Right
        float3( 0.0f, -0.5f, 0.0f), // Top
        float3(-0.5f,  0.5f, 0.0f)  // Bottom Left
    };

    float3 colors[3] = {
        float3(1.0f, 0.0f, 0.0f), // Red
        float3(0.0f, 1.0f, 0.0f), // Green
        float3(0.0f, 0.0f, 1.0f)  // Blue
    };

    // Use gtid.x to safely index the 1D arrays
    if (gtid.x < 3)
    {
        outVertices[gtid.x].position = float4(positions[gtid.x], 1.0f);
        outVertices[gtid.x].color = colors[gtid.x] * gPushConstants.dynamicColor.rgb;
    }

    if (gtid.x == 0)
    {
        // Write the single triangle's indices
        outIndices[0] = uint3(0, 1, 2); 
    }
}


// --- FRAGMENT SHADER STAGE ---
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(input.color, 1.0f);
}
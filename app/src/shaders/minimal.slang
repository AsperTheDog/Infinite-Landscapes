struct TerrainUniforms
{
    float4x4 viewProjectionMatrix;
    float4 frustumPlanes[6];
    float2 globalWorldOffset;
    float baseBlockScale;
    uint gridSize;
};
[[vk::push_constant]]
ConstantBuffer<TerrainUniforms> ubo;

struct TaskPayload
{
    float3 worldOrigin;
    float  scale;
    uint   ringLevel;
};

struct VertexOut
{
    float4 position : SV_Position;
    float3 color    : COLOR;
};

struct ChunkConfiguration 
{
    int2   gridCoords;
    float  scale;
    uint   ringLevel;
};

uint2 indexToCoordsBranchless(uint i)
{
    bool is_mid_or_bot = i >= 16;
    bool is_bot = i >= 32;

    uint i_mid = i - 16;
    uint i_bot = i - 32;

    uint y_top = i >> 3;         
    uint y_mid = (i_mid >> 2) + 2; 
    uint y_bot = (i_bot >> 3) + 6; 

    uint y = !is_mid_or_bot ? y_top : (!is_bot ? y_mid : y_bot);

    uint x_top = i & 7;          
    uint x_bot = i_bot & 7;      
    
    uint x_mid_local = i_mid & 3; 
    uint hole_jump = (x_mid_local >> 1) << 2; 
    uint x_mid = x_mid_local + hole_jump;

    uint x = !is_mid_or_bot ? x_top : (!is_bot ? x_mid : x_bot);

    return uint2(x, y);
}

ChunkConfiguration DecodeGlobalChunk(uint globalID, float baseScale)
{
    ChunkConfiguration config;
    uint2 localCoords = uint2(0, 0);

    if (globalID < 64) 
    {
        config.ringLevel = 0;
        localCoords.x = globalID & 7;
        localCoords.y = globalID >> 3;
    } 
    else 
    {
        uint ringOffset = globalID - 64;
        config.ringLevel = (ringOffset / 48) + 1;
        uint localRingIndex = ringOffset % 48;
        
        localCoords = indexToCoordsBranchless(localRingIndex);
    }

    config.scale = baseScale * float(1 << config.ringLevel);
    config.gridCoords = int2(localCoords) - int2(4, 4);

    return config;
}

bool IsChunkVisible(float3 chunkCenter, float chunkRadius)
{
    return true; // Disable frustum culling for testing
    //bool visible = true;
    //for (int i = 0; i < 6; ++i)
    //{
    //    float dist = dot(ubo.frustumPlanes[i].xyz, chunkCenter) + ubo.frustumPlanes[i].w;
    //    if (dist < -chunkRadius)
    //    {
    //        visible = false;
    //    }
    //}
    //return visible;
}

groupshared TaskPayload s_payload;

[shader("amplification")]
[numthreads(32, 1, 1)]
void taskMain(uint3 groupID : SV_GroupID, uint threadID : SV_GroupIndex)
{
    uint dispatchCount = 0;

    if (threadID == 0)
    {
        ChunkConfiguration chunk = DecodeGlobalChunk(groupID.x, ubo.baseBlockScale);

        float2 snappedOffset = floor(ubo.globalWorldOffset / ubo.baseBlockScale) * ubo.baseBlockScale;

        float3 chunkLocalOrigin = float3(
            snappedOffset.x + (float(chunk.gridCoords.x) * chunk.scale),
            0.0f,
            snappedOffset.y + (float(chunk.gridCoords.y) * chunk.scale)
        );

        float3 chunkCenter = chunkLocalOrigin + float3(chunk.scale * 0.5f, 0.0f, chunk.scale * 0.5f);
        float chunkRadius  = chunk.scale * 0.7071f;

        if (IsChunkVisible(chunkCenter, chunkRadius))
        {
            s_payload.worldOrigin = chunkLocalOrigin;
            s_payload.scale       = chunk.scale;
            s_payload.ringLevel   = chunk.ringLevel;
            
            dispatchCount = 1;
        }
    }

    DispatchMesh(dispatchCount, 1, 1, s_payload);
}

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void meshMain(
    in payload TaskPayload payload,
    OutputVertices<VertexOut, 256> verts,
    OutputIndices<uint3, 256> indices,
    uint threadID : SV_GroupIndex)
{
    SetMeshOutputCounts(144, 242);

    float3 ringColors[6] = {
        float3(1.0, 0.0, 0.0),
        float3(1.0, 1.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, 1.0, 1.0),
        float3(0.0, 0.0, 1.0),
        float3(1.0, 0.0, 1.0) 
    };
    float3 chunkColor = ringColors[payload.ringLevel % 6];

    for (uint v = 0; v < 3; ++v)
    {
        uint vIdx = threadID + (v * 64);

        if (vIdx < 144)
        {
            uint vertX = vIdx % 12;
            uint vertY = vIdx / 12;

            float2 normalizedPos = float2(vertX, vertY) / 11.0f;
            float3 localVertexPos = float3(normalizedPos.x * payload.scale, 0.0f, normalizedPos.y * payload.scale);
            float3 finalWorldPos = payload.worldOrigin + localVertexPos;

            VertexOut vout;
            vout.position = mul(float4(finalWorldPos, 1.0f), ubo.viewProjectionMatrix);
            vout.color = chunkColor;

            verts[vIdx] = vout;
        }
    }

    for (uint t = 0; t < 2; ++t)
    {
        uint qIdx = threadID + (t * 64);

        if (qIdx < 121)
        {
            uint quadX = qIdx % 11;
            uint quadY = qIdx / 11;

            uint rowOffset0 = quadY * 12;
            uint rowOffset1 = (quadY + 1) * 12;

            uint v0 = rowOffset0 + quadX;    
            uint v1 = rowOffset0 + quadX + 1;
            uint v2 = rowOffset1 + quadX;    
            uint v3 = rowOffset1 + quadX + 1;

            indices[qIdx * 2] = uint3(v0, v1, v2);
            indices[qIdx * 2 + 1] = uint3(v1, v3, v2);
        }
    }
}

[shader("fragment")]
float4 fragmentMain(VertexOut vin) : SV_Target
{
    return float4(vin.color, 1.0f);
}
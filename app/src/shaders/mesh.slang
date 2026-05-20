struct TerrainUniforms
{
    float4x4 viewProjectionMatrix;
    float4 frustumPlanes[6];
    float4 cameraRight;
    float4 cameraUp;
    float2 globalWorldOffset;
    float2 viewportSize;
    float baseBlockScale;
    float meshletPixelTarget;
    uint gridSize;
    uint edgeSnapEnabled;
};
[[vk::push_constant]]
ConstantBuffer<TerrainUniforms> ubo;

struct TaskPayload
{
    float3 chunkOrigin;
    float  chunkScale;
    uint   selfSegs;
    uint   neighborL;
};

struct VertexOut
{
    float4 position : SV_Position;
    float3 color    : COLOR;
};

bool IsChunkVisible(float3 chunkCenter, float chunkRadius)
{
    for (int i = 0; i < 6; ++i)
    {
        float dist = dot(ubo.frustumPlanes[i].xyz, chunkCenter) + ubo.frustumPlanes[i].w;
        if (dist < -chunkRadius)
        {
            return false;
        }
    }
    return true;
}

uint LtoN(uint L) { return (L <= 3u) ? 1u : (1u << (L - 3u)); }
uint LtoV(uint L) { return (L >= 3u) ? 9u : ((1u << L) + 1u); }

uint ComputeChunkLOD(float3 chunkCenter, float chunkRadius)
{
    float3 offsetRight = chunkRadius * ubo.cameraRight.xyz;
    float3 offsetUp    = chunkRadius * ubo.cameraUp.xyz;

    float4 cClip = mul(float4(chunkCenter, 1.0), ubo.viewProjectionMatrix);
    if (cClip.w <= 0.001) return 6u;

    float4 rClip = mul(float4(chunkCenter + offsetRight, 1.0), ubo.viewProjectionMatrix);
    float4 uClip = mul(float4(chunkCenter + offsetUp,    1.0), ubo.viewProjectionMatrix);

    float invW = 1.0 / cClip.w;
    float2 cNdc = cClip.xy * invW;
    float2 rNdc = rClip.xy * invW;
    float2 uNdc = uClip.xy * invW;

    float2 halfViewport = ubo.viewportSize * 0.5;
    float pxR = length((rNdc - cNdc) * halfViewport);
    float pxU = length((uNdc - cNdc) * halfViewport);
    float diameterPixels = 2.0 * max(pxR, pxU);

    float desiredSegs = diameterPixels * 8.0 / ubo.meshletPixelTarget;

    if (desiredSegs >= 64.0) return 6u;
    if (desiredSegs >= 32.0) return 5u;
    if (desiredSegs >= 16.0) return 4u;
    if (desiredSegs >= 8.0)  return 3u;
    if (desiredSegs >= 4.0)  return 2u;
    if (desiredSegs >= 2.0)  return 1u;
    return 0u;
}

float ComputeHeight(float worldX, float worldZ)
{
    float lowFreq  = sin(worldX * 0.15) * cos(worldZ * 0.15) * 3.0;
    float highFreq = sin(worldX * 0.6)  * cos(worldZ * 0.6)  * 0.4;
    return lowFreq + highFreq;
}

uint UnpackNeighborL(uint packed, uint edge)
{
    return (packed >> (edge * 4)) & 0xFu;
}

float SnapEdgeHeight(uint p_edge, uint selfSegs, uint neighborSegs,
                     float3 edgeOrigin, float3 edgeTangent, float edgeLength)
{
    float invMax = 1.0 / float(selfSegs);

    if (neighborSegs >= selfSegs)
    {
        float t = float(p_edge) * invMax;
        float3 p = edgeOrigin + edgeTangent * (t * edgeLength);
        return ComputeHeight(p.x, p.z);
    }

    uint R = selfSegs / neighborSegs;
    uint p_lo = (p_edge / R) * R;
    uint p_hi = p_lo + R;

    float t_lo = float(p_lo) * invMax;
    float t_hi = float(p_hi) * invMax;
    float3 pLo = edgeOrigin + edgeTangent * (t_lo * edgeLength);
    float3 pHi = edgeOrigin + edgeTangent * (t_hi * edgeLength);
    float yLo = ComputeHeight(pLo.x, pLo.z);
    float yHi = ComputeHeight(pHi.x, pHi.z);
    float frac = float(p_edge - p_lo) / float(R);
    return lerp(yLo, yHi, frac);
}

groupshared TaskPayload s_payload;

[shader("amplification")]
[numthreads(1, 1, 1)]
void taskMain(uint3 groupID : SV_GroupID)
{
    uint chunkIdx = groupID.x;
    int2 gridCoords = int2(chunkIdx % ubo.gridSize, chunkIdx / ubo.gridSize)
                    - int2(ubo.gridSize >> 1, ubo.gridSize >> 1);

    float chunkScale  = ubo.baseBlockScale;
    float2 snapped    = floor(ubo.globalWorldOffset / chunkScale) * chunkScale;
    float3 chunkOrigin = float3(
        snapped.x + float(gridCoords.x) * chunkScale,
        0.0,
        snapped.y + float(gridCoords.y) * chunkScale
    );

    float3 chunkCenter = chunkOrigin + float3(chunkScale * 0.5, 0.0, chunkScale * 0.5);
    float  chunkRadius = chunkScale * 0.7071;

    uint dispatchCount = 0;
    if (IsChunkVisible(chunkCenter, chunkRadius))
    {
        uint L = ComputeChunkLOD(chunkCenter, chunkRadius);
        uint N = LtoN(L);
        uint segs = 1u << L;

        float3 neighborCenters[4] = {
            chunkCenter + float3(-chunkScale, 0, 0),
            chunkCenter + float3(+chunkScale, 0, 0),
            chunkCenter + float3(0, 0, -chunkScale),
            chunkCenter + float3(0, 0, +chunkScale)
        };
        uint packedL = 0;
        for (uint e = 0; e < 4; ++e)
        {
            uint nL = ComputeChunkLOD(neighborCenters[e], chunkRadius);
            packedL |= (nL & 0xFu) << (e * 4);
        }

        s_payload.chunkOrigin = chunkOrigin;
        s_payload.chunkScale  = chunkScale;
        s_payload.selfSegs    = segs;
        s_payload.neighborL   = packedL;
        dispatchCount = N * N;
    }

    DispatchMesh(dispatchCount, 1, 1, s_payload);
}

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void meshMain(
    in payload TaskPayload payload,
    OutputVertices<VertexOut, 128> verts,
    OutputIndices<uint3, 128> indices,
    uint threadID : SV_GroupIndex,
    uint3 groupID : SV_GroupID)
{
    uint segs = payload.selfSegs;
    uint N = (segs >= 8u) ? (segs >> 3u) : 1u;
    uint V = (segs >= 8u) ? 9u : (segs + 1u);

    uint vertCount = V * V;
    uint quadCount = (V - 1u) * (V - 1u);
    uint primCount = quadCount * 2u;

    SetMeshOutputCounts(vertCount, primCount);

    float3 lodColors[7] = {
        float3(0.2, 0.0, 0.0), // L=0 (segs=1, 2 tris)
        float3(0.6, 0.0, 0.0), // L=1 (segs=2, 8 tris)
        float3(1.0, 0.4, 0.0), // L=2 (segs=4, 32 tris)
        float3(1.0, 1.0, 0.0), // L=3 (segs=8, 128 tris)
        float3(0.0, 1.0, 0.0), // L=4 (segs=16, 512 tris)
        float3(0.0, 0.7, 1.0), // L=5 (segs=32, 2048 tris)
        float3(0.5, 0.0, 1.0)  // L=6 (segs=64, 8192 tris)
    };
    uint colorIdx = (segs >= 64u) ? 6u
                  : (segs >= 32u) ? 5u
                  : (segs >= 16u) ? 4u
                  : (segs >= 8u)  ? 3u
                  : (segs >= 4u)  ? 2u
                  : (segs >= 2u)  ? 1u : 0u;
    float3 chunkColor = lodColors[colorIdx];

    uint meshletIdx = groupID.x;
    uint meshletI   = meshletIdx % N;
    uint meshletJ   = meshletIdx / N;
    float  subTileSize   = payload.chunkScale / float(N);
    float3 subTileOrigin = payload.chunkOrigin + float3(
        float(meshletI) * subTileSize,
        0.0,
        float(meshletJ) * subTileSize
    );

    // Vertex emission. Worst case V=9 → 81 verts → 2 iterations of 64 threads.
    for (uint v = 0; v < 2; ++v)
    {
        uint vIdx = threadID + (v * 64);
        if (vIdx < vertCount)
        {
            uint vertX = vIdx % V;
            uint vertY = vIdx / V;
            float invSeg = 1.0 / float(V - 1u);
            float2 normalizedPos = float2(vertX, vertY) * invSeg;
            float3 localVertexPos = float3(normalizedPos.x * subTileSize, 0.0, normalizedPos.y * subTileSize);
            float3 worldPos = subTileOrigin + localVertexPos;

            float worldY = ComputeHeight(worldPos.x, worldPos.z);

            bool snap = (ubo.edgeSnapEnabled != 0u);
            bool onSouth = snap && (meshletJ == 0u)        && (vertY == 0u);
            bool onNorth = snap && (meshletJ == N - 1u)    && (vertY == V - 1u);
            bool onWest  = snap && (meshletI == 0u)        && (vertX == 0u);
            bool onEast  = snap && (meshletI == N - 1u)    && (vertX == V - 1u);

            if (onSouth)
            {
                uint p_edge = meshletI * (V - 1u) + vertX;
                uint nSegs = 1u << UnpackNeighborL(payload.neighborL, 2u);
                worldY = SnapEdgeHeight(p_edge, segs, nSegs,
                                        payload.chunkOrigin,
                                        float3(1, 0, 0),
                                        payload.chunkScale);
            }
            else if (onNorth)
            {
                uint p_edge = meshletI * (V - 1u) + vertX;
                uint nSegs = 1u << UnpackNeighborL(payload.neighborL, 3u);
                worldY = SnapEdgeHeight(p_edge, segs, nSegs,
                                        payload.chunkOrigin + float3(0, 0, payload.chunkScale),
                                        float3(1, 0, 0),
                                        payload.chunkScale);
            }
            else if (onWest)
            {
                uint p_edge = meshletJ * (V - 1u) + vertY;
                uint nSegs = 1u << UnpackNeighborL(payload.neighborL, 0u);
                worldY = SnapEdgeHeight(p_edge, segs, nSegs,
                                        payload.chunkOrigin,
                                        float3(0, 0, 1),
                                        payload.chunkScale);
            }
            else if (onEast)
            {
                uint p_edge = meshletJ * (V - 1u) + vertY;
                uint nSegs = 1u << UnpackNeighborL(payload.neighborL, 1u);
                worldY = SnapEdgeHeight(p_edge, segs, nSegs,
                                        payload.chunkOrigin + float3(payload.chunkScale, 0, 0),
                                        float3(0, 0, 1),
                                        payload.chunkScale);
            }

            worldPos.y = worldY;

            VertexOut vout;
            vout.position = mul(float4(worldPos, 1.0), ubo.viewProjectionMatrix);
            vout.color    = chunkColor;
            verts[vIdx]   = vout;
        }
    }

    // Index emission. Worst case V=9 → 64 quads → fits in one iteration of 64 threads.
    {
        uint qIdx = threadID;
        if (qIdx < quadCount)
        {
            uint quadX = qIdx % (V - 1u);
            uint quadY = qIdx / (V - 1u);

            uint rowOffset0 = quadY * V;
            uint rowOffset1 = (quadY + 1u) * V;

            uint v0 = rowOffset0 + quadX;
            uint v1 = rowOffset0 + quadX + 1u;
            uint v2 = rowOffset1 + quadX;
            uint v3 = rowOffset1 + quadX + 1u;

            indices[qIdx * 2u]      = uint3(v0, v1, v2);
            indices[qIdx * 2u + 1u] = uint3(v1, v3, v2);
        }
    }
}

[shader("fragment")]
float4 fragmentMain(VertexOut vin) : SV_Target
{
    return float4(vin.color, 1.0);
}

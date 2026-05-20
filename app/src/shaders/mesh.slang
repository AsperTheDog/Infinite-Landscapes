module mesh;
import generation;
import compute;

struct GraphicsPC
{
    float4x4 viewProjMatrix;
    LeafBuf* leafBuf;
    float rootSize;
    uint maxDepth;
    uint edgeSnapEnabled;
};
[[vk::push_constant]] ConstantBuffer<GraphicsPC> gubo;

struct VertexOut
{
    float4 position : SV_Position;
    float3 color : COLOR;
};


struct TaskPayload
{
    float2 origin;
    uint   depth;
    uint   neighborDepths;
};

groupshared TaskPayload s_payload;

// Redundant for now, but left for potential future growth
[shader("amplification")]
[numthreads(1, 1, 1)]
void taskMain(uint3 groupID : SV_GroupID)
{
    uint idx = groupID.x;
    Leaf leaf = gubo.leafBuf.leaves[idx];

    s_payload.origin = leaf.origin;
    s_payload.depth = leaf.depth;
    s_payload.neighborDepths = leaf.neighborDepths;

    DispatchMesh(1u, 1u, 1u, s_payload);
}


static const uint V = 9u;
static const uint SEGS = V - 1u;

uint UnpackNeighborDepth(uint packed, uint edge)
{
    return (packed >> (edge * 4u)) & 0xFu;
}

float SnapEdgeHeight(uint p_edge, uint deltaDepth,
                     float3 edgeOrigin, float3 edgeTangent, float edgeLength)
{
    float invSegs = 1.0 / float(SEGS);

    if (deltaDepth == 0u)
    {
        float t = float(p_edge) * invSegs;
        float3 p = edgeOrigin + edgeTangent * (t * edgeLength);
        return ComputeHeight(p.x, p.z);
    }

    uint R = 1u << deltaDepth;
    uint p_lo = (p_edge / R) * R;
    uint p_hi = p_lo + R;

    float t_lo = float(p_lo) * invSegs;
    float t_hi = float(p_hi) * invSegs;
    float3 pLo = edgeOrigin + edgeTangent * (t_lo * edgeLength);
    float3 pHi = edgeOrigin + edgeTangent * (t_hi * edgeLength);
    float yLo = ComputeHeight(pLo.x, pLo.z);
    float yHi = ComputeHeight(pHi.x, pHi.z);
    float frac = float(p_edge - p_lo) / float(R);
    return lerp(yLo, yHi, frac);
}

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void meshMain(
    in payload TaskPayload payload,
    OutputVertices<VertexOut, 128> verts,
    OutputIndices<uint3, 128>      indices,
    uint  threadID : SV_GroupIndex)
{
    SetMeshOutputCounts(V * V, SEGS * SEGS * 2u);

    float size = CellSize(payload.depth, gubo.rootSize);
    float3 origin3 = float3(payload.origin.x, 0.0, payload.origin.y);

    float t = saturate(float(payload.depth) / float(max(gubo.maxDepth, 1u)));
    float3 chunkColor = lerp(float3(0.1, 0.1, 0.4), float3(1.0, 0.8, 0.0), t);

    for (uint v = 0u; v < 2u; ++v)
    {
        uint vIdx = threadID + (v * 64u);
        if (vIdx < V * V)
        {
            uint vertX = vIdx % V;
            uint vertY = vIdx / V;
            float invSeg = 1.0 / float(SEGS);
            float2 norm = float2(vertX, vertY) * invSeg;
            float3 worldPos = origin3 + float3(norm.x * size, 0.0, norm.y * size);

            float worldY = ComputeHeight(worldPos.x, worldPos.z);

            bool snap = (gubo.edgeSnapEnabled != 0u);
            bool onWest = snap && (vertX == 0u);
            bool onEast = snap && (vertX == V - 1u);
            bool onSouth = snap && (vertY == 0u);
            bool onNorth = snap && (vertY == V - 1u);

            if (onSouth)
            {
                uint nD = UnpackNeighborDepth(payload.neighborDepths, 2u);
                uint dD = (payload.depth > nD) ? min(payload.depth - nD, 3u) : 0u;
                worldY = SnapEdgeHeight(vertX, dD, origin3, float3(1, 0, 0), size);
            }
            else if (onNorth)
            {
                uint nD = UnpackNeighborDepth(payload.neighborDepths, 3u);
                uint dD = (payload.depth > nD) ? min(payload.depth - nD, 3u) : 0u;
                worldY = SnapEdgeHeight(vertX, dD, origin3 + float3(0, 0, size), float3(1, 0, 0), size);
            }
            else if (onWest)
            {
                uint nD = UnpackNeighborDepth(payload.neighborDepths, 0u);
                uint dD = (payload.depth > nD) ? min(payload.depth - nD, 3u) : 0u;
                worldY = SnapEdgeHeight(vertY, dD, origin3, float3(0, 0, 1), size);
            }
            else if (onEast)
            {
                uint nD = UnpackNeighborDepth(payload.neighborDepths, 1u);
                uint dD = (payload.depth > nD) ? min(payload.depth - nD, 3u) : 0u;
                worldY = SnapEdgeHeight(vertY, dD, origin3 + float3(size, 0, 0), float3(0, 0, 1), size);
            }

            worldPos.y = worldY;

            VertexOut vout;
            vout.position = mul(float4(worldPos, 1.0), gubo.viewProjMatrix);
            vout.color = chunkColor;
            verts[vIdx] = vout;
        }
    }

    {
        uint qIdx = threadID;
        if (qIdx < SEGS * SEGS)
        {
            uint quadX = qIdx % SEGS;
            uint quadY = qIdx / SEGS;

            uint rowOffset0 = quadY * V;
            uint rowOffset1 = (quadY + 1u) * V;

            uint v0 = rowOffset0 + quadX;
            uint v1 = rowOffset0 + quadX + 1u;
            uint v2 = rowOffset1 + quadX;
            uint v3 = rowOffset1 + quadX + 1u;

            indices[qIdx * 2u] = uint3(v0, v1, v2);
            indices[qIdx * 2u + 1u] = uint3(v1, v3, v2);
        }
    }
}

[shader("fragment")]
float4 fragmentMain(VertexOut vin) : SV_Target
{
    return float4(vin.color, 1.0);
}

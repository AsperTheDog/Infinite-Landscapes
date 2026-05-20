module compute;

static const uint kMaxNodes = 65536u;
static const uint kMaxLeaves = 65536u;
static const uint kMaxProbe = 128u;

struct IndirectCmd
{
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};

struct Node
{
    float2 origin;
    uint depth;
    uint _pad;
};

public struct Leaf
{
    public float2 origin;
    public uint depth;
    public uint neighborDepths;
};

struct HashEntry
{
    int ix;
    int iz;
    uint depth;
    uint leafIdx;
};

struct WorkBuf
{
    uint count;
    uint _pad[3];
    Node nodes[65536];
};

public struct LeafBuf
{
    public uint count;
    public uint _pad[3];
    public Leaf leaves[65536];
};

struct HashBuf
{
    HashEntry entries[131072];
};

struct ComputePC
{
    float4x4 viewProjMatrix;
    float4x4 frustumViewProjMatrix;
    float4 cameraRight;
    float4 cameraUp;
    float2 viewportSize;
    float2 rootOrigin;
    float rootSize;
    float meshletPixelTarget;
    uint maxDepth;
    uint currentDepth;
    uint hashSize;
    WorkBuf *workIn;
    WorkBuf *workOut;
    LeafBuf *leafBuf;
    HashBuf *hashBuf;
    IndirectCmd *indirect;
};
[[vk::push_constant]] ConstantBuffer<ComputePC> cubo;

public float CellSize(uint depth, float rootSize)
{
    return rootSize / float(1u << depth);
}

void ExtractFrustumPlanes(float4x4 m, out float4 planes[6])
{
    float4 c0 = float4(m._11, m._21, m._31, m._41);
    float4 c1 = float4(m._12, m._22, m._32, m._42);
    float4 c2 = float4(m._13, m._23, m._33, m._43);
    float4 c3 = float4(m._14, m._24, m._34, m._44);

    planes[0] = c3 + c0;
    planes[1] = c3 - c0;
    planes[2] = c3 + c1;
    planes[3] = c3 - c1;
    planes[4] = c3 + c2;
    planes[5] = c3 - c2;
    for (int i = 0; i < 6; ++i)
    {
        float len = length(planes[i].xyz);
        planes[i] /= max(len, 1e-6);
    }
}

bool SphereVisible(float3 center, float radius, float4 planes[6])
{
    for (int i = 0; i < 6; ++i)
    {
        if (dot(planes[i].xyz, center) + planes[i].w < -radius) return false;
    }
    return true;
}

float NodeScreenDiameter(float3 center, float radius)
{
    float3 oR = radius * cubo.cameraRight.xyz;
    float3 oU = radius * cubo.cameraUp.xyz;

    float4 cClip = mul(float4(center, 1.0), cubo.viewProjMatrix);
    if (cClip.w <= 0.001) return -1.0;
    float4 rClip = mul(float4(center + oR, 1.0), cubo.viewProjMatrix);
    float4 uClip = mul(float4(center + oU, 1.0), cubo.viewProjMatrix);

    float invW = 1.0 / cClip.w;
    float2 cNdc = cClip.xy * invW;
    float2 rNdc = rClip.xy * invW;
    float2 uNdc = uClip.xy * invW;
    float2 halfViewport = cubo.viewportSize * 0.5;
    float pxR = length((rNdc - cNdc) * halfViewport);
    float pxU = length((uNdc - cNdc) * halfViewport);
    return 2.0 * max(pxR, pxU);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void refineMain(uint3 dispatchID : SV_DispatchThreadID)
{
    uint idx = dispatchID.x;
    uint inCount = min(cubo.workIn.count, kMaxNodes);
    if (idx >= inCount) return;

    Node node = cubo.workIn.nodes[idx];
    if (node.depth != cubo.currentDepth) return;

    float cellSize = CellSize(node.depth, cubo.rootSize);
    float3 center = float3(node.origin.x + cellSize * 0.5, 0.0, node.origin.y + cellSize * 0.5);
    float  radius = cellSize * 0.7071 + 4.0;

    float4 planes[6];
    ExtractFrustumPlanes(cubo.frustumViewProjMatrix, planes);
    if (!SphereVisible(center, radius, planes)) return;

    float diameterPx = NodeScreenDiameter(center, radius);
    bool shouldSplit = (node.depth < cubo.maxDepth) && (diameterPx < 0.0 || diameterPx > cubo.meshletPixelTarget);

    if (shouldSplit)
    {
        uint base;
        InterlockedAdd(cubo.workOut.count, 4u, base);
        if (base + 4u <= kMaxNodes)
        {
            float childSize = cellSize * 0.5;
            float2 origins[4] = {
                float2(node.origin.x, node.origin.y),
                float2(node.origin.x + childSize, node.origin.y),
                float2(node.origin.x, node.origin.y + childSize),
                float2(node.origin.x + childSize, node.origin.y + childSize)
            };
            for (int c = 0; c < 4; ++c)
            {
                cubo.workOut.nodes[base + c].origin = origins[c];
                cubo.workOut.nodes[base + c].depth = node.depth + 1u;
                cubo.workOut.nodes[base + c]._pad = 0u;
            }
        }
    }
    else
    {
        uint slot;
        InterlockedAdd(cubo.leafBuf.count, 1u, slot);
        if (slot < kMaxLeaves)
        {
            cubo.leafBuf.leaves[slot].origin = node.origin;
            cubo.leafBuf.leaves[slot].depth = node.depth;
            cubo.leafBuf.leaves[slot].neighborDepths = 0u;
        }
    }
}


[shader("compute")]
[numthreads(1, 1, 1)]
void prepareIndirectMain(uint3 dispatchID : SV_DispatchThreadID)
{
    cubo.indirect.groupCountX = min(cubo.leafBuf.count, kMaxLeaves);
    cubo.indirect.groupCountY = 1u;
    cubo.indirect.groupCountZ = 1u;
}


uint HashKey(int ix, int iz, uint depth)
{
    uint k = uint(ix);
    k = k * 2654435761u + uint(iz);
    k = k * 2654435761u + depth;
    k ^= k >> 16;
    k *= 0x85ebca6bu;
    k ^= k >> 13;
    return k;
}

bool SameKey(HashEntry e, int ix, int iz, uint depth)
{
    return e.depth == depth && e.ix == ix && e.iz == iz;
}

void WorldToCell(float2 worldXZ, uint depth, out int ix, out int iz)
{
    float cs = CellSize(depth, cubo.rootSize);
    float2 rel = (worldXZ - cubo.rootOrigin) / cs;
    ix = int(floor(rel.x));
    iz = int(floor(rel.y));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void hashBuildMain(uint3 dispatchID : SV_DispatchThreadID)
{
    uint idx = dispatchID.x;
    if (idx >= cubo.leafBuf.count) return;

    Leaf leaf = cubo.leafBuf.leaves[idx];
    int ix, iz;
    WorldToCell(leaf.origin, leaf.depth, ix, iz);

    uint key = HashKey(ix, iz, leaf.depth);
    uint slot = key % cubo.hashSize;

    for (uint probe = 0; probe < kMaxProbe; ++probe)
    {
        uint test = (slot + probe) % cubo.hashSize;
        uint prev;
        InterlockedCompareExchange(cubo.hashBuf.entries[test].leafIdx, 0xFFFFFFFFu, idx, prev);
        if (prev == 0xFFFFFFFFu)
        {
            cubo.hashBuf.entries[test].ix = ix;
            cubo.hashBuf.entries[test].iz = iz;
            cubo.hashBuf.entries[test].depth = leaf.depth;
            return;
        }
    }
}


uint FindLeafDepth(float2 P)
{
    for (int D = int(cubo.maxDepth); D >= 0; --D)
    {
        int ix, iz;
        WorldToCell(P, uint(D), ix, iz);
        uint key = HashKey(ix, iz, uint(D));
        uint slot = key % cubo.hashSize;
        for (uint probe = 0; probe < kMaxProbe; ++probe)
        {
            uint test = (slot + probe) % cubo.hashSize;
            HashEntry e = cubo.hashBuf.entries[test];
            if (e.leafIdx == 0xFFFFFFFFu) break;
            if (SameKey(e, ix, iz, uint(D))) return uint(D);
        }
    }
    return 0xFFFFFFFFu;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void neighborLookupMain(uint3 dispatchID : SV_DispatchThreadID)
{
    uint idx = dispatchID.x;
    if (idx >= cubo.leafBuf.count) return;

    Leaf leaf = cubo.leafBuf.leaves[idx];
    float size = CellSize(leaf.depth, cubo.rootSize);
    float eps = size * 0.01;
    float halfSize = size * 0.5;

    float2 q[4] = {
        float2(leaf.origin.x - eps, leaf.origin.y + halfSize),
        float2(leaf.origin.x + size + eps, leaf.origin.y + halfSize),
        float2(leaf.origin.x + halfSize, leaf.origin.y - eps),
        float2(leaf.origin.x + halfSize, leaf.origin.y + size + eps)
    };

    uint packed = 0u;
    for (uint e = 0; e < 4u; ++e)
    {
        uint nD = FindLeafDepth(q[e]);
        if (nD == 0xFFFFFFFFu) nD = leaf.depth;
        packed |= (nD & 0xFu) << (e * 4u);
    }

    cubo.leafBuf.leaves[idx].neighborDepths = packed;
}
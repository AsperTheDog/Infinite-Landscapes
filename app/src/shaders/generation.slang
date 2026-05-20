module generation;

public struct TerrainData 
{
    public float height;
    public float3 normal;
}

float2 hash(float2 p)
{
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
}

float3 noised(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);

    float2 ga = hash(i + float2(0.0, 0.0));
    float2 gb = hash(i + float2(1.0, 0.0));
    float2 gc = hash(i + float2(0.0, 1.0));
    float2 gd = hash(i + float2(1.0, 1.0));

    float va = dot(ga, f - float2(0.0, 0.0));
    float vb = dot(gb, f - float2(1.0, 0.0));
    float vc = dot(gc, f - float2(0.0, 1.0));
    float vd = dot(gd, f - float2(1.0, 1.0));

    float v = va + 
              u.x * (vb - va) + 
              u.y * (vc - va) + 
              u.x * u.y * (va - vb - vc + vd);

    float2 d = ga + 
               u.x * (gb - ga) + 
               u.y * (gc - ga) + 
               u.x * u.y * (ga - gb - gc + gd) +
               du * (float2(vb - va, vc - va) + u.yx * (va - vb - vc + vd));

    return float3(v, d);
}

public TerrainData GetTerrainData(float2 pos, uint octaves)
{
    TerrainData output;

    float2 p = pos * 0.0005;

    float3 warp = noised(p * 2.0);
    p += warp.yz * 0.4;

    float height = 0.0;
    float2 derivatives = float2(0.0);

    float amplitude = 1.0;
    float frequency = 1.0;
    float weight = 1.0;

    [loop]
    for (int i = 0; i < octaves; i++)
    {
        float3 n = noised(p * frequency);

        float ridge = 1.0 - abs(n.x);

        float ridgeHeight = ridge * ridge * weight;

        height += ridgeHeight * amplitude;

        float2 localDeriv = -2.0 * n.yz * n.x * ridge * weight;
        derivatives += localDeriv * amplitude * frequency;

        weight = lerp(0.0, 1.0, ridgeHeight);
        amplitude *= 0.45;
        frequency *= 2.15;
    }

    output.height = -height * 300.0;

    float2 finalDeriv = derivatives * 300.0 * 0.0005;
    output.normal = normalize(float3(-finalDeriv.x, 1.0, -finalDeriv.y));

    return output;
}

public float SnowLevel(float3 pos)
{

}
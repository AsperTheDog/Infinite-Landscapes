module generation;
import noise;


public struct TerrainData
{
    public float height;
    public float3 normal;
};

public TerrainData GetTerrainData(float2 pos, float2 scale, float normalDist, float normalStrength)
{
    TerrainData output;
    FractalSimplexiNoise noise = { scale, 5, 2.0, 0.5 };
    output.height = noise.eval(pos);
    output.normal = noise.evalNormal(pos, normalStrength, normalDist);
    return output;
}

public float3 GetTerrainColor(float3 pos, float3 normal)
{
    return float3(1.0);
}
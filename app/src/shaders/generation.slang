module generation;
import noise;


public struct TerrainData
{
    public float height;
    public float3 normal;
};

public TerrainData GetTerrainData(float2 pos, float2 scale, float normalDist, float normalStrength, float lacunarity, float gain)
{
    TerrainData output;
    FinaliNoise noise = { scale, 6, lacunarity, gain };
    output.height = noise.eval(pos);
    output.normal = noise.evalNormal(pos, normalStrength, normalDist);
    return output;
}

bool BiasedRandomLinear(float minVal, float maxVal, float randVal, float weight)
{
    float t = (weight - minVal) / (maxVal - minVal);
    float probability = saturate(t);
    return probability > randVal;
}

public float3 GetTerrainColor(float3 pos, float3 normal, float scale, float2 snowRange, float2 rockRange)
{
    FractalSimplexiNoise rockNoise = { scale * 0.02, 6, 3.0, 0.5 };
    FractalSimplexiNoise snowNoise = { scale * 0.15, 3, 2.0, 0.5 };
    FractalSimplexiNoise grassExtraNoise = { scale * 0.001, 3, 2.0, 0.5 };
    FractalSimplexiNoise grassExtraExtraNoise = { scale * 0.5, 2, 2.0, 0.5 };
    FractalSimplexiNoise rockExtraNoise = { scale * 0.025, 2, 2.0, 0.5 };

    float3 grassColor = float3(0.1, 0.6, 0.1);
    float3 rockColor = float3(0.4, 0.4, 0.4);
    float3 snowColor = float3(1.0, 1.0, 1.0);

    bool snowBias = BiasedRandomLinear(snowRange.x, snowRange.y, snowNoise.eval(pos.xz), -pos.y);

    float slope = 1.0 - dot(normal, float3(0.0, 1.0, 0.0));
    bool rockBias = BiasedRandomLinear(rockRange.x, rockRange.y, rockNoise.eval(pos.xz), slope);

    float3 finalColor = snowBias ? 
        snowColor : 
        (rockBias ? 
            (rockColor + (rockExtraNoise.eval(pos.xz) * 2.0 - 1.0) * 0.1) : 
            (grassColor + (grassExtraNoise.eval(pos.xz) * 2.0 - 1.0) * 0.1 + (grassExtraExtraNoise.eval(pos.xz) * 2.0 - 1.0) * 0.2)
        );

    return finalColor;
}
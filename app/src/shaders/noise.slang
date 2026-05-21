module noise;

/* 
 * Original code from LYGIA Shader Library
 * Contributor: Patricio Gonzalez Vivo (patriciogonzalezvivo.com)
 * Source Code: https://github.com/patriciogonzalezvivo/lygia
 * 
 * Distributed under the The Prosperity Public License 3.0.0
 * License Text: https://prosperitylicense.com/versions/3.0.0
 */

const static float4 RANDOM_SCALE = float4(0.1031, 0.1030, 0.0973, 0.1099);

// Utils


float2 random2(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * RANDOM_SCALE.xyz);
    p3 += dot(p3, p3.yzx + 19.19);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float distEuclidean(float2 a, float2 b) 
{
    return distance(a, b);
}

float2 mod289(in float2 x) 
{ 
    return x - floor(x * (1. / 289.)) * 289.; 
}

float3 mod289(in float3 x)
{
    return x - floor(x * (1. / 289.)) * 289.;
}

float3 permute(in float3 x) 
{ 
    return mod289(((x * 34.0) + 1.0) * x); 
}

float3 taylorInvSqrt(in float3 r) 
{ 
    return 1.79284291400159 - 0.85373472095314 * r; 
}

// Interface

public interface INoise
{
    public float eval(float2 p);

    public float3 evalNormal(float2 p, float bumpStrength, float normalDist)
    {
        float2 eps = float2(normalDist, 0.0);

        float hL = eval(p + eps.xy);
        float hR = eval(p - eps.xy);
        float hD = eval(p + eps.yx);
        float hU = eval(p - eps.yx);

        float dx = (hR - hL) / (2.0 * normalDist);
        float dy = (hU - hD) / (2.0 * normalDist);

        return normalize(float3(-dx * bumpStrength, 1.0, -dy * bumpStrength));
    }
}

// Voronoi

public float voronoi(float2 p, float jitter, float2 scale)
{
    p = p / scale.x;
    float2 n = floor(p);
    float2 f = frac(p);

    float distF1 = 1.0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            float2 g = float2(i, j);
            float2 o = random2(n + g) * jitter;
            float2 p = g + o;
            float d = distEuclidean(p, f);
            if (d < distF1) {
                distF1 = d;
            }
        }
    }

    return distF1 * scale.y;
}

public struct VoronoiNoise : INoise
{
    public float2 scale;
    public float jitter;

    public float eval(float2 p)
    {
        return voronoi(p, jitter, scale);
    }
}

// Simplex

float simplex(float2 v, float scale) {
    const float4 C = float4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);
    v = v / scale.x;

    float2 i = floor(v + dot(v, C.yy));
    float2 x0 = v - i + dot(i, C.xx);

    float2 i1;
    i1.x = step(x0.y, x0.x);
    i1.y = 1.0 - i1.x;

    float2 x1 = x0 + C.xx - i1;
    float2 x2 = x0 + C.zz;

    i = mod289(i);
    float3 p =
    permute(permute(i.y + float3(0.0, i1.y, 1.0)) + i.x + float3(0.0, i1.x, 1.0));

    float3 m = max(0.5 - float3(dot(x0, x0), dot(x1, x1), dot(x2, x2)), 0.0);
    m = m * m;
    m = m * m;

    float3 x = 2.0 * frac(p * C.www) - 1.0;
    float3 h = abs(x) - 0.5;
    float3 ox = floor(x + 0.5);
    float3 a0 = x - ox;

    m *= taylorInvSqrt(a0 * a0 + h * h);

    float3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.y = a0.y * x1.x + h.y * x1.y;
    g.z = a0.z * x2.x + h.z * x2.y;

    float height = dot(m, g) * 130.0;

    return (height * 0.5 + 0.5);
}

public struct SimplexiNoise : INoise
{
    public float scale;
    public int octaves;

    public float eval(float2 p)
    {
        return simplex(p, scale);
    }
}

// Fractal Simplex

float fractalSimplex(float2 v, float scale, int octaves, float lacunarity = 2.0, float gain = 0.5) {
    float total = 0.0;
    float amplitude = 1.0;
    float max_amplitude = 0.0;

    float2 current_v = v;

    [MaxIters(8)]
    for (int i = 0; i < octaves; i++) {
        total += simplex(current_v, scale) * amplitude;
        max_amplitude += amplitude;

        current_v *= lacunarity;
        amplitude *= gain;
    }

    return total / max_amplitude;
}

public struct FractalSimplexiNoise : INoise
{
    public float scale;
    public int octaves;
    public float lacunarity;
    public float gain;

    public float eval(float2 p)
    {
        return fractalSimplex(p, scale, octaves, lacunarity, gain);
    }
}

// Final noise

float sigmoid(float x) {
    return 1.0 / (1.0 + exp(-x));
}

public struct FinaliNoise : INoise
{
    public float2 scale;
    public int octaves;
    public float lacunarity;
    public float gain;

    public float eval(float2 p)
    {
        float fracHeight = pow(fractalSimplex(p, scale.x, octaves, lacunarity, gain), 2) * scale.y;
        float macroHeight = simplex(p, scale.x * 5.0);
        float macroHeightOff = macroHeight * scale.y;
        return -(fracHeight * macroHeight + macroHeightOff);
    }
}
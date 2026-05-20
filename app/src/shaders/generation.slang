module generation;

public float ComputeHeight(float worldX, float worldZ)
{
    float lowFreq = sin(worldX * 0.15) * cos(worldZ * 0.15) * 3.0;
    float highFreq = sin(worldX * 0.6)  * cos(worldZ * 0.6)  * 0.4;
    return lowFreq + highFreq;
}
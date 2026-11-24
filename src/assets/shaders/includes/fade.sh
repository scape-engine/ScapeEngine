// How much of a draw is shown, for the standard vertex shaders: DrawList sets
// these per draw (DrawList::SetFade). u_fadeViewer.xyz is the viewer;
// u_fadeRange.xy the distances a draw fades out over (y <= x: it doesn't) and
// u_fadeRange.z its visibility (FadeComponent; 1 shown, 0 hidden). A mesh LOD
// fades in over u_fadeLod.xy and out over u_fadeLod.zw, as the LODs on either
// side of it do the other way (none where y <= x, w <= z)

uniform vec4 u_fadeViewer;
uniform vec4 u_fadeRange;
uniform vec4 u_fadeLod;

// How far `distance` is from `from` to `to`, 0..1
float FadeStep(float distance, float from, float to)
{
    return clamp((distance - from) / (to - from), 0.0, 1.0);
}

// The share shown of something placed at `origin` (world space), from 1
// (whole) to 0 (none), for DitherDiscard; negative while it blends in as a
// LOD, which takes the pattern's other side
float FadeAmount(vec3 origin)
{
    float d = distance(origin, u_fadeViewer.xyz);
    float shown = u_fadeRange.z;
    if (u_fadeRange.y > u_fadeRange.x)
        shown *= 1.0 - FadeStep(d, u_fadeRange.x, u_fadeRange.y);
    if (u_fadeLod.w > u_fadeLod.z)
        shown *= 1.0 - FadeStep(d, u_fadeLod.z, u_fadeLod.w);
    if (u_fadeLod.y > u_fadeLod.x && d < u_fadeLod.y)
        return -shown * FadeStep(d, u_fadeLod.x, u_fadeLod.y);
    return shown;
}

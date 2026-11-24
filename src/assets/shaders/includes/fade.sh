// How much of a draw is shown, for the standard vertex shaders: DrawList sets
// these per draw (DrawList::SetFade). u_fadeViewer.xyz is the viewer;
// u_fadeRange.xy the distances a draw fades out over (y <= x: it doesn't) and
// u_fadeRange.z its visibility (FadeComponent; 1 shown, 0 hidden)

uniform vec4 u_fadeViewer;
uniform vec4 u_fadeRange;

// The share shown of something placed at `origin` (world space), from 1
// (whole) to 0 (none), for DitherDiscard
float FadeAmount(vec3 origin)
{
    float shown = u_fadeRange.z;
    if (u_fadeRange.y > u_fadeRange.x)
    {
        float t = (distance(origin, u_fadeViewer.xyz) - u_fadeRange.x) /
                  (u_fadeRange.y - u_fadeRange.x);
        shown *= 1.0 - clamp(t, 0.0, 1.0);
    }
    return shown;
}

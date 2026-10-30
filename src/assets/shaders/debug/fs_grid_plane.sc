$input v_worldPos

#include <bgfx_shader.sh>

uniform vec4 u_viewPos;
uniform vec4 u_gridParams; // x: cell size, y: major line every N cells,
// z: fade start, w: fade end (world units)
uniform vec4 u_gridStyle; // x: opacity, y: axis lines (0 or 1)
uniform vec4 u_gridColor; // rgb: line color
uniform vec4 u_axisColorX; // rgb: X axis line (runs along X, where z = 0)
uniform vec4 u_axisColorZ; // rgb: Z axis line (runs along Z, where x = 0)

// Antialiased coverage of grid lines with the given spacing (0..1)
float gridLines(vec2 coord, float spacing)
{
    vec2 c = coord / spacing;
    vec2 g = abs(fract(c - 0.5) - 0.5) / fwidth(c);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main()
{
    vec3 worldPos = v_worldPos;
    float cell = u_gridParams.x;
    float major = cell * u_gridParams.y;

    // Fade out with distance from the camera
    float dist = length(worldPos.xz - u_viewPos.xz);
    float fade = 1.0 - smoothstep(u_gridParams.z, u_gridParams.w, dist);

    // Minor lines fade out once they get too dense on screen (fwidth is
    // world units per pixel); major lines stay
    float density = max(fwidth(worldPos.x), fwidth(worldPos.z)) / cell;
    float minorFade = 1.0 - smoothstep(0.1, 0.5, density);
    float minorAlpha = gridLines(worldPos.xz, cell) * 0.3 * minorFade;
    float majorAlpha = gridLines(worldPos.xz, major) * 0.6;
    float gridAlpha = max(minorAlpha, majorAlpha);

    // Axis lines, about 1.5 px wide at any distance
    float axisX = 0.0;
    float axisZ = 0.0;
    if (u_gridStyle.y > 0.5)
    {
        axisX = 1.0 - min(abs(worldPos.z) / (fwidth(worldPos.z) * 1.5), 1.0);
        axisZ = 1.0 - min(abs(worldPos.x) / (fwidth(worldPos.x) * 1.5), 1.0);
    }

    vec3 color = u_gridColor.rgb;
    color = mix(color, u_axisColorZ.rgb, axisZ);
    color = mix(color, u_axisColorX.rgb, axisX);

    float alpha = max(gridAlpha, max(axisX, axisZ)) * fade * u_gridStyle.x;
    gl_FragColor = vec4(color, alpha);
}

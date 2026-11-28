$input a_position        // Screen space position, XY in [-1,1]
$output v_viewDir        // World space view direction

#include "../common/common.sh" 

// --- Uniforms ---
uniform mat4 u_projInv;  // Inverse projection matrix
uniform mat4 u_viewInv;  // Inverse view matrix (camera to world transform)

// --- Main Shader Function ---
void main() {
    float2 ndc = a_position.xy; // Normalized Device Coordinates

    // --- Compute Clip Space Position on Far Plane ---
    float4 clipPos = float4(ndc, 1.0, 1.0);

    // --- Unproject to View Space ---
    float4 viewPosH = mul(u_projInv, clipPos);       // Homogeneous view space position
    float3 viewPos = viewPosH.xyz / viewPosH.w;      // Perspective divide to get 3D point

    // --- Calculate View Direction in View Space ---
    float3 viewDir_viewSpace = normalize(viewPos);

    // --- Transform View Direction to World Space ---
    float3 viewDir_world = normalize(mul(u_viewInv, vec4(viewDir_viewSpace, 0.0)).xyz);
    v_viewDir = viewDir_world;

    // --- Output final clip-space position ---
    // Z=0.0 to render at the (reversed) far plane, W=1.0 for rasterizer
    gl_Position = float4(ndc, 0.0, 1.0);
}
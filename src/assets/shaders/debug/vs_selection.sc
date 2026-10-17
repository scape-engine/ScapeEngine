$input a_position , a_texcoord1

#include "common.sh"

uniform vec4 u_outlineParams; // x = outline width (framebuffer pixels)

void main() {
      // a_texcoord1 holds smoothed normals (see Mesh): extruding along the
      // split per-face normals would tear the outline apart at hard edges.
      // Push out in screen space for a constant pixel width.
      vec4 position = mul(u_modelViewProj, vec4(a_position, 1.0));
      vec2 direction = mul(u_modelViewProj, vec4(a_texcoord1, 0.0)).xy;
      float len = length(direction);
      if (len > 0.0001)
            position.xy += (direction / len) * (2.0 * u_outlineParams.x / u_viewRect.zw) * position.w;
      gl_Position = position;
}

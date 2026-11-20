vec3 v_worldPos  : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
vec3 v_normal    : TEXCOORD1 = vec3(0.0, 1.0, 0.0);
vec4 v_tiles     : TEXCOORD2 = vec4(0.0, 0.0, 0.0, 0.0);
vec3 v_centrePos : TEXCOORD3 = vec3(0.0, 0.0, 0.0);
vec3 v_up        : TEXCOORD4 = vec3(0.0, 1.0, 0.0);

vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec4 a_texcoord0 : TEXCOORD0;
vec3 a_texcoord1 : TEXCOORD1;
vec3 a_texcoord2 : TEXCOORD2;

vec3 v_worldPos  : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
vec2 v_texcoord0 : TEXCOORD1 = vec2(0.0, 0.0);
vec3 v_axisX     : TEXCOORD2 = vec3(1.0, 0.0, 0.0);
vec3 v_axisY     : TEXCOORD3 = vec3(0.0, 1.0, 0.0);
vec3 v_axisZ     : TEXCOORD4 = vec3(0.0, 0.0, 1.0);
float v_fade     : TEXCOORD5 = 1.0;

vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec2 a_texcoord0 : TEXCOORD0;
vec3 a_tangent   : TANGENT;
vec3 a_bitangent : BITANGENT;

// the model matrix per instance (instanced draws)
vec4 i_data0 : TEXCOORD31;
vec4 i_data1 : TEXCOORD30;
vec4 i_data2 : TEXCOORD29;
vec4 i_data3 : TEXCOORD28;
vec4 i_data4 : TEXCOORD27;  // the anchor (InstancedMeshComponent)

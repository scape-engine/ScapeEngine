vec3 v_worldPos  : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
vec3 v_normal    : TEXCOORD1 = vec3(0.0, 1.0, 0.0);
vec2 v_texcoord0 : TEXCOORD2 = vec2(0.0, 0.0);
vec3 v_tangent   : TEXCOORD3 = vec3(0.0, 0.0, 0.0);
vec3 v_bitangent : TEXCOORD4 = vec3(0.0, 0.0, 0.0);
float v_fade     : TEXCOORD5 = 1.0;

vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec2 a_texcoord0 : TEXCOORD0;
vec3 a_tangent   : TANGENT;
vec3 a_bitangent : BITANGENT;

// four bones and their weights (skinned draws)
vec4 a_indices : BLENDINDICES;
vec4 a_weight : BLENDWEIGHT;

// the model matrix per instance (instanced draws)
vec4 i_data0 : TEXCOORD31;
vec4 i_data1 : TEXCOORD30;
vec4 i_data2 : TEXCOORD29;
vec4 i_data3 : TEXCOORD28;
vec4 i_data4 : TEXCOORD27;  // the anchor (InstancedMeshComponent)

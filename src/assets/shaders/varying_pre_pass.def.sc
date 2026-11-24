vec3 v_viewNormal : TEXCOORD0 = vec3(0.0, 0.0, -1.0);
float v_viewDepth : TEXCOORD1 = 0.0;
float v_fade : TEXCOORD2 = 1.0;

vec3 a_position : POSITION;
vec3 a_normal : NORMAL;

// four bones and their weights (skinned draws)
vec4 a_indices : BLENDINDICES;
vec4 a_weight : BLENDWEIGHT;

// the model matrix per instance (instanced draws)
vec4 i_data0 : TEXCOORD31;
vec4 i_data1 : TEXCOORD30;
vec4 i_data2 : TEXCOORD29;
vec4 i_data3 : TEXCOORD28;

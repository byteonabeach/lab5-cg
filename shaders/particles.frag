#version 450
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec4 gNormal;
layout(location = 1) out vec4 gAlbedo;

void main() {
    vec2 d = (inTexCoord - vec2(0.5)) * 2.0;
    float d2 = dot(d, d);
    if (d2 > 1.0) discard;
    float nz = sqrt(1.0 - d2);
    vec3 N_in = normalize(inNormal);
    vec3 T_in = normalize(inTangent);
    vec3 B_in = cross(N_in, T_in);
    vec3 N = normalize(T_in * d.x + B_in * d.y + N_in * nz);

    gNormal = vec4(N, 0.0);
    gAlbedo = vec4(inColor.rgb, 1.0);
}
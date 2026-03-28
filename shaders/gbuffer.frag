#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
<<<<<<< HEAD
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec4 inColor;
layout(location = 5) in flat int inIsUnlit;
=======
layout(location = 3) in vec4 inColor;
layout(location = 4) in flat int inIsUnlit;
>>>>>>> parent of ad8e0d8 (SPONZA!!!)

layout(set = 1, binding = 0) uniform sampler2D diffuseTex;
layout(set = 1, binding = 1) uniform sampler2D normalTex;

layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;

void main() {
    if (inIsUnlit != 0) {
        gPosition = vec4(inWorldPos, 1.0);
        gNormal = vec4(0.0);
        gAlbedo = inColor;
        return;
    }

    vec4 diffuse = texture(diffuseTex, inTexCoord);
    if (diffuse.a < 0.1) discard;

<<<<<<< HEAD
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent);

    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    vec3 tangentNormal = texture(normalTex, inTexCoord).xyz * 2.0 - 1.0;
    vec3 finalNormal = normalize(TBN * tangentNormal);

    gNormal = vec4(finalNormal, 0.0);
=======
    gPosition = vec4(inWorldPos, 1.0);
    gNormal = vec4(normalize(inNormal), 0.0);
>>>>>>> parent of ad8e0d8 (SPONZA!!!)
    gAlbedo = vec4(diffuse.rgb, 1.0);
}

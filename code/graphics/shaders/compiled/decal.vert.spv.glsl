#version 150

layout(std140) uniform decalGlobalData
{
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 invViewMatrix;
    mat4 invProjMatrix;
    vec2 viewportSize;
} _76;

in vec4 vertModelMatrix0;
in vec4 vertModelMatrix1;
in vec4 vertModelMatrix2;
in vec4 vertModelMatrix3;
flat out float normal_angle_cutoff;
flat out float angle_fade_start;
flat out float alpha_scale;
flat out mat4 invModelMatrix;
flat out vec3 decalDirection;
in vec4 vertPosition;

void main()
{
    normal_angle_cutoff = vertModelMatrix0.w;
    angle_fade_start = vertModelMatrix1.w;
    alpha_scale = vertModelMatrix2.w;
    mat4 _117 = mat4(vertModelMatrix0, vertModelMatrix1, vertModelMatrix2, vertModelMatrix3);
    _117[0].w = 0.0;
    mat4 _119 = _117;
    _119[1].w = 0.0;
    mat4 _121 = _119;
    _121[2].w = 0.0;
    invModelMatrix = inverse(_121);
    decalDirection = mat3(_76.viewMatrix[0].xyz, _76.viewMatrix[1].xyz, _76.viewMatrix[2].xyz) * _121[2].xyz;
    gl_Position = ((_76.projMatrix * _76.viewMatrix) * _121) * vertPosition;
}


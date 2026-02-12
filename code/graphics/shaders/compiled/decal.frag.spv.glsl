#version 150

layout(std140) uniform decalGlobalData
{
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 invViewMatrix;
    mat4 invProjMatrix;
    vec2 viewportSize;
} _83;

layout(std140) uniform decalInfoData
{
    int diffuse_index;
    int glow_index;
    int normal_index;
    int diffuse_blend_mode;
    int glow_blend_mode;
} _132;

uniform sampler2D gDepthBuffer;
uniform sampler2D gNormalBuffer;
uniform sampler2DArray decalTextures;

flat in vec3 decalDirection;
flat in float normal_angle_cutoff;
flat in float angle_fade_start;
flat in mat4 invModelMatrix;
flat in float alpha_scale;
out vec4 fragOut0;
out vec4 fragOut2;
out vec4 fragOut4;
out vec4 fragOut1;
out vec4 fragOut3;
out vec4 fragOut5;

vec4 _704;

void main()
{
    ivec2 _470 = ivec2(gl_FragCoord.xy);
    vec4 _479 = _83.invProjMatrix * vec4(((gl_FragCoord.xy / _83.viewportSize) * 2.0) - vec2(1.0), texelFetch(gDepthBuffer, _470, 0).x, 1.0);
    vec3 _485 = _479.xyz / vec3(_479.w);
    vec4 _500 = (invModelMatrix * _83.invViewMatrix) * vec4(_485, 1.0);
    if (any(greaterThan(abs(_500.xyz), vec3(0.5))) || any(isnan(_500)))
    {
        discard;
    }
    vec2 _524 = _500.xy + vec2(0.5);
    vec3 _664;
    vec3 _666;
    vec3 _667;
    if (_132.normal_index < 0)
    {
        _667 = vec3(0.0);
        _666 = vec3(0.0);
        _664 = texelFetch(gNormalBuffer, _470, 0).xyz;
    }
    else
    {
        vec3 _544 = dFdx(_485);
        vec3 _546 = dFdy(_485);
        _667 = normalize(_546);
        _666 = normalize(_544);
        _664 = normalize(cross(_544, _546));
    }
    float _560 = acos(clamp(dot(_664, decalDirection), -1.0, 1.0));
    if (_560 > normal_angle_cutoff)
    {
        discard;
    }
    float _572 = (alpha_scale * (1.0 - smoothstep(0.4000000059604644775390625, 0.5, abs(_500.z)))) * (1.0 - smoothstep(angle_fade_start, normal_angle_cutoff, _560));
    vec4 _688;
    if (_132.diffuse_index >= 0)
    {
        vec4 _297 = texture(decalTextures, vec3(_524, float(_132.diffuse_index)));
        vec3 _578 = pow(_297.xyz, vec3(2.2000000476837158203125));
        float _303 = _578.x;
        vec4 _621 = _704;
        _621.x = _303;
        vec4 _623 = _621;
        _623.y = _578.y;
        vec4 _625 = _623;
        _625.z = _578.z;
        vec4 _689;
        if (_132.diffuse_blend_mode == 0)
        {
            _689 = vec4(_303, _578.yz, _297.w * _572);
        }
        else
        {
            _689 = vec4(_625.xyz * _572, 1.0);
        }
        _688 = _689;
    }
    else
    {
        _688 = vec4(0.0);
    }
    vec4 _697;
    if (_132.glow_index >= 0)
    {
        vec4 _347 = texture(decalTextures, vec3(_524, float(_132.glow_index)));
        vec3 _353 = pow(_347.xyz, vec3(2.2000000476837158203125)) * 3.0;
        vec4 _628 = _704;
        _628.x = _353.x;
        vec4 _630 = _628;
        _630.y = _353.y;
        vec4 _632 = _630;
        _632.z = _353.z;
        vec3 _363 = _632.xyz * 1.5;
        float _365 = _363.x;
        vec4 _634 = _704;
        _634.x = _365;
        vec4 _636 = _634;
        _636.y = _363.y;
        vec4 _638 = _636;
        _638.z = _363.z;
        vec4 _698;
        if (_132.glow_blend_mode == 0)
        {
            _698 = vec4(_365, _363.yz, _347.w * _572);
        }
        else
        {
            vec3 _395 = _638.xyz * _572;
            vec4 _642 = vec4(0.0);
            _642.x = _395.x;
            vec4 _644 = _642;
            _644.y = _395.y;
            vec4 _646 = _644;
            _646.z = _395.z;
            _698 = _646;
        }
        _697 = _698;
    }
    else
    {
        _697 = vec4(0.0);
    }
    vec3 _691;
    if (_132.normal_index >= 0)
    {
        vec2 _589 = (texture(decalTextures, vec3(_524, float(_132.normal_index))).wy * 2.0) - vec2(1.0);
        float _591 = _589.x;
        float _593 = _589.y;
        _691 = (mat3(_667, _666, _664) * vec3(_591, _593, sqrt(max(0.0, (1.0 - (_591 * _591)) - (_593 * _593))))) * _572;
    }
    else
    {
        _691 = vec3(0.0);
    }
    fragOut0 = _688;
    fragOut2 = vec4(_691, 0.0);
    fragOut4 = _697;
    fragOut1 = vec4(0.0);
    fragOut3 = vec4(0.0);
    fragOut5 = vec4(0.0);
}


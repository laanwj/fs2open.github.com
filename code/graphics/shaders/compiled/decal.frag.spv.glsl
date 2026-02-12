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
} _134;

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

vec4 _710;

void main()
{
    vec2 _461 = gl_FragCoord.xy / _83.viewportSize;
    ivec2 _474 = ivec2(gl_FragCoord.xy);
    vec4 _483 = _83.invProjMatrix * vec4((_461.x * 2.0) - 1.0, 1.0 - (_461.y * 2.0), texelFetch(gDepthBuffer, _474, 0).x, 1.0);
    vec3 _489 = _483.xyz / vec3(_483.w);
    vec4 _504 = (invModelMatrix * _83.invViewMatrix) * vec4(_489, 1.0);
    if (any(greaterThan(abs(_504.xyz), vec3(0.5))) || any(isnan(_504)))
    {
        discard;
    }
    vec2 _528 = _504.xy + vec2(0.5);
    vec3 _670;
    vec3 _672;
    vec3 _673;
    if (_134.normal_index < 0)
    {
        _673 = vec3(0.0);
        _672 = vec3(0.0);
        _670 = texelFetch(gNormalBuffer, _474, 0).xyz;
    }
    else
    {
        vec3 _548 = dFdx(_489);
        vec3 _550 = dFdy(_489);
        _673 = normalize(_550);
        _672 = normalize(_548);
        _670 = normalize(cross(_548, _550));
    }
    float _564 = acos(clamp(dot(_670, decalDirection), -1.0, 1.0));
    if (_564 > normal_angle_cutoff)
    {
        discard;
    }
    float _576 = (alpha_scale * (1.0 - smoothstep(0.4000000059604644775390625, 0.5, abs(_504.z)))) * (1.0 - smoothstep(angle_fade_start, normal_angle_cutoff, _564));
    vec4 _694;
    if (_134.diffuse_index >= 0)
    {
        vec4 _299 = texture(decalTextures, vec3(_528, float(_134.diffuse_index)));
        vec3 _582 = pow(_299.xyz, vec3(2.2000000476837158203125));
        float _305 = _582.x;
        vec4 _627 = _710;
        _627.x = _305;
        vec4 _629 = _627;
        _629.y = _582.y;
        vec4 _631 = _629;
        _631.z = _582.z;
        vec4 _695;
        if (_134.diffuse_blend_mode == 0)
        {
            _695 = vec4(_305, _582.yz, _299.w * _576);
        }
        else
        {
            _695 = vec4(_631.xyz * _576, 1.0);
        }
        _694 = _695;
    }
    else
    {
        _694 = vec4(0.0);
    }
    vec4 _703;
    if (_134.glow_index >= 0)
    {
        vec4 _349 = texture(decalTextures, vec3(_528, float(_134.glow_index)));
        vec3 _355 = pow(_349.xyz, vec3(2.2000000476837158203125)) * 3.0;
        vec4 _634 = _710;
        _634.x = _355.x;
        vec4 _636 = _634;
        _636.y = _355.y;
        vec4 _638 = _636;
        _638.z = _355.z;
        vec3 _365 = _638.xyz * 1.5;
        float _367 = _365.x;
        vec4 _640 = _710;
        _640.x = _367;
        vec4 _642 = _640;
        _642.y = _365.y;
        vec4 _644 = _642;
        _644.z = _365.z;
        vec4 _704;
        if (_134.glow_blend_mode == 0)
        {
            _704 = vec4(_367, _365.yz, _349.w * _576);
        }
        else
        {
            vec3 _397 = _644.xyz * _576;
            vec4 _648 = vec4(0.0);
            _648.x = _397.x;
            vec4 _650 = _648;
            _650.y = _397.y;
            vec4 _652 = _650;
            _652.z = _397.z;
            _704 = _652;
        }
        _703 = _704;
    }
    else
    {
        _703 = vec4(0.0);
    }
    vec3 _697;
    if (_134.normal_index >= 0)
    {
        vec2 _593 = (texture(decalTextures, vec3(_528, float(_134.normal_index))).wy * 2.0) - vec2(1.0);
        float _595 = _593.x;
        float _597 = _593.y;
        _697 = (mat3(_673, _672, _670) * vec3(_595, _597, sqrt(max(0.0, (1.0 - (_595 * _595)) - (_597 * _597))))) * _576;
    }
    else
    {
        _697 = vec3(0.0);
    }
    fragOut0 = _694;
    fragOut2 = vec4(_697, 0.0);
    fragOut4 = _703;
    fragOut1 = vec4(0.0);
    fragOut3 = vec4(0.0);
    fragOut5 = vec4(0.0);
}


#version 150

layout(std140) uniform genericData
{
    float timer;
    float noise_amount;
    float saturation;
    float brightness;
    float contrast;
    float film_grain;
    float tv_stripes;
    float cutoff;
    vec3 tint;
    float dither;
    vec3 custom_effect_vec3_a;
    float custom_effect_float_a;
    vec3 custom_effect_vec3_b;
    float custom_effect_float_b;
    int effectFlags;
} _17;

uniform sampler2D tex;

in vec2 fragTexCoord;
out vec4 fragOut0;

vec4 _443;

void main()
{
    vec2 _426;
    if ((_17.effectFlags & 1) != 0)
    {
        float _51 = _17.timer * sin(((fragTexCoord.x * fragTexCoord.y) * 100.0) + _17.timer);
        float _58 = mod(_51, 8.0) * mod(_51, 4.0);
        _426 = vec2(mod(_58, _17.noise_amount), mod(_58, _17.noise_amount + 0.00200000009499490261077880859375));
    }
    else
    {
        _426 = vec2(0.0);
    }
    vec4 _81 = texture(tex, fragTexCoord + _426);
    vec4 _427;
    if ((_17.effectFlags & 2) != 0)
    {
        float _97 = dot(_81.xyz, vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.18400000035762786865234375));
        vec4 _370 = _81;
        _370.x = _97;
        vec4 _372 = _370;
        _372.y = _97;
        vec4 _374 = _372;
        _374.z = _97;
        _427 = mix(_81, _374, vec4(1.0 - _17.saturation));
    }
    else
    {
        _427 = _81;
    }
    vec4 _428;
    if ((_17.effectFlags & 4) != 0)
    {
        vec3 _130 = _427.xyz * vec3(_17.brightness);
        vec4 _376 = _427;
        _376.x = _130.x;
        vec4 _378 = _376;
        _378.y = _130.y;
        vec4 _380 = _378;
        _380.z = _130.z;
        _428 = _380;
    }
    else
    {
        _428 = _427;
    }
    vec4 _429;
    if ((_17.effectFlags & 8) != 0)
    {
        vec3 _152 = _428.xyz + vec3(0.5 - (0.5 * _17.contrast));
        vec4 _382 = _428;
        _382.x = _152.x;
        vec4 _384 = _382;
        _384.y = _152.y;
        vec4 _386 = _384;
        _386.z = _152.z;
        _429 = _386;
    }
    else
    {
        _429 = _428;
    }
    vec4 _436;
    if ((_17.effectFlags & 16) != 0)
    {
        float _176 = ((fragTexCoord.x * fragTexCoord.y) * _17.timer) * 1000.0;
        vec3 _208 = mix(_429.xyz, _429.xyz + (_429.xyz * clamp(0.100000001490116119384765625 + (mod(mod(_176, 13.0) * mod(_176, 123.0), 0.00999999977648258209228515625) * 100.0), 0.0, 1.0)), vec3(_17.film_grain));
        vec4 _388 = _429;
        _388.x = _208.x;
        vec4 _390 = _388;
        _390.y = _208.y;
        vec4 _392 = _390;
        _392.z = _208.z;
        _436 = _392;
    }
    else
    {
        _436 = _429;
    }
    vec4 _444;
    if ((_17.effectFlags & 32) != 0)
    {
        float _226 = fragTexCoord.y * 2048.0;
        float _227 = sin(_226);
        vec3 _257 = mix(_436.xyz, _436.xyz + ((_436.xyz * vec3(_227, cos(_226), _227)) * 0.800000011920928955078125), vec3(_17.tv_stripes));
        vec4 _401 = _436;
        _401.x = _257.x;
        vec4 _403 = _401;
        _403.y = _257.y;
        vec4 _405 = _403;
        _405.z = _257.z;
        _444 = _405;
    }
    else
    {
        _444 = _436;
    }
    vec4 _447;
    if ((_17.effectFlags & 64) != 0)
    {
        vec4 _448;
        if (_17.cutoff > 0.0)
        {
            float _280 = dot(_81.xyz, vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.18400000035762786865234375));
            vec4 _407 = _443;
            _407.x = _280;
            vec4 _409 = _407;
            _409.y = _280;
            vec4 _411 = _409;
            _411.z = _280;
            float _291 = length(_444.xyz);
            vec4 _445;
            if (_291 > 1.0)
            {
                _445 = _444 / vec4(_291);
            }
            else
            {
                _445 = _444;
            }
            _448 = mix(_411, _444, vec4(dot(_445.xyz, vec3(0.577300012111663818359375)) * _17.cutoff));
        }
        else
        {
            _448 = _444;
        }
        _447 = _448;
    }
    else
    {
        _447 = _444;
    }
    vec4 _449;
    if ((_17.effectFlags & 128) != 0)
    {
        vec3 _338 = floor((_447.xyz * 4.0) + vec3(0.5)) * vec3(0.25);
        vec4 _413 = _447;
        _413.x = _338.x;
        vec4 _415 = _413;
        _415.y = _338.y;
        vec4 _417 = _415;
        _417.z = _338.z;
        _449 = _417;
    }
    else
    {
        _449 = _447;
    }
    vec4 _450;
    if ((_17.effectFlags & 256) != 0)
    {
        vec3 _357 = _449.xyz + _17.tint;
        vec4 _419 = _449;
        _419.x = _357.x;
        vec4 _421 = _419;
        _421.y = _357.y;
        vec4 _423 = _421;
        _423.z = _357.z;
        _450 = _423;
    }
    else
    {
        _450 = _449;
    }
    vec4 _425 = _450;
    _425.w = 1.0;
    fragOut0 = _425;
}


#version 150

layout(std140) uniform lightData
{
    vec3 diffuseLightColor;
    float coneAngle;
    vec3 lightDir;
    float coneInnerAngle;
    vec3 coneDir;
    float dualCone;
    vec3 scale;
    float lightRadius;
    int lightType;
    int enable_shadows;
    float sourceRadius;
    float pad0;
} _223;

layout(std140) uniform matrixData
{
    mat4 modelViewMatrix;
    mat4 projMatrix;
} _265;

layout(std140) uniform globalDeferredData
{
    mat4 shadow_mv_matrix;
    mat4 shadow_proj_matrix[4];
    mat4 inv_view_matrix;
    float veryneardist;
    float neardist;
    float middist;
    float fardist;
    float invScreenWidth;
    float invScreenHeight;
    float nearPlane;
    float globalPad;
} _528;

uniform sampler2D sTextures[16];

out vec4 fragOut0;

float _1172;
vec3 _1177;

void main()
{
    vec2 _534 = gl_FragCoord.xy * vec2(_528.invScreenWidth, _528.invScreenHeight);
    vec4 _547 = texture(sTextures[2], _534);
    vec3 _550 = _547.xyz;
    if (abs(dot(_550, _550)) < (_528.nearPlane * _528.nearPlane))
    {
        discard;
    }
    vec4 _569 = texture(sTextures[0], _534);
    vec3 _572 = _569.xyz;
    vec4 _577 = texture(sTextures[1], _534);
    vec3 _581 = normalize(_577.xyz);
    float _589 = clamp(1.0 - _577.w, 0.0, 1.0);
    float _593 = _589 * _589;
    vec3 _597 = normalize(-_550);
    vec3 _602 = reflect(-_597, _581);
    vec4 _607 = texture(sTextures[3], _534);
    vec4 _1191;
    if (_223.lightType == 4)
    {
        vec3 _623 = (_223.diffuseLightColor * _572) * _547.w;
        vec4 _1157 = vec4(1.0);
        _1157.x = _623.x;
        vec4 _1159 = _1157;
        _1159.y = _623.y;
        vec4 _1161 = _1159;
        _1161.z = _623.z;
        _1191 = _1161;
    }
    else
    {
        vec3 _1179;
        float _1180;
        float _1181;
        if (_223.lightType == 0)
        {
            _1181 = 1.0;
            _1180 = 1.0;
            _1179 = normalize(_223.lightDir);
        }
        else
        {
            float _1169;
            vec3 _1174;
            float _1182;
            if (_223.lightType == 1)
            {
                vec3 _747 = _265.modelViewMatrix[3].xyz - _550;
                vec3 _964 = (_602 * max(dot(_747, _602), _223.sourceRadius)) - _747;
                vec3 _974 = _747 + (_964 * clamp(_223.sourceRadius / length(_964), 0.0, 1.0));
                float _754 = length(_974);
                float _765 = _593 / clamp(_593 + (_223.sourceRadius / (2.0 * _754)), 0.0, 1.0);
                if (_754 > _223.lightRadius)
                {
                    discard;
                }
                _1182 = _765 * _765;
                _1174 = _974;
                _1169 = 1.0 - clamp(sqrt(_754 / _223.lightRadius), 0.0, 1.0);
            }
            else
            {
                float _1170;
                vec3 _1175;
                float _1183;
                if (_223.lightType == 2)
                {
                    vec3 _797 = vec3((_265.modelViewMatrix * vec4(0.0, 0.0, -_223.scale.z, 0.0)).xyz);
                    vec3 _799 = normalize(_797);
                    vec3 _817 = (_265.modelViewMatrix[3].xyz - (_799 * _223.lightRadius)) - _550;
                    vec3 _823 = cross(_602, _799);
                    vec3 _837 = (_817 - (_602 * dot(_817, _602))) - (_823 * dot(_817, _823));
                    vec3 _851 = _817 - (_799 * clamp(dot(_837, _837) / dot(_799, _837), 0.0, length(_797 - (_799 * (2.0 * _223.lightRadius)))));
                    vec3 _987 = (_602 * max(dot(_851, _602), _223.sourceRadius)) - _851;
                    vec3 _997 = _851 + (_987 * clamp(_223.sourceRadius / length(_987), 0.0, 1.0));
                    float _856 = length(_997);
                    if (_856 > _223.lightRadius)
                    {
                        discard;
                    }
                    _1183 = _593 / min(_593 + (_223.sourceRadius / (2.0 * _856)), 1.0);
                    _1175 = _997;
                    _1170 = 1.0 - clamp(sqrt(_856 / _223.lightRadius), 0.0, 1.0);
                }
                else
                {
                    bool _884 = _223.lightType == 3;
                    float _1171;
                    vec3 _1176;
                    if (_884)
                    {
                        vec3 _888 = _265.modelViewMatrix[3].xyz - _550;
                        float _894 = dot(normalize(-_888), _223.coneDir);
                        float _903 = 1.0 - clamp(sqrt(length(_888) / _223.lightRadius), 0.0, 1.0);
                        float _1173;
                        if (_223.dualCone > 0.5)
                        {
                            float _909 = abs(_894);
                            if (_909 < _223.coneAngle)
                            {
                                discard;
                            }
                            _1173 = _903 * smoothstep(_223.coneAngle, _223.coneInnerAngle, _909);
                        }
                        else
                        {
                            if (_894 < _223.coneAngle)
                            {
                                discard;
                            }
                            _1173 = _903 * smoothstep(_223.coneAngle, _223.coneInnerAngle, _894);
                        }
                        _1176 = _888;
                        _1171 = _1173;
                    }
                    else
                    {
                        _1176 = _1177;
                        _1171 = _1172;
                    }
                    _1183 = _884 ? 1.0 : _1172;
                    _1175 = _1176;
                    _1170 = _1171;
                }
                _1182 = _1183;
                _1174 = _1175;
                _1169 = _1170;
            }
            _1181 = _1182;
            _1180 = _1169 * _1169;
            _1179 = normalize(_1174);
        }
        vec3 _656 = normalize(_1179 + _597);
        float _661 = clamp(dot(_581, _1179), 0.0, 1.0);
        vec3 _664 = _607.xyz;
        float _1048 = _593 * _593;
        float _1052 = clamp(dot(_581, _656), 0.0, 1.0);
        float _1056 = clamp(dot(_581, _597), 0.0, 1.0);
        float _1063 = ((_1052 * _1052) * (_1048 - 1.0)) + 1.00010001659393310546875;
        vec3 _1125 = vec3(1.0) - _664;
        vec3 _1077 = mix(_664, _664 + (_1125 * pow(1.0 - clamp(dot(_597, _656), 0.0, 1.0), 5.0)), vec3(_607.w));
        float _1079 = _589 + 1.0;
        float _1083 = (_1079 * _1079) * 0.125;
        float _1139 = 1.0 - _1083;
        vec3 _688 = (((((((_1077 * (_1048 / ((3.141590118408203125 * _1063) * _1063))) * ((_661 / ((_661 * _1139) + _1083)) * (_1056 / ((_1056 * _1139) + _1083)))) / vec3(((4.0 * _1056) * _661) + 9.9999997473787516355514526367188e-05)) + ((((vec3(1.0) - _1077) * _1125) * _572) * vec3(0.31831014156341552734375))) * _661) * _223.diffuseLightColor) * _1180) * _1181;
        vec4 _1164 = vec4(1.0);
        _1164.x = _688.x;
        vec4 _1166 = _1164;
        _1166.y = _688.y;
        vec4 _1168 = _1166;
        _1168.z = _688.z;
        _1191 = _1168;
    }
    fragOut0 = max(_1191, vec4(0.0));
}


#version 150
#extension GL_ARB_texture_query_levels : require

layout(std140) uniform genericData
{
    mat4 p_inv;
    mat4 v_inv;
    vec3 camera;
    float zNear;
    vec3 globalLightDirection;
    float zFar;
    vec3 globalLightDiffuse;
    float stepsize;
    vec3 nebPos;
    float opacitydistance;
    vec3 nebSize;
    float alphalim;
    vec3 nebulaColor;
    float udfScale;
    float emissiveSpreadFactor;
    float emissiveIntensity;
    float emissiveFalloff;
    float henyeyGreensteinCoeff;
    vec3 noiseColor;
    int directionalLightSampleSteps;
    float directionalLightStepSize;
    float noiseColorScale1;
    float noiseColorScale2;
    float noiseIntensity;
    float aspect;
    float fov;
    int doEdgeSmoothing;
    int useNoise;
} _21;

uniform sampler2D tex2D[16];
uniform sampler2D depth;
uniform sampler3D volume_tex;
uniform sampler3D noise_volume_tex;

in vec2 fragTexCoord;
out vec4 fragOut0;

void main()
{
    vec4 _739 = _21.p_inv * vec4((fragTexCoord * 2.0) - vec2(1.0), -1.0, 1.0);
    _739.w = 0.0;
    vec3 _81 = normalize((_21.v_inv * _739).xyz);
    vec4 _93 = texture(tex2D[0], fragTexCoord);
    vec3 _103 = _21.nebSize * 0.5;
    vec3 _119 = ((_21.nebPos - _103) - _21.camera) / _81;
    vec3 _126 = ((_21.nebPos + _103) - _21.camera) / _81;
    vec3 _130 = min(_119, _126);
    vec3 _134 = max(_119, _126);
    vec2 _143 = (fragTexCoord - vec2(0.5)) * _21.fov;
    vec4 _156 = texture(depth, fragTexCoord);
    float _181 = tan(_143.x * _21.aspect);
    float _190 = tan(_143.y);
    float _197 = ((_21.zNear * _21.zFar) / (_21.zFar - (_156.x * (_21.zFar - _21.zNear)))) * sqrt((1.0 + (_181 * _181)) + (_190 * _190));
    float _208 = max(0.0, max(_130.x, max(_130.y, _130.z)));
    float _219 = min(_197, min(_134.x, min(_134.y, _134.z)));
    vec3 _235 = ((_21.camera + (_81 * _208)) / _21.nebSize) + vec3(0.5);
    vec3 _238 = dFdx(_235);
    vec3 _241 = dFdy(_235);
    vec3 _253 = vec3(1.0) / vec3(textureSize(volume_tex, 0));
    vec3 _807;
    _807 = vec3(0.0);
    float _687;
    float _690;
    float _810;
    vec3 _811;
    vec3 _812;
    float _813;
    float _785 = _208;
    float _786 = 1.0;
    float _798 = 0.0;
    for (;;)
    {
        if (_785 < _219)
        {
            vec3 _274 = (_21.camera + (_81 * _785)) - _21.nebPos;
            vec3 _281 = (_274 / _21.nebSize) + vec3(0.5);
            vec4 _287 = textureGrad(volume_tex, _281, _238, _241);
            float _290 = _287.w;
            float _787;
            if ((_21.doEdgeSmoothing != 0) && (_786 > 0.800000011920928955078125))
            {
                float _307 = _253.x;
                float _309 = _253.y;
                float _311 = _253.z;
                float _326 = -_311;
                float _340 = -_309;
                float _371 = -_307;
                _787 = (_290 * 0.5) + ((((((((textureGrad(volume_tex, _281 + _253, _238, _241).w + textureGrad(volume_tex, _281 + vec3(_307, _309, _326), _238, _241).w) + textureGrad(volume_tex, _281 + vec3(_307, _340, _311), _238, _241).w) + textureGrad(volume_tex, _281 + vec3(_307, _340, _326), _238, _241).w) + textureGrad(volume_tex, _281 + vec3(_371, _309, _311), _238, _241).w) + textureGrad(volume_tex, _281 + vec3(_371, _309, _326), _238, _241).w) + textureGrad(volume_tex, _281 + vec3(_371, _340, _311), _238, _241).w) + textureGrad(volume_tex, _281 + vec3(_371, _340, _326), _238, _241).w) * 0.0625);
            }
            else
            {
                _787 = _290;
            }
            float _456 = min(max(_21.stepsize, (step(_787, 0.00999999977648258209228515625) * _287.x) * _21.udfScale), _219 - _785);
            float _471 = (1.0 - pow(_21.alphalim, 1.0 / (_21.opacitydistance / _456))) * _787;
            if (_787 > 0.00999999977648258209228515625)
            {
                vec3 _789;
                if (_21.useNoise != 0)
                {
                    _789 = mix(_21.nebulaColor, _21.noiseColor, vec3(smoothstep(0.0, 1.0, ((textureGrad(noise_volume_tex, _274 / vec3(_21.noiseColorScale1), _238, _241).x + textureGrad(noise_volume_tex, _274 / vec3(_21.noiseColorScale2), _238, _241).y) * 0.5) * _21.noiseIntensity)));
                }
                else
                {
                    _789 = _21.nebulaColor;
                }
                float _720 = _21.henyeyGreensteinCoeff * _21.henyeyGreensteinCoeff;
                float _540 = 4.0 / float(_21.directionalLightSampleSteps);
                float _791;
                _791 = 0.100000001490116119384765625;
                for (int _790 = 1; _790 <= _21.directionalLightSampleSteps; )
                {
                    vec3 _570 = ((_274 - (_21.globalLightDirection * (float(_790) * _21.directionalLightStepSize))) / _21.nebSize) + vec3(0.5);
                    float _579 = _570.x;
                    float _587 = _570.y;
                    float _595 = _570.z;
                    _791 += (((((((textureGrad(volume_tex, _570, _238, _241).w * step(0.0, _579)) * step(_579, 1.0)) * step(0.0, _587)) * step(_587, 1.0)) * step(0.0, _595)) * step(_595, 1.0)) * _540);
                    _790++;
                    continue;
                }
                float _626 = _798 + (_787 * _456);
                _813 = _626;
                _812 = _807 + (clamp(((_789 * ((0.2820949256420135498046875 * (1.0 - _720)) / pow((1.0 + _720) + ((2.0 * _21.henyeyGreensteinCoeff) * dot(_81, _21.globalLightDirection)), 1.5))) * ((2.5980761051177978515625 * (1.0 - exp(_791 * (-2.0)))) * exp(-_791))) + clamp((textureLod(tex2D[1], fragTexCoord, clamp(_626 * _21.emissiveSpreadFactor, 0.0, float(textureQueryLevels(tex2D[1]) - 1))).xyz * pow(_21.alphalim, 1.0 / (_21.opacitydistance / (((_197 - _785) * _21.emissiveFalloff) + 0.00999999977648258209228515625)))) * _21.emissiveIntensity, vec3(0.0), vec3(1.0)), vec3(0.0), vec3(1.0)) * (_471 * _786));
            }
            else
            {
                _813 = _798;
                _812 = _807;
            }
            _687 = _786 * (1.0 - _471);
            _690 = _785 + _456;
            if (_687 < _21.alphalim)
            {
                _811 = _812;
                _810 = _687;
                break;
            }
            _807 = _812;
            _798 = _813;
            _786 = _687;
            _785 = _690;
            continue;
        }
        else
        {
            _811 = _807;
            _810 = _786;
            break;
        }
    }
    fragOut0 = vec4((_93.xyz * _810) + (_811 * (1.0 - _810)), 1.0);
}


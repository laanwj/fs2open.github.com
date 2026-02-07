#version 150

layout(std140) uniform NanoVGUniformData
{
    mat3 scissorMat;
    mat3 paintMat;
    vec4 innerCol;
    vec4 outerCol;
    vec2 scissorExt;
    vec2 scissorScale;
    vec2 extent;
    float radius;
    float feather;
    float strokeMult;
    float strokeThr;
    int texType;
    int type;
    vec2 viewSize;
    int texArrayIndex;
} _57;

uniform sampler2DArray nvg_tex;

in vec2 fpos;
in vec2 ftcoord;
out vec4 outColor;

vec4 _355;

void main()
{
    vec3 _293 = vec3(fpos, 1.0);
    vec2 _304 = vec2(0.5) - ((abs((_57.scissorMat * _293).xy) - _57.scissorExt) * _57.scissorScale);
    float _311 = clamp(_304.x, 0.0, 1.0) * clamp(_304.y, 0.0, 1.0);
    vec4 _351;
    if (_57.type == 0)
    {
        vec2 _323 = abs((_57.paintMat * _293).xy) - (_57.extent - vec2(_57.radius));
        _351 = mix(_57.innerCol, _57.outerCol, vec4(clamp((((min(max(_323.x, _323.y), 0.0) + length(max(_323, vec2(0.0)))) - _57.radius) + (_57.feather * 0.5)) / _57.feather, 0.0, 1.0))) * _311;
    }
    else
    {
        vec4 _352;
        if (_57.type == 1)
        {
            vec4 _190 = texture(nvg_tex, vec3((_57.paintMat * _293).xy / _57.extent, float(_57.texArrayIndex)));
            vec4 _349;
            if (_57.texType == 1)
            {
                float _201 = _190.w;
                _349 = vec4(_190.xyz * _201, _201);
            }
            else
            {
                _349 = _190;
            }
            vec4 _350;
            if (_57.texType == 2)
            {
                _350 = vec4(_349.x);
            }
            else
            {
                _350 = _349;
            }
            _352 = (_350 * _57.innerCol) * _311;
        }
        else
        {
            vec4 _353;
            if (_57.type == 2)
            {
                _353 = vec4(1.0);
            }
            else
            {
                vec4 _354;
                if (_57.type == 3)
                {
                    vec4 _250 = texture(nvg_tex, vec3(ftcoord, float(_57.texArrayIndex)));
                    vec4 _347;
                    if (_57.texType == 1)
                    {
                        float _259 = _250.w;
                        _347 = vec4(_250.xyz * _259, _259);
                    }
                    else
                    {
                        _347 = _250;
                    }
                    vec4 _348;
                    if (_57.texType == 2)
                    {
                        _348 = vec4(_347.x);
                    }
                    else
                    {
                        _348 = _347;
                    }
                    _354 = (_348 * _311) * _57.innerCol;
                }
                else
                {
                    _354 = _355;
                }
                _353 = _354;
            }
            _352 = _353;
        }
        _351 = _352;
    }
    outColor = _351;
}


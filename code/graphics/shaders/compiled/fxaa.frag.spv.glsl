#version 150

const float _452[9] = float[](1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

layout(std140) uniform genericData
{
    float rt_w;
    float rt_h;
    float pad0;
    float pad1;
} _27;

uniform sampler2D tex;

in vec2 fragTexCoord;
out vec4 fragOut0;

void main()
{
    do
    {
        float _33 = 1.0 / _27.rt_w;
        float _37 = 1.0 / _27.rt_h;
        vec4 _51 = textureLod(tex, fragTexCoord, 0.0);
        float _55 = _51.w;
        vec4 _61 = textureLodOffset(tex, fragTexCoord, 0.0, ivec2(0, 1));
        float _611 = _61.y;
        vec4 _68 = textureLodOffset(tex, fragTexCoord, 0.0, ivec2(1, 0));
        float _615 = _68.y;
        vec4 _76 = textureLodOffset(tex, fragTexCoord, 0.0, ivec2(0, -1));
        float _619 = _76.y;
        vec4 _83 = textureLodOffset(tex, fragTexCoord, 0.0, ivec2(-1, 0));
        float _623 = _83.y;
        float _95 = max(max(max(_611, _615), max(_619, _623)), _55);
        float _109 = _95 - min(min(min(_611, _615), min(_619, _623)), _55);
        if (_109 < max(0.0416666679084300994873046875, _95 * 0.083333335816860198974609375))
        {
            fragOut0 = _51;
            break;
        }
        vec4 _128 = textureLodOffset(tex, fragTexCoord, 0.0, ivec2(-1));
        float _627 = _128.y;
        vec4 _135 = textureLodOffset(tex, fragTexCoord, 0.0, ivec2(1));
        float _631 = _135.y;
        vec4 _142 = textureLodOffset(tex, fragTexCoord, 0.0, ivec2(1, -1));
        float _635 = _142.y;
        vec4 _149 = textureLodOffset(tex, fragTexCoord, 0.0, ivec2(-1, 1));
        float _639 = _149.y;
        float _155 = _619 + _611;
        float _159 = _623 + _615;
        float _167 = _627 + _639;
        float _170 = _635 + _631;
        float _188 = clamp(abs(((((_155 + _159) * 2.0) + (_167 + _170)) * 0.083333335816860198974609375) - _55) / _109, 0.0, 1.0);
        float _202 = (((-2.0) * _188) + 3.0) * (_188 * _188);
        float _211 = (-2.0) * _55;
        bool _271 = ((abs(((-2.0) * _623) + _167) + (abs(_211 + _155) * 2.0)) + abs(((-2.0) * _615) + _170)) >= ((abs(((-2.0) * _611) + (_639 + _631)) + (abs(_211 + _159) * 2.0)) + abs(((-2.0) * _619) + (_627 + _635)));
        float _785 = _271 ? _37 : _33;
        float _288 = _271 ? _619 : _623;
        float _293 = _271 ? _611 : _615;
        float _312 = abs(_288 - _55);
        float _314 = abs(_293 - _55);
        bool _315 = _312 >= _314;
        float _707;
        if (_315)
        {
            _707 = -_785;
        }
        else
        {
            _707 = _785;
        }
        bool _331 = !_271;
        vec2 _784 = vec2(_331 ? 0.0 : _33, _271 ? 0.0 : _37);
        vec2 _711;
        if (_331)
        {
            vec2 _675 = fragTexCoord;
            _675.x = fragTexCoord.x + (_707 * 0.5);
            _711 = _675;
        }
        else
        {
            _711 = fragTexCoord;
        }
        vec2 _712;
        if (_271)
        {
            vec2 _678 = _711;
            _678.y = _711.y + (_707 * 0.5);
            _712 = _678;
        }
        else
        {
            _712 = _711;
        }
        vec2 _372 = _784 * 1.0;
        vec2 _373 = _712 - _372;
        vec2 _378 = _712 + _372;
        float _399 = max(_312, _314) * 0.25;
        float _403 = ((!_315) ? (_293 + _55) : (_288 + _55)) * 0.5;
        bool _405 = (_55 - _403) < 0.0;
        float _409 = textureLod(tex, _373, 0.0).y - _403;
        float _413 = textureLod(tex, _378, 0.0).y - _403;
        bool _418 = abs(_409) >= _399;
        bool _423 = abs(_413) >= _399;
        vec2 _727;
        vec2 _732;
        float _738;
        float _742;
        _742 = _413;
        _738 = _409;
        _732 = _378;
        _727 = _373;
        bool _769;
        bool _770;
        vec2 _772;
        vec2 _773;
        float _775;
        float _776;
        int _723 = 1;
        bool _724 = _418;
        bool _725 = _423;
        for (;;)
        {
            bool _433 = _723 < 9;
            bool _441;
            if (_433)
            {
                _441 = (!_724) || (!_725);
            }
            else
            {
                _441 = _433;
            }
            if (_441)
            {
                if (!_724)
                {
                    vec2 _460 = _727 - (_784 * _452[_723]);
                    float _468 = textureLod(tex, _460, 0.0).y - _403;
                    _775 = _468;
                    _772 = _460;
                    _769 = abs(_468) >= _399;
                }
                else
                {
                    _775 = _738;
                    _772 = _727;
                    _769 = _724;
                }
                if (!_725)
                {
                    vec2 _484 = _732 + (_784 * _452[_723]);
                    float _492 = textureLod(tex, _484, 0.0).y - _403;
                    _776 = _492;
                    _773 = _484;
                    _770 = abs(_492) >= _399;
                }
                else
                {
                    _776 = _742;
                    _773 = _732;
                    _770 = _725;
                }
                _742 = _776;
                _738 = _775;
                _732 = _773;
                _727 = _772;
                _725 = _770;
                _724 = _769;
                _723++;
                continue;
            }
            else
            {
                break;
            }
        }
        float _729;
        if (_271)
        {
            _729 = fragTexCoord.x - _727.x;
        }
        else
        {
            _729 = fragTexCoord.y - _727.y;
        }
        float _734;
        if (_271)
        {
            _734 = _732.x - fragTexCoord.x;
        }
        else
        {
            _734 = _732.y - fragTexCoord.y;
        }
        float _743;
        if ((_729 < _734) ? ((_738 < 0.0) != _405) : ((_742 < 0.0) != _405))
        {
            _743 = (min(_729, _734) * ((-1.0) / (_734 + _729))) + 0.5;
        }
        else
        {
            _743 = 0.0;
        }
        float _576 = max(_743, (_202 * _202) * 0.3300000131130218505859375);
        vec2 _752;
        if (_331)
        {
            vec2 _693 = fragTexCoord;
            _693.x = fragTexCoord.x + (_576 * _707);
            _752 = _693;
        }
        else
        {
            _752 = fragTexCoord;
        }
        vec2 _753;
        if (_271)
        {
            vec2 _696 = _752;
            _696.y = _752.y + (_576 * _707);
            _753 = _696;
        }
        else
        {
            _753 = _752;
        }
        fragOut0 = textureLod(tex, _753, 0.0);
        break;
    } while(false);
}


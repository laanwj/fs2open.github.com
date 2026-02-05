#version 150

struct model_light
{
    vec4 position;
    vec3 diffuse_color;
    int light_type;
    vec3 direction;
    float attenuation;
    float ml_sourceRadius;
};

layout(std140) uniform modelData
{
    mat4 modelViewMatrix;
    mat4 modelMatrix;
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 textureMatrix;
    mat4 shadow_mv_matrix;
    mat4 shadow_proj_matrix[4];
    vec4 color;
    model_light lights[8];
    float outlineWidth;
    float fogStart;
    float fogScale;
    int buffer_matrix_offset;
    vec4 clip_equation;
    float thruster_scale;
    int use_clip_plane;
    int n_lights;
    float defaultGloss;
    vec3 ambientFactor;
    int desaturate;
    vec3 diffuseFactor;
    int blend_alpha;
    vec3 emissionFactor;
    int alphaGloss;
    int gammaSpec;
    int envGloss;
    int effect_num;
    int sBasemapIndex;
    vec4 fogColor;
    vec3 base_color;
    float anim_timer;
    vec3 stripe_color;
    float vpwidth;
    float vpheight;
    int team_glow_enabled;
    float znear;
    float zfar;
    float veryneardist;
    float neardist;
    float middist;
    float fardist;
    int sGlowmapIndex;
    int sSpecmapIndex;
    int sNormalmapIndex;
    int sAmbientmapIndex;
    int sMiscmapIndex;
    float alphaMult;
    int flags;
    float _pad0;
} _249;

uniform sampler2DArray materialTextures[16];

in vec4 fragPosition;
in vec4 fragTexCoord;
in vec3 fragTangent;
in vec3 fragBitangent;
in vec3 fragTangentNormal;
in vec3 fragNormal;
in float fragFogDist;
out vec4 fragOut0;

vec3 _2205;
vec4 _2206;

void main()
{
    vec3 _453 = normalize(-fragPosition.xyz);
    vec3 _500 = normalize(fragNormal);
    vec2 _1996;
    if ((_249.flags & 128) != 0)
    {
        _1996 = texture(materialTextures[5], vec3(fragTexCoord.xy, float(_249.sAmbientmapIndex))).xy;
    }
    else
    {
        _1996 = vec2(1.0);
    }
    vec3 _2020;
    if ((_249.flags & 64) != 0)
    {
        vec4 _549 = texture(materialTextures[3], vec3(fragTexCoord.xy, float(_249.sNormalmapIndex)));
        vec2 _554 = (_549.wy * 2.0) - vec2(1.0);
        float _557 = _554.x;
        vec3 _1790 = _2205;
        _1790.x = _557;
        vec3 _1792 = _1790;
        _1792.y = _554.y;
        vec3 _573 = mat3(fragTangent, fragBitangent, fragTangentNormal) * vec3(_557, _554.y, clamp(sqrt(1.0 - dot(_1792.xy, _1792.xy)), 9.9999997473787516355514526367188e-05, 1.0));
        float _576 = length(_573);
        vec3 _2021;
        if (_576 > 0.0)
        {
            _2021 = _573 / vec3(_576);
        }
        else
        {
            _2021 = _500;
        }
        _2020 = _2021;
    }
    else
    {
        _2020 = _500;
    }
    vec2 _1968;
    if (_249.effect_num >= 0)
    {
        float _609 = ((fragPosition.x * fragPosition.w) * 0.004999999888241291046142578125) + (_249.anim_timer * 20.0);
        float _616 = (fragPosition.y * fragPosition.w) * 0.004999999888241291046142578125;
        _1968 = vec2(cos(_609) * sin(_616), sin(_609) * cos(_616)) * 0.02999999932944774627685546875;
    }
    else
    {
        _1968 = vec2(0.0);
    }
    bool _644 = (_249.flags & 8) != 0;
    vec4 _1983;
    if (_644)
    {
        vec2 _1969;
        if (_249.effect_num == 2)
        {
            _1969 = fragTexCoord.xy + (_1968 * (1.0 - _249.anim_timer));
        }
        else
        {
            _1969 = fragTexCoord.xy;
        }
        vec4 _671 = texture(materialTextures[0], vec3(_1969, float(_249.sBasemapIndex)));
        vec4 _1970;
        if ((_249.flags & 4) != 0)
        {
            vec3 _1421 = pow(_671.xyz, vec3(2.2000000476837158203125));
            vec4 _1796 = _671;
            _1796.x = _1421.x;
            vec4 _1798 = _1796;
            _1798.y = _1421.y;
            vec4 _1800 = _1798;
            _1800.z = _1421.z;
            _1970 = _1800;
        }
        else
        {
            _1970 = _671;
        }
        vec4 _1971;
        if ((_249.flags & 16384) != 0)
        {
            vec4 _1803 = _1970;
            _1803.w = _1970.w * _249.alphaMult;
            _1971 = _1803;
        }
        else
        {
            _1971 = _1970;
        }
        bool _705 = _249.blend_alpha == 0;
        bool _712;
        if (_705)
        {
            _712 = _1971.w < 0.949999988079071044921875;
        }
        else
        {
            _712 = _705;
        }
        if (_712)
        {
            discard;
        }
        vec4 _1988;
        if (_249.blend_alpha == 1)
        {
            vec3 _725 = _1971.xyz * _1971.w;
            vec4 _1807 = _1971;
            _1807.x = _725.x;
            vec4 _1809 = _1807;
            _1809.y = _725.y;
            vec4 _1811 = _1809;
            _1811.z = _725.z;
            _1988 = _1811;
        }
        else
        {
            _1988 = _1971;
        }
        _1983 = _1988;
    }
    else
    {
        _1983 = _249.color;
    }
    vec2 _734 = _500.xy;
    vec2 _735 = dFdx(_734);
    vec2 _739 = dFdy(_734);
    float _754 = min(_249.defaultGloss, 1.0 - pow(clamp(max(dot(_735, _735), dot(_739, _739)), 0.0, 1.0), 0.3300000131130218505859375));
    bool _769 = (_249.flags & 32) != 0;
    vec4 _2012;
    float _2035;
    float _2050;
    if (_769)
    {
        vec4 _782 = texture(materialTextures[2], vec3(fragTexCoord.xy, float(_249.sSpecmapIndex)));
        vec4 _1989;
        if ((_249.flags & 16384) != 0)
        {
            _1989 = _782 * _249.alphaMult;
        }
        else
        {
            _1989 = _782;
        }
        float _2038;
        if (_249.alphaGloss != 0)
        {
            _2038 = _1989.w;
        }
        else
        {
            _2038 = _754;
        }
        bool _804 = _249.gammaSpec != 0;
        vec4 _1991;
        if (_804)
        {
            vec3 _810 = max(_1989.xyz, vec3(0.02999999932944774627685546875));
            vec4 _1814 = _2206;
            _1814.x = _810.x;
            vec4 _1816 = _1814;
            _1816.y = _810.y;
            vec4 _1818 = _1816;
            _1818.z = _810.z;
            _1991 = _1818;
        }
        else
        {
            _1991 = _1989;
        }
        vec4 _2013;
        if ((_249.flags & 4) != 0)
        {
            vec3 _1425 = pow(_1991.xyz, vec3(2.2000000476837158203125));
            vec4 _1820 = _2206;
            _1820.x = _1425.x;
            vec4 _1822 = _1820;
            _1822.y = _1425.y;
            vec4 _1824 = _1822;
            _1824.z = _1425.z;
            _2013 = _1824;
        }
        else
        {
            _2013 = _1991;
        }
        _2050 = float(_804);
        _2035 = _2038;
        _2012 = _2013;
    }
    else
    {
        _2050 = 0.0;
        _2035 = _754;
        _2012 = vec4(_1983.xyz * 0.100000001490116119384765625, _754);
    }
    vec3 _837 = _1983.xyz * _1996.y;
    vec4 _1827 = _1983;
    _1827.x = _837.x;
    vec4 _1829 = _1827;
    _1829.y = _837.y;
    vec4 _1831 = _1829;
    _1831.z = _837.z;
    vec3 _848 = _2012.xyz * _1996.y;
    vec4 _1834 = _2206;
    _1834.x = _848.x;
    vec4 _1836 = _1834;
    _1836.y = _848.y;
    vec4 _1838 = _1836;
    _1838.z = _848.z;
    bool _862 = (_249.flags & 256) != 0;
    vec4 _2043;
    vec4 _2046;
    vec3 _2076;
    vec4 _2084;
    if (_862)
    {
        vec4 _2044;
        vec4 _2047;
        vec3 _2077;
        vec4 _2085;
        if ((_249.flags & 512) != 0)
        {
            vec4 _883 = texture(materialTextures[6], vec3(fragTexCoord.xy, float(_249.sMiscmapIndex)));
            float _888 = _883.x;
            float _890 = _883.y;
            vec3 _908 = ((_249.base_color * _888) + (_249.stripe_color * _890)) + (vec3(-0.5) * (_888 + _890));
            bool _923 = (_249.flags & 4) != 0;
            vec4 _2014;
            vec4 _2015;
            if (_923)
            {
                vec3 _1429 = pow(_1831.xyz, vec3(0.4545454680919647216796875));
                vec4 _1846 = _1831;
                _1846.x = _1429.x;
                vec4 _1848 = _1846;
                _1848.y = _1429.y;
                vec4 _1850 = _1848;
                _1850.z = _1429.z;
                vec3 _1433 = pow(_1838.xyz, vec3(0.4545454680919647216796875));
                vec4 _1852 = _2206;
                _1852.x = _1433.x;
                vec4 _1854 = _1852;
                _1854.y = _1433.y;
                vec4 _1856 = _1854;
                _1856.z = _1433.z;
                _2015 = _1856;
                _2014 = _1850;
            }
            else
            {
                _2015 = _1838;
                _2014 = _1831;
            }
            vec3 _949 = _2014.xyz + _908;
            vec4 _1858 = _2014;
            _1858.x = _949.x;
            vec4 _1860 = _1858;
            _1860.y = _949.y;
            vec4 _1862 = _1860;
            _1862.z = _949.z;
            vec3 _958 = max(_1862.xyz, vec3(0.0));
            vec4 _1864 = _1862;
            _1864.x = _958.x;
            vec4 _1866 = _1864;
            _1866.y = _958.y;
            vec4 _1868 = _1866;
            _1868.z = _958.z;
            vec3 _968 = _2015.xyz + _908;
            vec4 _1870 = _2206;
            _1870.x = _968.x;
            vec4 _1872 = _1870;
            _1872.y = _968.y;
            vec4 _1874 = _1872;
            _1874.z = _968.z;
            vec3 _977 = max(_1874.xyz, vec3(0.02999999932944774627685546875));
            vec4 _1876 = _2206;
            _1876.x = _977.x;
            vec4 _1878 = _1876;
            _1878.y = _977.y;
            vec4 _1880 = _1878;
            _1880.z = _977.z;
            vec4 _2045;
            vec4 _2048;
            if (_923)
            {
                vec3 _1437 = pow(_1868.xyz, vec3(2.2000000476837158203125));
                vec4 _1882 = _1868;
                _1882.x = _1437.x;
                vec4 _1884 = _1882;
                _1884.y = _1437.y;
                vec4 _1886 = _1884;
                _1886.z = _1437.z;
                vec3 _1441 = pow(_1880.xyz, vec3(2.2000000476837158203125));
                vec4 _1888 = _2206;
                _1888.x = _1441.x;
                vec4 _1890 = _1888;
                _1890.y = _1441.y;
                vec4 _1892 = _1890;
                _1892.z = _1441.z;
                _2048 = _1886;
                _2045 = _1892;
            }
            else
            {
                _2048 = _1868;
                _2045 = _1880;
            }
            _2085 = _883;
            _2077 = (_249.base_color * _883.z) + (_249.stripe_color * _883.w);
            _2047 = _2048;
            _2044 = _2045;
        }
        else
        {
            _2085 = vec4(0.0);
            _2077 = vec3(0.0);
            _2047 = _1831;
            _2044 = _1838;
        }
        _2084 = _2085;
        _2076 = _2077;
        _2046 = _2047;
        _2043 = _2044;
    }
    else
    {
        _2084 = vec4(0.0);
        _2076 = vec3(0.0);
        _2046 = _1831;
        _2043 = _1838;
    }
    bool _1013 = (_249.flags & 2) == 0;
    vec4 _2126;
    if (_1013)
    {
        vec4 _2127;
        if ((_249.flags & 1) != 0)
        {
            vec3 _2073;
            _2073 = vec3(0.0);
            vec3 _1537;
            for (int _2072 = 0; _2072 < _249.n_lights; _2073 = _1537, _2072++)
            {
                float _1488 = clamp(1.0 - _2035, 0.0, 1.0);
                vec3 _2173;
                float _2174;
                if (_249.lights[_2072].light_type != 0)
                {
                    vec3 _2171;
                    float _2172;
                    if (_249.lights[_2072].light_type == 2)
                    {
                        vec3 _1589 = normalize(_249.lights[_2072].direction);
                        vec3 _1611 = (_249.lights[_2072].position.xyz - (_1589 * abs(dot(fragPosition.xyz - _249.lights[_2072].position.xyz, _1589)))) - fragPosition.xyz;
                        _2172 = length(_1611);
                        _2171 = _1611;
                    }
                    else
                    {
                        _2172 = distance(_249.lights[_2072].position.xyz, fragPosition.xyz);
                        _2171 = _249.lights[_2072].position.xyz - fragPosition.xyz;
                    }
                    _2174 = 1.0 / (1.0 + (_249.lights[_2072].attenuation * _2172));
                    _2173 = normalize(_2171);
                }
                else
                {
                    _2174 = 1.0;
                    _2173 = normalize(_249.lights[_2072].position.xyz);
                }
                vec3 _1499 = normalize(_2173 + _453);
                float _1503 = clamp(dot(_2020, _2173), 0.0, 1.0);
                float _1672 = _1488 * _1488;
                float _1675 = _1672 * _1672;
                float _1679 = clamp(dot(_2020, _1499), 0.0, 1.0);
                float _1683 = clamp(dot(_2020, _453), 0.0, 1.0);
                float _1690 = ((_1679 * _1679) * (_1675 - 1.0)) + 1.00010001659393310546875;
                vec3 _1752 = vec3(1.0) - _2043.xyz;
                vec3 _1704 = mix(_2043.xyz, _2043.xyz + (_1752 * pow(1.0 - clamp(dot(_453, _1499), 0.0, 1.0), 5.0)), vec3(_2050));
                float _1706 = _1488 + 1.0;
                float _1710 = (_1706 * _1706) * 0.125;
                float _1766 = 1.0 - _1710;
                _1537 = _2073 + (((_249.lights[_2072].diffuse_color * (((((_1704 * (_1675 / ((3.141590118408203125 * _1690) * _1690))) * ((_1503 / ((_1503 * _1766) + _1710)) * (_1683 / ((_1683 * _1766) + _1710)))) / vec3(((4.0 * _1683) * _1503) + 9.9999997473787516355514526367188e-05)) + ((((vec3(1.0) - _1704) * _1752) * _2046.xyz) * vec3(0.31831014156341552734375))) * _1503)) * _2174) * 1.0);
            }
            vec3 _1546 = (_2046.xyz * (_249.ambientFactor * _1996.x)) + _2073;
            vec4 _1895 = _2046;
            _1895.x = _1546.x;
            vec4 _1897 = _1895;
            _1897.y = _1546.y;
            vec4 _1899 = _1897;
            _1899.z = _1546.z;
            _2127 = _1899;
        }
        else
        {
            vec4 _2128;
            if (_769)
            {
                vec3 _1069 = _2046.xyz + (_2043.xyz * pow(1.0 - clamp(dot(_453, _2020), 0.0, 1.0), 5.0 * clamp(_2035, 0.00999999977648258209228515625, 1.0)));
                vec4 _1901 = _2046;
                _1901.x = _1069.x;
                vec4 _1903 = _1901;
                _1903.y = _1069.y;
                vec4 _1905 = _1903;
                _1905.z = _1069.z;
                _2128 = _1905;
            }
            else
            {
                _2128 = _2046;
            }
            _2127 = _2128;
        }
        _2126 = _2127;
    }
    else
    {
        _2126 = _2046;
    }
    vec4 _2122;
    if ((_249.flags & 16) != 0)
    {
        vec4 _1093 = texture(materialTextures[1], vec3(fragTexCoord.xy, float(_249.sGlowmapIndex)));
        vec3 _1094 = _1093.xyz;
        vec3 _2092;
        if (_862)
        {
            vec3 _2093;
            if ((_249.flags & 512) != 0)
            {
                vec3 _2091;
                if (_249.team_glow_enabled != 0)
                {
                    _2091 = mix(max(_2076, vec3(0.0)), _1094, vec3(clamp((dot(_1094, vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625)) - _2084.z) - _2084.w, 0.0, 1.0)));
                }
                else
                {
                    _2091 = _1094;
                }
                _2093 = _2091;
            }
            else
            {
                _2093 = _1094;
            }
            _2092 = _2093;
        }
        else
        {
            _2092 = _1094;
        }
        vec3 _2094;
        if ((_249.flags & 4) != 0)
        {
            _2094 = pow(_2092, vec3(2.2000000476837158203125)) * 3.0;
        }
        else
        {
            _2094 = _2092;
        }
        vec3 _1150 = _2094 * 1.5;
        vec4 _1909 = _2206;
        _1909.x = _1150.x;
        vec4 _1911 = _1909;
        _1911.y = _1150.y;
        vec4 _1913 = _1911;
        _1913.z = _1150.z;
        _2122 = _1913;
    }
    else
    {
        _2122 = vec4(0.0, 0.0, 0.0, 1.0);
    }
    vec4 _2136;
    if ((_249.flags & 16384) != 0)
    {
        _2136 = _2122 * _249.alphaMult;
    }
    else
    {
        _2136 = _2122;
    }
    vec4 _2155;
    vec4 _2159;
    if ((_249.flags & 1024) != 0)
    {
        vec3 _2133;
        if ((_249.flags & 4) != 0)
        {
            _2133 = pow(_249.fogColor.xyz, vec3(2.2000000476837158203125));
        }
        else
        {
            _2133 = _249.fogColor.xyz;
        }
        vec3 _2140;
        if (_644)
        {
            vec3 _2141;
            if (_249.blend_alpha == 1)
            {
                _2141 = _2133 * _2126.w;
            }
            else
            {
                _2141 = _2133;
            }
            _2140 = _2141;
        }
        else
        {
            _2140 = _2133;
        }
        vec3 _1215 = mix(_2136.xyz + _2126.xyz, _2140, vec3(fragFogDist));
        vec4 _1916 = _2126;
        _1916.x = _1215.x;
        vec4 _1918 = _1916;
        _1918.y = _1215.y;
        vec4 _1920 = _1918;
        _1920.z = _1215.z;
        vec4 _1922 = _2206;
        _1922.x = 0.0;
        vec4 _1924 = _1922;
        _1924.y = 0.0;
        vec4 _1926 = _1924;
        _1926.z = 0.0;
        _2159 = _1926;
        _2155 = _1920;
    }
    else
    {
        _2159 = _2136;
        _2155 = _2126;
    }
    vec4 _2156;
    if (_644)
    {
        vec4 _2157;
        if (_249.desaturate == 1)
        {
            vec3 _1258 = (_249.color.xyz * dot(vec3(1.0), _2155.xyz)) * 0.333333313465118408203125;
            vec4 _1934 = _2155;
            _1934.x = _1258.x;
            vec4 _1936 = _1934;
            _1936.y = _1258.y;
            vec4 _1938 = _1936;
            _1938.z = _1258.z;
            _2157 = _1938;
        }
        else
        {
            _2157 = _2155;
        }
        _2156 = _2157;
    }
    else
    {
        _2156 = _2155;
    }
    vec4 _2161;
    vec4 _2165;
    if (_249.effect_num == 0)
    {
        float _1277 = fract(abs(fragTexCoord.x)) - _249.anim_timer;
        float _1284 = 1000.0 / (1.0 + pow(abs(_1277 * 1000.0), 2.0));
        vec3 _1289 = _2159.xyz + vec3(_1284);
        vec4 _1941 = _2206;
        _1941.x = _1289.x;
        vec4 _1943 = _1941;
        _1943.y = _1289.y;
        vec4 _1945 = _1943;
        _1945.z = _1289.z;
        vec4 _1949 = _2156;
        _1949.w = _2156.w * clamp((_1284 * _1277) * (-10000.0), 0.0, 1.0);
        _2165 = _1949;
        _2161 = _1945;
    }
    else
    {
        vec4 _2162;
        vec4 _2166;
        if (_249.effect_num == 1)
        {
            float _1323 = fragPosition.y - _249.anim_timer;
            vec3 _1332 = _2159.xyz + vec3(1.0 / (1.0 + pow(abs(_1323), 2.0)));
            vec4 _1951 = _2206;
            _1951.x = _1332.x;
            vec4 _1953 = _1951;
            _1953.y = _1332.y;
            vec4 _1955 = _1953;
            _1955.z = _1332.z;
            vec4 _2167;
            if ((_249.flags & 1) == 0)
            {
                vec4 _1957 = _2156;
                _1957.w = clamp(_1323 * 10000.0, 0.0, 1.0);
                _2167 = _1957;
            }
            else
            {
                _2167 = _2156;
            }
            _2166 = _2167;
            _2162 = _1955;
        }
        else
        {
            vec4 _2168;
            if (_249.effect_num == 2)
            {
                vec4 _1960 = _2156;
                _1960.w = _2156.w;
                _2168 = _1960;
            }
            else
            {
                _2168 = _2156;
            }
            _2166 = _2168;
            _2162 = _2159;
        }
        _2165 = _2166;
        _2161 = _2162;
    }
    vec4 _2169;
    if (_1013)
    {
        vec3 _1407 = _2165.xyz + _2161.xyz;
        vec4 _1962 = _2165;
        _1962.x = _1407.x;
        vec4 _1964 = _1962;
        _1964.y = _1407.y;
        vec4 _1966 = _1964;
        _1966.z = _1407.z;
        _2169 = _1966;
    }
    else
    {
        _2169 = _2165;
    }
    fragOut0 = _2169;
}


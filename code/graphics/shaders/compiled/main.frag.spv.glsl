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
out vec4 fragOut1;
out vec4 fragOut2;
out vec4 fragOut3;
out vec4 fragOut4;

vec3 _2343;
vec4 _2344;

void main()
{
    vec3 _453 = normalize(-fragPosition.xyz);
    vec3 _500 = normalize(fragNormal);
    vec2 _2029;
    if ((_249.flags & 128) != 0)
    {
        _2029 = texture(materialTextures[5], vec3(fragTexCoord.xy, float(_249.sAmbientmapIndex))).xy;
    }
    else
    {
        _2029 = vec2(1.0);
    }
    vec3 _2053;
    if ((_249.flags & 64) != 0)
    {
        vec4 _549 = texture(materialTextures[3], vec3(fragTexCoord.xy, float(_249.sNormalmapIndex)));
        vec2 _554 = (_549.wy * 2.0) - vec2(1.0);
        float _557 = _554.x;
        vec3 _1822 = _2343;
        _1822.x = _557;
        vec3 _1824 = _1822;
        _1824.y = _554.y;
        vec3 _573 = mat3(fragTangent, fragBitangent, fragTangentNormal) * vec3(_557, _554.y, clamp(sqrt(1.0 - dot(_1824.xy, _1824.xy)), 9.9999997473787516355514526367188e-05, 1.0));
        float _576 = length(_573);
        vec3 _2054;
        if (_576 > 0.0)
        {
            _2054 = _573 / vec3(_576);
        }
        else
        {
            _2054 = _500;
        }
        _2053 = _2054;
    }
    else
    {
        _2053 = _500;
    }
    vec2 _2001;
    if (_249.effect_num >= 0)
    {
        float _609 = ((fragPosition.x * fragPosition.w) * 0.004999999888241291046142578125) + (_249.anim_timer * 20.0);
        float _616 = (fragPosition.y * fragPosition.w) * 0.004999999888241291046142578125;
        _2001 = vec2(cos(_609) * sin(_616), sin(_609) * cos(_616)) * 0.02999999932944774627685546875;
    }
    else
    {
        _2001 = vec2(0.0);
    }
    bool _644 = (_249.flags & 8) != 0;
    vec4 _2016;
    if (_644)
    {
        vec2 _2002;
        if (_249.effect_num == 2)
        {
            _2002 = fragTexCoord.xy + (_2001 * (1.0 - _249.anim_timer));
        }
        else
        {
            _2002 = fragTexCoord.xy;
        }
        vec4 _671 = texture(materialTextures[0], vec3(_2002, float(_249.sBasemapIndex)));
        vec4 _2003;
        if ((_249.flags & 4) != 0)
        {
            vec3 _1453 = pow(_671.xyz, vec3(2.2000000476837158203125));
            vec4 _1828 = _671;
            _1828.x = _1453.x;
            vec4 _1830 = _1828;
            _1830.y = _1453.y;
            vec4 _1832 = _1830;
            _1832.z = _1453.z;
            _2003 = _1832;
        }
        else
        {
            _2003 = _671;
        }
        vec4 _2004;
        if ((_249.flags & 16384) != 0)
        {
            vec4 _1835 = _2003;
            _1835.w = _2003.w * _249.alphaMult;
            _2004 = _1835;
        }
        else
        {
            _2004 = _2003;
        }
        bool _705 = _249.blend_alpha == 0;
        bool _712;
        if (_705)
        {
            _712 = _2004.w < 0.949999988079071044921875;
        }
        else
        {
            _712 = _705;
        }
        if (_712)
        {
            discard;
        }
        vec4 _2021;
        if (_249.blend_alpha == 1)
        {
            vec3 _725 = _2004.xyz * _2004.w;
            vec4 _1839 = _2004;
            _1839.x = _725.x;
            vec4 _1841 = _1839;
            _1841.y = _725.y;
            vec4 _1843 = _1841;
            _1843.z = _725.z;
            _2021 = _1843;
        }
        else
        {
            _2021 = _2004;
        }
        _2016 = _2021;
    }
    else
    {
        _2016 = _249.color;
    }
    vec2 _734 = _500.xy;
    vec2 _735 = dFdx(_734);
    vec2 _739 = dFdy(_734);
    float _754 = min(_249.defaultGloss, 1.0 - pow(clamp(max(dot(_735, _735), dot(_739, _739)), 0.0, 1.0), 0.3300000131130218505859375));
    bool _769 = (_249.flags & 32) != 0;
    vec4 _2045;
    float _2068;
    float _2083;
    if (_769)
    {
        vec4 _782 = texture(materialTextures[2], vec3(fragTexCoord.xy, float(_249.sSpecmapIndex)));
        vec4 _2022;
        if ((_249.flags & 16384) != 0)
        {
            _2022 = _782 * _249.alphaMult;
        }
        else
        {
            _2022 = _782;
        }
        float _2071;
        if (_249.alphaGloss != 0)
        {
            _2071 = _2022.w;
        }
        else
        {
            _2071 = _754;
        }
        bool _804 = _249.gammaSpec != 0;
        vec4 _2024;
        if (_804)
        {
            vec3 _810 = max(_2022.xyz, vec3(0.02999999932944774627685546875));
            vec4 _1846 = _2344;
            _1846.x = _810.x;
            vec4 _1848 = _1846;
            _1848.y = _810.y;
            vec4 _1850 = _1848;
            _1850.z = _810.z;
            _2024 = _1850;
        }
        else
        {
            _2024 = _2022;
        }
        vec4 _2046;
        if ((_249.flags & 4) != 0)
        {
            vec3 _1457 = pow(_2024.xyz, vec3(2.2000000476837158203125));
            vec4 _1852 = _2344;
            _1852.x = _1457.x;
            vec4 _1854 = _1852;
            _1854.y = _1457.y;
            vec4 _1856 = _1854;
            _1856.z = _1457.z;
            _2046 = _1856;
        }
        else
        {
            _2046 = _2024;
        }
        _2083 = float(_804);
        _2068 = _2071;
        _2045 = _2046;
    }
    else
    {
        _2083 = 0.0;
        _2068 = _754;
        _2045 = vec4(_2016.xyz * 0.100000001490116119384765625, _754);
    }
    vec3 _837 = _2016.xyz * _2029.y;
    vec4 _1859 = _2016;
    _1859.x = _837.x;
    vec4 _1861 = _1859;
    _1861.y = _837.y;
    vec4 _1863 = _1861;
    _1863.z = _837.z;
    vec3 _848 = _2045.xyz * _2029.y;
    vec4 _1866 = _2344;
    _1866.x = _848.x;
    vec4 _1868 = _1866;
    _1868.y = _848.y;
    vec4 _1870 = _1868;
    _1870.z = _848.z;
    bool _862 = (_249.flags & 256) != 0;
    vec4 _2076;
    vec4 _2079;
    vec3 _2109;
    vec4 _2117;
    if (_862)
    {
        vec4 _2077;
        vec4 _2080;
        vec3 _2110;
        vec4 _2118;
        if ((_249.flags & 512) != 0)
        {
            vec4 _883 = texture(materialTextures[6], vec3(fragTexCoord.xy, float(_249.sMiscmapIndex)));
            float _888 = _883.x;
            float _890 = _883.y;
            vec3 _908 = ((_249.base_color * _888) + (_249.stripe_color * _890)) + (vec3(-0.5) * (_888 + _890));
            bool _923 = (_249.flags & 4) != 0;
            vec4 _2047;
            vec4 _2048;
            if (_923)
            {
                vec3 _1461 = pow(_1863.xyz, vec3(0.4545454680919647216796875));
                vec4 _1878 = _1863;
                _1878.x = _1461.x;
                vec4 _1880 = _1878;
                _1880.y = _1461.y;
                vec4 _1882 = _1880;
                _1882.z = _1461.z;
                vec3 _1465 = pow(_1870.xyz, vec3(0.4545454680919647216796875));
                vec4 _1884 = _2344;
                _1884.x = _1465.x;
                vec4 _1886 = _1884;
                _1886.y = _1465.y;
                vec4 _1888 = _1886;
                _1888.z = _1465.z;
                _2048 = _1888;
                _2047 = _1882;
            }
            else
            {
                _2048 = _1870;
                _2047 = _1863;
            }
            vec3 _949 = _2047.xyz + _908;
            vec4 _1890 = _2047;
            _1890.x = _949.x;
            vec4 _1892 = _1890;
            _1892.y = _949.y;
            vec4 _1894 = _1892;
            _1894.z = _949.z;
            vec3 _958 = max(_1894.xyz, vec3(0.0));
            vec4 _1896 = _1894;
            _1896.x = _958.x;
            vec4 _1898 = _1896;
            _1898.y = _958.y;
            vec4 _1900 = _1898;
            _1900.z = _958.z;
            vec3 _968 = _2048.xyz + _908;
            vec4 _1902 = _2344;
            _1902.x = _968.x;
            vec4 _1904 = _1902;
            _1904.y = _968.y;
            vec4 _1906 = _1904;
            _1906.z = _968.z;
            vec3 _977 = max(_1906.xyz, vec3(0.02999999932944774627685546875));
            vec4 _1908 = _2344;
            _1908.x = _977.x;
            vec4 _1910 = _1908;
            _1910.y = _977.y;
            vec4 _1912 = _1910;
            _1912.z = _977.z;
            vec4 _2078;
            vec4 _2081;
            if (_923)
            {
                vec3 _1469 = pow(_1900.xyz, vec3(2.2000000476837158203125));
                vec4 _1914 = _1900;
                _1914.x = _1469.x;
                vec4 _1916 = _1914;
                _1916.y = _1469.y;
                vec4 _1918 = _1916;
                _1918.z = _1469.z;
                vec3 _1473 = pow(_1912.xyz, vec3(2.2000000476837158203125));
                vec4 _1920 = _2344;
                _1920.x = _1473.x;
                vec4 _1922 = _1920;
                _1922.y = _1473.y;
                vec4 _1924 = _1922;
                _1924.z = _1473.z;
                _2081 = _1918;
                _2078 = _1924;
            }
            else
            {
                _2081 = _1900;
                _2078 = _1912;
            }
            _2118 = _883;
            _2110 = (_249.base_color * _883.z) + (_249.stripe_color * _883.w);
            _2080 = _2081;
            _2077 = _2078;
        }
        else
        {
            _2118 = vec4(0.0);
            _2110 = vec3(0.0);
            _2080 = _1863;
            _2077 = _1870;
        }
        _2117 = _2118;
        _2109 = _2110;
        _2079 = _2080;
        _2076 = _2077;
    }
    else
    {
        _2117 = vec4(0.0);
        _2109 = vec3(0.0);
        _2079 = _1863;
        _2076 = _1870;
    }
    int _1012 = _249.flags & 2;
    bool _1013 = _1012 == 0;
    vec4 _2159;
    if (_1013)
    {
        vec4 _2160;
        if ((_249.flags & 1) != 0)
        {
            vec3 _2106;
            _2106 = vec3(0.0);
            vec3 _1569;
            for (int _2105 = 0; _2105 < _249.n_lights; _2106 = _1569, _2105++)
            {
                float _1520 = clamp(1.0 - _2068, 0.0, 1.0);
                vec3 _2299;
                float _2300;
                if (_249.lights[_2105].light_type != 0)
                {
                    vec3 _2297;
                    float _2298;
                    if (_249.lights[_2105].light_type == 2)
                    {
                        vec3 _1621 = normalize(_249.lights[_2105].direction);
                        vec3 _1643 = (_249.lights[_2105].position.xyz - (_1621 * abs(dot(fragPosition.xyz - _249.lights[_2105].position.xyz, _1621)))) - fragPosition.xyz;
                        _2298 = length(_1643);
                        _2297 = _1643;
                    }
                    else
                    {
                        _2298 = distance(_249.lights[_2105].position.xyz, fragPosition.xyz);
                        _2297 = _249.lights[_2105].position.xyz - fragPosition.xyz;
                    }
                    _2300 = 1.0 / (1.0 + (_249.lights[_2105].attenuation * _2298));
                    _2299 = normalize(_2297);
                }
                else
                {
                    _2300 = 1.0;
                    _2299 = normalize(_249.lights[_2105].position.xyz);
                }
                vec3 _1531 = normalize(_2299 + _453);
                float _1535 = clamp(dot(_2053, _2299), 0.0, 1.0);
                float _1704 = _1520 * _1520;
                float _1707 = _1704 * _1704;
                float _1711 = clamp(dot(_2053, _1531), 0.0, 1.0);
                float _1715 = clamp(dot(_2053, _453), 0.0, 1.0);
                float _1722 = ((_1711 * _1711) * (_1707 - 1.0)) + 1.00010001659393310546875;
                vec3 _1784 = vec3(1.0) - _2076.xyz;
                vec3 _1736 = mix(_2076.xyz, _2076.xyz + (_1784 * pow(1.0 - clamp(dot(_453, _1531), 0.0, 1.0), 5.0)), vec3(_2083));
                float _1738 = _1520 + 1.0;
                float _1742 = (_1738 * _1738) * 0.125;
                float _1798 = 1.0 - _1742;
                _1569 = _2106 + (((_249.lights[_2105].diffuse_color * (((((_1736 * (_1707 / ((3.141590118408203125 * _1722) * _1722))) * ((_1535 / ((_1535 * _1798) + _1742)) * (_1715 / ((_1715 * _1798) + _1742)))) / vec3(((4.0 * _1715) * _1535) + 9.9999997473787516355514526367188e-05)) + ((((vec3(1.0) - _1736) * _1784) * _2079.xyz) * vec3(0.31831014156341552734375))) * _1535)) * _2300) * 1.0);
            }
            vec3 _1578 = (_2079.xyz * (_249.ambientFactor * _2029.x)) + _2106;
            vec4 _1927 = _2079;
            _1927.x = _1578.x;
            vec4 _1929 = _1927;
            _1929.y = _1578.y;
            vec4 _1931 = _1929;
            _1931.z = _1578.z;
            _2160 = _1931;
        }
        else
        {
            vec4 _2161;
            if (_769)
            {
                vec3 _1069 = _2079.xyz + (_2076.xyz * pow(1.0 - clamp(dot(_453, _2053), 0.0, 1.0), 5.0 * clamp(_2068, 0.00999999977648258209228515625, 1.0)));
                vec4 _1933 = _2079;
                _1933.x = _1069.x;
                vec4 _1935 = _1933;
                _1935.y = _1069.y;
                vec4 _1937 = _1935;
                _1937.z = _1069.z;
                _2161 = _1937;
            }
            else
            {
                _2161 = _2079;
            }
            _2160 = _2161;
        }
        _2159 = _2160;
    }
    else
    {
        _2159 = _2079;
    }
    vec4 _2155;
    if ((_249.flags & 16) != 0)
    {
        vec4 _1093 = texture(materialTextures[1], vec3(fragTexCoord.xy, float(_249.sGlowmapIndex)));
        vec3 _1094 = _1093.xyz;
        vec3 _2125;
        if (_862)
        {
            vec3 _2126;
            if ((_249.flags & 512) != 0)
            {
                vec3 _2124;
                if (_249.team_glow_enabled != 0)
                {
                    _2124 = mix(max(_2109, vec3(0.0)), _1094, vec3(clamp((dot(_1094, vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625)) - _2117.z) - _2117.w, 0.0, 1.0)));
                }
                else
                {
                    _2124 = _1094;
                }
                _2126 = _2124;
            }
            else
            {
                _2126 = _1094;
            }
            _2125 = _2126;
        }
        else
        {
            _2125 = _1094;
        }
        vec3 _2127;
        if ((_249.flags & 4) != 0)
        {
            _2127 = pow(_2125, vec3(2.2000000476837158203125)) * 3.0;
        }
        else
        {
            _2127 = _2125;
        }
        vec3 _1150 = _2127 * 1.5;
        vec4 _1941 = vec4(0.0, 0.0, 0.0, 1.0);
        _1941.x = _1150.x;
        vec4 _1943 = _1941;
        _1943.y = _1150.y;
        vec4 _1945 = _1943;
        _1945.z = _1150.z;
        _2155 = _1945;
    }
    else
    {
        _2155 = vec4(0.0, 0.0, 0.0, 1.0);
    }
    vec4 _2169;
    if ((_249.flags & 16384) != 0)
    {
        _2169 = _2155 * _249.alphaMult;
    }
    else
    {
        _2169 = _2155;
    }
    vec4 _2188;
    vec4 _2192;
    vec4 _2269;
    if ((_249.flags & 1024) != 0)
    {
        vec3 _2166;
        if ((_249.flags & 4) != 0)
        {
            _2166 = pow(_249.fogColor.xyz, vec3(2.2000000476837158203125));
        }
        else
        {
            _2166 = _249.fogColor.xyz;
        }
        vec3 _2173;
        if (_644)
        {
            vec3 _2174;
            if (_249.blend_alpha == 1)
            {
                _2174 = _2166 * _2159.w;
            }
            else
            {
                _2174 = _2166;
            }
            _2173 = _2174;
        }
        else
        {
            _2173 = _2166;
        }
        vec3 _1215 = mix(_2169.xyz + _2159.xyz, _2173, vec3(fragFogDist));
        vec4 _1948 = _2159;
        _1948.x = _1215.x;
        vec4 _1950 = _1948;
        _1950.y = _1215.y;
        vec4 _1952 = _1950;
        _1952.z = _1215.z;
        vec4 _1954 = _2169;
        _1954.x = 0.0;
        vec4 _1956 = _1954;
        _1956.y = 0.0;
        vec4 _1958 = _1956;
        _1958.z = 0.0;
        vec3 _1231 = _2076.xyz * fragFogDist;
        vec4 _1960 = _2344;
        _1960.x = _1231.x;
        vec4 _1962 = _1960;
        _1962.y = _1231.y;
        vec4 _1964 = _1962;
        _1964.z = _1231.z;
        _2269 = _1964;
        _2192 = _1958;
        _2188 = _1952;
    }
    else
    {
        _2269 = _2076;
        _2192 = _2169;
        _2188 = _2159;
    }
    vec4 _2189;
    if (_644)
    {
        vec4 _2190;
        if (_249.desaturate == 1)
        {
            vec3 _1258 = (_249.color.xyz * dot(vec3(1.0), _2188.xyz)) * 0.333333313465118408203125;
            vec4 _1966 = _2188;
            _1966.x = _1258.x;
            vec4 _1968 = _1966;
            _1968.y = _1258.y;
            vec4 _1970 = _1968;
            _1970.z = _1258.z;
            _2190 = _1970;
        }
        else
        {
            _2190 = _2188;
        }
        _2189 = _2190;
    }
    else
    {
        _2189 = _2188;
    }
    vec4 _2194;
    vec4 _2198;
    if (_249.effect_num == 0)
    {
        float _1277 = fract(abs(fragTexCoord.x)) - _249.anim_timer;
        float _1284 = 1000.0 / (1.0 + pow(abs(_1277 * 1000.0), 2.0));
        vec3 _1289 = _2192.xyz + vec3(_1284);
        vec4 _1973 = _2192;
        _1973.x = _1289.x;
        vec4 _1975 = _1973;
        _1975.y = _1289.y;
        vec4 _1977 = _1975;
        _1977.z = _1289.z;
        vec4 _1981 = _2189;
        _1981.w = _2189.w * clamp((_1284 * _1277) * (-10000.0), 0.0, 1.0);
        _2198 = _1981;
        _2194 = _1977;
    }
    else
    {
        vec4 _2195;
        vec4 _2199;
        if (_249.effect_num == 1)
        {
            float _1323 = fragPosition.y - _249.anim_timer;
            vec3 _1332 = _2192.xyz + vec3(1.0 / (1.0 + pow(abs(_1323), 2.0)));
            vec4 _1983 = _2192;
            _1983.x = _1332.x;
            vec4 _1985 = _1983;
            _1985.y = _1332.y;
            vec4 _1987 = _1985;
            _1987.z = _1332.z;
            vec4 _2200;
            if ((_249.flags & 1) == 0)
            {
                vec4 _1989 = _2189;
                _1989.w = clamp(_1323 * 10000.0, 0.0, 1.0);
                _2200 = _1989;
            }
            else
            {
                _2200 = _2189;
            }
            _2199 = _2200;
            _2195 = _1987;
        }
        else
        {
            vec4 _2201;
            if (_249.effect_num == 2)
            {
                vec4 _1992 = _2189;
                _1992.w = _2189.w;
                _2201 = _1992;
            }
            else
            {
                _2201 = _2189;
            }
            _2199 = _2201;
            _2195 = _2192;
        }
        _2198 = _2199;
        _2194 = _2195;
    }
    vec4 _2202;
    if (_1013)
    {
        vec3 _1407 = _2198.xyz + _2194.xyz;
        vec4 _1994 = _2198;
        _1994.x = _1407.x;
        vec4 _1996 = _1994;
        _1996.y = _1407.y;
        vec4 _1998 = _1996;
        _1998.z = _1407.z;
        _2202 = _1998;
    }
    else
    {
        _2202 = _2198;
    }
    fragOut0 = _2202;
    if (_1012 != 0)
    {
        fragOut1 = vec4(fragPosition.xyz, _2029.x);
        fragOut2 = vec4(_2053, _2068);
        fragOut3 = vec4(_2269.xyz, _2083);
        fragOut4 = _2194;
    }
}


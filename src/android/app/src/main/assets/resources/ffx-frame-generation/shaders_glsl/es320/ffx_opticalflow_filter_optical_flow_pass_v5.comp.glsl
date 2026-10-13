#version 320 es
layout(local_size_x = 16, local_size_y = 4, local_size_z = 1) in;

layout(binding = 0, r32i) uniform writeonly highp iimage2D rw_optical_flow;
uniform highp isampler2D SPIRV_Cross_Combinedr_optical_flow_previousSPIRV_Cross_DummySampler;

void main()
{
    int _40;
    _40 = -1;
    ivec2 _36[9];
    int _44;
    for (int _43 = 0; _40 < 2; _40++, _43 = _44)
    {
        _44 = _43;
        for (int _51 = -1; _51 < 2; )
        {
            ivec4 _59 = texelFetch(SPIRV_Cross_Combinedr_optical_flow_previousSPIRV_Cross_DummySampler, ivec2(uvec2(ivec3(gl_GlobalInvocationID).xy + ivec2(_40, _51))), int(0u));
            int _60 = _59.x;
            _36[_44] = ivec2((_60 << 16) >> 16, _60 >> 16);
            _44++;
            _51++;
            continue;
        }
    }
    uint _67;
    int _70;
    _67 = 4294967295u;
    _70 = 0;
    uint _68;
    for (; _70 < 9; _67 = _68, _70++)
    {
        uint _76;
        _76 = 0u;
        for (int _79 = 0; _79 < 9; )
        {
            ivec2 _87 = _36[_70] - _36[_79];
            int _88 = _87.x;
            int _91 = _87.y;
            _76 = uint(_88 * _88) + (uint(_91 * _91) + _76);
            _79++;
            continue;
        }
        _68 = min(((_76 << 4u) | uint(_70)), _67);
    }
    uint _98 = _67 & 15u;
    imageStore(rw_optical_flow, ivec2(uvec2(ivec3(gl_GlobalInvocationID).xy)), ivec4((_36[_98].x & 65535) | (_36[_98].y << 16)));
}


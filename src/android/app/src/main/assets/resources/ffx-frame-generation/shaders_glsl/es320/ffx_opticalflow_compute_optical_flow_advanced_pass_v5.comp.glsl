#version 320 es
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

ivec2 _100;

layout(binding = 0, std140) uniform type_cbOF
{
    ivec2 iInputLumaResolution;
    uint uOpticalFlowPyramidLevel;
    uint uOpticalFlowPyramidLevelCount;
    uint iFrameIndex;
    uint backbufferTransferFunction;
    vec2 minMaxLuminance;
} cbOF;

layout(binding = 0, r32i) uniform highp iimage2D rw_optical_flow;
layout(binding = 1, r32ui) uniform readonly highp uimage2D rw_optical_flow_scd_output;
uniform highp usampler2D SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler;
uniform highp usampler2D SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler;

shared uint pixels[8][2];
shared uint searchBuffer[1][144];
shared uint sadMapBuffer[4][16][5];
shared uint sRed[64];

void main()
{
    do
    {
        uint _108 = uint(int(gl_LocalInvocationIndex));
        int _110 = int(_108 & 3u);
        int _113 = int((_108 >> 2u) & 15u);
        int _121 = int(((_108 >> 1u) & 1u) | (((_108 >> 5u) & 1u) << 1u));
        ivec2 _122 = ivec3(gl_WorkGroupID).xy << ivec2(4);
        ivec2 _124 = _122 + (ivec2(_110, _113) * ivec2(4, 1));
        bool _139;
        do
        {
            if (cbOF.iFrameIndex <= 5u)
            {
                _139 = true;
                break;
            }
            else
            {
                _139 = (imageLoad(rw_optical_flow_scd_output, ivec2(uvec2(1u, 0u))).x & 15u) != 0u;
                break;
            }
            break; // unreachable workaround
        } while(false);
        if (_139)
        {
            bool _148;
            if ((_113 & 7) == 0)
            {
                _148 = (_110 & 1) == 0;
            }
            else
            {
                _148 = false;
            }
            if (_148)
            {
                imageStore(rw_optical_flow, ivec2(uvec2(_124 >> ivec2(3))), ivec4(0));
            }
            break;
        }
        bool _158 = cbOF.uOpticalFlowPyramidLevel != (cbOF.uOpticalFlowPyramidLevelCount - 1u);
        int _163 = int(cbOF.uOpticalFlowPyramidLevel) & 31;
        int _164 = cbOF.iInputLumaResolution.x >> _163;
        int _167 = _164 - 4;
        int _168 = _124.x;
        int _171 = (cbOF.iInputLumaResolution.y >> _163) - 1;
        ivec2 _175 = ivec2(max(0, min(_167, _168)), max(0, min(_171, _124.y)));
        uvec2 _176 = uvec2(_175);
        uvec2 _181 = uvec2(_175 + ivec2(1, 0));
        uvec2 _186 = uvec2(_175 + ivec2(2, 0));
        uvec2 _191 = uvec2(_175 + ivec2(3, 0));
        uint _200 = ((texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(_176), int(0u)).x | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(_181), int(0u)).x << 8u)) | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(_186), int(0u)).x << 16u)) | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(_191), int(0u)).x << 24u);
        bool _201 = _168 < 0;
        uint _250;
        if (_201)
        {
            uint _231 = _200 & 255u;
            uint _237;
            if (_168 <= (-1))
            {
                _237 = (_200 << 8u) | _231;
            }
            else
            {
                _237 = _200;
            }
            uint _243;
            if (_168 <= (-2))
            {
                _243 = (_237 << 8u) | _231;
            }
            else
            {
                _243 = _237;
            }
            uint _249;
            if (_168 <= (-3))
            {
                _249 = (_243 << 8u) | _231;
            }
            else
            {
                _249 = _243;
            }
            _250 = _249;
        }
        else
        {
            uint _230;
            if (_168 > _167)
            {
                uint _208 = _200 & 4278190080u;
                uint _215;
                if (_168 >= (_164 - 3))
                {
                    _215 = (_200 >> 8u) | _208;
                }
                else
                {
                    _215 = _200;
                }
                uint _222;
                if (_168 >= (_164 - 2))
                {
                    _222 = (_215 >> 8u) | _208;
                }
                else
                {
                    _222 = _215;
                }
                uint _229;
                if (_168 >= (_164 - 1))
                {
                    _229 = (_222 >> 8u) | _208;
                }
                else
                {
                    _229 = _222;
                }
                _230 = _229;
            }
            else
            {
                _230 = _200;
            }
            _250 = _230;
        }
        uint _268 = ((texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(_176), int(0u)).x | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(_181), int(0u)).x << 8u)) | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(_186), int(0u)).x << 16u)) | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(_191), int(0u)).x << 24u);
        uint _317;
        if (_201)
        {
            uint _298 = _268 & 255u;
            uint _304;
            if (_168 <= (-1))
            {
                _304 = (_268 << 8u) | _298;
            }
            else
            {
                _304 = _268;
            }
            uint _310;
            if (_168 <= (-2))
            {
                _310 = (_304 << 8u) | _298;
            }
            else
            {
                _310 = _304;
            }
            uint _316;
            if (_168 <= (-3))
            {
                _316 = (_310 << 8u) | _298;
            }
            else
            {
                _316 = _310;
            }
            _317 = _316;
        }
        else
        {
            uint _297;
            if (_168 > _167)
            {
                uint _275 = _268 & 4278190080u;
                uint _282;
                if (_168 >= (_164 - 3))
                {
                    _282 = (_268 >> 8u) | _275;
                }
                else
                {
                    _282 = _268;
                }
                uint _289;
                if (_168 >= (_164 - 2))
                {
                    _289 = (_282 >> 8u) | _275;
                }
                else
                {
                    _289 = _282;
                }
                uint _296;
                if (_168 >= (_164 - 1))
                {
                    _296 = (_289 >> 8u) | _275;
                }
                else
                {
                    _296 = _289;
                }
                _297 = _296;
            }
            else
            {
                _297 = _268;
            }
            _317 = _297;
        }
        uint _318 = _250 & 255u;
        uint _320 = _317 & 255u;
        uint _324 = _250 >> 8u;
        uint _327 = _317 >> 8u;
        uint _333 = _250 >> 16u;
        uint _336 = _317 >> 16u;
        uint _342 = _250 >> 24u;
        uint _345 = _317 >> 24u;
        uint _351 = uint(((abs(int(_318) - int(_320)) + abs(int(_324 & 255u) - int(_327 & 255u))) + abs(int(_333 & 255u) - int(_336 & 255u))) + abs(int(_342 & 255u) - int(_345 & 255u)));
        ivec2 _352 = ivec3(gl_WorkGroupID).xy << ivec2(1);
        ivec2 _353;
        _353.y = 0;
        ivec2 _355;
        _355 = _353;
        ivec2 _356;
        ivec2 _364;
        for (; _355.y < 2; _356 = _364, _356.y = _364.y + 1, _355 = _356)
        {
            ivec2 _362 = _355;
            _362.x = 0;
            _364 = _362;
            ivec2 _365;
            int _367;
            for (;;)
            {
                _367 = _364.x;
                if (_367 < 2)
                {
                    ivec2 _371 = _352 + _364;
                    uvec2 _372 = uvec2(_371);
                    ivec4 _374 = imageLoad(rw_optical_flow, ivec2(_372));
                    int _375 = _374.x;
                    ivec2 _382 = mix(ivec2((_375 << 16) >> 16, _375 >> 16), ivec2(0), bvec2(!_158));
                    int _383 = _364.y;
                    int _384 = _383 * 2;
                    if (_121 == (_384 + _367))
                    {
                        pixels[_113 & 7][_110 & 1] = _250;
                    }
                    ivec2 _394 = (_122 + (_364 * ivec2(8))) + _382;
                    int _396 = _394.x - 8;
                    int _398 = _394.y - 8;
                    for (int _400 = int(gl_LocalInvocationIndex); _400 < 144; _400 += 64)
                    {
                        int _409 = _396 + ((_400 - 6 * (_400 / 6)) * 4);
                        ivec2 _415 = ivec2(max(0, min(_167, _409)), max(0, min(_171, (_398 + (_400 / 6)))));
                        uint _440 = ((texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_415)), int(0u)).x | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_415 + ivec2(1, 0))), int(0u)).x << 8u)) | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_415 + ivec2(2, 0))), int(0u)).x << 16u)) | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_415 + ivec2(3, 0))), int(0u)).x << 24u);
                        uint _490;
                        if (_409 < 0)
                        {
                            uint _471 = _440 & 255u;
                            uint _477;
                            if (_409 <= (-1))
                            {
                                _477 = (_440 << 8u) | _471;
                            }
                            else
                            {
                                _477 = _440;
                            }
                            uint _483;
                            if (_409 <= (-2))
                            {
                                _483 = (_477 << 8u) | _471;
                            }
                            else
                            {
                                _483 = _477;
                            }
                            uint _489;
                            if (_409 <= (-3))
                            {
                                _489 = (_483 << 8u) | _471;
                            }
                            else
                            {
                                _489 = _483;
                            }
                            _490 = _489;
                        }
                        else
                        {
                            uint _470;
                            if (_409 > _167)
                            {
                                uint _448 = _440 & 4278190080u;
                                uint _455;
                                if (_409 >= (_164 - 3))
                                {
                                    _455 = (_440 >> 8u) | _448;
                                }
                                else
                                {
                                    _455 = _440;
                                }
                                uint _462;
                                if (_409 >= (_164 - 2))
                                {
                                    _462 = (_455 >> 8u) | _448;
                                }
                                else
                                {
                                    _462 = _455;
                                }
                                uint _469;
                                if (_409 >= (_164 - 1))
                                {
                                    _469 = (_462 >> 8u) | _448;
                                }
                                else
                                {
                                    _469 = _462;
                                }
                                _470 = _469;
                            }
                            else
                            {
                                _470 = _440;
                            }
                            _490 = _470;
                        }
                        searchBuffer[0][_400] = _490;
                    }
                    barrier();
                    uvec4 _493;
                    _493 = uvec4(0u);
                    for (int _496 = 0; _496 < 8; )
                    {
                        int _502 = ((_113 + _496) * 6) + _110;
                        int _505 = _502 + 1;
                        int _508 = _502 + 2;
                        int _516 = int(pixels[_496][0] & 255u);
                        uint _519 = searchBuffer[0][_502] >> 8u;
                        int _524 = int((pixels[_496][0] >> 8u) & 255u);
                        int _533 = int((pixels[_496][0] >> 16u) & 255u);
                        int _542 = int((pixels[_496][0] >> 24u) & 255u);
                        uint _547 = searchBuffer[0][_505] & 255u;
                        uint _549 = _519 | (_547 << 24u);
                        uint _550 = searchBuffer[0][_505] >> 8u;
                        uint _555 = _549 >> 8u;
                        uint _574 = _550 & 255u;
                        uint _576 = _555 | (_574 << 24u);
                        uint _582 = _576 >> 8u;
                        uint _603 = _582 | (((_550 >> 8u) & 255u) << 24u);
                        uvec4 _628 = _493 + uvec4(uint(((abs(int(searchBuffer[0][_502] & 255u) - _516) + abs(int(_519 & 255u) - _524)) + abs(int((searchBuffer[0][_502] >> 16u) & 255u) - _533)) + abs(int((searchBuffer[0][_502] >> 24u) & 255u) - _542)), uint(((abs(int(_549 & 255u) - _516) + abs(int(_555 & 255u) - _524)) + abs(int((_549 >> 16u) & 255u) - _533)) + abs(int((_549 >> 24u) & 255u) - _542)), uint(((abs(int(_576 & 255u) - _516) + abs(int(_582 & 255u) - _524)) + abs(int((_576 >> 16u) & 255u) - _533)) + abs(int((_576 >> 24u) & 255u) - _542)), uint(((abs(int(_603 & 255u) - _516) + abs(int((_603 >> 8u) & 255u) - _524)) + abs(int((_603 >> 16u) & 255u) - _533)) + abs(int((_603 >> 24u) & 255u) - _542)));
                        int _633 = int(pixels[_496][1] & 255u);
                        int _639 = int((pixels[_496][1] >> 8u) & 255u);
                        int _648 = int((pixels[_496][1] >> 16u) & 255u);
                        int _657 = int((pixels[_496][1] >> 24u) & 255u);
                        uint _664 = _550 | ((searchBuffer[0][_508] & 255u) << 24u);
                        uint _665 = searchBuffer[0][_508] >> 8u;
                        uint _670 = _664 >> 8u;
                        uint _691 = _670 | ((_665 & 255u) << 24u);
                        uint _697 = _691 >> 8u;
                        uint _718 = _697 | (((_665 >> 8u) & 255u) << 24u);
                        uvec4 _494 = _628 + uvec4(uint(((abs(int(_547) - _633) + abs(int(_574) - _639)) + abs(int((searchBuffer[0][_505] >> 16u) & 255u) - _648)) + abs(int((searchBuffer[0][_505] >> 24u) & 255u) - _657)), uint(((abs(int(_664 & 255u) - _633) + abs(int(_670 & 255u) - _639)) + abs(int((_664 >> 16u) & 255u) - _648)) + abs(int((_664 >> 24u) & 255u) - _657)), uint(((abs(int(_691 & 255u) - _633) + abs(int(_697 & 255u) - _639)) + abs(int((_691 >> 16u) & 255u) - _648)) + abs(int((_691 >> 24u) & 255u) - _657)), uint(((abs(int(_718 & 255u) - _633) + abs(int((_718 >> 8u) & 255u) - _639)) + abs(int((_718 >> 16u) & 255u) - _648)) + abs(int((_718 >> 24u) & 255u) - _657)));
                        _493 = _494;
                        _496++;
                        continue;
                    }
                    int _745 = _110 * 4;
                    ivec2 _747 = ivec2(_745, _113) - ivec2(8);
                    ivec2 _748 = _747 >> ivec2(31);
                    uvec2 _751 = uvec2((_747 + _748) ^ _748);
                    uint _758 = uint(_113 << 4);
                    sadMapBuffer[0][_113][_110] = (_493.x << 16u) | ((((_751.y << 12u) | (_751.x << 8u)) | _758) | uint(_745));
                    int _766 = _745 + 1;
                    ivec2 _768 = ivec2(_766, _113) - ivec2(8);
                    ivec2 _769 = _768 >> ivec2(31);
                    uvec2 _772 = uvec2((_768 + _769) ^ _769);
                    sadMapBuffer[1][_113][_110] = (_493.y << 16u) | ((((_772.y << 12u) | (_772.x << 8u)) | _758) | uint(_766));
                    int _785 = _745 + 2;
                    ivec2 _787 = ivec2(_785, _113) - ivec2(8);
                    ivec2 _788 = _787 >> ivec2(31);
                    uvec2 _791 = uvec2((_787 + _788) ^ _788);
                    sadMapBuffer[2][_113][_110] = (_493.z << 16u) | ((((_791.y << 12u) | (_791.x << 8u)) | _758) | uint(_785));
                    int _804 = _745 + 3;
                    ivec2 _806 = ivec2(_804, _113) - ivec2(8);
                    ivec2 _807 = _806 >> ivec2(31);
                    uvec2 _810 = uvec2((_806 + _807) ^ _807);
                    sadMapBuffer[3][_113][_110] = (_493.w << 16u) | ((((_810.y << 12u) | (_810.x << 8u)) | _758) | uint(_804));
                    barrier();
                    sRed[int(gl_LocalInvocationIndex)] = min(min(sadMapBuffer[0][_113][_110], sadMapBuffer[1][_113][_110]), min(sadMapBuffer[2][_113][_110], sadMapBuffer[3][_113][_110]));
                    barrier();
                    for (uint _830 = 32u; _830 > 0u; _830 = _830 >> 1u)
                    {
                        if ((_108 & 63u) < _830)
                        {
                            sRed[int(gl_LocalInvocationIndex)] = min(sRed[int(gl_LocalInvocationIndex)], sRed[_108 + _830]);
                        }
                        barrier();
                    }
                    int _845 = int(gl_LocalInvocationIndex) & (-64);
                    uint _847 = sRed[_845];
                    barrier();
                    sRed[int(gl_LocalInvocationIndex)] = (_121 != (_367 + _384)) ? 0u : _351;
                    barrier();
                    for (uint _861 = 32u; _861 > 0u; _861 = _861 >> 1u)
                    {
                        if ((_108 & 63u) < _861)
                        {
                            sRed[int(gl_LocalInvocationIndex)] += sRed[_108 + _861];
                        }
                        barrier();
                    }
                    uint _876 = sRed[_845];
                    barrier();
                    bool _882;
                    if (cbOF.uOpticalFlowPyramidLevel == 0u)
                    {
                        _882 = _876 <= (_847 >> 16u);
                    }
                    else
                    {
                        _882 = false;
                    }
                    ivec2 _884 = mix(_382 + ivec2(int(_847 & 15u) - 8, int((_847 >> 4u) & 15u) - 8), ivec2(0), bvec2(_882));
                    imageStore(rw_optical_flow, ivec2(_372), ivec4((_884.x & 65535) | (_884.y << 16)));
                    _365 = _364;
                    _365.x = _367 + 1;
                    _364 = _365;
                    continue;
                }
                else
                {
                    break;
                }
            }
        }
        break;
    } while(false);
}


#version 310 es
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(binding = 0, std140) uniform type_cbOF
{
    ivec2 iInputLumaResolution;
    uint uOpticalFlowPyramidLevel;
    uint uOpticalFlowPyramidLevelCount;
    uint iFrameIndex;
    uint backbufferTransferFunction;
    vec2 minMaxLuminance;
} cbOF;

layout(binding = 0, r32i) uniform writeonly highp iimage2D rw_optical_flow_next_level;
layout(binding = 1, r32ui) uniform readonly highp uimage2D rw_optical_flow_scd_output;
uniform highp isampler2D SPIRV_Cross_Combinedr_optical_flowSPIRV_Cross_DummySampler;
uniform highp usampler2D SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler;
uniform highp usampler2D SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler;

shared ivec2 nearestVectors[4][4][4];
shared uint localRegion[4][4][4];
shared uint sads[4][4][4];

void main()
{
    do
    {
        bool _97;
        do
        {
            if (cbOF.iFrameIndex <= 5u)
            {
                _97 = true;
                break;
            }
            else
            {
                _97 = (imageLoad(rw_optical_flow_scd_output, ivec2(uvec2(1u, 0u))).x & 15u) != 0u;
                break;
            }
            break; // unreachable workaround
        } while(false);
        if (_97)
        {
            imageStore(rw_optical_flow_next_level, ivec2(uvec2(ivec3(gl_GlobalInvocationID).xy)), ivec4(0));
            break;
        }
        ivec4 _124 = texelFetch(SPIRV_Cross_Combinedr_optical_flowSPIRV_Cross_DummySampler, ivec2(uvec2(ivec2((int(gl_GlobalInvocationID.x) / 2) + (((int(gl_LocalInvocationID.z) - 2 * (int(gl_LocalInvocationID.z) / 2)) - 1) + (int(gl_GlobalInvocationID.x) - 2 * (int(gl_GlobalInvocationID.x) / 2))), (int(gl_GlobalInvocationID.y) / 2) + (((int(gl_LocalInvocationID.z) / 2) - 1) + (int(gl_GlobalInvocationID.y) - 2 * (int(gl_GlobalInvocationID.y) / 2)))))), int(0u));
        int _125 = _124.x;
        ivec2 _129 = ivec2((_125 << 16) >> 16, _125 >> 16);
        nearestVectors[int(gl_LocalInvocationID.z)][int(gl_LocalInvocationID.y)][int(gl_LocalInvocationID.x)] = _129 * ivec2(2);
        for (int _137 = int(gl_LocalInvocationID.z); _137 < 4; _137 += 4)
        {
            int _143 = int(gl_GlobalInvocationID.x) * 4;
            int _152 = int(cbOF.uOpticalFlowPyramidLevel) & 31;
            int _153 = cbOF.iInputLumaResolution.x >> _152;
            int _156 = _153 - 4;
            ivec2 _162 = ivec2(max(0, min(_156, _143)), max(0, min(((cbOF.iInputLumaResolution.y >> _152) - 1), ((int(gl_GlobalInvocationID.y) * 4) + _137))));
            uint _187 = ((texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_162)), int(0u)).x | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_162 + ivec2(1, 0))), int(0u)).x << 8u)) | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_162 + ivec2(2, 0))), int(0u)).x << 16u)) | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_162 + ivec2(3, 0))), int(0u)).x << 24u);
            uint _237;
            if (_143 < 0)
            {
                uint _218 = _187 & 255u;
                uint _224;
                if (_143 <= (-1))
                {
                    _224 = (_187 << 8u) | _218;
                }
                else
                {
                    _224 = _187;
                }
                uint _230;
                if (_143 <= (-2))
                {
                    _230 = (_224 << 8u) | _218;
                }
                else
                {
                    _230 = _224;
                }
                uint _236;
                if (_143 <= (-3))
                {
                    _236 = (_230 << 8u) | _218;
                }
                else
                {
                    _236 = _230;
                }
                _237 = _236;
            }
            else
            {
                uint _217;
                if (_143 > _156)
                {
                    uint _195 = _187 & 4278190080u;
                    uint _202;
                    if (_143 >= (_153 - 3))
                    {
                        _202 = (_187 >> 8u) | _195;
                    }
                    else
                    {
                        _202 = _187;
                    }
                    uint _209;
                    if (_143 >= (_153 - 2))
                    {
                        _209 = (_202 >> 8u) | _195;
                    }
                    else
                    {
                        _209 = _202;
                    }
                    uint _216;
                    if (_143 >= (_153 - 1))
                    {
                        _216 = (_209 >> 8u) | _195;
                    }
                    else
                    {
                        _216 = _209;
                    }
                    _217 = _216;
                }
                else
                {
                    _217 = _187;
                }
                _237 = _217;
            }
            localRegion[_137][int(gl_LocalInvocationID.y)][int(gl_LocalInvocationID.x)] = _237;
        }
        barrier();
        uint _240;
        _240 = 0u;
        uint _241;
        for (int _243 = 0; _243 < 4; _240 = _241, _243++)
        {
            ivec2 _252 = ivec2(int(gl_GlobalInvocationID.x) * 4, (int(gl_GlobalInvocationID.y) * 4) + _243) + _129;
            int _259 = int(cbOF.uOpticalFlowPyramidLevel) & 31;
            int _260 = cbOF.iInputLumaResolution.x >> _259;
            int _263 = _260 - 4;
            int _264 = _252.x;
            ivec2 _271 = ivec2(max(0, min(_263, _264)), max(0, min(((cbOF.iInputLumaResolution.y >> _259) - 1), _252.y)));
            uint _296 = ((texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_271)), int(0u)).x | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_271 + ivec2(1, 0))), int(0u)).x << 8u)) | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_271 + ivec2(2, 0))), int(0u)).x << 16u)) | (texelFetch(SPIRV_Cross_Combinedr_optical_flow_previous_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_271 + ivec2(3, 0))), int(0u)).x << 24u);
            uint _346;
            if (_264 < 0)
            {
                uint _327 = _296 & 255u;
                uint _333;
                if (_264 <= (-1))
                {
                    _333 = (_296 << 8u) | _327;
                }
                else
                {
                    _333 = _296;
                }
                uint _339;
                if (_264 <= (-2))
                {
                    _339 = (_333 << 8u) | _327;
                }
                else
                {
                    _339 = _333;
                }
                uint _345;
                if (_264 <= (-3))
                {
                    _345 = (_339 << 8u) | _327;
                }
                else
                {
                    _345 = _339;
                }
                _346 = _345;
            }
            else
            {
                uint _326;
                if (_264 > _263)
                {
                    uint _304 = _296 & 4278190080u;
                    uint _311;
                    if (_264 >= (_260 - 3))
                    {
                        _311 = (_296 >> 8u) | _304;
                    }
                    else
                    {
                        _311 = _296;
                    }
                    uint _318;
                    if (_264 >= (_260 - 2))
                    {
                        _318 = (_311 >> 8u) | _304;
                    }
                    else
                    {
                        _318 = _311;
                    }
                    uint _325;
                    if (_264 >= (_260 - 1))
                    {
                        _325 = (_318 >> 8u) | _304;
                    }
                    else
                    {
                        _325 = _318;
                    }
                    _326 = _325;
                }
                else
                {
                    _326 = _296;
                }
                _346 = _326;
            }
            _241 = _240 + uint(((abs(int(localRegion[_243][int(gl_LocalInvocationID.y)][int(gl_LocalInvocationID.x)] & 255u) - int(_346 & 255u)) + abs(int((localRegion[_243][int(gl_LocalInvocationID.y)][int(gl_LocalInvocationID.x)] >> 8u) & 255u) - int((_346 >> 8u) & 255u))) + abs(int((localRegion[_243][int(gl_LocalInvocationID.y)][int(gl_LocalInvocationID.x)] >> 16u) & 255u) - int((_346 >> 16u) & 255u))) + abs(int((localRegion[_243][int(gl_LocalInvocationID.y)][int(gl_LocalInvocationID.x)] >> 24u) & 255u) - int((_346 >> 24u) & 255u)));
        }
        sads[int(gl_LocalInvocationID.z)][int(gl_LocalInvocationID.y)][int(gl_LocalInvocationID.x)] = _240;
        barrier();
        if (int(gl_LocalInvocationID.z) == 0)
        {
            uint _391;
            _391 = 0u;
            uint _389;
            uint _392;
            uint _388 = 4294967295u;
            int _393 = 0;
            for (; _393 < 4; _388 = _389, _391 = _392, _393++)
            {
                if (sads[_393][int(gl_LocalInvocationID.y)][int(gl_LocalInvocationID.x)] < _388)
                {
                    _389 = sads[_393][int(gl_LocalInvocationID.y)][int(gl_LocalInvocationID.x)];
                    _392 = uint(_393);
                }
                else
                {
                    _389 = _388;
                    _392 = _391;
                }
            }
            imageStore(rw_optical_flow_next_level, ivec2(uvec2(ivec3(gl_GlobalInvocationID).xy)), ivec4((nearestVectors[_391][int(gl_LocalInvocationID.y)][int(gl_LocalInvocationID.x)].x & 65535) | (nearestVectors[_391][int(gl_LocalInvocationID.y)][int(gl_LocalInvocationID.x)].y << 16)));
        }
        break;
    } while(false);
}


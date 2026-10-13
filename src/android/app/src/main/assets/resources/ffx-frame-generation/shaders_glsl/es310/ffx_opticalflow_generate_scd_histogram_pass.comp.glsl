#version 310 es
#extension GL_OES_shader_image_atomic : require
layout(local_size_x = 32, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, std140) uniform type_cbOF
{
    ivec2 iInputLumaResolution;
    uint uOpticalFlowPyramidLevel;
    uint uOpticalFlowPyramidLevelCount;
    uint iFrameIndex;
    uint backbufferTransferFunction;
    vec2 minMaxLuminance;
} cbOF;

layout(binding = 0, r32ui) uniform highp uimage2D rw_optical_flow_scd_histogram;
uniform highp usampler2D SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler;

shared uint scdBuffer[2560];

void main()
{
    uint _58 = uint(cbOF.iInputLumaResolution.x) / 3u;
    uint _61 = uint(cbOF.iInputLumaResolution.y) / 3u;
    uint _63 = uint(ivec3(gl_GlobalInvocationID).z);
    uint _66 = _58 * (_63 % 3u);
    uint _67 = _61 * (_63 / 3u);
    uint _69 = _67 + _61;
    uint _71 = uint(int(gl_LocalInvocationIndex) * 10);
    for (int _73 = 0; _73 < 10; )
    {
        scdBuffer[_71 + uint(_73)] = 0u;
        _73++;
        continue;
    }
    barrier();
    int _85 = int(_66 + uint(4 * ivec3(gl_GlobalInvocationID).x));
    if (uint(_85) < (_66 + _58))
    {
        ivec2 _96;
        _96 = ivec2(_85, int(_67 + uint(ivec3(gl_GlobalInvocationID).y)));
        for (; uint(_96.y) < _69; )
        {
            uvec4 _123 = uvec4(texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_96)), int(0u)).x, texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_96 + ivec2(1, 0))), int(0u)).x, texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_96 + ivec2(2, 0))), int(0u)).x, texelFetch(SPIRV_Cross_Combinedr_optical_flow_inputSPIRV_Cross_DummySampler, ivec2(uvec2(_96 + ivec2(3, 0))), int(0u)).x) * uvec4(10u);
            uint _125 = uint(int(gl_LocalInvocationIndex) - 10 * (int(gl_LocalInvocationIndex) / 10));
            uint _129 = atomicAdd(scdBuffer[_123.x + _125], 1u);
            uint _133 = atomicAdd(scdBuffer[_123.y + _125], 1u);
            uint _137 = atomicAdd(scdBuffer[_123.z + _125], 1u);
            uint _141 = atomicAdd(scdBuffer[_123.w + _125], 1u);
            ivec2 _97 = _96;
            _97.y = _96.y + 128;
            _96 = _97;
            continue;
        }
    }
    barrier();
    uint _144;
    _144 = 0u;
    for (int _147 = 0; _147 < 10; )
    {
        _144 += scdBuffer[_71 + uint(_147)];
        _147++;
        continue;
    }
    uint _163 = imageAtomicAdd(rw_optical_flow_scd_histogram, ivec2(uvec2(ivec2(int(uint(256 * ivec3(gl_GlobalInvocationID).z) + uint(int(gl_LocalInvocationIndex))), 0))), _144);
}


#version 310 es
#extension GL_OES_shader_image_atomic : require
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(binding = 0, r32ui) uniform highp uimage2D rw_optical_flow_scd_histogram;
layout(binding = 1, r32f) uniform highp image2D rw_optical_flow_scd_previous_histogram;
layout(binding = 2, r32ui) uniform highp uimage2D rw_optical_flow_scd_temp;
layout(binding = 3, r32ui) uniform highp uimage2D rw_optical_flow_scd_output;

shared float sourceHistogram[256];
shared float filteredHistogram[256];
shared float tempBuffer[256];
shared vec2 tempBuffer2[256];

void main()
{
    uvec2 _79 = uvec2(ivec2(ivec3(gl_GlobalInvocationID).x, 0));
    sourceHistogram[int(gl_LocalInvocationIndex)] = float(imageLoad(rw_optical_flow_scd_histogram, ivec2(_79)).x);
    barrier();
    int _125 = int(gl_LocalInvocationIndex) + 1;
    int _132 = int(gl_LocalInvocationIndex) + 2;
    int _146 = int(gl_LocalInvocationIndex) + 4;
    float _159 = ((((((((((0.008812229149043560028076171875 * sourceHistogram[max(0, min(255, (int(gl_LocalInvocationIndex) + (-5))))]) + (0.02714357711374759674072265625 * sourceHistogram[max(0, min(255, (int(gl_LocalInvocationIndex) + (-4))))])) + (0.065114058554172515869140625 * sourceHistogram[max(0, min(255, (int(gl_LocalInvocationIndex) + (-3))))])) + (0.12164907157421112060546875 * sourceHistogram[max(0, min(255, (int(gl_LocalInvocationIndex) + (-2))))])) + (0.1769983470439910888671875 * sourceHistogram[max(0, min(255, (int(gl_LocalInvocationIndex) + (-1))))])) + (0.20056541264057159423828125 * sourceHistogram[max(0, min(255, int(gl_LocalInvocationIndex)))])) + (0.1769983470439910888671875 * sourceHistogram[max(0, min(255, _125))])) + (0.12164907157421112060546875 * sourceHistogram[max(0, min(255, _132))])) + (0.065114058554172515869140625 * sourceHistogram[max(0, min(255, (int(gl_LocalInvocationIndex) + 3)))])) + (0.02714357711374759674072265625 * sourceHistogram[max(0, min(255, _146))])) + (0.008812229149043560028076171875 * sourceHistogram[max(0, min(255, (int(gl_LocalInvocationIndex) + 5)))]);
    float _160 = _159 + 1.0;
    if (ivec3(gl_GlobalInvocationID).y == 0)
    {
        if (int(gl_LocalInvocationIndex) == 0)
        {
            filteredHistogram[255] = 1.0;
        }
        else
        {
            filteredHistogram[int(gl_LocalInvocationIndex) - 1] = _160;
        }
    }
    else
    {
        if (ivec3(gl_GlobalInvocationID).y == 1)
        {
            filteredHistogram[int(gl_LocalInvocationIndex)] = _160;
        }
        else
        {
            if (ivec3(gl_GlobalInvocationID).y == 2)
            {
                if (int(gl_LocalInvocationIndex) == 255)
                {
                    filteredHistogram[0] = 1.0;
                }
                else
                {
                    filteredHistogram[_125] = _160;
                }
            }
        }
    }
    barrier();
    tempBuffer[int(gl_LocalInvocationIndex)] = filteredHistogram[int(gl_LocalInvocationIndex)];
    barrier();
    bool _190 = int(gl_LocalInvocationIndex) < 128;
    if (_190)
    {
        tempBuffer[int(gl_LocalInvocationIndex)] += tempBuffer[int(gl_LocalInvocationIndex) + 128];
    }
    barrier();
    bool _198 = int(gl_LocalInvocationIndex) < 64;
    if (_198)
    {
        tempBuffer[int(gl_LocalInvocationIndex)] += tempBuffer[int(gl_LocalInvocationIndex) + 64];
    }
    barrier();
    bool _206 = int(gl_LocalInvocationIndex) < 32;
    if (_206)
    {
        tempBuffer[int(gl_LocalInvocationIndex)] += tempBuffer[int(gl_LocalInvocationIndex) + 32];
    }
    bool _214 = int(gl_LocalInvocationIndex) < 16;
    if (_214)
    {
        tempBuffer[int(gl_LocalInvocationIndex)] += tempBuffer[int(gl_LocalInvocationIndex) + 16];
    }
    bool _222 = int(gl_LocalInvocationIndex) < 8;
    if (_222)
    {
        tempBuffer[int(gl_LocalInvocationIndex)] += tempBuffer[int(gl_LocalInvocationIndex) + 8];
    }
    bool _230 = int(gl_LocalInvocationIndex) < 4;
    if (_230)
    {
        tempBuffer[int(gl_LocalInvocationIndex)] += tempBuffer[_146];
    }
    bool _237 = int(gl_LocalInvocationIndex) < 2;
    if (_237)
    {
        tempBuffer[int(gl_LocalInvocationIndex)] += tempBuffer[_132];
    }
    if (int(gl_LocalInvocationIndex) < 1)
    {
        tempBuffer[int(gl_LocalInvocationIndex)] += tempBuffer[_125];
    }
    barrier();
    filteredHistogram[int(gl_LocalInvocationIndex)] /= tempBuffer[0];
    float _255 = filteredHistogram[int(gl_LocalInvocationIndex)];
    vec4 _257 = imageLoad(rw_optical_flow_scd_previous_histogram, ivec2(_79));
    float _258 = _257.x;
    tempBuffer2[int(gl_LocalInvocationIndex)] = vec2(_255 * log(_255 / _258), _258 * log(_258 / _255));
    barrier();
    if (_190)
    {
        tempBuffer2[int(gl_LocalInvocationIndex)] += tempBuffer2[int(gl_LocalInvocationIndex) + 128];
    }
    barrier();
    if (_198)
    {
        tempBuffer2[int(gl_LocalInvocationIndex)] += tempBuffer2[int(gl_LocalInvocationIndex) + 64];
    }
    barrier();
    if (_206)
    {
        tempBuffer2[int(gl_LocalInvocationIndex)] += tempBuffer2[int(gl_LocalInvocationIndex) + 32];
    }
    if (_214)
    {
        tempBuffer2[int(gl_LocalInvocationIndex)] += tempBuffer2[int(gl_LocalInvocationIndex) + 16];
    }
    if (_222)
    {
        tempBuffer2[int(gl_LocalInvocationIndex)] += tempBuffer2[int(gl_LocalInvocationIndex) + 8];
    }
    if (_230)
    {
        tempBuffer2[int(gl_LocalInvocationIndex)] += tempBuffer2[_146];
    }
    if (_237)
    {
        tempBuffer2[int(gl_LocalInvocationIndex)] += tempBuffer2[_132];
    }
    if (int(gl_LocalInvocationIndex) == 0)
    {
        vec2 _321 = tempBuffer2[0] + tempBuffer2[1];
        uint _335 = imageAtomicAdd(rw_optical_flow_scd_temp, ivec2(uvec2(ivec2(ivec3(gl_GlobalInvocationID).y, 0))), uint((1.0 - exp(-(abs(_321.x) + abs(_321.y)))) * 111111.109375));
        uint _337 = imageAtomicAdd(rw_optical_flow_scd_output, ivec2(uvec2(2u, 0u)), 1u);
        if (_337 == 26u)
        {
            float _353 = float(min(imageLoad(rw_optical_flow_scd_temp, ivec2(uvec2(0u))).x, min(imageLoad(rw_optical_flow_scd_temp, ivec2(uvec2(1u, 0u))).x, imageLoad(rw_optical_flow_scd_temp, ivec2(uvec2(2u, 0u))).x))) * 9.9999999747524270787835121154785e-07;
            uint _357 = imageLoad(rw_optical_flow_scd_output, ivec2(uvec2(1u, 0u))).x << 1u;
            uint _362;
            if (_353 > 0.449999988079071044921875)
            {
                _362 = _357 | 1u;
            }
            else
            {
                _362 = _357;
            }
            imageStore(rw_optical_flow_scd_output, ivec2(uvec2(0u)), uvec4(floatBitsToUint(_353)));
            imageStore(rw_optical_flow_scd_output, ivec2(uvec2(1u, 0u)), uvec4(_362));
            imageStore(rw_optical_flow_scd_output, ivec2(uvec2(2u, 0u)), uvec4(0u));
            imageStore(rw_optical_flow_scd_temp, ivec2(uvec2(0u)), uvec4(0u));
            imageStore(rw_optical_flow_scd_temp, ivec2(uvec2(1u, 0u)), uvec4(0u));
            imageStore(rw_optical_flow_scd_temp, ivec2(uvec2(2u, 0u)), uvec4(0u));
        }
    }
    if (ivec3(gl_GlobalInvocationID).y == 1)
    {
        imageStore(rw_optical_flow_scd_previous_histogram, ivec2(_79), vec4(_255));
        imageStore(rw_optical_flow_scd_histogram, ivec2(_79), uvec4(0u));
    }
}


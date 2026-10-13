#version 320 es
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, std140) uniform type_cbOF
{
    ivec2 iInputLumaResolution;
    uint uOpticalFlowPyramidLevel;
    uint uOpticalFlowPyramidLevelCount;
    uint iFrameIndex;
    uint backbufferTransferFunction;
    vec2 minMaxLuminance;
} cbOF;

layout(binding = 0, r32ui) uniform writeonly highp uimage2D rw_optical_flow_input;
uniform highp sampler2D SPIRV_Cross_Combinedr_input_colorSPIRV_Cross_DummySampler;

void main()
{
    for (int _70 = 0; _70 < 2; _70++)
    {
        for (int _77 = 0; _77 < 2; _77++)
        {
            uvec2 _86 = uvec2((ivec3(gl_GlobalInvocationID).xy * ivec2(2)) + ivec2(_77, _70));
            vec4 _88 = texelFetch(SPIRV_Cross_Combinedr_input_colorSPIRV_Cross_DummySampler, ivec2(_86), int(0u));
            vec3 _89 = _88.xyz;
            float _172;
            if (cbOF.backbufferTransferFunction == 0u)
            {
                _172 = ((0.2125999927520751953125 * _88.x) + (0.715200006961822509765625 * _88.y)) + (0.072200000286102294921875 * _88.z);
            }
            else
            {
                float _171;
                if (cbOF.backbufferTransferFunction == 1u)
                {
                    vec3 _111 = pow(_89, vec3(0.01268330030143260955810546875));
                    vec3 _119 = pow(clamp(_111 - vec3(0.835937976837158203125), vec3(0.0), vec3(1.0)) / (vec3(18.85160064697265625) - (vec3(18.6875) * _111)), vec3(6.277390003204345703125)) * (10000.0 / cbOF.minMaxLuminance.y);
                    float _127 = ((0.2626999914646148681640625 * _119.x) + (0.677999973297119140625 * _119.y)) + (0.0593000017106533050537109375 * _119.z);
                    float _136;
                    if (_127 <= 0.008856452070176601409912109375)
                    {
                        _136 = _127 * 903.29632568359375;
                    }
                    else
                    {
                        _136 = (pow(_127, 0.3333333432674407958984375) * 116.0) - 16.0;
                    }
                    _171 = _136 * 0.00999999977648258209228515625;
                }
                else
                {
                    float _170;
                    if (cbOF.backbufferTransferFunction == 2u)
                    {
                        vec3 _151 = (_89 - vec3(cbOF.minMaxLuminance.x * 0.012500000186264514923095703125)) / vec3((cbOF.minMaxLuminance.y - cbOF.minMaxLuminance.x) * 0.012500000186264514923095703125);
                        float _159 = ((0.2125999927520751953125 * _151.x) + (0.715200006961822509765625 * _151.y)) + (0.072200000286102294921875 * _151.z);
                        float _168;
                        if (_159 <= 0.008856452070176601409912109375)
                        {
                            _168 = _159 * 903.29632568359375;
                        }
                        else
                        {
                            _168 = (pow(_159, 0.3333333432674407958984375) * 116.0) - 16.0;
                        }
                        _170 = _168 * 0.00999999977648258209228515625;
                    }
                    else
                    {
                        _170 = 0.0;
                    }
                    _171 = _170;
                }
                _172 = _171;
            }
            imageStore(rw_optical_flow_input, ivec2(_86), uvec4(uint(_172 * 255.0)));
        }
    }
}


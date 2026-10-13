#version 320 es
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, std140) uniform type_cbInterp
{
    ivec2 iSize;
    ivec2 iFlowSize;
    float fFlowSign;
    float fFlowScale;
    float fT;
    float fErrThreshold;
    int iBlock;
    int _pad0;
    int _pad1;
    int _pad2;
} cbInterp;

layout(binding = 0, rgba16f) uniform writeonly highp image2D rw_out;
uniform highp isampler2D SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler;
uniform highp sampler2D SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler;
uniform highp sampler2D SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        bool _81;
        if (!(gl_GlobalInvocationID.x >= uint(cbInterp.iSize.x)))
        {
            _81 = gl_GlobalInvocationID.y >= uint(cbInterp.iSize.y);
        }
        else
        {
            _81 = true;
        }
        if (_81)
        {
            break;
        }
        ivec2 _85 = ivec2(gl_GlobalInvocationID.xy);
        vec2 _87 = vec2(_85) + vec2(0.5);
        vec2 _94 = _87 / (vec2(cbInterp.iSize) / vec2(cbInterp.iFlowSize));
        vec2 _95 = _94 - vec2(0.5);
        ivec2 _97 = ivec2(floor(_95));
        vec2 _99 = _95 - vec2(_97);
        ivec2 _100 = cbInterp.iFlowSize - ivec2(1);
        ivec4 _107 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_97, ivec2(0), _100), 0).xy, 0);
        int _108 = _107.x;
        ivec4 _122 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_97 + ivec2(1, 0), ivec2(0), _100), 0).xy, 0);
        int _123 = _122.x;
        ivec4 _137 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_97 + ivec2(0, 1), ivec2(0), _100), 0).xy, 0);
        int _138 = _137.x;
        ivec4 _152 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_97 + ivec2(1), ivec2(0), _100), 0).xy, 0);
        int _153 = _152.x;
        vec2 _161 = vec2(_99.x);
        vec2 _172 = (mix(mix(vec2(float((_108 << 16) >> 16), float(_108 >> 16)), vec2(float((_123 << 16) >> 16), float(_123 >> 16)), _161), mix(vec2(float((_138 << 16) >> 16), float(_138 >> 16)), vec2(float((_153 << 16) >> 16), float(_153 >> 16)), _161), vec2(_99.y)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
        vec4 _1253;
        if (cbInterp.fT > 1.0)
        {
            ivec2 _181 = cbInterp.iSize - ivec2(1);
            vec4 _187 = texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_85, ivec2(0), _181), 0).xy, 0);
            vec2 _190 = (_87 + _172) - vec2(0.5);
            ivec2 _192 = ivec2(floor(_190));
            vec2 _194 = _190 - vec2(_192);
            vec4 _223 = vec4(_194.x);
            vec3 _230 = _187.xyz;
            vec3 _232 = abs(mix(mix(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_192, ivec2(0), _181), 0).xy, 0), texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_192 + ivec2(1, 0), ivec2(0), _181), 0).xy, 0), _223), mix(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_192 + ivec2(0, 1), ivec2(0), _181), 0).xy, 0), texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_192 + ivec2(1), ivec2(0), _181), 0).xy, 0), _223), vec4(_194.y)).xyz - _230);
            float _233 = _232.x;
            float _234 = _232.y;
            float _235 = _232.z;
            float _236 = isnan(_235) ? _234 : (isnan(_234) ? _235 : max(_234, _235));
            float _237 = isnan(_236) ? _233 : (isnan(_233) ? _236 : max(_233, _236));
            float _595;
            vec2 _596;
            if (_237 > cbInterp.fErrThreshold)
            {
                ivec2 _244 = ivec2(floor(_94));
                ivec4 _252 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_244 + ivec2(-1), ivec2(0), _100), 0).xy, 0);
                int _253 = _252.x;
                vec2 _261 = (vec2(float((_253 << 16) >> 16), float(_253 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec3 _274 = abs(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_261)), ivec2(0), _181), 0).xy, 0).xyz - _230);
                float _275 = _274.x;
                float _276 = _274.y;
                float _277 = _274.z;
                float _278 = isnan(_277) ? _276 : (isnan(_276) ? _277 : max(_276, _277));
                float _279 = isnan(_278) ? _275 : (isnan(_275) ? _278 : max(_275, _278));
                bool _280 = _279 < _237;
                float _281 = _280 ? _279 : _237;
                ivec4 _291 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_244 + ivec2(0, -1), ivec2(0), _100), 0).xy, 0);
                int _292 = _291.x;
                vec2 _300 = (vec2(float((_292 << 16) >> 16), float(_292 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec3 _313 = abs(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_300)), ivec2(0), _181), 0).xy, 0).xyz - _230);
                float _314 = _313.x;
                float _315 = _313.y;
                float _316 = _313.z;
                float _317 = isnan(_316) ? _315 : (isnan(_315) ? _316 : max(_315, _316));
                float _318 = isnan(_317) ? _314 : (isnan(_314) ? _317 : max(_314, _317));
                bool _319 = _318 < _281;
                float _320 = _319 ? _318 : _281;
                ivec4 _330 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_244 + ivec2(1, -1), ivec2(0), _100), 0).xy, 0);
                int _331 = _330.x;
                vec2 _339 = (vec2(float((_331 << 16) >> 16), float(_331 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec3 _352 = abs(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_339)), ivec2(0), _181), 0).xy, 0).xyz - _230);
                float _353 = _352.x;
                float _354 = _352.y;
                float _355 = _352.z;
                float _356 = isnan(_355) ? _354 : (isnan(_354) ? _355 : max(_354, _355));
                float _357 = isnan(_356) ? _353 : (isnan(_353) ? _356 : max(_353, _356));
                bool _358 = _357 < _320;
                float _359 = _358 ? _357 : _320;
                ivec4 _369 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_244 + ivec2(-1, 0), ivec2(0), _100), 0).xy, 0);
                int _370 = _369.x;
                vec2 _378 = (vec2(float((_370 << 16) >> 16), float(_370 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec3 _391 = abs(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_378)), ivec2(0), _181), 0).xy, 0).xyz - _230);
                float _392 = _391.x;
                float _393 = _391.y;
                float _394 = _391.z;
                float _395 = isnan(_394) ? _393 : (isnan(_393) ? _394 : max(_393, _394));
                float _396 = isnan(_395) ? _392 : (isnan(_392) ? _395 : max(_392, _395));
                bool _397 = _396 < _359;
                float _398 = _397 ? _396 : _359;
                ivec4 _407 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_244, ivec2(0), _100), 0).xy, 0);
                int _408 = _407.x;
                vec2 _416 = (vec2(float((_408 << 16) >> 16), float(_408 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec3 _429 = abs(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_416)), ivec2(0), _181), 0).xy, 0).xyz - _230);
                float _430 = _429.x;
                float _431 = _429.y;
                float _432 = _429.z;
                float _433 = isnan(_432) ? _431 : (isnan(_431) ? _432 : max(_431, _432));
                float _434 = isnan(_433) ? _430 : (isnan(_430) ? _433 : max(_430, _433));
                bool _435 = _434 < _398;
                float _436 = _435 ? _434 : _398;
                ivec4 _446 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_244 + ivec2(1, 0), ivec2(0), _100), 0).xy, 0);
                int _447 = _446.x;
                vec2 _455 = (vec2(float((_447 << 16) >> 16), float(_447 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec3 _468 = abs(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_455)), ivec2(0), _181), 0).xy, 0).xyz - _230);
                float _469 = _468.x;
                float _470 = _468.y;
                float _471 = _468.z;
                float _472 = isnan(_471) ? _470 : (isnan(_470) ? _471 : max(_470, _471));
                float _473 = isnan(_472) ? _469 : (isnan(_469) ? _472 : max(_469, _472));
                bool _474 = _473 < _436;
                float _475 = _474 ? _473 : _436;
                ivec4 _485 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_244 + ivec2(-1, 1), ivec2(0), _100), 0).xy, 0);
                int _486 = _485.x;
                vec2 _494 = (vec2(float((_486 << 16) >> 16), float(_486 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec3 _507 = abs(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_494)), ivec2(0), _181), 0).xy, 0).xyz - _230);
                float _508 = _507.x;
                float _509 = _507.y;
                float _510 = _507.z;
                float _511 = isnan(_510) ? _509 : (isnan(_509) ? _510 : max(_509, _510));
                float _512 = isnan(_511) ? _508 : (isnan(_508) ? _511 : max(_508, _511));
                bool _513 = _512 < _475;
                float _514 = _513 ? _512 : _475;
                ivec4 _524 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_244 + ivec2(0, 1), ivec2(0), _100), 0).xy, 0);
                int _525 = _524.x;
                vec2 _533 = (vec2(float((_525 << 16) >> 16), float(_525 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec3 _546 = abs(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_533)), ivec2(0), _181), 0).xy, 0).xyz - _230);
                float _547 = _546.x;
                float _548 = _546.y;
                float _549 = _546.z;
                float _550 = isnan(_549) ? _548 : (isnan(_548) ? _549 : max(_548, _549));
                float _551 = isnan(_550) ? _547 : (isnan(_547) ? _550 : max(_547, _550));
                bool _552 = _551 < _514;
                float _553 = _552 ? _551 : _514;
                ivec4 _563 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_244 + ivec2(1), ivec2(0), _100), 0).xy, 0);
                int _564 = _563.x;
                vec2 _572 = (vec2(float((_564 << 16) >> 16), float(_564 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec3 _585 = abs(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_572)), ivec2(0), _181), 0).xy, 0).xyz - _230);
                float _586 = _585.x;
                float _587 = _585.y;
                float _588 = _585.z;
                float _589 = isnan(_588) ? _587 : (isnan(_587) ? _588 : max(_587, _588));
                float _590 = isnan(_589) ? _586 : (isnan(_586) ? _589 : max(_586, _589));
                bool _591 = _590 < _553;
                _595 = _591 ? _590 : _553;
                _596 = mix(mix(mix(mix(mix(mix(mix(mix(mix(_172, _261, bvec2(_280)), _300, bvec2(_319)), _339, bvec2(_358)), _378, bvec2(_397)), _416, bvec2(_435)), _455, bvec2(_474)), _494, bvec2(_513)), _533, bvec2(_552)), _572, bvec2(_591));
            }
            else
            {
                _595 = _237;
                _596 = _172;
            }
            vec2 _600 = (_87 + (_596 * (cbInterp.fT - 1.0))) - vec2(0.5);
            ivec2 _602 = ivec2(floor(_600));
            vec2 _604 = _600 - vec2(_602);
            vec4 _633 = vec4(_604.x);
            _1253 = mix(mix(mix(texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_602, ivec2(0), _181), 0).xy, 0), texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_602 + ivec2(1, 0), ivec2(0), _181), 0).xy, 0), _633), mix(texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_602 + ivec2(0, 1), ivec2(0), _181), 0).xy, 0), texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_602 + ivec2(1), ivec2(0), _181), 0).xy, 0), _633), vec4(_604.y)), _187, vec4(clamp((_595 - cbInterp.fErrThreshold) / (isnan(9.9999997473787516355514526367188e-05) ? cbInterp.fErrThreshold : (isnan(cbInterp.fErrThreshold) ? 9.9999997473787516355514526367188e-05 : max(cbInterp.fErrThreshold, 9.9999997473787516355514526367188e-05))), 0.0, 1.0)));
        }
        else
        {
            vec2 _648 = (_87 + (_172 * cbInterp.fT)) - vec2(0.5);
            ivec2 _650 = ivec2(floor(_648));
            vec2 _652 = _648 - vec2(_650);
            ivec2 _653 = cbInterp.iSize - ivec2(1);
            vec4 _682 = vec4(_652.x);
            vec4 _687 = mix(mix(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_650, ivec2(0), _653), 0).xy, 0), texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_650 + ivec2(1, 0), ivec2(0), _653), 0).xy, 0), _682), mix(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_650 + ivec2(0, 1), ivec2(0), _653), 0).xy, 0), texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_650 + ivec2(1), ivec2(0), _653), 0).xy, 0), _682), vec4(_652.y));
            float _689 = 1.0 - cbInterp.fT;
            vec2 _692 = (_87 - (_172 * _689)) - vec2(0.5);
            ivec2 _694 = ivec2(floor(_692));
            vec2 _696 = _692 - vec2(_694);
            vec4 _725 = vec4(_696.x);
            vec4 _730 = mix(mix(texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_694, ivec2(0), _653), 0).xy, 0), texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_694 + ivec2(1, 0), ivec2(0), _653), 0).xy, 0), _725), mix(texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_694 + ivec2(0, 1), ivec2(0), _653), 0).xy, 0), texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_694 + ivec2(1), ivec2(0), _653), 0).xy, 0), _725), vec4(_696.y));
            ivec2 _736 = ivec3(clamp(_85, ivec2(0), _653), 0).xy;
            vec4 _740 = vec4(cbInterp.fT);
            vec3 _745 = abs(_687.xyz - _730.xyz);
            float _746 = _745.x;
            float _747 = _745.y;
            float _748 = _745.z;
            float _749 = isnan(_748) ? _747 : (isnan(_747) ? _748 : max(_747, _748));
            float _750 = isnan(_749) ? _746 : (isnan(_746) ? _749 : max(_746, _749));
            vec4 _1252;
            if (_750 > cbInterp.fErrThreshold)
            {
                ivec2 _758 = ivec2(floor(_94));
                ivec4 _767 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_758 + ivec2(-1), ivec2(0), _100), 0).xy, 0);
                int _768 = _767.x;
                vec2 _776 = (vec2(float((_768 << 16) >> 16), float(_768 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec4 _787 = texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_776 * cbInterp.fT)), ivec2(0), _653), 0).xy, 0);
                vec4 _798 = texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_85 - ivec2(roundEven(_776 * _689)), ivec2(0), _653), 0).xy, 0);
                vec3 _802 = abs(_787.xyz - _798.xyz);
                float _803 = _802.x;
                float _804 = _802.y;
                float _805 = _802.z;
                float _806 = isnan(_805) ? _804 : (isnan(_804) ? _805 : max(_804, _805));
                float _807 = isnan(_806) ? _803 : (isnan(_803) ? _806 : max(_803, _806));
                bool _808 = _807 < _750;
                vec4 _812;
                if (_808)
                {
                    _812 = mix(_787, _798, _740);
                }
                else
                {
                    _812 = mix(_687, _730, _740);
                }
                float _813 = _808 ? _807 : _750;
                ivec4 _821 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_758 + ivec2(0, -1), ivec2(0), _100), 0).xy, 0);
                int _822 = _821.x;
                vec2 _830 = (vec2(float((_822 << 16) >> 16), float(_822 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec4 _841 = texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_830 * cbInterp.fT)), ivec2(0), _653), 0).xy, 0);
                vec4 _852 = texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_85 - ivec2(roundEven(_830 * _689)), ivec2(0), _653), 0).xy, 0);
                vec3 _856 = abs(_841.xyz - _852.xyz);
                float _857 = _856.x;
                float _858 = _856.y;
                float _859 = _856.z;
                float _860 = isnan(_859) ? _858 : (isnan(_858) ? _859 : max(_858, _859));
                float _861 = isnan(_860) ? _857 : (isnan(_857) ? _860 : max(_857, _860));
                bool _862 = _861 < _813;
                vec4 _866;
                if (_862)
                {
                    _866 = mix(_841, _852, _740);
                }
                else
                {
                    _866 = _812;
                }
                float _867 = _862 ? _861 : _813;
                ivec4 _875 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_758 + ivec2(1, -1), ivec2(0), _100), 0).xy, 0);
                int _876 = _875.x;
                vec2 _884 = (vec2(float((_876 << 16) >> 16), float(_876 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec4 _895 = texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_884 * cbInterp.fT)), ivec2(0), _653), 0).xy, 0);
                vec4 _906 = texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_85 - ivec2(roundEven(_884 * _689)), ivec2(0), _653), 0).xy, 0);
                vec3 _910 = abs(_895.xyz - _906.xyz);
                float _911 = _910.x;
                float _912 = _910.y;
                float _913 = _910.z;
                float _914 = isnan(_913) ? _912 : (isnan(_912) ? _913 : max(_912, _913));
                float _915 = isnan(_914) ? _911 : (isnan(_911) ? _914 : max(_911, _914));
                bool _916 = _915 < _867;
                vec4 _920;
                if (_916)
                {
                    _920 = mix(_895, _906, _740);
                }
                else
                {
                    _920 = _866;
                }
                float _921 = _916 ? _915 : _867;
                ivec4 _929 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_758 + ivec2(-1, 0), ivec2(0), _100), 0).xy, 0);
                int _930 = _929.x;
                vec2 _938 = (vec2(float((_930 << 16) >> 16), float(_930 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec4 _949 = texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_938 * cbInterp.fT)), ivec2(0), _653), 0).xy, 0);
                vec4 _960 = texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_85 - ivec2(roundEven(_938 * _689)), ivec2(0), _653), 0).xy, 0);
                vec3 _964 = abs(_949.xyz - _960.xyz);
                float _965 = _964.x;
                float _966 = _964.y;
                float _967 = _964.z;
                float _968 = isnan(_967) ? _966 : (isnan(_966) ? _967 : max(_966, _967));
                float _969 = isnan(_968) ? _965 : (isnan(_965) ? _968 : max(_965, _968));
                bool _970 = _969 < _921;
                vec4 _974;
                if (_970)
                {
                    _974 = mix(_949, _960, _740);
                }
                else
                {
                    _974 = _920;
                }
                float _975 = _970 ? _969 : _921;
                ivec4 _982 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_758, ivec2(0), _100), 0).xy, 0);
                int _983 = _982.x;
                vec2 _991 = (vec2(float((_983 << 16) >> 16), float(_983 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec4 _1002 = texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_991 * cbInterp.fT)), ivec2(0), _653), 0).xy, 0);
                vec4 _1013 = texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_85 - ivec2(roundEven(_991 * _689)), ivec2(0), _653), 0).xy, 0);
                vec3 _1017 = abs(_1002.xyz - _1013.xyz);
                float _1018 = _1017.x;
                float _1019 = _1017.y;
                float _1020 = _1017.z;
                float _1021 = isnan(_1020) ? _1019 : (isnan(_1019) ? _1020 : max(_1019, _1020));
                float _1022 = isnan(_1021) ? _1018 : (isnan(_1018) ? _1021 : max(_1018, _1021));
                bool _1023 = _1022 < _975;
                vec4 _1027;
                if (_1023)
                {
                    _1027 = mix(_1002, _1013, _740);
                }
                else
                {
                    _1027 = _974;
                }
                float _1028 = _1023 ? _1022 : _975;
                ivec4 _1036 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_758 + ivec2(1, 0), ivec2(0), _100), 0).xy, 0);
                int _1037 = _1036.x;
                vec2 _1045 = (vec2(float((_1037 << 16) >> 16), float(_1037 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec4 _1056 = texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_1045 * cbInterp.fT)), ivec2(0), _653), 0).xy, 0);
                vec4 _1067 = texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_85 - ivec2(roundEven(_1045 * _689)), ivec2(0), _653), 0).xy, 0);
                vec3 _1071 = abs(_1056.xyz - _1067.xyz);
                float _1072 = _1071.x;
                float _1073 = _1071.y;
                float _1074 = _1071.z;
                float _1075 = isnan(_1074) ? _1073 : (isnan(_1073) ? _1074 : max(_1073, _1074));
                float _1076 = isnan(_1075) ? _1072 : (isnan(_1072) ? _1075 : max(_1072, _1075));
                bool _1077 = _1076 < _1028;
                vec4 _1081;
                if (_1077)
                {
                    _1081 = mix(_1056, _1067, _740);
                }
                else
                {
                    _1081 = _1027;
                }
                float _1082 = _1077 ? _1076 : _1028;
                ivec4 _1090 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_758 + ivec2(-1, 1), ivec2(0), _100), 0).xy, 0);
                int _1091 = _1090.x;
                vec2 _1099 = (vec2(float((_1091 << 16) >> 16), float(_1091 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec4 _1110 = texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_1099 * cbInterp.fT)), ivec2(0), _653), 0).xy, 0);
                vec4 _1121 = texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_85 - ivec2(roundEven(_1099 * _689)), ivec2(0), _653), 0).xy, 0);
                vec3 _1125 = abs(_1110.xyz - _1121.xyz);
                float _1126 = _1125.x;
                float _1127 = _1125.y;
                float _1128 = _1125.z;
                float _1129 = isnan(_1128) ? _1127 : (isnan(_1127) ? _1128 : max(_1127, _1128));
                float _1130 = isnan(_1129) ? _1126 : (isnan(_1126) ? _1129 : max(_1126, _1129));
                bool _1131 = _1130 < _1082;
                vec4 _1135;
                if (_1131)
                {
                    _1135 = mix(_1110, _1121, _740);
                }
                else
                {
                    _1135 = _1081;
                }
                float _1136 = _1131 ? _1130 : _1082;
                ivec4 _1144 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_758 + ivec2(0, 1), ivec2(0), _100), 0).xy, 0);
                int _1145 = _1144.x;
                vec2 _1153 = (vec2(float((_1145 << 16) >> 16), float(_1145 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec4 _1164 = texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_1153 * cbInterp.fT)), ivec2(0), _653), 0).xy, 0);
                vec4 _1175 = texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_85 - ivec2(roundEven(_1153 * _689)), ivec2(0), _653), 0).xy, 0);
                vec3 _1179 = abs(_1164.xyz - _1175.xyz);
                float _1180 = _1179.x;
                float _1181 = _1179.y;
                float _1182 = _1179.z;
                float _1183 = isnan(_1182) ? _1181 : (isnan(_1181) ? _1182 : max(_1181, _1182));
                float _1184 = isnan(_1183) ? _1180 : (isnan(_1180) ? _1183 : max(_1180, _1183));
                bool _1185 = _1184 < _1136;
                vec4 _1189;
                if (_1185)
                {
                    _1189 = mix(_1164, _1175, _740);
                }
                else
                {
                    _1189 = _1135;
                }
                float _1190 = _1185 ? _1184 : _1136;
                ivec4 _1198 = texelFetch(SPIRV_Cross_Combinedr_flowSPIRV_Cross_DummySampler, ivec3(clamp(_758 + ivec2(1), ivec2(0), _100), 0).xy, 0);
                int _1199 = _1198.x;
                vec2 _1207 = (vec2(float((_1199 << 16) >> 16), float(_1199 >> 16)) * cbInterp.fFlowScale) * cbInterp.fFlowSign;
                vec4 _1218 = texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, ivec3(clamp(_85 + ivec2(roundEven(_1207 * cbInterp.fT)), ivec2(0), _653), 0).xy, 0);
                vec4 _1229 = texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, ivec3(clamp(_85 - ivec2(roundEven(_1207 * _689)), ivec2(0), _653), 0).xy, 0);
                vec3 _1233 = abs(_1218.xyz - _1229.xyz);
                float _1234 = _1233.x;
                float _1235 = _1233.y;
                float _1236 = _1233.z;
                float _1237 = isnan(_1236) ? _1235 : (isnan(_1235) ? _1236 : max(_1235, _1236));
                float _1238 = isnan(_1237) ? _1234 : (isnan(_1234) ? _1237 : max(_1234, _1237));
                bool _1239 = _1238 < _1190;
                vec4 _1243;
                if (_1239)
                {
                    _1243 = mix(_1218, _1229, _740);
                }
                else
                {
                    _1243 = _1189;
                }
                _1252 = mix(_1243, mix(texelFetch(SPIRV_Cross_Combinedr_prevSPIRV_Cross_DummySampler, _736, 0), texelFetch(SPIRV_Cross_Combinedr_curSPIRV_Cross_DummySampler, _736, 0), _740), vec4(clamp(((_1239 ? _1238 : _1190) - cbInterp.fErrThreshold) / (isnan(9.9999997473787516355514526367188e-05) ? cbInterp.fErrThreshold : (isnan(cbInterp.fErrThreshold) ? 9.9999997473787516355514526367188e-05 : max(cbInterp.fErrThreshold, 9.9999997473787516355514526367188e-05))), 0.0, 1.0)));
            }
            else
            {
                _1252 = mix(_687, _730, _740);
            }
            _1253 = _1252;
        }
        vec4 _1254 = _1253;
        _1254.w = 1.0;
        imageStore(rw_out, ivec2(uvec2(_85)), _1254);
        break;
    } while(false);
}


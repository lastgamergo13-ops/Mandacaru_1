#version 320 es
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, r32ui) uniform writeonly highp uimage2D rw_dst;
uniform highp usampler2D SPIRV_Cross_Combinedr_srcSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        uvec2 _37 = uvec2(imageSize(rw_dst));
        uvec2 _41 = uvec2(textureSize(SPIRV_Cross_Combinedr_srcSPIRV_Cross_DummySampler, 0));
        bool _51;
        if (!(gl_GlobalInvocationID.x >= _37.x))
        {
            _51 = gl_GlobalInvocationID.y >= _37.y;
        }
        else
        {
            _51 = true;
        }
        if (_51)
        {
            break;
        }
        ivec2 _56 = ivec2(gl_GlobalInvocationID.xy) * ivec2(2);
        ivec2 _60 = ivec2(int(_41.x), int(_41.y)) - ivec2(1);
        imageStore(rw_dst, ivec2(gl_GlobalInvocationID.xy), uvec4(((((texelFetch(SPIRV_Cross_Combinedr_srcSPIRV_Cross_DummySampler, ivec3(min(_56, _60), 0).xy, 0).x + texelFetch(SPIRV_Cross_Combinedr_srcSPIRV_Cross_DummySampler, ivec3(min((_56 + ivec2(1, 0)), _60), 0).xy, 0).x) + texelFetch(SPIRV_Cross_Combinedr_srcSPIRV_Cross_DummySampler, ivec3(min((_56 + ivec2(0, 1)), _60), 0).xy, 0).x) + texelFetch(SPIRV_Cross_Combinedr_srcSPIRV_Cross_DummySampler, ivec3(min((_56 + ivec2(1)), _60), 0).xy, 0).x) + 2u) / 4u));
        break;
    } while(false);
}


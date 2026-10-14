#include "Cafe/HW/Latte/Renderer/RendererOuputShader.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h" // for OpenGLRenderer::IsGLES() - see GetOpenGlVertexSource/PrependFragmentPreamble
#include "Cafe/HW/Latte/Core/Latte.h"
#include "config/ActiveSettings.h"

const std::string RendererOutputShader::s_copy_shader_source =
R"(
void outputShader()
{
	colorOut0 = vec4(texture(textureSrc, passUV).rgb,1.0);
}
)";

const std::string RendererOutputShader::s_copy_shader_source_mtl =
R"(#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float2 uv;
};

fragment float4 main0(VertexOut in [[stage_in]], texture2d<float> textureSrc [[texture(0)]], sampler samplr [[sampler(0)]]) {
	return float4(textureSrc.sample(samplr, in.uv).rgb, 1.0);
}
)";

const std::string RendererOutputShader::s_bicubic_shader_source =
R"(
vec4 cubic(float x)
{
	float x2 = x * x;
	float x3 = x2 * x;
	vec4 w;
	w.x = -x3 + 3.0 * x2 - 3.0 * x + 1.0;
	w.y = 3.0 * x3 - 6.0 * x2 + 4.0;
	w.z = -3.0 * x3 + 3.0 * x2 + 3.0 * x + 1.0;
	w.w = x3;
	return w / 6.0;
}

vec4 bcFilter(vec2 uv, vec4 texelSize)
{
	vec2 pixel = uv*texelSize.zw - 0.5;
	vec2 pixelFrac = fract(pixel);
	vec2 pixelInt = pixel - pixelFrac;

	vec4 xcubic = cubic(pixelFrac.x);
	vec4 ycubic = cubic(pixelFrac.y);

	vec4 c = vec4(pixelInt.x - 0.5, pixelInt.x + 1.5, pixelInt.y - 0.5, pixelInt.y + 1.5);
	vec4 s = vec4(xcubic.x + xcubic.y, xcubic.z + xcubic.w, ycubic.x + ycubic.y, ycubic.z + ycubic.w);
	vec4 offset = c + vec4(xcubic.y, xcubic.w, ycubic.y, ycubic.w) / s;

	vec4 sample0 = texture(textureSrc, vec2(offset.x, offset.z) * texelSize.xy);
	vec4 sample1 = texture(textureSrc, vec2(offset.y, offset.z) * texelSize.xy);
	vec4 sample2 = texture(textureSrc, vec2(offset.x, offset.w) * texelSize.xy);
	vec4 sample3 = texture(textureSrc, vec2(offset.y, offset.w) * texelSize.xy);

	float sx = s.x / (s.x + s.y);
	float sy = s.z / (s.z + s.w);

	return mix(
		mix(sample3, sample2, sx),
		mix(sample1, sample0, sx), sy);
}

void outputShader(){
	vec4 texelSize = vec4( 1.0 / textureSrcResolution.xy, textureSrcResolution.xy);
	colorOut0 = vec4(bcFilter(passUV, texelSize).rgb,1.0);
}
)";

const std::string RendererOutputShader::s_bicubic_shader_source_mtl =
R"(#include <metal_stdlib>
using namespace metal;

float4 cubic(float x) {
	float x2 = x * x;
	float x3 = x2 * x;
	float4 w;
	w.x = -x3 + 3.0 * x2 - 3.0 * x + 1.0;
	w.y = 3.0 * x3 - 6.0 * x2 + 4.0;
	w.z = -3.0 * x3 + 3.0 * x2 + 3.0 * x + 1.0;
	w.w = x3;
	return w / 6.0;
}

float4 bcFilter(texture2d<float> textureSrc, sampler samplr, float2 texcoord, float2 texscale) {
	float fx = fract(texcoord.x);
	float fy = fract(texcoord.y);
	texcoord.x -= fx;
	texcoord.y -= fy;

	float4 xcubic = cubic(fx);
	float4 ycubic = cubic(fy);

	float4 c = float4(texcoord.x - 0.5, texcoord.x + 1.5, texcoord.y - 0.5, texcoord.y + 1.5);
	float4 s = float4(xcubic.x + xcubic.y, xcubic.z + xcubic.w, ycubic.x + ycubic.y, ycubic.z + ycubic.w);
	float4 offset = c + float4(xcubic.y, xcubic.w, ycubic.y, ycubic.w) / s;

	float4 sample0 = textureSrc.sample(samplr, float2(offset.x, offset.z) * texscale);
	float4 sample1 = textureSrc.sample(samplr, float2(offset.y, offset.z) * texscale);
	float4 sample2 = textureSrc.sample(samplr, float2(offset.x, offset.w) * texscale);
	float4 sample3 = textureSrc.sample(samplr, float2(offset.y, offset.w) * texscale);

	float sx = s.x / (s.x + s.y);
	float sy = s.z / (s.z + s.w);

	return mix(
		mix(sample3, sample2, sx),
		mix(sample1, sample0, sx), sy);
}

struct VertexOut {
    float2 uv;
};

fragment float4 main0(VertexOut in [[stage_in]], texture2d<float> textureSrc [[texture(0)]], sampler samplr [[sampler(0)]]) {
    float2 textureSrcResolution = float2(textureSrc.get_width(), textureSrc.get_height());
	return float4(bcFilter(textureSrc, samplr, in.uv * textureSrcResolution, float2(1.0, 1.0) / textureSrcResolution).rgb, 1.0);
}
)";

const std::string RendererOutputShader::s_hermite_shader_source =
R"(
// https://www.shadertoy.com/view/MllSzX

vec3 CubicHermite (vec3 A, vec3 B, vec3 C, vec3 D, float t)
{
	float t2 = t*t;
    float t3 = t*t*t;
    vec3 a = -A/2.0 + (3.0*B)/2.0 - (3.0*C)/2.0 + D/2.0;
    vec3 b = A - (5.0*B)/2.0 + 2.0*C - D / 2.0;
    vec3 c = -A/2.0 + C/2.0;
   	vec3 d = B;

    return a*t3 + b*t2 + c*t + d;
}


vec3 BicubicHermiteTexture(vec2 uv, vec4 texelSize)
{
	vec2 pixel = uv*texelSize.zw + 0.5;
	vec2 frac = fract(pixel);
    pixel = floor(pixel) / texelSize.zw - vec2(texelSize.xy/2.0);

	vec4 doubleSize = texelSize*2.0;

	vec3 C00 = texture(textureSrc, pixel + vec2(-texelSize.x ,-texelSize.y)).rgb;
    vec3 C10 = texture(textureSrc, pixel + vec2( 0.0        ,-texelSize.y)).rgb;
    vec3 C20 = texture(textureSrc, pixel + vec2( texelSize.x ,-texelSize.y)).rgb;
    vec3 C30 = texture(textureSrc, pixel + vec2( doubleSize.x,-texelSize.y)).rgb;

    vec3 C01 = texture(textureSrc, pixel + vec2(-texelSize.x , 0.0)).rgb;
    vec3 C11 = texture(textureSrc, pixel + vec2( 0.0        , 0.0)).rgb;
    vec3 C21 = texture(textureSrc, pixel + vec2( texelSize.x , 0.0)).rgb;
    vec3 C31 = texture(textureSrc, pixel + vec2( doubleSize.x, 0.0)).rgb;

    vec3 C02 = texture(textureSrc, pixel + vec2(-texelSize.x , texelSize.y)).rgb;
    vec3 C12 = texture(textureSrc, pixel + vec2( 0.0        , texelSize.y)).rgb;
    vec3 C22 = texture(textureSrc, pixel + vec2( texelSize.x , texelSize.y)).rgb;
    vec3 C32 = texture(textureSrc, pixel + vec2( doubleSize.x, texelSize.y)).rgb;

    vec3 C03 = texture(textureSrc, pixel + vec2(-texelSize.x , doubleSize.y)).rgb;
    vec3 C13 = texture(textureSrc, pixel + vec2( 0.0        , doubleSize.y)).rgb;
    vec3 C23 = texture(textureSrc, pixel + vec2( texelSize.x , doubleSize.y)).rgb;
    vec3 C33 = texture(textureSrc, pixel + vec2( doubleSize.x, doubleSize.y)).rgb;

    vec3 CP0X = CubicHermite(C00, C10, C20, C30, frac.x);
    vec3 CP1X = CubicHermite(C01, C11, C21, C31, frac.x);
    vec3 CP2X = CubicHermite(C02, C12, C22, C32, frac.x);
    vec3 CP3X = CubicHermite(C03, C13, C23, C33, frac.x);

    return CubicHermite(CP0X, CP1X, CP2X, CP3X, frac.y);
}

void outputShader(){
	vec4 texelSize = vec4( 1.0 / textureSrcResolution.xy, textureSrcResolution.xy);
	colorOut0 = vec4(BicubicHermiteTexture(passUV, texelSize), 1.0);
}
)";

// AMD FidelityFX Super Resolution 1.0 - EASU (edge-adaptive spatial upsampling) pass.
// Ported by hand from the official ffx_fsr1.h/ffx_a.h reference (MIT licensed, AMD 2021) into
// plain GLSL for this project's fragment-shader-based output filter pipeline (see
// Renderer::DrawBackbufferQuadFSR()), rather than pulling in the AU/AF macro layer and the
// original compute-shader entry point. Reads the low-resolution game image (textureSrc) and
// writes the upsampled result into an intermediate texture at output resolution; RCAS below
// then reads that intermediate texture and sharpens it into the real backbuffer.
// AMD's fast approximate reciprocal/rsqrt bit-trick helpers (APrxLoRcpF1 and friends) are
// replaced with plain division/inversesqrt here - mathematically equivalent (they were a GCN
// hardware micro-optimization, not part of the actual algorithm), and one less thing to get
// subtly wrong porting by hand.
const std::string RendererOutputShader::s_fsr_easu_shader_source =
R"(
void FsrEasuTapF(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c)
{
	vec2 v;
	v.x = (off.x*( dir.x)) + (off.y*dir.y);
	v.y = (off.x*(-dir.y)) + (off.y*dir.x);
	v *= len;
	float d2 = v.x*v.x + v.y*v.y;
	d2 = min(d2, clp);
	float wB = (2.0/5.0)*d2 - 1.0;
	float wA = lob*d2 - 1.0;
	wB *= wB;
	wA *= wA;
	wB = (25.0/16.0)*wB - (25.0/16.0-1.0);
	float w = wB*wA;
	aC += c*w; aW += w;
}

void FsrEasuSetF(inout vec2 dir, inout float len, vec2 pp, bool biS, bool biT, bool biU, bool biV, float lA, float lB, float lC, float lD, float lE)
{
	float w = 0.0;
	if(biS) w = (1.0-pp.x)*(1.0-pp.y);
	if(biT) w =      pp.x *(1.0-pp.y);
	if(biU) w = (1.0-pp.x)*     pp.y ;
	if(biV) w =      pp.x *     pp.y ;
	float dc = lD-lC;
	float cb = lC-lB;
	float lenX = max(abs(dc), abs(cb));
	lenX = 1.0/max(lenX, 1e-8); // AMD's approximate reciprocal saturates instead of reaching
	                            // true infinity for flat regions (dc==cb==0) - guard the same
	                            // case here to avoid an outright NaN from 0.0*(1.0/0.0).
	float dirX = lD-lB;
	dir.x += dirX*w;
	lenX = clamp(abs(dirX)*lenX, 0.0, 1.0);
	lenX *= lenX;
	len += lenX*w;
	float ec = lE-lC;
	float ca = lC-lA;
	float lenY = max(abs(ec), abs(ca));
	lenY = 1.0/max(lenY, 1e-8); // see the lenX guard above
	float dirY = lE-lA;
	dir.y += dirY*w;
	lenY = clamp(abs(dirY)*lenY, 0.0, 1.0);
	lenY *= lenY;
	len += lenY*w;
}

vec3 FsrEasuF(vec2 ip)
{
	// con0: output-pixel -> input-viewport-space; con1..con3: gather4 tap offsets in
	// normalized input-texture space. See FsrEasuCon() in ffx_fsr1.h - inlined here directly
	// from the resolutions this project's output shaders already carry (textureSrcResolution
	// = input viewport in pixels, nativeResolution = full input texture size, outputResolution
	// = output size), rather than precomputing them on the CPU into a separate uniform block.
	vec2 con0_xy = textureSrcResolution / outputResolution;
	vec2 con0_zw = 0.5*con0_xy - 0.5;
	vec2 rcpIn = 1.0 / nativeResolution;
	vec2 con1_xy = rcpIn;
	vec2 con1_zw = vec2(1.0,-1.0) * rcpIn;
	vec2 con2_xy = vec2(-1.0, 2.0) * rcpIn;
	vec2 con2_zw = vec2( 1.0, 2.0) * rcpIn;
	vec2 con3_xy = vec2( 0.0, 4.0) * rcpIn;

	vec2 pp = ip*con0_xy + con0_zw;
	vec2 fp = floor(pp);
	pp -= fp;

	vec2 p0 = fp*con1_xy + con1_zw;
	vec2 p1 = p0 + con2_xy;
	vec2 p2 = p0 + con2_zw;
	vec2 p3 = p0 + con3_xy;
	vec4 bczzR = textureGather(textureSrc, p0, 0);
	vec4 bczzG = textureGather(textureSrc, p0, 1);
	vec4 bczzB = textureGather(textureSrc, p0, 2);
	vec4 ijfeR = textureGather(textureSrc, p1, 0);
	vec4 ijfeG = textureGather(textureSrc, p1, 1);
	vec4 ijfeB = textureGather(textureSrc, p1, 2);
	vec4 klhgR = textureGather(textureSrc, p2, 0);
	vec4 klhgG = textureGather(textureSrc, p2, 1);
	vec4 klhgB = textureGather(textureSrc, p2, 2);
	vec4 zzonR = textureGather(textureSrc, p3, 0);
	vec4 zzonG = textureGather(textureSrc, p3, 1);
	vec4 zzonB = textureGather(textureSrc, p3, 2);

	vec4 bczzL = bczzB*0.5 + (bczzR*0.5 + bczzG);
	vec4 ijfeL = ijfeB*0.5 + (ijfeR*0.5 + ijfeG);
	vec4 klhgL = klhgB*0.5 + (klhgR*0.5 + klhgG);
	vec4 zzonL = zzonB*0.5 + (zzonR*0.5 + zzonG);
	float bL=bczzL.x, cL=bczzL.y;
	float iL=ijfeL.x, jL=ijfeL.y, fL=ijfeL.z, eL=ijfeL.w;
	float kL=klhgL.x, lL=klhgL.y, hL=klhgL.z, gL=klhgL.w;
	float oL=zzonL.z, nL=zzonL.w;

	vec2 dir = vec2(0.0);
	float len = 0.0;
	FsrEasuSetF(dir,len,pp,true, false,false,false,bL,eL,fL,gL,jL);
	FsrEasuSetF(dir,len,pp,false,true ,false,false,cL,fL,gL,hL,kL);
	FsrEasuSetF(dir,len,pp,false,false,true ,false,fL,iL,jL,kL,nL);
	FsrEasuSetF(dir,len,pp,false,false,false,true ,gL,jL,kL,lL,oL);

	vec2 dir2 = dir*dir;
	float dirR = dir2.x + dir2.y;
	bool zro = dirR < (1.0/32768.0);
	dirR = inversesqrt(dirR);
	dirR = zro ? 1.0 : dirR;
	dir.x = zro ? 1.0 : dir.x;
	dir *= vec2(dirR);
	len = len*0.5;
	len *= len;
	float stretch = (dir.x*dir.x + dir.y*dir.y) / max(abs(dir.x), abs(dir.y));
	vec2 len2 = vec2(1.0 + (stretch-1.0)*len, 1.0 - 0.5*len);
	float lob = 0.5 + ((1.0/4.0-0.04)-0.5)*len;
	float clp = 1.0/lob;

	vec3 min4 = min(min(vec3(ijfeR.z,ijfeG.z,ijfeB.z), vec3(klhgR.w,klhgG.w,klhgB.w)), min(vec3(ijfeR.y,ijfeG.y,ijfeB.y), vec3(klhgR.x,klhgG.x,klhgB.x)));
	vec3 max4 = max(max(vec3(ijfeR.z,ijfeG.z,ijfeB.z), vec3(klhgR.w,klhgG.w,klhgB.w)), max(vec3(ijfeR.y,ijfeG.y,ijfeB.y), vec3(klhgR.x,klhgG.x,klhgB.x)));

	vec3 aC = vec3(0.0);
	float aW = 0.0;
	FsrEasuTapF(aC,aW, vec2( 0.0,-1.0)-pp, dir,len2,lob,clp, vec3(bczzR.x,bczzG.x,bczzB.x));
	FsrEasuTapF(aC,aW, vec2( 1.0,-1.0)-pp, dir,len2,lob,clp, vec3(bczzR.y,bczzG.y,bczzB.y));
	FsrEasuTapF(aC,aW, vec2(-1.0, 1.0)-pp, dir,len2,lob,clp, vec3(ijfeR.x,ijfeG.x,ijfeB.x));
	FsrEasuTapF(aC,aW, vec2( 0.0, 1.0)-pp, dir,len2,lob,clp, vec3(ijfeR.y,ijfeG.y,ijfeB.y));
	FsrEasuTapF(aC,aW, vec2( 0.0, 0.0)-pp, dir,len2,lob,clp, vec3(ijfeR.z,ijfeG.z,ijfeB.z));
	FsrEasuTapF(aC,aW, vec2(-1.0, 0.0)-pp, dir,len2,lob,clp, vec3(ijfeR.w,ijfeG.w,ijfeB.w));
	FsrEasuTapF(aC,aW, vec2( 1.0, 1.0)-pp, dir,len2,lob,clp, vec3(klhgR.x,klhgG.x,klhgB.x));
	FsrEasuTapF(aC,aW, vec2( 2.0, 1.0)-pp, dir,len2,lob,clp, vec3(klhgR.y,klhgG.y,klhgB.y));
	FsrEasuTapF(aC,aW, vec2( 2.0, 0.0)-pp, dir,len2,lob,clp, vec3(klhgR.z,klhgG.z,klhgB.z));
	FsrEasuTapF(aC,aW, vec2( 1.0, 0.0)-pp, dir,len2,lob,clp, vec3(klhgR.w,klhgG.w,klhgB.w));
	FsrEasuTapF(aC,aW, vec2( 1.0, 2.0)-pp, dir,len2,lob,clp, vec3(zzonR.z,zzonG.z,zzonB.z));
	FsrEasuTapF(aC,aW, vec2( 0.0, 2.0)-pp, dir,len2,lob,clp, vec3(zzonR.w,zzonG.w,zzonB.w));

	return min(max4, max(min4, aC/aW));
}

void outputShader(){
	vec2 ip = floor(passUV * outputResolution);
	colorOut0 = vec4(FsrEasuF(ip), 1.0);
}
)";

// AMD FidelityFX Super Resolution 1.0 - RCAS (robust contrast-adaptive sharpening) pass.
// Reads the EASU output (already at full output resolution, bound here as textureSrc) via
// texelFetch and writes the final sharpened image to the real backbuffer. Same porting notes
// as EASU above apply (fast-reciprocal bit tricks replaced with plain division).
const std::string RendererOutputShader::s_fsr_rcas_shader_source =
R"(
void outputShader(){
	ivec2 sp = ivec2(floor(passUV * outputResolution));
	vec3 b = texelFetch(textureSrc, sp + ivec2( 0,-1), 0).rgb;
	vec3 d = texelFetch(textureSrc, sp + ivec2(-1, 0), 0).rgb;
	vec3 e = texelFetch(textureSrc, sp,                0).rgb;
	vec3 f = texelFetch(textureSrc, sp + ivec2( 1, 0), 0).rgb;
	vec3 h = texelFetch(textureSrc, sp + ivec2( 0, 1), 0).rgb;

	float bL = b.b*0.5 + (b.r*0.5 + b.g);
	float dL = d.b*0.5 + (d.r*0.5 + d.g);
	float eL = e.b*0.5 + (e.r*0.5 + e.g);
	float fL = f.b*0.5 + (f.r*0.5 + f.g);
	float hL = h.b*0.5 + (h.r*0.5 + h.g);

	float nz = 0.25*bL + 0.25*dL + 0.25*fL + 0.25*hL - eL;
	float rangeL = max(max(max(bL,dL),eL),max(fL,hL)) - min(min(min(bL,dL),eL),min(fL,hL));
	nz = clamp(abs(nz)/max(rangeL, 1.0/32768.0), 0.0, 1.0);
	nz = -0.5*nz + 1.0;

	vec3 mn4 = min(min(min(b,d),f),h);
	vec3 mx4 = max(max(max(b,d),f),h);
	vec2 peakC = vec2(1.0, -4.0);
	vec3 hitMin = min(mn4, e) / max(4.0*mx4, 1e-8); // guarded against pure-black regions
	// 4*mn4+peakC.y is always <= 0 here (mn4 in [0,1], peakC.y=-4) - clamp the upper bound
	// instead of the lower one so a near-zero denominator (mn4==1, pure white) doesn't flip
	// sign or blow up, unlike a plain max() would.
	vec3 hitMax = (peakC.x - max(mx4, e)) / min(4.0*mn4 + peakC.y, -1e-8);
	vec3 lobeRGB = max(-hitMin, hitMax);
	float lobe = max(-0.1875, min(max(max(lobeRGB.r,lobeRGB.g),lobeRGB.b), 0.0)) * exp2(-fsrRcasSharpness);

	float rcpL = 1.0 / (4.0*lobe + 1.0);
	vec3 outColor = (lobe*b + lobe*d + lobe*h + lobe*f + e) * rcpL;
	colorOut0 = vec4(outColor, 1.0);
}
)";

const std::string RendererOutputShader::s_hermite_shader_source_mtl =
R"(#include <metal_stdlib>
using namespace metal;

// https://www.shadertoy.com/view/MllSzX

float3 CubicHermite(float3 A, float3 B, float3 C, float3 D, float t) {
	float t2 = t*t;
    float t3 = t*t*t;
    float3 a = -A/2.0 + (3.0*B)/2.0 - (3.0*C)/2.0 + D/2.0;
    float3 b = A - (5.0*B)/2.0 + 2.0*C - D / 2.0;
    float3 c = -A/2.0 + C/2.0;
   	float3 d = B;

    return a*t3 + b*t2 + c*t + d;
}


float3 BicubicHermiteTexture(texture2d<float> textureSrc, sampler samplr, float2 uv, float4 texelSize) {
	float2 pixel = uv*texelSize.zw + 0.5;
	float2 frac = fract(pixel);
    pixel = floor(pixel) / texelSize.zw - float2(texelSize.xy/2.0);

	float4 doubleSize = texelSize*texelSize;

	float3 C00 = textureSrc.sample(samplr, pixel + float2(-texelSize.x ,-texelSize.y)).rgb;
    float3 C10 = textureSrc.sample(samplr, pixel + float2( 0.0        ,-texelSize.y)).rgb;
    float3 C20 = textureSrc.sample(samplr, pixel + float2( texelSize.x ,-texelSize.y)).rgb;
    float3 C30 = textureSrc.sample(samplr, pixel + float2( doubleSize.x,-texelSize.y)).rgb;

    float3 C01 = textureSrc.sample(samplr, pixel + float2(-texelSize.x , 0.0)).rgb;
    float3 C11 = textureSrc.sample(samplr, pixel + float2( 0.0        , 0.0)).rgb;
    float3 C21 = textureSrc.sample(samplr, pixel + float2( texelSize.x , 0.0)).rgb;
    float3 C31 = textureSrc.sample(samplr, pixel + float2( doubleSize.x, 0.0)).rgb;

    float3 C02 = textureSrc.sample(samplr, pixel + float2(-texelSize.x , texelSize.y)).rgb;
    float3 C12 = textureSrc.sample(samplr, pixel + float2( 0.0        , texelSize.y)).rgb;
    float3 C22 = textureSrc.sample(samplr, pixel + float2( texelSize.x , texelSize.y)).rgb;
    float3 C32 = textureSrc.sample(samplr, pixel + float2( doubleSize.x, texelSize.y)).rgb;

    float3 C03 = textureSrc.sample(samplr, pixel + float2(-texelSize.x , doubleSize.y)).rgb;
    float3 C13 = textureSrc.sample(samplr, pixel + float2( 0.0        , doubleSize.y)).rgb;
    float3 C23 = textureSrc.sample(samplr, pixel + float2( texelSize.x , doubleSize.y)).rgb;
    float3 C33 = textureSrc.sample(samplr, pixel + float2( doubleSize.x, doubleSize.y)).rgb;

    float3 CP0X = CubicHermite(C00, C10, C20, C30, frac.x);
    float3 CP1X = CubicHermite(C01, C11, C21, C31, frac.x);
    float3 CP2X = CubicHermite(C02, C12, C22, C32, frac.x);
    float3 CP3X = CubicHermite(C03, C13, C23, C33, frac.x);

    return CubicHermite(CP0X, CP1X, CP2X, CP3X, frac.y);
}

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

fragment float4 main0(VertexOut in [[stage_in]], texture2d<float> textureSrc [[texture(0)]], sampler samplr [[sampler(0)]], constant float2& outputResolution [[buffer(0)]]) {
	float4 texelSize = float4(1.0 / outputResolution.xy, outputResolution.xy);
	return float4(BicubicHermiteTexture(textureSrc, samplr, in.uv, texelSize), 1.0);
}
)";

RendererOutputShader::RendererOutputShader(const std::string& vertex_source, const std::string& fragment_source)
{
    std::string finalFragmentSrc;
	switch(g_renderer->GetType())
	{
#ifdef ENABLE_METAL
	case RendererAPI::Metal:
		finalFragmentSrc = fragment_source;
		break;
#endif
	default:
		finalFragmentSrc = PrependFragmentPreamble(fragment_source);
		break;
	}

	m_vertex_shader.reset(g_renderer->shader_create(RendererShader::ShaderType::kVertex, 0, 0, vertex_source, false, false));
	m_fragment_shader.reset(g_renderer->shader_create(RendererShader::ShaderType::kFragment, 0, 0, finalFragmentSrc, false, false));

	m_vertex_shader->PreponeCompilation(true);
	m_fragment_shader->PreponeCompilation(true);

	if (!m_vertex_shader->WaitForCompiled())
		throw std::exception();

	if(!m_fragment_shader->WaitForCompiled())
		throw std::exception();

}

RendererOutputShader::OutputUniformVariables RendererOutputShader::FillUniformBlockBuffer(const LatteTextureView& texture_view, const Vector2i& output_res, const bool padView) const
{
	OutputUniformVariables vars;

	sint32 effectiveWidth, effectiveHeight;
	texture_view.baseTexture->GetEffectiveSize(effectiveWidth, effectiveHeight, 0);
	vars.textureSrcResolution = {(float)effectiveWidth, (float)effectiveHeight};

	vars.nativeResolution = {(float)texture_view.baseTexture->width, (float)texture_view.baseTexture->height};
	vars.outputResolution = output_res;

	vars.applySRGBEncoding = padView ? LatteGPUState.drcBufferUsesSRGB : LatteGPUState.tvBufferUsesSRGB;
	vars.targetGamma = padView ? ActiveSettings::GetDRCGamma() : ActiveSettings::GetTVGamma();
	vars.displayGamma = GetConfig().userDisplayGamma;
	vars.fsrRcasSharpness = GetConfig().fsr_sharpness;

	return vars;
}

RendererOutputShader* RendererOutputShader::s_copy_shader;
RendererOutputShader* RendererOutputShader::s_copy_shader_ud;

RendererOutputShader* RendererOutputShader::s_bicubic_shader;
RendererOutputShader* RendererOutputShader::s_bicubic_shader_ud;

RendererOutputShader* RendererOutputShader::s_hermit_shader;
RendererOutputShader* RendererOutputShader::s_hermit_shader_ud;

RendererOutputShader* RendererOutputShader::s_fsr_easu_shader;
RendererOutputShader* RendererOutputShader::s_fsr_rcas_shader;
RendererOutputShader* RendererOutputShader::s_fsr_rcas_shader_ud;

std::string RendererOutputShader::GetOpenGlVertexSource(bool render_upside_down)
{
	// vertex shader
	std::ostringstream vertex_source;
	bool isGLES = OpenGLRenderer::IsGLES();
	if (isGLES)
	{
		// GLSL ES has no #version 420 (desktop-only) and no gl_PerVertex redeclaration block
		// at all (that's a desktop compatibility-profile concept GLES never had, so it's
		// simply omitted rather than replaced with anything).
		vertex_source <<
			R"(#version 320 es
precision highp float;
layout(location = 0) out vec2 passUV;

void main(){
	vec2 vPos;
	vec2 vUV;
	int vID = gl_VertexID;
)";
	}
	else
	{
		vertex_source <<
			R"(#version 420
layout(location = 0) smooth out vec2 passUV;

out gl_PerVertex
{
   vec4 gl_Position;
};

void main(){
	vec2 vPos;
	vec2 vUV;
	int vID = gl_VertexID;
)";
	}

		if (render_upside_down)
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	)";
		}
		else
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	)";
		}

		vertex_source <<
			R"(	passUV = vUV;
)";
		if (OpenGLRenderer::IsClipControlEmulated())
			vertex_source << "\tgl_Position = vec4(vPos.x, -vPos.y, 0.0, 1.0);\n}\n"; // emula origem UPPER_LEFT
		else
			vertex_source << "\tgl_Position = vec4(vPos, 0.0, 1.0);\n}\n";
		return vertex_source.str();
}

std::string RendererOutputShader::GetVulkanVertexSource(bool render_upside_down)
{
	// vertex shader
	std::ostringstream vertex_source;
		vertex_source <<
			R"(#version 450
layout(location = 0) out vec2 passUV;

out gl_PerVertex
{
   vec4 gl_Position;
};

void main(){
	vec2 vPos;
	vec2 vUV;
	int vID = gl_VertexIndex;
)";

		if (render_upside_down)
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	)";
		}
		else
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	)";
		}

		vertex_source <<
			R"(	passUV = vUV;
	gl_Position = vec4(vPos, 0.0, 1.0);
}
)";
		return vertex_source.str();
}

std::string RendererOutputShader::GetMetalVertexSource(bool render_upside_down)
{
	// vertex shader
	std::ostringstream vertex_source;
		vertex_source <<
			R"(#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut main0(ushort vid [[vertex_id]]) {
	VertexOut out;
	float2 pos;
	if (vid == 0) pos = float2(-1.0, -3.0);
	else if (vid == 1) pos = float2(-1.0, 1.0);
	else if (vid == 2) pos = float2(3.0, 1.0);
	out.uv = pos * 0.5 + 0.5;
	out.uv.y = 1.0 - out.uv.y;
)";

		if (render_upside_down)
		{
			vertex_source <<
				R"(	pos.y = -pos.y;
	)";
		}

		vertex_source <<
			R"(	out.position = float4(pos, 0.0, 1.0);
	return out;
}
)";
		return vertex_source.str();
}

std::string RendererOutputShader::PrependFragmentPreamble(const std::string& shaderSrc)
{
	if (OpenGLRenderer::IsGLES())
	{
		// GLSL ES: no #version 430, needs explicit precision qualifiers, and textureGather
		// (used by the FSR EASU shader below) is core since GLES 3.1 - 320 es covers it.
		return R"(#version 320 es
precision highp float;
precision highp sampler2D;
layout(location = 0) in vec2 passUV;
layout(binding = 0) uniform sampler2D textureSrc;
layout(location = 0) out vec4 colorOut0;

layout (binding = 0, std140)
uniform parameters {
uniform vec2 textureSrcResolution;
uniform vec2 nativeResolution;
uniform vec2 outputResolution;
uniform bool applySRGBEncoding;
uniform float targetGamma;
uniform float displayGamma;
uniform float fsrRcasSharpness;
};

float sRGBEncode(float linear)
{
	if(linear <= 0.0031308)
		return 12.92 * linear;
	else
		return 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
}

vec3 sRGBEncode(vec3 linear)
{
	return vec3(sRGBEncode(linear.r), sRGBEncode(linear.g), sRGBEncode(linear.b));
}

// fwd. declaration
void outputShader();
void main()
{
	outputShader(); // sets colorOut0
	if(applySRGBEncoding)
		colorOut0 = vec4(sRGBEncode(colorOut0.rgb), 1.0);

	if (displayGamma > 0.0)
		colorOut0 = pow(colorOut0, vec4(targetGamma / displayGamma) );
	else
		colorOut0 = vec4( sRGBEncode( pow(colorOut0.rgb, vec3(targetGamma)) ), 1.0);

}

)" + shaderSrc;
	}
	return R"(#version 430
layout(location = 0) smooth in vec2 passUV;
layout(binding = 0) uniform sampler2D textureSrc;
layout(location = 0) out vec4 colorOut0;

#ifdef VULKAN
layout (binding = 1, std140)
#else
layout (binding = 0, std140)
#endif
uniform parameters {
uniform vec2 textureSrcResolution;
uniform vec2 nativeResolution;
uniform vec2 outputResolution;
uniform bool applySRGBEncoding;
uniform float targetGamma;
uniform float displayGamma;
uniform float fsrRcasSharpness;
};

float sRGBEncode(float linear)
{
	if(linear <= 0.0031308)
		return 12.92f * linear;
	else
		return 1.055f * pow(linear, 1.0f / 2.4f) - 0.055f;

}

vec3 sRGBEncode(vec3 linear)
{
	return vec3(sRGBEncode(linear.r), sRGBEncode(linear.g), sRGBEncode(linear.b));
}

// fwd. declaration
void outputShader();
void main()
{
	outputShader(); // sets colorOut0
	if(applySRGBEncoding)
		colorOut0 = vec4(sRGBEncode(colorOut0.rgb), 1.0f);

	if (displayGamma > 0.0f)
		colorOut0 = pow(colorOut0, vec4(targetGamma / displayGamma) );
	else
		colorOut0 = vec4( sRGBEncode( pow(colorOut0.rgb, vec3(targetGamma)) ), 1.0f);

}

)" + shaderSrc;
}
void RendererOutputShader::InitializeStatic()
{
	switch(g_renderer->GetType())
	{
#ifdef ENABLE_METAL
    case RendererAPI::Metal:
    {
        std::string vertex_source = GetMetalVertexSource(false);
        std::string vertex_source_ud = GetMetalVertexSource(true);

       	s_copy_shader = new RendererOutputShader(vertex_source, s_copy_shader_source_mtl);
       	s_copy_shader_ud = new RendererOutputShader(vertex_source_ud, s_copy_shader_source_mtl);

       	s_bicubic_shader = new RendererOutputShader(vertex_source, s_bicubic_shader_source_mtl);
       	s_bicubic_shader_ud = new RendererOutputShader(vertex_source_ud, s_bicubic_shader_source_mtl);

       	s_hermit_shader = new RendererOutputShader(vertex_source, s_hermite_shader_source_mtl);
       	s_hermit_shader_ud = new RendererOutputShader(vertex_source_ud, s_hermite_shader_source_mtl);
		break;
    }
#endif
#ifdef ENABLE_OPENGL
    case RendererAPI::OpenGL:
    {
    	std::string vertex_source, vertex_source_ud;
    	// vertex shader
		vertex_source = GetOpenGlVertexSource(false);
		vertex_source_ud = GetOpenGlVertexSource(true);

    	s_copy_shader = new RendererOutputShader(vertex_source, s_copy_shader_source);
    	s_copy_shader_ud = new RendererOutputShader(vertex_source_ud, s_copy_shader_source);

    	s_bicubic_shader = new RendererOutputShader(vertex_source, s_bicubic_shader_source);
    	s_bicubic_shader_ud = new RendererOutputShader(vertex_source_ud, s_bicubic_shader_source);

    	s_hermit_shader = new RendererOutputShader(vertex_source, s_hermite_shader_source);
    	s_hermit_shader_ud = new RendererOutputShader(vertex_source_ud, s_hermite_shader_source);

    	s_fsr_easu_shader = new RendererOutputShader(vertex_source, s_fsr_easu_shader_source);
    	s_fsr_rcas_shader = new RendererOutputShader(vertex_source, s_fsr_rcas_shader_source);
    	s_fsr_rcas_shader_ud = new RendererOutputShader(vertex_source_ud, s_fsr_rcas_shader_source);
		break;
    }
#endif
#ifdef ENABLE_VULKAN
    case RendererAPI::Vulkan:
    {
    	std::string vertex_source, vertex_source_ud;
    	// vertex shader
		vertex_source = GetVulkanVertexSource(false);
		vertex_source_ud = GetVulkanVertexSource(true);
			
    	s_copy_shader = new RendererOutputShader(vertex_source, s_copy_shader_source);
    	s_copy_shader_ud = new RendererOutputShader(vertex_source_ud, s_copy_shader_source);

    	s_bicubic_shader = new RendererOutputShader(vertex_source, s_bicubic_shader_source);
    	s_bicubic_shader_ud = new RendererOutputShader(vertex_source_ud, s_bicubic_shader_source);

    	s_hermit_shader = new RendererOutputShader(vertex_source, s_hermite_shader_source);
    	s_hermit_shader_ud = new RendererOutputShader(vertex_source_ud, s_hermite_shader_source);

    	s_fsr_easu_shader = new RendererOutputShader(vertex_source, s_fsr_easu_shader_source);
    	s_fsr_rcas_shader = new RendererOutputShader(vertex_source, s_fsr_rcas_shader_source);
    	s_fsr_rcas_shader_ud = new RendererOutputShader(vertex_source_ud, s_fsr_rcas_shader_source);
		break;
    }
#endif
	}
}

void RendererOutputShader::ShutdownStatic()
{
	delete s_copy_shader;
	delete s_copy_shader_ud;

	delete s_bicubic_shader;
	delete s_bicubic_shader_ud;

	delete s_hermit_shader;
	delete s_hermit_shader_ud;

	delete s_fsr_easu_shader;
	delete s_fsr_rcas_shader;
	delete s_fsr_rcas_shader_ud;
}

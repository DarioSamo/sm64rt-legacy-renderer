//
// RT64
//

// Samplers

SamplerState pointWrapWrap : register(s0);
SamplerState pointWrapMirror : register(s1);
SamplerState pointWrapClamp : register(s2);
SamplerState pointMirrorWrap : register(s3);
SamplerState pointMirrorMirror : register(s4);
//SamplerState pointMirrorClamp : register(s5);
SamplerState pointClampWrap : register(s5);
//SamplerState pointClampMirror : register(s7);
SamplerState pointClampClamp : register(s6);
SamplerState linearWrapWrap : register(s7);
SamplerState linearWrapMirror : register(s8);
SamplerState linearWrapClamp : register(s9);
SamplerState linearMirrorWrap : register(s10);
SamplerState linearMirrorMirror : register(s11);
SamplerState linearMirrorClamp : register(s12);
SamplerState linearClampWrap : register(s13);
SamplerState linearClampMirror : register(s14);
SamplerState linearClampClamp : register(s15);

// Functions

float4 SampleTexture(Texture2D<float4> tex2D, float2 uv, int filter, int cms, int cmt) {
	if (filter == 0) {
		if (cms == 0) {
			if (cmt == 0) {
				return tex2D.SampleLevel(pointWrapWrap, uv, 0);
			}
			else if (cmt == 1) {
				return tex2D.SampleLevel(pointWrapMirror, uv, 0);
			}
			else {
				return tex2D.SampleLevel(pointWrapClamp, uv, 0);
			}
		}
		else if (cms == 1) {
			if (cmt == 0) {
				return tex2D.SampleLevel(pointMirrorWrap, uv, 0);
			}
			else if (cmt == 1) {
				return tex2D.SampleLevel(pointMirrorMirror, uv, 0);
			}
			else {
				return float4(1.0f, 0.0f, 1.0f, 1.0f);
				//return tex2D.SampleLevel(pointMirrorClamp, uv, 0);
			}
		}
		else {
			if (cmt == 0) {
				return tex2D.SampleLevel(pointClampWrap, uv, 0);
			}
			else if (cmt == 1) {
				return float4(1.0f, 0.0f, 1.0f, 1.0f);
				//return tex2D.SampleLevel(pointClampMirror, uv, 0);
			}
			else {
				return tex2D.SampleLevel(pointClampClamp, uv, 0);
			}
		}
	}
	else {
		if (cms == 0) {
			if (cmt == 0) {
				return tex2D.SampleLevel(linearWrapWrap, uv, 0);
			}
			else if (cmt == 1) {
				return tex2D.SampleLevel(linearWrapMirror, uv, 0);
			}
			else {
				return tex2D.SampleLevel(linearWrapClamp, uv, 0);
			}
		}
		else if (cms == 1) {
			if (cmt == 0) {
				return tex2D.SampleLevel(linearMirrorWrap, uv, 0);
			}
			else if (cmt == 1) {
				return tex2D.SampleLevel(linearMirrorMirror, uv, 0);
			}
			else {
				return tex2D.SampleLevel(linearMirrorClamp, uv, 0);
			}
		}
		else {
			if (cmt == 0) {
				return tex2D.SampleLevel(linearClampWrap, uv, 0);
			}
			else if (cmt == 1) {
				return tex2D.SampleLevel(linearClampMirror, uv, 0);
			}
			else {
				return tex2D.SampleLevel(linearClampClamp, uv, 0);
			}
		}
	}
}
//
// RT64
//

#include "Random.hlsli"

#define NOISE_SCALE_HEIGHT	240

#define SHADER_0 0
#define SHADER_INPUT_1 1
#define SHADER_INPUT_2 2
#define SHADER_INPUT_3 3
#define SHADER_INPUT_4 4
#define SHADER_TEXEL0 5
#define SHADER_TEXEL0A 6
#define SHADER_TEXEL1 7

struct ColorCombinerInputs {
	float4 input1;
	float4 input2;
	float4 input3;
	float4 input4;
	float4 texVal0;
	float4 texVal1;
};

struct ColorCombinerFeatures {
	int4 c0;
	int4 c1;
	int2 do_single;
	int2 do_multiply;
	int2 do_mix;
	int color_alpha_same;
	int opt_alpha;
	int opt_fog;
	int opt_texture_edge;
	int opt_noise;

	// Align to 16 bytes.
	float _padding;
};

float4 ColorInput(ColorCombinerInputs inputs, int item, bool with_alpha, bool inputs_have_alpha, bool hint_single_element) {
	switch (item) {
	default:
	case SHADER_0:
		return with_alpha ? float4(0.0f, 0.0f, 0.0f, 0.0f) : float4(0.0f, 0.0f, 0.0f, 1.0f);
	case SHADER_INPUT_1:
		return with_alpha || !inputs_have_alpha ? inputs.input1 : float4(inputs.input1.rgb, 1.0f);
	case SHADER_INPUT_2:
		return with_alpha || !inputs_have_alpha ? inputs.input2 : float4(inputs.input2.rgb, 1.0f);
	case SHADER_INPUT_3:
		return with_alpha || !inputs_have_alpha ? inputs.input3 : float4(inputs.input3.rgb, 1.0f);
	case SHADER_INPUT_4:
		return with_alpha || !inputs_have_alpha ? inputs.input4 : float4(inputs.input4.rgb, 1.0f);
	case SHADER_TEXEL0:
		return with_alpha ? inputs.texVal0 : float4(inputs.texVal0.rgb, 1.0f);
	case SHADER_TEXEL0A:
		return hint_single_element ? 
			float4(inputs.texVal0.a, inputs.texVal0.a, inputs.texVal0.a, inputs.texVal0.a)
		: 
			(with_alpha ? 
				float4(inputs.texVal0.a, inputs.texVal0.a, inputs.texVal0.a, inputs.texVal0.a) 
			: 
				float4(inputs.texVal0.a, inputs.texVal0.a, inputs.texVal0.a, 1.0f));

	case SHADER_TEXEL1:
		return with_alpha ? inputs.texVal1 : float4(inputs.texVal1.rgb, 1.0f);
	}

	return float4(0.0f, 0.0f, 0.0f, 1.0f);
}

float AlphaInput(ColorCombinerInputs inputs, int item, bool with_alpha, bool inputs_have_alpha, bool hint_single_element) {
	switch (item) {
	default:
	case SHADER_0:
		return 0.0f;
	case SHADER_INPUT_1:
		return inputs.input1.a;
	case SHADER_INPUT_2:
		return inputs.input2.a;
	case SHADER_INPUT_3:
		return inputs.input3.a;
	case SHADER_INPUT_4:
		return inputs.input4.a;
	case SHADER_TEXEL0:
		return inputs.texVal0.a;
	case SHADER_TEXEL0A:
		return inputs.texVal0.a;
	case SHADER_TEXEL1:
		return inputs.texVal1.a;
	}

	return 0.0f;
}

float4 ColorFormula(ColorCombinerFeatures cc, ColorCombinerInputs inputs, bool do_single, bool do_multiply, bool do_mix, bool with_alpha, bool opt_alpha) {
	float4 result = float4(0.0f, 0.0f, 0.0f, 1.0f);
	if (do_single) {
		result = ColorInput(inputs, cc.c0[3], with_alpha, opt_alpha, false);
	} 
	else if (do_multiply) {
		result = ColorInput(inputs, cc.c0[0], with_alpha, opt_alpha, false) * ColorInput(inputs, cc.c0[2], with_alpha, opt_alpha, true);
	} 
	else if (do_mix) {
		result = lerp(ColorInput(inputs, cc.c0[1], with_alpha, opt_alpha, false), ColorInput(inputs, cc.c0[0], with_alpha, opt_alpha, false), ColorInput(inputs, cc.c0[2], with_alpha, opt_alpha, true));
	} 
	else {
		result = 
			(ColorInput(inputs, cc.c0[0], with_alpha, opt_alpha, false) - ColorInput(inputs, cc.c0[1], with_alpha, opt_alpha, false)) * 
			ColorInput(inputs, cc.c0[2], with_alpha, opt_alpha, true).r + 
			ColorInput(inputs, cc.c0[3], with_alpha, opt_alpha, false);
	}

	return result;
}

float AlphaFormula(ColorCombinerFeatures cc, ColorCombinerInputs inputs, bool do_single, bool do_multiply, bool do_mix, bool with_alpha, bool opt_alpha) {
	float result = 0.0f;
	if (do_single) {
		result = AlphaInput(inputs, cc.c1[3], with_alpha, opt_alpha, false);
	}
	else if (do_multiply) {
		result = AlphaInput(inputs, cc.c1[0], with_alpha, opt_alpha, false) * AlphaInput(inputs, cc.c1[2], with_alpha, opt_alpha, true);
	}
	else if (do_mix) {
		result = lerp(AlphaInput(inputs, cc.c1[1], with_alpha, opt_alpha, false), AlphaInput(inputs, cc.c1[0], with_alpha, opt_alpha, false), AlphaInput(inputs, cc.c1[2], with_alpha, opt_alpha, true));
	}
	else {
		result = 
			(AlphaInput(inputs, cc.c1[0], with_alpha, opt_alpha, false) - AlphaInput(inputs, cc.c1[1], with_alpha, opt_alpha, false)) * 
			AlphaInput(inputs, cc.c1[2], with_alpha, opt_alpha, true) + 
			AlphaInput(inputs, cc.c1[3], with_alpha, opt_alpha, false);
	}

	return result;
}

float4 CombineColors(ColorCombinerFeatures cc, ColorCombinerInputs inputs, uint seed) {
	float4 result = float4(0.0f, 0.0f, 0.0f, 1.0f);
	if (!cc.color_alpha_same && cc.opt_alpha) {
		result = float4(ColorFormula(cc, inputs, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], false, true).rgb, AlphaFormula(cc, inputs, cc.do_single[1], cc.do_multiply[1], cc.do_mix[1], true, true));
	}
	else {
		result = ColorFormula(cc, inputs, cc.do_single[0], cc.do_multiply[0], cc.do_mix[0], cc.opt_alpha, cc.opt_alpha);
	}

	if (cc.opt_noise) {
		float noiseAlpha = round(nextRand(seed));
		result.a *= noiseAlpha;
	}

	/* SUPPORT TEXTURE EDGE?
	if (cc.opt_texture_edge && cc.opt_alpha) {
		append_line(buf, &len, "if (texel.a > 0.3) texel.a = 1.0; else discard;");
	}
	*/

	return result;
}
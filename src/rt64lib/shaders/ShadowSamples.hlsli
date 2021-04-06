//
// RT64
//

static const float2 ShadowSamples[128] = {
	float2(0.0f, 0.0f),
	float2(1.0f, 0.0f),
	float2(0.0f, 1.0f),
	float2(-1.0f, 0.0f),
	float2(0.0f, 1.0f),
	float2(0.478712,0.875764),
	float2(-0.337956,-0.793959),
	float2(-0.955259,-0.028164),
	float2(0.864527,0.325689),
	float2(0.209342,-0.395657),
	float2(-0.106779,0.672585),
	float2(0.156213,0.235113),
	float2(-0.413644,-0.082856),
	float2(-0.415667,0.323909),
	float2(0.141896,-0.939980),
	float2(0.954932,-0.182516),
	float2(-0.766184,0.410799),
	float2(-0.434912,-0.458845),
	float2(0.415242,-0.078724),
	float2(0.728335,-0.491777),
	float2(-0.058086,-0.066401),
	float2(0.202990,0.686837),
	float2(-0.808362,-0.556402),
	float2(0.507386,-0.640839),
	float2(-0.723494,-0.229240),
	float2(0.489740,0.317826),
	float2(-0.622663,0.765301),
	float2(-0.010640,0.929347),
	float2(0.663146,0.647618),
	float2(-0.096674,-0.413835),
	float2(0.525945,-0.321063),
	float2(-0.122533,0.366019),
	float2(0.195235,-0.687983),
	float2(-0.563203,0.098748),
	float2(0.418563,0.561335),
	float2(-0.378595,0.800367),
	float2(0.826922,0.001024),
	float2(-0.085372,-0.766651),
	float2(-0.921920,0.183673),
	float2(-0.590008,-0.721799),
	float2(0.167751,-0.164393),
	float2(0.032961,-0.562530),
	float2(0.632900,-0.107059),
	float2(-0.464080,0.569669),
	float2(-0.173676,-0.958758),
	float2(-0.242648,-0.234303),
	float2(-0.275362,0.157163),
	float2(0.382295,-0.795131),
	float2(0.562955,0.115562),
	float2(0.190586,0.470121),
	float2(0.770764,-0.297576),
	float2(0.237281,0.931050),
	float2(-0.666642,-0.455871),
	float2(-0.905649,-0.298379),
	float2(0.339520,0.157829),
	float2(0.701438,-0.704100),
	float2(-0.062758,0.160346),
	float2(-0.220674,0.957141),
	float2(0.642692,0.432706),
	float2(-0.773390,-0.015272),
	float2(-0.671467,0.246880),
	float2(0.158051,0.062859),
	float2(0.806009,0.527232),
	float2(-0.057620,-0.247071),
	float2(-0.684726,0.886653),
	float2(-0.005871,-0.507466),
	float2(0.968117,-0.881211),
	float2(-0.331882,-0.486458),
	float2(0.121406,0.310214),
	float2(-0.846279,0.263863),
	float2(0.648677,0.242216),
	float2(-0.284201,0.500180),
	float2(0.765039,-0.902279),
	float2(-0.752769,0.352473),
	float2(-0.775969,0.558147),
	float2(0.676931,-0.731825),
	float2(0.918432,-0.941064),
	float2(0.360640,-0.506660),
	float2(-0.229817,0.178272),
	float2(-0.587235,-0.995212),
	float2(-0.936073,-0.258163),
	float2(0.467225,-0.643335),
	float2(-0.560923,0.670039),
	float2(-0.152846,-0.745311),
	float2(0.862429,-0.663706),
	float2(-0.464844,0.014992),
	float2(0.922357,0.868972),
	float2(-0.947883,0.935868),
	float2(-0.086317,0.987543),
	float2(0.364811,0.813263),
	float2(0.226991,-0.697185),
	float2(-0.289739,0.993347),
	float2(0.386027,0.410250),
	float2(0.376464,-0.208795),
	float2(-0.053501,0.155123),
	float2(0.797020,0.989451),
	float2(-0.747605,-0.289618),
	float2(0.899774,-0.604678),
	float2(-0.745703,0.984002),
	float2(-0.546981,-0.012226),
	float2(0.748658,-0.167848),
	float2(0.279648,0.813114),
	float2(0.183272,-0.535544),
	float2(-0.975190,0.348742),
	float2(-0.134461,0.443494),
	float2(-0.053425,0.750286),
	float2(0.157293,-0.908211),
	float2(0.843724,-0.913467),
	float2(-0.891244,0.863804),
	float2(-0.769858,-0.928254),
	float2(0.128160,0.155634),
	float2(-0.386101,-0.685759),
	float2(0.862378,0.232363),
	float2(-0.044777,0.419829),
	float2(0.141333,-0.763607),
	float2(0.073785,0.721567),
	float2(-0.806452,-0.372354),
	float2(0.656011,-0.843561),
	float2(-0.512672,0.282497),
	float2(0.391904,-0.272665),
	float2(-0.337777,0.429540),
	float2(0.458997,-0.477167),
	float2(0.746163,-0.609705),
	float2(0.037061,-0.718591),
	float2(0.687189,0.204987),
	float2(-0.586774,-0.193371),
	float2(0.482926,0.397171),
	float2(-0.505806,0.283079)
};
//
// RT64
//

#pragma once

#include "rt64_common.h"

namespace RT64 {
	class Device;

	class Shader {
	public:
		enum class Filter : int {
			Point,
			Linear
		};

		enum class AddressingMode : int {
			Wrap,
			Mirror,
			Clamp
		};

		struct HitGroup {
			IDxcBlob *shaderBlob;
			std::wstring hitGroupName;
			std::wstring closestHitName;
			std::wstring anyHitName;
			ID3D12RootSignature *rootSignature;
			void *id;
		};
	private:
		Device *device;
		HitGroup surfaceHitGroup;
		HitGroup shadowHitGroup;

		unsigned int uniqueSamplerRegisterIndex(Filter filter, AddressingMode hAddr, AddressingMode vAddr);
		void generateSurfaceHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &hitGroupName, const std::string &closestHitName, const std::string &anyHitName);
		void generateShadowHitGroup(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &hitGroupName, const std::string &closestHitName, const std::string &anyHitName);
		void compileShaderCode(const std::string &shaderCode, IDxcBlob **shaderBlob);
	public:
		Shader(Device *device, unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr);
		~Shader();
		HitGroup &getSurfaceHitGroup();
		HitGroup &getShadowHitGroup();
	};
};
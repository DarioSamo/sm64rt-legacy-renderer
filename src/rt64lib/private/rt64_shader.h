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
	private:
		Device *device;
		IDxcBlob *shaderBlob;
		std::vector<std::wstring> shaderSymbols;
		std::wstring hitGroupName;
		std::wstring closestHitName;
		std::wstring anyHitName;
		ID3D12RootSignature *rootSignature;

		unsigned int uniqueSamplerRegisterIndex(Filter filter, AddressingMode hAddr, AddressingMode vAddr);
		void generateShader(unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr, const std::string &closestHitName, const std::string &anyHitName);
		void compileShaderCode(const std::string &shaderCode);
		void generateRootSignature(Filter filter, AddressingMode hAddr, AddressingMode vAddr, unsigned int samplerRegisterIndex);
	public:
		Shader(Device *device, unsigned int shaderId, Filter filter, AddressingMode hAddr, AddressingMode vAddr);
		~Shader();
		IDxcBlob *getBlob() const;
		const std::vector<std::wstring> &getSymbolExports() const;
		const std::wstring &getHitGroupName() const;
		const std::wstring &getClosestHitName() const;
		const std::wstring &getAnyHitName() const;
		ID3D12RootSignature *getRootSignature() const;
	};
};
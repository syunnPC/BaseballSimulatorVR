#pragma once

#if defined(_WIN64)

#include "../Framework.hpp"

#include <d3dcompiler.h>
#include <cstring>
#include <wrl/client.h>

namespace BaseballSimulatorVR::Common
{
	[[nodiscard]] inline bool CompileD3d12Shader(
	    const char* const source,
	    const char* const entryPoint,
	    const char* const target,
	    Microsoft::WRL::ComPtr<ID3DBlob>* const bytecode)
	{
		if ((source == nullptr) || (entryPoint == nullptr) ||
		    (target == nullptr) || (bytecode == nullptr))
		{
			return false;
		}

		UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
		compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		Microsoft::WRL::ComPtr<ID3DBlob> shaderBytecode{};
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob{};
		const HRESULT compileResult = D3DCompile(
		    source,
		    std::strlen(source),
		    nullptr,
		    nullptr,
		    nullptr,
		    entryPoint,
		    target,
		    compileFlags,
		    0,
		    &shaderBytecode,
		    &errorBlob);
		if (FAILED(compileResult))
		{
			if ((errorBlob != nullptr) && (errorBlob->GetBufferPointer() != nullptr))
			{
				OutputDebugStringA(
				    static_cast<const char*>(errorBlob->GetBufferPointer()));
			}
			return false;
		}

		*bytecode = shaderBytecode;
		return true;
	}
} // namespace BaseballSimulatorVR::Common

#endif

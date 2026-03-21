#pragma once

#if defined(_WIN64)

#include "D3d12ShaderCompiler.hpp"
#include "FxaaShaderSource.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace BaseballSimulatorVR::Common
{
	constexpr UINT FxaaParameterCount = 5U;

	struct FxaaPipelineObjects final
	{
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature{};
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState{};
	};

	[[nodiscard]] inline bool BuildFxaaPipeline(
	    ID3D12Device* const device,
	    const DXGI_FORMAT renderTargetFormat,
	    const UINT sampleCount,
	    FxaaPipelineObjects* const fxaaObjects)
	{
		if ((device == nullptr) || (fxaaObjects == nullptr))
		{
			return false;
		}

		D3D12_DESCRIPTOR_RANGE srvRange{};
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors = 1;
		srvRange.BaseShaderRegister = 0;
		srvRange.RegisterSpace = 0;
		srvRange.OffsetInDescriptorsFromTableStart =
		    D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER rootParameters[2]{};
		rootParameters[0].ParameterType =
		    D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[0].DescriptorTable.pDescriptorRanges = &srvRange;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[1].ParameterType =
		    D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParameters[1].Constants.ShaderRegister = 0;
		rootParameters[1].Constants.RegisterSpace = 0;
		rootParameters[1].Constants.Num32BitValues = FxaaParameterCount;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC staticSampler{};
		staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.MipLODBias = 0.0F;
		staticSampler.MaxAnisotropy = 1U;
		staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		staticSampler.MinLOD = 0.0F;
		staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
		staticSampler.ShaderRegister = 0;
		staticSampler.RegisterSpace = 0;
		staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription{};
		rootSignatureDescription.NumParameters =
		    static_cast<UINT>(std::size(rootParameters));
		rootSignatureDescription.pParameters = rootParameters;
		rootSignatureDescription.NumStaticSamplers = 1;
		rootSignatureDescription.pStaticSamplers = &staticSampler;
		rootSignatureDescription.Flags =
		    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSignature{};
		Microsoft::WRL::ComPtr<ID3DBlob> rootErrorBlob{};
		if (FAILED(D3D12SerializeRootSignature(
		        &rootSignatureDescription,
		        D3D_ROOT_SIGNATURE_VERSION_1,
		        &serializedRootSignature,
		        &rootErrorBlob)))
		{
			if ((rootErrorBlob != nullptr) &&
			    (rootErrorBlob->GetBufferPointer() != nullptr))
			{
				OutputDebugStringA(
				    static_cast<const char*>(rootErrorBlob->GetBufferPointer()));
			}
			return false;
		}

		FxaaPipelineObjects builtObjects{};
		if (FAILED(device->CreateRootSignature(
		        0,
		        serializedRootSignature->GetBufferPointer(),
		        serializedRootSignature->GetBufferSize(),
		        IID_PPV_ARGS(&builtObjects.rootSignature))))
		{
			return false;
		}

		Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderBytecode{};
		if (!CompileD3d12Shader(
		        FxaaShaderSource::VertexShaderSource,
		        "main",
		        "vs_5_0",
		        &vertexShaderBytecode))
		{
			return false;
		}

		Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderBytecode{};
		if (!CompileD3d12Shader(
		        FxaaShaderSource::PixelShaderSource,
		        "main",
		        "ps_5_0",
		        &pixelShaderBytecode))
		{
			return false;
		}

		D3D12_BLEND_DESC blendDescription{};
		blendDescription.AlphaToCoverageEnable = FALSE;
		blendDescription.IndependentBlendEnable = FALSE;
		for (D3D12_RENDER_TARGET_BLEND_DESC& renderTargetBlend :
		     blendDescription.RenderTarget)
		{
			renderTargetBlend.BlendEnable = FALSE;
			renderTargetBlend.LogicOpEnable = FALSE;
			renderTargetBlend.SrcBlend = D3D12_BLEND_ONE;
			renderTargetBlend.DestBlend = D3D12_BLEND_ZERO;
			renderTargetBlend.BlendOp = D3D12_BLEND_OP_ADD;
			renderTargetBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
			renderTargetBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
			renderTargetBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			renderTargetBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
			renderTargetBlend.RenderTargetWriteMask =
			    D3D12_COLOR_WRITE_ENABLE_ALL;
		}

		D3D12_RASTERIZER_DESC rasterizerDescription{};
		rasterizerDescription.FillMode = D3D12_FILL_MODE_SOLID;
		rasterizerDescription.CullMode = D3D12_CULL_MODE_NONE;
		rasterizerDescription.FrontCounterClockwise = FALSE;
		rasterizerDescription.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		rasterizerDescription.DepthBiasClamp =
		    D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		rasterizerDescription.SlopeScaledDepthBias =
		    D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		rasterizerDescription.DepthClipEnable = TRUE;
		rasterizerDescription.MultisampleEnable = FALSE;
		rasterizerDescription.AntialiasedLineEnable = FALSE;
		rasterizerDescription.ForcedSampleCount = 0;
		rasterizerDescription.ConservativeRaster =
		    D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		D3D12_DEPTH_STENCIL_DESC depthStencilDescription{};
		depthStencilDescription.DepthEnable = FALSE;
		depthStencilDescription.StencilEnable = FALSE;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDescription{};
		pipelineStateDescription.pRootSignature =
		    builtObjects.rootSignature.Get();
		pipelineStateDescription.VS = D3D12_SHADER_BYTECODE{
			vertexShaderBytecode->GetBufferPointer(),
			vertexShaderBytecode->GetBufferSize()
		};
		pipelineStateDescription.PS = D3D12_SHADER_BYTECODE{
			pixelShaderBytecode->GetBufferPointer(),
			pixelShaderBytecode->GetBufferSize()
		};
		pipelineStateDescription.BlendState = blendDescription;
		pipelineStateDescription.SampleMask = UINT_MAX;
		pipelineStateDescription.RasterizerState = rasterizerDescription;
		pipelineStateDescription.DepthStencilState = depthStencilDescription;
		pipelineStateDescription.InputLayout = D3D12_INPUT_LAYOUT_DESC{
			nullptr,
			0
		};
		pipelineStateDescription.IBStripCutValue =
		    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
		pipelineStateDescription.PrimitiveTopologyType =
		    D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipelineStateDescription.NumRenderTargets = 1;
		pipelineStateDescription.RTVFormats[0] = renderTargetFormat;
		pipelineStateDescription.DSVFormat = DXGI_FORMAT_UNKNOWN;
		pipelineStateDescription.SampleDesc.Count = (std::max)(1U, sampleCount);
		pipelineStateDescription.SampleDesc.Quality = 0;

		if (FAILED(device->CreateGraphicsPipelineState(
		        &pipelineStateDescription,
		        IID_PPV_ARGS(&builtObjects.pipelineState))))
		{
			return false;
		}

		*fxaaObjects = std::move(builtObjects);
		return true;
	}
} // namespace BaseballSimulatorVR::Common

#endif

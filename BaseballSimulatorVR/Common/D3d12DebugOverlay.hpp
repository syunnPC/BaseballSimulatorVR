#pragma once

#include "../Framework.hpp"
#include "DebugWorldProjection.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#if defined(_WIN64)
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#endif

namespace BaseballSimulatorVR::Common::DebugOverlay
{
	inline constexpr char ShaderSource[] = R"(
struct VSInput
{
	float3 position : POSITION;
	float4 color : COLOR;
};

struct PSInput
{
	float4 position : SV_Position;
	float4 color : COLOR;
};

PSInput VSMain(VSInput inputVertex)
{
	PSInput outputVertex;
	outputVertex.position = float4(inputVertex.position, 1.0f);
	outputVertex.color = inputVertex.color;
	return outputVertex;
}

float4 PSMain(PSInput inputPixel) : SV_Target
{
	return inputPixel.color;
}
)";

	struct Vertex final
	{
		float position[3];
		float color[4];
	};

	struct ScreenSpacePoint final
	{
		double x{ 0.0 };
		double y{ 0.0 };
		float depth{ 0.0F };
	};

	[[nodiscard]] inline float ToNdcX(const double pixelX, const LONG width) noexcept
	{
		const LONG safeWidth = (std::max)(1L, width - 1);
		const double normalizedX = pixelX / static_cast<double>(safeWidth);
		return static_cast<float>((normalizedX * 2.0) - 1.0);
	}

	[[nodiscard]] inline float ToNdcY(const double pixelY, const LONG height) noexcept
	{
		const LONG safeHeight = (std::max)(1L, height - 1);
		const double normalizedY = pixelY / static_cast<double>(safeHeight);
		return static_cast<float>(1.0 - (normalizedY * 2.0));
	}

	[[nodiscard]] inline Vertex BuildVertex(
	    const ScreenSpacePoint& point,
	    const float* const color,
	    const LONG width,
	    const LONG height)
	{
		Vertex vertex{};
		vertex.position[0] = ToNdcX(point.x, width);
		vertex.position[1] = ToNdcY(point.y, height);
		vertex.position[2] = std::clamp(point.depth, 0.0F, 1.0F);
		vertex.color[0] = color[0];
		vertex.color[1] = color[1];
		vertex.color[2] = color[2];
		vertex.color[3] = color[3];
		return vertex;
	}

	inline void AppendTriangle(
	    std::vector<Vertex>* const vertices,
	    const ScreenSpacePoint& p0,
	    const ScreenSpacePoint& p1,
	    const ScreenSpacePoint& p2,
	    const float* const color,
	    const LONG width,
	    const LONG height)
	{
		if (vertices == nullptr)
		{
			return;
		}

		vertices->push_back(BuildVertex(p0, color, width, height));
		vertices->push_back(BuildVertex(p1, color, width, height));
		vertices->push_back(BuildVertex(p2, color, width, height));
	}

	inline void AppendQuad(
	    std::vector<Vertex>* const vertices,
	    const ScreenSpacePoint& p0,
	    const ScreenSpacePoint& p1,
	    const ScreenSpacePoint& p2,
	    const ScreenSpacePoint& p3,
	    const float* const color,
	    const LONG width,
	    const LONG height)
	{
		AppendTriangle(vertices, p0, p1, p2, color, width, height);
		AppendTriangle(vertices, p0, p2, p3, color, width, height);
	}

	inline void AppendScreenTriangle(
	    std::vector<Vertex>* const vertices,
	    const ScreenPoint& p0,
	    const ScreenPoint& p1,
	    const ScreenPoint& p2,
	    const float* const color,
	    const LONG width,
	    const LONG height)
	{
		AppendTriangle(
		    vertices,
		    ScreenSpacePoint{
		        p0.x,
		        p0.y,
		        p0.depth },
		    ScreenSpacePoint{
		        p1.x,
		        p1.y,
		        p1.depth },
		    ScreenSpacePoint{
		        p2.x,
		        p2.y,
		        p2.depth },
		    color,
		    width,
		    height);
	}

	inline void AppendMarkerQuad(
	    std::vector<Vertex>* const vertices,
	    const double centerX,
	    const double centerY,
	    const LONG halfSize,
	    const float* const color,
	    const LONG width,
	    const LONG height,
	    const float depth = 0.0F)
	{
		const double left = centerX - static_cast<double>(halfSize);
		const double top = centerY - static_cast<double>(halfSize);
		const double right = centerX + static_cast<double>(halfSize) + 1.0;
		const double bottom = centerY + static_cast<double>(halfSize) + 1.0;
		const ScreenSpacePoint p0{ left, top, depth };
		const ScreenSpacePoint p1{ right, top, depth };
		const ScreenSpacePoint p2{ right, bottom, depth };
		const ScreenSpacePoint p3{ left, bottom, depth };
		AppendQuad(vertices, p0, p1, p2, p3, color, width, height);
	}

	inline void AppendThickScreenLine(
	    std::vector<Vertex>* const vertices,
	    const ScreenPoint& startPoint,
	    const ScreenPoint& endPoint,
	    const LONG halfSize,
	    const float* const color,
	    const LONG width,
	    const LONG height)
	{
		const double startX = startPoint.x;
		const double startY = startPoint.y;
		const double endX = endPoint.x;
		const double endY = endPoint.y;
		const double deltaX = endX - startX;
		const double deltaY = endY - startY;
		const double length = std::sqrt((deltaX * deltaX) + (deltaY * deltaY));
		if (length < 1e-4)
		{
			AppendMarkerQuad(
			    vertices,
			    startPoint.x,
			    startPoint.y,
			    halfSize,
			    color,
			    width,
			    height,
			    startPoint.depth);
			return;
		}

		const double thickness = static_cast<double>(halfSize) + 0.5;
		const double offsetX = (-deltaY / length) * thickness;
		const double offsetY = (deltaX / length) * thickness;
		const ScreenSpacePoint p0{ startX + offsetX, startY + offsetY, startPoint.depth };
		const ScreenSpacePoint p1{ startX - offsetX, startY - offsetY, startPoint.depth };
		const ScreenSpacePoint p2{ endX - offsetX, endY - offsetY, endPoint.depth };
		const ScreenSpacePoint p3{ endX + offsetX, endY + offsetY, endPoint.depth };
		AppendQuad(vertices, p0, p1, p2, p3, color, width, height);
	}

#if defined(_WIN64)
	enum class PipelineBlendMode
	{
		Opaque,
		AlphaBlend
	};

	enum class PipelineDepthWriteMode
	{
		Enabled,
		Disabled
	};

	enum class PipelineInitializationPhase
	{
		None,
		InvalidArguments,
		SerializeRootSignature,
		CreateRootSignature,
		CompileVertexShader,
		CompilePixelShader,
		CreatePipelineState
	};

	enum class VertexBufferInitializationPhase
	{
		None,
		InvalidArguments,
		CreateCommittedResource
	};

	enum class VertexUploadPhase
	{
		None,
		InvalidArguments,
		Map
	};

	enum class DrawCommandValidationPhase
	{
		None,
		InvalidArguments,
		VertexRange
	};

	inline void SetPipelineInitializationFailure(
	    const PipelineInitializationPhase phase,
	    const HRESULT result,
	    PipelineInitializationPhase* const failedPhase,
	    HRESULT* const failedResult) noexcept
	{
		if (failedPhase != nullptr)
		{
			*failedPhase = phase;
		}

		if (failedResult != nullptr)
		{
			*failedResult = result;
		}
	}

	inline void SetVertexBufferInitializationFailure(
	    const VertexBufferInitializationPhase phase,
	    const HRESULT result,
	    VertexBufferInitializationPhase* const failedPhase,
	    HRESULT* const failedResult) noexcept
	{
		if (failedPhase != nullptr)
		{
			*failedPhase = phase;
		}

		if (failedResult != nullptr)
		{
			*failedResult = result;
		}
	}

	inline void SetVertexUploadFailure(
	    const VertexUploadPhase phase,
	    const HRESULT result,
	    VertexUploadPhase* const failedPhase,
	    HRESULT* const failedResult) noexcept
	{
		if (failedPhase != nullptr)
		{
			*failedPhase = phase;
		}

		if (failedResult != nullptr)
		{
			*failedResult = result;
		}
	}

	[[nodiscard]] inline bool CompileShader(
	    const char* const sourceCode,
	    const char* const entryPoint,
	    const char* const targetProfile,
	    Microsoft::WRL::ComPtr<ID3DBlob>* const shaderBlob)
	{
		if ((sourceCode == nullptr) || (entryPoint == nullptr) ||
		    (targetProfile == nullptr) || (shaderBlob == nullptr))
		{
			return false;
		}

		UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
		compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
		Microsoft::WRL::ComPtr<ID3DBlob> compiledShader{};
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob{};
		const HRESULT compileResult = D3DCompile(
		    sourceCode,
		    std::strlen(sourceCode),
		    nullptr,
		    nullptr,
		    nullptr,
		    entryPoint,
		    targetProfile,
		    compileFlags,
		    0,
		    &compiledShader,
		    &errorBlob);
		if (FAILED(compileResult))
		{
			if ((errorBlob != nullptr) && (errorBlob->GetBufferSize() > 0U))
			{
				OutputDebugStringA(
				    static_cast<const char*>(errorBlob->GetBufferPointer()));
			}
			return false;
		}

		*shaderBlob = compiledShader;
		return true;
	}

	[[nodiscard]] inline bool InitializePipeline(
	    ID3D12Device* const device,
	    const DXGI_FORMAT renderTargetFormat,
	    const DXGI_FORMAT depthStencilFormat,
	    Microsoft::WRL::ComPtr<ID3D12RootSignature>* const rootSignature,
	    Microsoft::WRL::ComPtr<ID3D12PipelineState>* const pipelineState,
	    PipelineInitializationPhase* const failedPhase = nullptr,
	    HRESULT* const failedResult = nullptr,
	    const PipelineBlendMode blendMode = PipelineBlendMode::Opaque,
	    const PipelineDepthWriteMode depthWriteMode = PipelineDepthWriteMode::Enabled,
	    const bool reuseRootSignature = false,
	    const UINT sampleCount = 1U)
	{
		SetPipelineInitializationFailure(
		    PipelineInitializationPhase::None,
		    S_OK,
		    failedPhase,
		    failedResult);
		if ((device == nullptr) || (rootSignature == nullptr) ||
		    (pipelineState == nullptr) ||
		    (reuseRootSignature && (rootSignature->Get() == nullptr)))
		{
			SetPipelineInitializationFailure(
			    PipelineInitializationPhase::InvalidArguments,
			    E_INVALIDARG,
			    failedPhase,
			    failedResult);
			return false;
		}

		pipelineState->Reset();
		if (!reuseRootSignature)
		{
			rootSignature->Reset();
		}

		if (rootSignature->Get() == nullptr)
		{
			D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription{};
			rootSignatureDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

			Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSignature{};
			Microsoft::WRL::ComPtr<ID3DBlob> rootSignatureErrorBlob{};
			const HRESULT serializeResult = D3D12SerializeRootSignature(
			    &rootSignatureDescription,
			    D3D_ROOT_SIGNATURE_VERSION_1,
			    &serializedRootSignature,
			    &rootSignatureErrorBlob);
			if (FAILED(serializeResult))
			{
				if ((rootSignatureErrorBlob != nullptr) &&
				    (rootSignatureErrorBlob->GetBufferSize() > 0U))
				{
					OutputDebugStringA(
					    static_cast<const char*>(rootSignatureErrorBlob->GetBufferPointer()));
				}
				SetPipelineInitializationFailure(
				    PipelineInitializationPhase::SerializeRootSignature,
				    serializeResult,
				    failedPhase,
				    failedResult);
				return false;
			}

			ID3D12RootSignature* rawRootSignature = nullptr;
			const HRESULT rootSignatureResult = device->CreateRootSignature(
			    0,
			    serializedRootSignature->GetBufferPointer(),
			    serializedRootSignature->GetBufferSize(),
			    IID_PPV_ARGS(&rawRootSignature));
			if (FAILED(rootSignatureResult))
			{
				SetPipelineInitializationFailure(
				    PipelineInitializationPhase::CreateRootSignature,
				    rootSignatureResult,
				    failedPhase,
				    failedResult);
				return false;
			}
			rootSignature->Attach(rawRootSignature);
		}

		Microsoft::WRL::ComPtr<ID3DBlob> vertexShader{};
		Microsoft::WRL::ComPtr<ID3DBlob> pixelShader{};
		if (!CompileShader(ShaderSource, "VSMain", "vs_5_0", &vertexShader))
		{
			SetPipelineInitializationFailure(
			    PipelineInitializationPhase::CompileVertexShader,
			    E_FAIL,
			    failedPhase,
			    failedResult);
			return false;
		}

		if (!CompileShader(ShaderSource, "PSMain", "ps_5_0", &pixelShader))
		{
			SetPipelineInitializationFailure(
			    PipelineInitializationPhase::CompilePixelShader,
			    E_FAIL,
			    failedPhase,
			    failedResult);
			return false;
		}

		const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		const UINT resolvedSampleCount = (std::max)(1U, sampleCount);

		D3D12_BLEND_DESC blendDescription{};
		blendDescription.AlphaToCoverageEnable = FALSE;
		blendDescription.IndependentBlendEnable = FALSE;
		const bool enableAlphaBlend = blendMode == PipelineBlendMode::AlphaBlend;
		D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDescription{};
		renderTargetBlendDescription.BlendEnable = enableAlphaBlend ? TRUE : FALSE;
		renderTargetBlendDescription.LogicOpEnable = FALSE;
		renderTargetBlendDescription.SrcBlend = enableAlphaBlend
		                                            ? D3D12_BLEND_SRC_ALPHA
		                                            : D3D12_BLEND_ONE;
		renderTargetBlendDescription.DestBlend = enableAlphaBlend
		                                             ? D3D12_BLEND_INV_SRC_ALPHA
		                                             : D3D12_BLEND_ZERO;
		renderTargetBlendDescription.BlendOp = D3D12_BLEND_OP_ADD;
		renderTargetBlendDescription.SrcBlendAlpha = D3D12_BLEND_ONE;
		renderTargetBlendDescription.DestBlendAlpha = enableAlphaBlend
		                                                  ? D3D12_BLEND_INV_SRC_ALPHA
		                                                  : D3D12_BLEND_ZERO;
		renderTargetBlendDescription.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		renderTargetBlendDescription.LogicOp = D3D12_LOGIC_OP_NOOP;
		renderTargetBlendDescription.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		blendDescription.RenderTarget[0] = renderTargetBlendDescription;

		D3D12_RASTERIZER_DESC rasterizerDescription{};
		rasterizerDescription.FillMode = D3D12_FILL_MODE_SOLID;
		rasterizerDescription.CullMode = D3D12_CULL_MODE_NONE;
		rasterizerDescription.FrontCounterClockwise = FALSE;
		rasterizerDescription.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		rasterizerDescription.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		rasterizerDescription.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		rasterizerDescription.DepthClipEnable = TRUE;
		rasterizerDescription.MultisampleEnable =
		    (resolvedSampleCount > 1U) ? TRUE : FALSE;
		rasterizerDescription.AntialiasedLineEnable = FALSE;
		rasterizerDescription.ForcedSampleCount = 0;
		rasterizerDescription.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		D3D12_DEPTH_STENCIL_DESC depthStencilDescription{};
		const bool enableDepth = depthStencilFormat != DXGI_FORMAT_UNKNOWN;
		const bool enableDepthWrite = depthWriteMode == PipelineDepthWriteMode::Enabled;
		depthStencilDescription.DepthEnable = enableDepth ? TRUE : FALSE;
		depthStencilDescription.DepthWriteMask = (enableDepth && enableDepthWrite)
		                                             ? D3D12_DEPTH_WRITE_MASK_ALL
		                                             : D3D12_DEPTH_WRITE_MASK_ZERO;
		depthStencilDescription.DepthFunc = enableDepth
		                                        ? D3D12_COMPARISON_FUNC_LESS_EQUAL
		                                        : D3D12_COMPARISON_FUNC_ALWAYS;
		depthStencilDescription.StencilEnable = FALSE;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDescription{};
		pipelineStateDescription.pRootSignature = rootSignature->Get();
		pipelineStateDescription.VS = D3D12_SHADER_BYTECODE{
			vertexShader->GetBufferPointer(),
			vertexShader->GetBufferSize()
		};
		pipelineStateDescription.PS = D3D12_SHADER_BYTECODE{
			pixelShader->GetBufferPointer(),
			pixelShader->GetBufferSize()
		};
		pipelineStateDescription.BlendState = blendDescription;
		pipelineStateDescription.SampleMask = UINT_MAX;
		pipelineStateDescription.RasterizerState = rasterizerDescription;
		pipelineStateDescription.DepthStencilState = depthStencilDescription;
		pipelineStateDescription.InputLayout = D3D12_INPUT_LAYOUT_DESC{
			inputElements,
			static_cast<UINT>(_countof(inputElements))
		};
		pipelineStateDescription.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipelineStateDescription.NumRenderTargets = 1;
		pipelineStateDescription.RTVFormats[0] = renderTargetFormat;
		pipelineStateDescription.DSVFormat =
		    enableDepth ? depthStencilFormat : DXGI_FORMAT_UNKNOWN;
		pipelineStateDescription.SampleDesc.Count = resolvedSampleCount;
		pipelineStateDescription.SampleDesc.Quality = 0;
		ID3D12PipelineState* rawPipelineState = nullptr;
		const HRESULT pipelineStateResult = device->CreateGraphicsPipelineState(
		    &pipelineStateDescription,
		    IID_PPV_ARGS(&rawPipelineState));
		if (FAILED(pipelineStateResult))
		{
			SetPipelineInitializationFailure(
			    PipelineInitializationPhase::CreatePipelineState,
			    pipelineStateResult,
			    failedPhase,
			    failedResult);
			return false;
		}
		pipelineState->Attach(rawPipelineState);

		return true;
	}

	[[nodiscard]] inline bool InitializeVertexBuffer(
	    ID3D12Device* const device,
	    const UINT maxVertexCount,
	    Microsoft::WRL::ComPtr<ID3D12Resource>* const vertexBuffer,
	    D3D12_VERTEX_BUFFER_VIEW* const vertexBufferView,
	    UINT* const vertexCapacity,
	    VertexBufferInitializationPhase* const failedPhase = nullptr,
	    HRESULT* const failedResult = nullptr)
	{
		SetVertexBufferInitializationFailure(
		    VertexBufferInitializationPhase::None,
		    S_OK,
		    failedPhase,
		    failedResult);
		if ((device == nullptr) || (vertexBuffer == nullptr) ||
		    (vertexBufferView == nullptr) || (vertexCapacity == nullptr) ||
		    (maxVertexCount == 0U))
		{
			SetVertexBufferInitializationFailure(
			    VertexBufferInitializationPhase::InvalidArguments,
			    E_INVALIDARG,
			    failedPhase,
			    failedResult);
			return false;
		}

		vertexBuffer->Reset();
		*vertexBufferView = D3D12_VERTEX_BUFFER_VIEW{};
		*vertexCapacity = 0;

		const UINT64 bufferSizeBytes =
		    static_cast<UINT64>(sizeof(Vertex)) * static_cast<UINT64>(maxVertexCount);
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProperties.CreationNodeMask = 1;
		heapProperties.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC resourceDescription{};
		resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDescription.Alignment = 0;
		resourceDescription.Width = bufferSizeBytes;
		resourceDescription.Height = 1;
		resourceDescription.DepthOrArraySize = 1;
		resourceDescription.MipLevels = 1;
		resourceDescription.Format = DXGI_FORMAT_UNKNOWN;
		resourceDescription.SampleDesc.Count = 1;
		resourceDescription.SampleDesc.Quality = 0;
		resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

		ID3D12Resource* rawVertexBuffer = nullptr;
		const HRESULT createResult = device->CreateCommittedResource(
		    &heapProperties,
		    D3D12_HEAP_FLAG_NONE,
		    &resourceDescription,
		    D3D12_RESOURCE_STATE_GENERIC_READ,
		    nullptr,
		    IID_PPV_ARGS(&rawVertexBuffer));
		if (FAILED(createResult))
		{
			SetVertexBufferInitializationFailure(
			    VertexBufferInitializationPhase::CreateCommittedResource,
			    createResult,
			    failedPhase,
			    failedResult);
			return false;
		}
		vertexBuffer->Attach(rawVertexBuffer);

		vertexBufferView->BufferLocation = vertexBuffer->Get()->GetGPUVirtualAddress();
		vertexBufferView->SizeInBytes = static_cast<UINT>(bufferSizeBytes);
		vertexBufferView->StrideInBytes = sizeof(Vertex);
		*vertexCapacity = maxVertexCount;
		return true;
	}

	[[nodiscard]] inline bool UploadVertices(
	    ID3D12Resource* const vertexBuffer,
	    const std::vector<Vertex>& vertices,
	    VertexUploadPhase* const failedPhase = nullptr,
	    HRESULT* const failedResult = nullptr)
	{
		SetVertexUploadFailure(
		    VertexUploadPhase::None,
		    S_OK,
		    failedPhase,
		    failedResult);
		if (vertexBuffer == nullptr)
		{
			SetVertexUploadFailure(
			    VertexUploadPhase::InvalidArguments,
			    E_INVALIDARG,
			    failedPhase,
			    failedResult);
			return false;
		}

		if (vertices.empty())
		{
			return true;
		}

		Vertex* mappedVertices = nullptr;
		D3D12_RANGE readRange{ 0, 0 };
		const HRESULT mapResult = vertexBuffer->Map(
		    0,
		    &readRange,
		    reinterpret_cast<void**>(&mappedVertices));
		if (FAILED(mapResult) || (mappedVertices == nullptr))
		{
			SetVertexUploadFailure(
			    VertexUploadPhase::Map,
			    FAILED(mapResult) ? mapResult : E_POINTER,
			    failedPhase,
			    failedResult);
			return false;
		}

		std::copy(vertices.begin(), vertices.end(), mappedVertices);
		const SIZE_T bytesWritten =
		    static_cast<SIZE_T>(vertices.size() * sizeof(Vertex));
		D3D12_RANGE writeRange{ 0, bytesWritten };
		vertexBuffer->Unmap(0, &writeRange);
		return true;
	}

	[[nodiscard]] inline bool IssueDrawCommands(
	    ID3D12GraphicsCommandList* const commandList,
	    const LONG width,
	    const LONG height,
	    ID3D12RootSignature* const rootSignature,
	    ID3D12PipelineState* const pipelineState,
	    const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
	    const UINT vertexCount,
	    const UINT startVertexLocation = 0U,
	    DrawCommandValidationPhase* const failedPhase = nullptr)
	{
		if (failedPhase != nullptr)
		{
			*failedPhase = DrawCommandValidationPhase::None;
		}

		if ((commandList == nullptr) || (width <= 0) || (height <= 0) ||
		    (rootSignature == nullptr) || (pipelineState == nullptr) ||
		    (vertexBufferView.SizeInBytes == 0U) || (vertexBufferView.StrideInBytes == 0U) ||
		    (vertexCount == 0U))
		{
			if (failedPhase != nullptr)
			{
				*failedPhase = DrawCommandValidationPhase::InvalidArguments;
			}
			return false;
		}

		const UINT maxVertexCount = vertexBufferView.SizeInBytes / vertexBufferView.StrideInBytes;
		if ((startVertexLocation >= maxVertexCount) ||
		    (vertexCount > (maxVertexCount - startVertexLocation)))
		{
			if (failedPhase != nullptr)
			{
				*failedPhase = DrawCommandValidationPhase::VertexRange;
			}
			return false;
		}

		D3D12_VIEWPORT viewport{};
		viewport.TopLeftX = 0.0F;
		viewport.TopLeftY = 0.0F;
		viewport.Width = static_cast<float>(width);
		viewport.Height = static_cast<float>(height);
		viewport.MinDepth = 0.0F;
		viewport.MaxDepth = 1.0F;
		commandList->RSSetViewports(1, &viewport);

		D3D12_RECT scissorRect{ 0, 0, width, height };
		commandList->RSSetScissorRects(1, &scissorRect);
		commandList->SetGraphicsRootSignature(rootSignature);
		commandList->SetPipelineState(pipelineState);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
		commandList->DrawInstanced(vertexCount, 1, startVertexLocation, 0);
		return true;
	}
#endif
} // namespace BaseballSimulatorVR::Common::DebugOverlay

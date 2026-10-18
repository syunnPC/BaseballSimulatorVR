#pragma once

#include "D3d12DebugOverlay.hpp"
#include "UiTextAtlas.hpp"

#include <cstring>

namespace BaseballSimulatorVR::Common::UiText
{
	// One instance per independently in-flight frame. The caller must wait for
	// its GPU fence before invoking UploadAndDraw again or destroying it.
	class AtlasPipeline final
	{
	  public:
		AtlasPipeline() = default;
		AtlasPipeline(const AtlasPipeline&) = delete;
		AtlasPipeline& operator=(const AtlasPipeline&) = delete;
		AtlasPipeline(AtlasPipeline&&) = default;
		AtlasPipeline& operator=(AtlasPipeline&&) = default;
		[[nodiscard]] bool Initialize(
		    ID3D12Device* device, DXGI_FORMAT color, DXGI_FORMAT depth, UINT samples)
		{
			if (!device || samples == 0)
			{
				return false;
			}
			m_uploadedGeneration = 0;
			m_uploadCount = 0;
			m_vertexBytes = 0;
			m_vertices.Reset();
			m_texture.Reset();
			m_atlasUpload.Reset();
			m_srv.Reset();
			m_root.Reset();
			m_pipeline.Reset();
			m_device = device;
			static constexpr char shader[] = R"(
Texture2D<float> glyphAtlas : register(t0);
SamplerState glyphSampler : register(s0);
struct Input { float4 position:POSITION; float2 uv:TEXCOORD; float4 color:COLOR; };
struct Output { float4 position:SV_Position; float2 uv:TEXCOORD; float4 color:COLOR; };
Output VSMain(Input v) { Output o;o.position=v.position;o.uv=v.uv;o.color=v.color;return o; }
float4 PSMain(Output p):SV_Target { float a=glyphAtlas.Sample(glyphSampler,p.uv);return float4(p.color.rgb,p.color.a*a); }
)";
			D3D12_DESCRIPTOR_RANGE range{};
			range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			range.NumDescriptors = 1;
			D3D12_ROOT_PARAMETER parameter{};
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable.NumDescriptorRanges = 1;
			parameter.DescriptorTable.pDescriptorRanges = &range;
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			D3D12_STATIC_SAMPLER_DESC sampler{};
			sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			sampler.AddressU = sampler.AddressV = sampler.AddressW =
			    D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.MaxLOD = D3D12_FLOAT32_MAX;
			sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			D3D12_ROOT_SIGNATURE_DESC root{};
			root.NumParameters = 1;
			root.pParameters = &parameter;
			root.NumStaticSamplers = 1;
			root.pStaticSamplers = &sampler;
			root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
			Microsoft::WRL::ComPtr<ID3DBlob> serialized, error, vs, ps;
			if (FAILED(D3D12SerializeRootSignature(
			        &root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error)))
			{
				return false;
			}
			if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(),
			        serialized->GetBufferSize(), IID_PPV_ARGS(&m_root))))
			{
				return false;
			}
			if (!DebugOverlay::CompileShader(shader, "VSMain", "vs_5_0", &vs) ||
			    !DebugOverlay::CompileShader(shader, "PSMain", "ps_5_0", &ps))
			{
				return false;
			}
			const D3D12_INPUT_ELEMENT_DESC elements[]{
			    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
			        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16,
			        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
			        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
			D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
			pso.pRootSignature = m_root.Get();
			pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
			pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
			pso.InputLayout = {elements, 3};
			pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			pso.SampleMask = UINT_MAX;
			pso.NumRenderTargets = 1;
			pso.RTVFormats[0] = color;
			pso.DSVFormat = depth;
			pso.SampleDesc.Count = samples;
			pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
			pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
			pso.RasterizerState.DepthClipEnable = TRUE;
			pso.RasterizerState.MultisampleEnable = samples > 1;
			pso.DepthStencilState.DepthEnable = depth != DXGI_FORMAT_UNKNOWN;
			pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			auto& blend = pso.BlendState.RenderTarget[0];
			blend.BlendEnable = TRUE;
			blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			blend.BlendOp = D3D12_BLEND_OP_ADD;
			blend.SrcBlendAlpha = D3D12_BLEND_ONE;
			blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pipeline))))
			{
				return false;
			}
			D3D12_DESCRIPTOR_HEAP_DESC heap{};
			heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heap.NumDescriptors = 1;
			heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			if (FAILED(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_srv))))
			{
				return false;
			}
			D3D12_RESOURCE_DESC texture{};
			texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			texture.Width = GlyphAtlas::Width;
			texture.Height = GlyphAtlas::Height;
			texture.DepthOrArraySize = 1;
			texture.MipLevels = 1;
			texture.Format = DXGI_FORMAT_R8_UNORM;
			texture.SampleDesc.Count = 1;
			D3D12_HEAP_PROPERTIES properties{};
			properties.Type = D3D12_HEAP_TYPE_DEFAULT;
			if (FAILED(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &texture,
			        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_texture))))
			{
				return false;
			}
			D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_R8_UNORM;
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv.Texture2D.MipLevels = 1;
			device->CreateShaderResourceView(
			    m_texture.Get(), &srv, m_srv->GetCPUDescriptorHandleForHeapStart());
			return CreateUpload(
			    static_cast<UINT64>(GlyphAtlas::Width) * GlyphAtlas::Height, &m_atlasUpload);
		}
		[[nodiscard]] bool UploadAndDraw(ID3D12GraphicsCommandList* commands, GlyphAtlas& atlas,
		    const std::vector<AtlasVertex>& vertices, LONG width, LONG height)
		{
			if (vertices.empty())
			{
				return true;
			}
			if (!commands || !m_pipeline || width <= 0 || height <= 0)
			{
				return false;
			}
			const UINT64 required = static_cast<UINT64>(vertices.size()) * sizeof(AtlasVertex);
			if (required > UINT_MAX)
			{
				return false;
			}
			if (required > m_vertexBytes)
			{
				UINT64 size = (std::max)(m_vertexBytes, UINT64{65536});
				while (size < required)
				{
					size *= 2;
				}
				if (size > UINT_MAX)
				{
					size = required;
				}
				if (!CreateUpload(size, &m_vertices))
				{
					return false;
				}
				m_vertexBytes = size;
			}
			void* vertexMapped{};
			D3D12_RANGE vertexRead{0, 0};
			if (FAILED(m_vertices->Map(0, &vertexRead, &vertexMapped)))
			{
				return false;
			}
			std::memcpy(vertexMapped, vertices.data(), static_cast<std::size_t>(required));
			const D3D12_RANGE vertexWritten{0, static_cast<SIZE_T>(required)};
			m_vertices->Unmap(0, &vertexWritten);
			if (atlas.Generation() != m_uploadedGeneration)
			{
				int top = atlas.DirtyTop(), bottom = atlas.DirtyBottom();
				if (m_uploadedGeneration == 0 ||
				    m_uploadedGeneration != atlas.CommittedGeneration() || top >= bottom)
				{
					top = 0;
					bottom = GlyphAtlas::Height;
				}
				void* mapped{};
				D3D12_RANGE read{0, 0};
				if (FAILED(m_atlasUpload->Map(0, &read, &mapped)))
				{
					return false;
				}
				const std::size_t bytes =
				    static_cast<std::size_t>(bottom - top) * GlyphAtlas::Width;
				std::memcpy(mapped,
				    atlas.Pixels().data() + static_cast<std::size_t>(top) * GlyphAtlas::Width,
				    bytes);
				const D3D12_RANGE written{0, bytes};
				m_atlasUpload->Unmap(0, &written);
				if (m_uploadedGeneration != 0)
				{
					Barrier(commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
					    D3D12_RESOURCE_STATE_COPY_DEST);
				}
				D3D12_TEXTURE_COPY_LOCATION source{};
				source.pResource = m_atlasUpload.Get();
				source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				source.PlacedFootprint.Footprint = {DXGI_FORMAT_R8_UNORM, GlyphAtlas::Width,
				    static_cast<UINT>(bottom - top), 1, GlyphAtlas::Width};
				D3D12_TEXTURE_COPY_LOCATION destination{};
				destination.pResource = m_texture.Get();
				destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				commands->CopyTextureRegion(
				    &destination, 0, static_cast<UINT>(top), 0, &source, nullptr);
				Barrier(commands, D3D12_RESOURCE_STATE_COPY_DEST,
				    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				m_uploadedGeneration = atlas.Generation();
				atlas.CommitUpload();
				++m_uploadCount;
			}
			const D3D12_VERTEX_BUFFER_VIEW vb{m_vertices->GetGPUVirtualAddress(),
			    static_cast<UINT>(required), sizeof(AtlasVertex)};
			const D3D12_VIEWPORT viewport{
			    0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
			const D3D12_RECT scissor{0, 0, width, height};
			commands->RSSetViewports(1, &viewport);
			commands->RSSetScissorRects(1, &scissor);
			commands->SetGraphicsRootSignature(m_root.Get());
			commands->SetPipelineState(m_pipeline.Get());
			ID3D12DescriptorHeap* heaps[]{m_srv.Get()};
			commands->SetDescriptorHeaps(1, heaps);
			commands->SetGraphicsRootDescriptorTable(
			    0, m_srv->GetGPUDescriptorHandleForHeapStart());
			commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commands->IASetVertexBuffers(0, 1, &vb);
			commands->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
			return true;
		}
		[[nodiscard]] std::uint64_t UploadCount() const noexcept
		{
			return m_uploadCount;
		}

	  private:
		[[nodiscard]] bool CreateUpload(
		    UINT64 size, Microsoft::WRL::ComPtr<ID3D12Resource>* resource)
		{
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC desc{};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = size;
			desc.Height = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.SampleDesc.Count = 1;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			Microsoft::WRL::ComPtr<ID3D12Resource> created;
			if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&created))))
			{
				return false;
			}
			*resource = std::move(created);
			return true;
		}
		void Barrier(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES before,
		    D3D12_RESOURCE_STATES after)
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = m_texture.Get();
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = before;
			barrier.Transition.StateAfter = after;
			commands->ResourceBarrier(1, &barrier);
		}
		Microsoft::WRL::ComPtr<ID3D12Device> m_device;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipeline;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srv;
		Microsoft::WRL::ComPtr<ID3D12Resource> m_texture, m_atlasUpload, m_vertices;
		UINT64 m_vertexBytes{};
		std::uint64_t m_uploadedGeneration{}, m_uploadCount{};
	};
} // namespace BaseballSimulatorVR::Common::UiText

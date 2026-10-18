#pragma once

#include "DebugWorldProjection.hpp"
#include "FieldPresentationOverlay.hpp"
#include "UiTextRasterization.hpp"

#include <limits>
#include <string>

namespace BaseballSimulatorVR::Common::UiText
{
	struct PanelLocalRect final
	{
		double left{}, top{}, right{}, bottom{};
	};
	struct PanelLayout final
	{
		Physics::Vector3 center{}, rightAxis{1, 0, 0}, upAxis{0, 1, 0}, forwardAxis{0, 0, -1};
		double widthMeters{.30}, heightMeters{.24}, paddingMeters{.012}, cellGapMeters{.006};
		int columns{1}, rows{1};
	};
	[[nodiscard]] inline PanelLocalRect BuildPanelCellRect(double innerLeft, double innerTop,
	    double cellWidth, double cellHeight, double gap, int columns, int index)
	{
		if (columns <= 0 || index < 0 || cellWidth <= 0 || cellHeight <= 0)
		{
			throw std::invalid_argument("Invalid panel cell dimensions");
		}
		PanelLocalRect rect;
		rect.left = innerLeft + (index % columns) * (cellWidth + gap);
		rect.top = innerTop - (index / columns) * (cellHeight + gap);
		rect.right = rect.left + cellWidth;
		rect.bottom = rect.top - cellHeight;
		return rect;
	}
	struct AtlasGlyph final
	{
		int advance{}, lineHeight{}, originX{}, originY{}, width{}, height{};
		float u0{}, v0{}, u1{}, v1{};
	};
	class GlyphAtlas final
	{
	  public:
		static constexpr int Width = 4096, Height = 4096, CellSize = 128, FontPixelHeight = 64;
		static constexpr std::size_t Capacity = 1023;
		GlyphAtlas() : m_pixels(static_cast<std::size_t>(Width) * Height)
		{
			// A padded white patch lets solid transparent triangles share the
			// glyph shader and the exact same depth-ordered render pass.
			for (int y = 0; y < 4; ++y)
			{
				for (int x = 0; x < 4; ++x)
				{
					m_pixels[static_cast<std::size_t>(y) * Width + x] = 255;
				}
			}
			m_dirtyTop = 0;
			m_dirtyBottom = 4;
		}
		void BeginFrame() noexcept
		{
			++m_frame;
		}
		[[nodiscard]] const AtlasGlyph& Get(std::uint32_t scalar)
		{
			if (scalar > 0x10FFFFU || (scalar >= 0xD800U && scalar <= 0xDFFFU))
			{
				throw std::invalid_argument("Atlas keys must be Unicode scalar values");
			}
			if (const auto found = m_entries.find(scalar); found != m_entries.end())
			{
				found->second.usedFrame = m_frame;
				found->second.use = ++m_use;
				return found->second.glyph;
			}
			std::size_t slot;
			auto evicted = m_entries.end();
			if (m_entries.size() < Capacity)
			{
				slot = m_entries.size() + 1;
			}
			else
			{
				auto oldest = m_entries.end();
				for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
				{
					if (it->second.usedFrame != m_frame &&
					    (oldest == m_entries.end() || it->second.use < oldest->second.use))
					{
						oldest = it;
					}
				}
				if (oldest == m_entries.end())
				{
					throw std::length_error(
					    "UI uses more atlas glyphs in one frame than its capacity");
				}
				slot = oldest->second.slot;
				evicted = oldest;
			}
			const auto& source = m_glyphs.Get(scalar, FontPixelHeight);
			AtlasGlyph glyph;
			glyph.advance = source.advance;
			glyph.lineHeight = source.lineHeight;
			int left = 0, top = 0, right = 0, bottom = 0;
			if (!source.runs.empty())
			{
				left = top = (std::numeric_limits<int>::max)();
				right = bottom = (std::numeric_limits<int>::min)();
				for (const auto& run : source.runs)
				{
					left = (std::min)(left, run.x);
					top = (std::min)(top, run.y);
					right = (std::max)(right, run.x + run.width);
					bottom = (std::max)(bottom, run.y + run.height);
				}
			}
			glyph.originX = left;
			glyph.originY = top;
			glyph.width = right - left;
			glyph.height = bottom - top;
			if (glyph.width > CellSize - 4 || glyph.height > CellSize - 4)
			{
				throw std::length_error("UI glyph exceeds its atlas cell");
			}
			if (evicted != m_entries.end())
			{
				m_entries.erase(evicted);
			}
			const int cellX = static_cast<int>(slot % 32) * CellSize,
			          cellY = static_cast<int>(slot / 32) * CellSize;
			for (int y = cellY; y < cellY + CellSize; ++y)
			{
				std::fill_n(m_pixels.begin() + static_cast<std::size_t>(y) * Width + cellX,
				    CellSize, std::uint8_t{0});
			}
			const int x = cellX + 2, y = cellY + 2;
			for (const auto& run : source.runs)
			{
				for (int dy = 0; dy < run.height; ++dy)
				{
					std::fill_n(m_pixels.begin() +
					                static_cast<std::size_t>(y + run.y - top + dy) * Width + x +
					                run.x - left,
					    run.width, run.coverage);
				}
			}
			glyph.u0 = static_cast<float>(x) / Width;
			glyph.v0 = static_cast<float>(y) / Height;
			glyph.u1 = static_cast<float>(x + glyph.width) / Width;
			glyph.v1 = static_cast<float>(y + glyph.height) / Height;
			m_dirtyTop = (std::min)(m_dirtyTop, cellY);
			m_dirtyBottom = (std::max)(m_dirtyBottom, cellY + CellSize);
			++m_generation;
			return m_entries.emplace(scalar, Entry{glyph, slot, m_frame, ++m_use})
			    .first->second.glyph;
		}
		[[nodiscard]] std::uint64_t Generation() const noexcept
		{
			return m_generation;
		}
		[[nodiscard]] std::size_t Size() const noexcept
		{
			return m_entries.size();
		}
		[[nodiscard]] const std::vector<std::uint8_t>& Pixels() const noexcept
		{
			return m_pixels;
		}
		[[nodiscard]] int DirtyTop() const noexcept
		{
			return m_dirtyTop;
		}
		[[nodiscard]] int DirtyBottom() const noexcept
		{
			return m_dirtyBottom;
		}
		void CommitUpload() noexcept
		{
			m_dirtyTop = Height;
			m_dirtyBottom = 0;
			m_committedGeneration = m_generation;
		}
		[[nodiscard]] std::uint64_t CommittedGeneration() const noexcept
		{
			return m_committedGeneration;
		}
		[[nodiscard]] GlyphCache& Metrics() noexcept
		{
			return m_glyphs;
		}

	  private:
		struct Entry final
		{
			AtlasGlyph glyph;
			std::size_t slot;
			std::uint64_t usedFrame, use;
		};
		GlyphCache m_glyphs;
		std::unordered_map<std::uint32_t, Entry> m_entries;
		std::vector<std::uint8_t> m_pixels;
		std::uint64_t m_frame{1}, m_use{}, m_generation{1}, m_committedGeneration{};
		int m_dirtyTop{Height}, m_dirtyBottom{};
	};
	struct GlyphPlacement final
	{
		std::uint32_t scalar{};
		int x{}, y{};
	};
	struct TextLayout final
	{
		int width{}, height{};
		std::vector<GlyphPlacement> glyphs;
	};
	class TextLayoutCache final
	{
	  public:
		static constexpr std::size_t Capacity = 256;
		[[nodiscard]] const TextLayout& Get(std::wstring_view text, GlyphAtlas& atlas)
		{
			const std::wstring key{text};
			if (const auto found = m_lookup.find(key); found != m_lookup.end())
			{
				m_lru.splice(m_lru.begin(), m_lru, found->second);
				return found->second->second;
			}
			TextLayout layout;
			const auto& space = atlas.Metrics().Get(' ', GlyphAtlas::FontPixelHeight);
			const int lineHeight = space.lineHeight, tabWidth = (std::max)(1, space.advance * 4);
			int x = 0, y = 0;
			for (const auto scalar : DecodeUtf16(text))
			{
				if (scalar == '\n')
				{
					layout.width = (std::max)(layout.width, x);
					x = 0;
					y += lineHeight;
					continue;
				}
				if (scalar == '\t')
				{
					x = (x / tabWidth + 1) * tabWidth;
					continue;
				}
				const auto& glyph = atlas.Metrics().Get(scalar, GlyphAtlas::FontPixelHeight);
				layout.glyphs.push_back({scalar, x, y});
				x += glyph.advance;
			}
			layout.width = (std::max)(layout.width, x);
			layout.height = text.empty() ? 0 : y + lineHeight;
			if (m_lru.size() == Capacity)
			{
				m_lookup.erase(m_lru.back().first);
				m_lru.pop_back();
			}
			m_lru.emplace_front(key, std::move(layout));
			m_lookup.emplace(key, m_lru.begin());
			return m_lru.front().second;
		}
		[[nodiscard]] std::size_t Size() const noexcept
		{
			return m_lru.size();
		}

	  private:
		using Entry = std::pair<std::wstring, TextLayout>;
		std::list<Entry> m_lru;
		std::unordered_map<std::wstring, std::list<Entry>::iterator> m_lookup;
	};
	struct AtlasVertex final
	{
		float position[4];
		float uv[2];
		float color[4];
		float sortDepth{};
		std::uint32_t sortGroup{};
		std::uint32_t sortLayer{};
	};
	struct PanelSortGroup final
	{
		std::array<double, 12> plane;
		float depth{};
	};
	struct AtlasDrawContext final
	{
		GlyphAtlas& atlas;
		TextLayoutCache& layouts;
		std::vector<AtlasVertex>& vertices;
		std::vector<PanelSortGroup> groups{};
		std::uint32_t currentGroup{};
		float currentDepth{};
	};
	class ScopedPanelGroup final
	{
	  public:
		ScopedPanelGroup(
		    AtlasDrawContext& context, const PanelLayout& panel, const ViewProjectionState& view)
		    : m_context(context), m_previousGroup(context.currentGroup),
		      m_previousDepth(context.currentDepth)
		{
			const std::array<double, 12> plane{panel.center.x, panel.center.y, panel.center.z,
			    panel.rightAxis.x, panel.rightAxis.y, panel.rightAxis.z, panel.upAxis.x,
			    panel.upAxis.y, panel.upAxis.z, panel.forwardAxis.x, panel.forwardAxis.y,
			    panel.forwardAxis.z};
			std::size_t index = 0;
			while (index < context.groups.size() && context.groups[index].plane != plane)
			{
				++index;
			}
			if (index == context.groups.size())
			{
				ScreenPoint center;
				const bool valid =
				    TryProjectWorldToScreen(panel.center, view, 2, 2, &center, false);
				context.groups.push_back({plane, valid ? center.depth : 0.0F});
			}
			context.currentGroup = static_cast<std::uint32_t>(index + 1);
			context.currentDepth = context.groups[index].depth;
		}
		~ScopedPanelGroup()
		{
			m_context.currentGroup = m_previousGroup;
			m_context.currentDepth = m_previousDepth;
		}
		ScopedPanelGroup(const ScopedPanelGroup&) = delete;
		ScopedPanelGroup& operator=(const ScopedPanelGroup&) = delete;

	  private:
		AtlasDrawContext& m_context;
		std::uint32_t m_previousGroup;
		float m_previousDepth;
	};
	inline void SetSortGroup(
	    AtlasVertex& vertex, const AtlasDrawContext& context, std::uint32_t layer)
	{
		vertex.sortDepth = context.currentDepth;
		vertex.sortGroup = context.currentGroup;
		vertex.sortLayer = layer;
	}
	[[nodiscard]] inline AtlasVertex BuildAtlasVertex(
	    const ScreenPoint& p, float u, float v, const float* color, LONG width, LONG height)
	{
		const double w =
		    DebugWorldLayout::NearClipMeters +
		    p.depth * (DebugWorldLayout::FarClipMeters - DebugWorldLayout::NearClipMeters);
		return {{static_cast<float>((p.x / (std::max)(1L, width - 1) * 2 - 1) * w),
		            static_cast<float>((1 - p.y / (std::max)(1L, height - 1) * 2) * w),
		            static_cast<float>(p.depth * w), static_cast<float>(w)},
		    {u, v}, {color[0], color[1], color[2], color[3]}};
	}
	inline void AppendUntexturedTriangle(std::vector<AtlasVertex>& vertices, const ScreenPoint& a,
	    const ScreenPoint& b, const ScreenPoint& c, const float* color, LONG width, LONG height)
	{
		constexpr float white = 1.5F / GlyphAtlas::Width;
		vertices.push_back(BuildAtlasVertex(a, white, white, color, width, height));
		vertices.push_back(BuildAtlasVertex(b, white, white, color, width, height));
		vertices.push_back(BuildAtlasVertex(c, white, white, color, width, height));
	}
	inline void AppendUntexturedTriangle(AtlasDrawContext& context, const ScreenPoint& a,
	    const ScreenPoint& b, const ScreenPoint& c, const float* color, LONG width, LONG height)
	{
		const std::size_t start = context.vertices.size();
		AppendUntexturedTriangle(context.vertices, a, b, c, color, width, height);
		for (std::size_t i = start; i < context.vertices.size(); ++i)
		{
			SetSortGroup(context.vertices[i], context, 0);
		}
	}
	struct WorldAtlasPoint final
	{
		Physics::Vector3 position;
		float u{}, v{};
	};
	[[nodiscard]] inline double ViewDepth(
	    const Physics::Vector3& position, const ViewProjectionState& view)
	{
		const auto delta = position - view.position;
		const auto q = view.orientation;
		return -(2 * (q.x * q.z + q.y * view.orientationW) * delta.x +
		         2 * (q.y * q.z - q.x * view.orientationW) * delta.y +
		         (1 - 2 * (q.x * q.x + q.y * q.y)) * delta.z);
	}
	inline void AppendProjectedQuad(const std::array<WorldAtlasPoint, 4>& quad,
	    const std::array<float, 4>& color, const ViewProjectionState& view, LONG width, LONG height,
	    AtlasDrawContext& context, std::uint32_t layer)
	{
		if (!view.isValid || width <= 1 || height <= 1)
		{
			return;
		}
		constexpr double nearDepth = DebugWorldLayout::NearClipMeters + 1e-6;
		std::array<double, 4> depths;
		int frontCount = 0;
		for (std::size_t i = 0; i < quad.size(); ++i)
		{
			depths[i] = ViewDepth(quad[i].position, view);
			frontCount += depths[i] >= nearDepth ? 1 : 0;
		}
		if (frontCount == 0)
		{
			return;
		}
		std::array<WorldAtlasPoint, 6> clipped;
		std::size_t count = 0;
		if (frontCount == 4)
		{
			std::copy(quad.begin(), quad.end(), clipped.begin());
			count = 4;
		}
		else
		{
			// Clip in world space before projecting. Interpolate UVs on exactly
			// the same edge parameter, preserving the original glyph image.
			for (std::size_t current = 0; current < quad.size(); ++current)
			{
				const std::size_t previous = (current + 3) % 4;
				const bool inside = depths[current] >= nearDepth;
				const bool wasInside = depths[previous] >= nearDepth;
				if (inside != wasInside)
				{
					const double t =
					    (nearDepth - depths[previous]) / (depths[current] - depths[previous]);
					const auto& a = quad[previous];
					const auto& b = quad[current];
					clipped[count++] = {a.position + (b.position - a.position) * t,
					    static_cast<float>(a.u + (b.u - a.u) * t),
					    static_cast<float>(a.v + (b.v - a.v) * t)};
				}
				if (inside)
				{
					clipped[count++] = quad[current];
				}
			}
		}
		std::array<AtlasVertex, 6> vertices;
		for (std::size_t i = 0; i < count; ++i)
		{
			ScreenPoint projected;
			if (!TryProjectWorldToScreen(
			        clipped[i].position, view, width, height, &projected, false))
			{
				return;
			}
			vertices[i] = BuildAtlasVertex(
			    projected, clipped[i].u, clipped[i].v, color.data(), width, height);
			SetSortGroup(vertices[i], context, layer);
		}
		for (std::size_t i = 1; i + 1 < count; ++i)
		{
			context.vertices.push_back(vertices[0]);
			context.vertices.push_back(vertices[i]);
			context.vertices.push_back(vertices[i + 1]);
		}
	}
	template <typename TWorldPoint>
	void AppendProjectedText(std::wstring_view label, double left, double top, double maxWidth,
	    double maxHeight, double alignX, double alignY, const std::array<float, 4>& color,
	    TWorldPoint&& worldPoint, const ViewProjectionState& view, LONG width, LONG height,
	    AtlasDrawContext& context)
	{
		if (label.empty() || !view.isValid || width <= 1 || height <= 1 || maxWidth <= 0 ||
		    maxHeight <= 0)
		{
			return;
		}
		const auto& layout = context.layouts.Get(label, context.atlas);
		if (layout.width <= 0 || layout.height <= 0)
		{
			return;
		}
		const double scale = (std::min)(maxWidth / layout.width, maxHeight / layout.height);
		left += (maxWidth - layout.width * scale) * std::clamp(alignX, 0.0, 1.0);
		top -= (maxHeight - layout.height * scale) * std::clamp(alignY, 0.0, 1.0);
		for (const auto& placement : layout.glyphs)
		{
			const auto& glyph = context.atlas.Get(placement.scalar);
			if (glyph.width == 0 || glyph.height == 0)
			{
				continue;
			}
			const double x = left + (placement.x + glyph.originX) * scale,
			             y = top - (placement.y + glyph.originY) * scale;
			const std::array<WorldAtlasPoint, 4> corners{{{worldPoint(x, y), glyph.u0, glyph.v0},
			    {worldPoint(x + glyph.width * scale, y), glyph.u1, glyph.v0},
			    {worldPoint(x + glyph.width * scale, y - glyph.height * scale), glyph.u1, glyph.v1},
			    {worldPoint(x, y - glyph.height * scale), glyph.u0, glyph.v1}}};
			AppendProjectedQuad(corners, color, view, width, height, context, 1);
		}
	}
	inline void AppendProjectedPanelText(const PanelLayout& panel, const PanelLocalRect& rect,
	    std::wstring_view label, double widthUsage, double heightUsage, double alignX,
	    double alignY, const std::array<float, 4>& color, const ViewProjectionState& view,
	    LONG width, LONG height, AtlasDrawContext& context)
	{
		ScopedPanelGroup group(context, panel, view);
		const double availableWidth = (rect.right - rect.left) * std::clamp(widthUsage, .1, 1.0);
		const double availableHeight = (rect.top - rect.bottom) * std::clamp(heightUsage, .1, 1.0);
		const double left = rect.left + ((rect.right - rect.left) - availableWidth) * .5;
		const double top = rect.top - ((rect.top - rect.bottom) - availableHeight) * .5;
		const double depth =
		    .024 * (Physics::Dot(view.position - panel.center, panel.forwardAxis) >= 0 ? 1 : -1);
		AppendProjectedText(
		    label, left, top, availableWidth, availableHeight, alignX, alignY, color,
		    [&](double x, double y)
		    {
			    return panel.center + panel.rightAxis * x + panel.upAxis * y +
			           panel.forwardAxis * depth;
		    },
		    view, width, height, context);
	}
	inline void AppendProjectedPanelRect(const PanelLayout& panel, const PanelLocalRect& rect,
	    const std::array<float, 4>& color, double depthOffset, const ViewProjectionState& view,
	    LONG width, LONG height, AtlasDrawContext& context)
	{
		ScopedPanelGroup group(context, panel, view);
		const auto world = [&](double x, double y)
		{
			return panel.center + panel.rightAxis * x + panel.upAxis * y +
			       panel.forwardAxis * depthOffset;
		};
		constexpr float white = 1.5F / GlyphAtlas::Width;
		const std::array<WorldAtlasPoint, 4> corners{{{world(rect.left, rect.top), white, white},
		    {world(rect.right, rect.top), white, white},
		    {world(rect.right, rect.bottom), white, white},
		    {world(rect.left, rect.bottom), white, white}}};
		AppendProjectedQuad(corners, color, view, width, height, context, 0);
	}
	inline void SortAtlasTrianglesBackToFront(std::vector<AtlasVertex>& vertices)
	{
		if (vertices.size() % 3)
		{
			throw std::logic_error("Atlas geometry must contain complete triangles");
		}
		struct Triangle final
		{
			std::array<AtlasVertex, 3> vertices;
			float depth;
			std::uint32_t group;
			std::uint32_t layer;
		};
		std::vector<Triangle> triangles;
		triangles.reserve(vertices.size() / 3);
		for (std::size_t i = 0; i < vertices.size(); i += 3)
		{
			const auto& first = vertices[i];
			const float depth =
			    first.sortGroup != 0
			        ? first.sortDepth
			        : (vertices[i].position[2] / vertices[i].position[3] +
			              vertices[i + 1].position[2] / vertices[i + 1].position[3] +
			              vertices[i + 2].position[2] / vertices[i + 2].position[3]) /
			              3;
			triangles.push_back({{vertices[i], vertices[i + 1], vertices[i + 2]}, depth,
			    first.sortGroup, first.sortLayer});
		}
		std::stable_sort(triangles.begin(), triangles.end(),
		    [](const auto& a, const auto& b)
		    {
			    if (a.depth != b.depth)
			    {
				    return a.depth > b.depth;
			    }
			    if (a.group != b.group)
			    {
				    return a.group < b.group;
			    }
			    return a.layer < b.layer;
		    });
		std::size_t i = 0;
		for (const auto& triangle : triangles)
		{
			for (const auto& vertex : triangle.vertices)
			{
				vertices[i++] = vertex;
			}
		}
	}
	template <typename TTriangle>
	void EmitProjectedFieldPresentation(const FieldPresentationState& state,
	    const ViewProjectionState& view, LONG width, LONG height, AtlasDrawContext& context,
	    TTriangle&& triangle)
	{
		EmitProjectedPitcherMesh(state.pitcher, view, width, height, triangle);
		const auto board = BuildFieldScoreboardLayout();
		PanelLayout panel;
		panel.center = {0, board.bottom + board.height * .5, board.centerZ};
		panel.forwardAxis = {0, 0, 1};
		ScopedPanelGroup group(context, panel, view);
		const auto boardTriangle = [&](const Physics::Vector3& a, const Physics::Vector3& b,
		                               const Physics::Vector3& c, const float* color)
		{
			EmitProjectedWorldTriangle(a, b, c, view, width, height, color,
			    [&](const ScreenPoint& p0, const ScreenPoint& p1, const ScreenPoint& p2,
			        const float* shade)
			    { AppendUntexturedTriangle(context, p0, p1, p2, shade, width, height); });
		};
		EmitFieldScoreboardGeometry(state.statistics, boardTriangle,
		    [&](std::wstring_view label, double left, double top, double maxWidth, double maxHeight,
		        const std::array<float, 4>& color, double z)
		    {
			    AppendProjectedText(
			        label, left, top, maxWidth, maxHeight, .5, 0, color, [z](double x, double y)
			        { return Physics::Vector3{x, y, z}; }, view, width, height, context);
		    });
	}
} // namespace BaseballSimulatorVR::Common::UiText

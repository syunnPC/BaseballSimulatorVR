#pragma once

#include "DebugWorldProjection.hpp"

#include <cstdint>

#pragma comment(lib, "msimg32.lib")

namespace BaseballSimulatorVR::Common
{
	// A reusable, triangle-sized premultiplied DIB for the software mirror.
	class GdiAlphaTriangleRasterizer final
	{
	  public:
		GdiAlphaTriangleRasterizer() = default;
		GdiAlphaTriangleRasterizer(const GdiAlphaTriangleRasterizer&) = delete;
		GdiAlphaTriangleRasterizer& operator=(const GdiAlphaTriangleRasterizer&) = delete;
		~GdiAlphaTriangleRasterizer()
		{
			if (m_dc && m_originalBitmap) SelectObject(m_dc, m_originalBitmap);
			if (m_bitmap) DeleteObject(m_bitmap);
			if (m_dc) DeleteDC(m_dc);
		}

		[[nodiscard]] bool Draw(HDC target, const LONG width, const LONG height,
		    std::array<ScreenPoint, 3> points, const std::array<float, 4>& color)
		{
			const auto edge = [](const ScreenPoint& a, const ScreenPoint& b, double x, double y)
			{ return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x); };
			double area = edge(points[0], points[1], points[2].x, points[2].y);
			if (std::abs(area) < 1e-12) return true;
			if (area < 0.0) std::swap(points[1], points[2]);
			const LONG left = static_cast<LONG>(std::clamp(std::floor((std::min)(
			    points[0].x, (std::min)(points[1].x, points[2].x))), 0.0, static_cast<double>(width)));
			const LONG top = static_cast<LONG>(std::clamp(std::floor((std::min)(
			    points[0].y, (std::min)(points[1].y, points[2].y))), 0.0, static_cast<double>(height)));
			const LONG right = static_cast<LONG>(std::clamp(std::ceil((std::max)(
			    points[0].x, (std::max)(points[1].x, points[2].x))), 0.0, static_cast<double>(width)));
			const LONG bottom = static_cast<LONG>(std::clamp(std::ceil((std::max)(
			    points[0].y, (std::max)(points[1].y, points[2].y))), 0.0, static_cast<double>(height)));
			if (left >= right || top >= bottom) return true;
			const LONG drawWidth = right - left, drawHeight = bottom - top;
			if (!EnsureCapacity(target, drawWidth, drawHeight)) return false;
			GdiFlush();
			const auto channel = [&](float value)
			{ return static_cast<std::uint32_t>(std::lround(value * color[3] * 255.0F)); };
			const auto alpha = static_cast<std::uint32_t>(std::lround(color[3] * 255.0F));
			const std::uint32_t pixel = (alpha << 24) | (channel(color[0]) << 16) |
			    (channel(color[1]) << 8) | channel(color[2]);
			const auto covered = [&](const ScreenPoint& a, const ScreenPoint& b, double x, double y)
			{
				const double value = edge(a, b, x, y);
				const bool topLeft = b.y < a.y || (b.y == a.y && b.x > a.x);
				return value > 0.0 || (value == 0.0 && topLeft);
			};
			for (LONG y = 0; y < drawHeight; ++y)
			for (LONG x = 0; x < drawWidth; ++x)
			{
				const double px = left + x + .5, py = top + y + .5;
				m_pixels[static_cast<std::size_t>(y) * m_width + x] =
				    covered(points[0], points[1], px, py) && covered(points[1], points[2], px, py) &&
				    covered(points[2], points[0], px, py) ? pixel : 0U;
			}
			const BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
			return AlphaBlend(target, left, top, drawWidth, drawHeight, m_dc,
			    0, 0, drawWidth, drawHeight, blend) != FALSE;
		}

	  private:
		[[nodiscard]] bool EnsureCapacity(HDC target, LONG width, LONG height)
		{
			if (width <= m_width && height <= m_height) return true;
			if (!m_dc) m_dc = CreateCompatibleDC(target);
			if (!m_dc) return false;
			width = (std::max)(width, m_width);
			height = (std::max)(height, m_height);
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = width;
			info.bmiHeader.biHeight = -height;
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			void* pixels{};
			const HBITMAP bitmap = CreateDIBSection(target, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
			if (!bitmap) return false;
			const HGDIOBJ previous = SelectObject(m_dc, bitmap);
			if (!previous || previous == HGDI_ERROR) { DeleteObject(bitmap); return false; }
			if (!m_originalBitmap) m_originalBitmap = previous;
			if (m_bitmap) DeleteObject(m_bitmap);
			m_bitmap = bitmap;
			m_pixels = static_cast<std::uint32_t*>(pixels);
			m_width = width;
			m_height = height;
			return true;
		}

		HDC m_dc{};
		HBITMAP m_bitmap{};
		HGDIOBJ m_originalBitmap{};
		std::uint32_t* m_pixels{};
		LONG m_width{}, m_height{};
	};
}

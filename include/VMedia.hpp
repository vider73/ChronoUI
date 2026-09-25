// =============================================================================
// VMedia.hpp — pictures and marks, WinUI 3 vocabulary.
//
//   VIcon          one Segoe MDL2 Assets glyph, a size, a colour
//   VAnimatedIcon  a glyph that swells under the mouse and bounces on click
//   VImage         a picture from a file: None / Fill / Uniform / UniformToFill,
//                  rounded corners, a placeholder when the file is missing
//   VShape         rectangle, ellipse, line or polygon with a fill and a stroke
//
// VImage decodes with WIC into a Direct2D bitmap and keeps it for the render
// target that made it; a new target (device loss) reloads on the next paint.
// =============================================================================

#pragma once

#include "VirtualWidget.hpp"
#include "VControls.hpp"
#include "VDraw.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace ChronoUI {

	// -------------------------------------------------------------------------
	class VIcon : public VirtualWidgetImpl {
		std::wstring m_glyph;
		float m_size = 16.0f;
		D2D1_COLOR_F m_col = vctl::Ink();
	public:
		explicit VIcon(std::wstring glyph) : m_glyph(std::move(glyph)) {}
		const char* GetTypeName() const override { return "VIcon"; }
		VIcon& Glyph(std::wstring g)     { m_glyph = std::move(g); return *this; }
		VIcon& Size(float px)            { m_size = px; return *this; }
		VIcon& Color(D2D1_COLOR_F c)     { m_col = c; return *this; }
		void OnDraw(ID2D1RenderTarget* rt) override { vd::Text(rt, m_glyph, m_bounds, m_col, vd::Style().Icon().Size(m_size).Center()); }
	};

	// -------------------------------------------------------------------------
	class VAnimatedIcon : public VirtualWidgetImpl {
		std::wstring m_glyph;
		float m_size = 18.0f, m_hover = 0.0f, m_kick = 0.0f;
		D2D1_COLOR_F m_col = vctl::Ink(), m_accent = vctl::Blue();
	public:
		explicit VAnimatedIcon(std::wstring glyph) : m_glyph(std::move(glyph)) {}
		const char* GetTypeName() const override { return "VAnimatedIcon"; }
		bool CanFocus() const override { return true; }
		VAnimatedIcon& Size(float px)          { m_size = px; return *this; }
		VAnimatedIcon& Color(D2D1_COLOR_F c)   { m_col = c; return *this; }
		VAnimatedIcon& Accent(D2D1_COLOR_F c)  { m_accent = c; return *this; }
		bool OnUpdate(float dt) override {
			float th = m_hovered ? 1.0f : 0.0f;
			bool busy = fabsf(m_hover - th) > 0.005f || m_kick > 0.005f;
			m_hover = vd::Approach(m_hover, th, dt, 14.0f);
			m_kick  = vd::Approach(m_kick, 0.0f, dt, 5.0f);
			if (!busy) { m_hover = th; m_kick = 0.0f; }
			return busy;
		}
		VInputResult OnMouseEnter() override { m_hovered = true;  return VInputResult::Handled; }
		VInputResult OnMouseLeave() override { m_hovered = false; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1 || !HitTest(x, y)) return VInputResult::NotHandled;
			m_kick = 1.0f; if (m_onClick) m_onClick(); return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (vk != VK_SPACE && vk != VK_RETURN) return VInputResult::NotHandled;
			m_kick = 1.0f; if (m_onClick) m_onClick(); return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			float cx = vd::CX(m_bounds), cy = vd::CY(m_bounds);
			float scale = 1.0f + 0.18f * vd::EaseOut(m_hover) + 0.25f * sinf(m_kick * vd::kPi);
			float rot   = 12.0f * m_hover * sinf(m_kick * vd::kPi * 2.0f) - 8.0f * (1.0f - m_hover) * m_kick;
			if (m_hover > 0.02f) vd::Circle(rt, cx, cy, m_size * 1.1f, vd::Alpha(m_accent, 0.12f * m_hover));
			D2D1_MATRIX_3X2_F old; rt->GetTransform(&old);
			rt->SetTransform(D2D1::Matrix3x2F::Rotation(rot, D2D1::Point2F(cx, cy)) * D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(cx, cy)) * old);
			vd::Text(rt, m_glyph, m_bounds, vd::Mix(m_col, m_accent, m_hover), vd::Style().Icon().Size(m_size).Center());
			rt->SetTransform(old);
			if (m_focused) vd::Ring(rt, cx, cy, m_size * 1.2f, vd::Alpha(m_accent, 0.5f), 1.0f);
		}
	};

	// -------------------------------------------------------------------------
	class VImage : public VirtualWidgetImpl {
	public:
		enum class Stretch { None, Fill, Uniform, UniformToFill };
	private:
		std::wstring m_path;
		Stretch m_stretch = Stretch::Uniform;
		float   m_radius = 0.0f, m_opacity = 1.0f;
		ComPtr<ID2D1Bitmap> m_bmp;
		ID2D1RenderTarget*  m_owner = nullptr;     // the target m_bmp was made for

		// The controller's WIC factory when the app initialised COM; otherwise
		// one of our own, made once, after joining the thread's apartment (a
		// virtual-widget app has no other reason to call CoInitialize).
		static ComPtr<IWICImagingFactory> Factory() {
			if (auto f = ChronoControllerImpl::Instance().m_pWICFactory) return f;
			static ComPtr<IWICImagingFactory> own;
			if (!own) {
				CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
				CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&own));
			}
			return own;
		}
		void Load(ID2D1RenderTarget* rt) {
			m_bmp.Reset(); m_owner = rt;
			auto wic = Factory();
			if (!wic || m_path.empty()) return;
			ComPtr<IWICBitmapDecoder> dec;
			if (FAILED(wic->CreateDecoderFromFilename(m_path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) return;
			ComPtr<IWICBitmapFrameDecode> frame;
			if (FAILED(dec->GetFrame(0, &frame))) return;
			ComPtr<IWICFormatConverter> conv;
			if (FAILED(wic->CreateFormatConverter(&conv))) return;
			if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut))) return;
			rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &m_bmp);
		}
		D2D1_RECT_F Dest() const {
			if (!m_bmp) return m_bounds;
			D2D1_SIZE_F s = m_bmp->GetSize();
			float bw = vd::W(m_bounds), bh = vd::H(m_bounds), sx = bw / s.width, sy = bh / s.height, k = 1.0f;
			switch (m_stretch) {
				case Stretch::None:          k = 1.0f; break;
				case Stretch::Fill:          return m_bounds;
				case Stretch::Uniform:       k = (std::min)(sx, sy); break;
				case Stretch::UniformToFill: k = (std::max)(sx, sy); break;
			}
			float w = s.width * k, h = s.height * k;
			return vd::Rect(vd::CX(m_bounds) - w * 0.5f, vd::CY(m_bounds) - h * 0.5f, w, h);
		}
	public:
		const char* GetTypeName() const override { return "VImage"; }
		VImage& Source(std::wstring path)   { m_path = std::move(path); m_bmp.Reset(); m_owner = nullptr; return *this; }
		VImage& Mode(Stretch s)             { m_stretch = s; return *this; }
		VImage& Radius(float r)             { m_radius = r; return *this; }
		VImage& Opacity(float a)            { m_opacity = vd::Clamp01(a); return *this; }
		bool Loaded() const                 { return m_bmp != nullptr; }
		D2D1_SIZE_F PixelSize() const       { return m_bmp ? m_bmp->GetSize() : D2D1::SizeF(0.0f, 0.0f); }

		void OnDraw(ID2D1RenderTarget* rt) override {
			if (m_owner != rt) Load(rt);
			if (!m_bmp) {
				vd::Fill(rt, m_bounds, vd::Col(0xF3F4F6), m_radius);
				vd::Stroke(rt, m_bounds, vd::Col(0xE5E7EB), m_radius, 1.0f);
				vd::Text(rt, L"\xEB9F", m_bounds, vd::Col(0x9CA3AF), vd::Style().Icon().Size((std::min)(32.0f, vd::H(m_bounds) * 0.4f)).Center());
				return;
			}
			ComPtr<ID2D1Layer> layer; ComPtr<ID2D1RoundedRectangleGeometry> geom;
			if (m_radius > 0.0f) {
				ComPtr<ID2D1Factory> f; rt->GetFactory(&f);
				if (f) f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(m_bounds, m_radius, m_radius), &geom);
				rt->CreateLayer(nullptr, &layer);
				if (layer && geom) rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geom.Get()), layer.Get());
			} else {
				rt->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			}
			rt->DrawBitmap(m_bmp.Get(), Dest(), m_opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
			if (m_radius > 0.0f) { if (layer && geom) rt->PopLayer(); }
			else rt->PopAxisAlignedClip();
		}
	};

	// -------------------------------------------------------------------------
	// VShape — points for Line and Polygon are fractions of the bounds (0..1),
	// so the shape follows the widget when the layout moves it.
	// -------------------------------------------------------------------------
	class VShape : public VirtualWidgetImpl {
	public:
		enum class Kind { Rectangle, Ellipse, Line, Polygon };
	private:
		Kind m_kind;
		D2D1_COLOR_F m_fill = vctl::Blue(), m_stroke = vd::Col(0x000000, 0.0f);
		float m_sw = 2.0f, m_radius = 0.0f;
		std::vector<D2D1_POINT_2F> m_pts;
		D2D1_POINT_2F Map(D2D1_POINT_2F p) const { return D2D1::Point2F(m_bounds.left + p.x * vd::W(m_bounds), m_bounds.top + p.y * vd::H(m_bounds)); }
	public:
		explicit VShape(Kind k) : m_kind(k) {}
		const char* GetTypeName() const override { return "VShape"; }
		VShape& Fill(D2D1_COLOR_F c)                     { m_fill = c; return *this; }
		VShape& Stroke(D2D1_COLOR_F c, float width = 2.0f) { m_stroke = c; m_sw = width; return *this; }
		VShape& Radius(float r)                          { m_radius = r; return *this; }
		VShape& Points(std::vector<D2D1_POINT_2F> pts)   { m_pts = std::move(pts); return *this; }
		void OnDraw(ID2D1RenderTarget* rt) override {
			D2D1_RECT_F r = vd::Inset(m_bounds, m_sw * 0.5f, m_sw * 0.5f);
			switch (m_kind) {
				case Kind::Rectangle:
					if (m_fill.a > 0.0f)   vd::Fill(rt, r, m_fill, m_radius);
					if (m_stroke.a > 0.0f) vd::Stroke(rt, r, m_stroke, m_radius, m_sw);
					break;
				case Kind::Ellipse: {
					D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(vd::CX(r), vd::CY(r)), vd::W(r) * 0.5f, vd::H(r) * 0.5f);
					if (m_fill.a > 0.0f)   { auto b = vd::Brush(rt, m_fill);   if (b) rt->FillEllipse(e, b.Get()); }
					if (m_stroke.a > 0.0f) { auto b = vd::Brush(rt, m_stroke); if (b) rt->DrawEllipse(e, b.Get(), m_sw); }
					break;
				}
				case Kind::Line: {
					D2D1_POINT_2F a = m_pts.size() >= 2 ? Map(m_pts[0]) : D2D1::Point2F(m_bounds.left, vd::CY(m_bounds));
					D2D1_POINT_2F b = m_pts.size() >= 2 ? Map(m_pts[1]) : D2D1::Point2F(m_bounds.right, vd::CY(m_bounds));
					vd::Line(rt, a.x, a.y, b.x, b.y, m_stroke.a > 0.0f ? m_stroke : m_fill, m_sw);
					break;
				}
				case Kind::Polygon: {
					std::vector<D2D1_POINT_2F> pts; for (const auto& p : m_pts) pts.push_back(Map(p));
					if (m_fill.a > 0.0f)   vd::Polyline(rt, pts, m_fill, 1.0f, true);
					if (m_stroke.a > 0.0f && !pts.empty()) { pts.push_back(pts[0]); vd::Polyline(rt, pts, m_stroke, m_sw); }
					break;
				}
			}
		}
	};

} // namespace ChronoUI

// =============================================================================
// VLayout.hpp — scrolling and layout, WinUI 3 vocabulary.
//
//   VScrollViewer  a viewport over content painted by a callback: wheel scrolls,
//                  Shift+wheel sideways, Ctrl+wheel zooms about the cursor,
//                  drag pans, thumbs on both edges
//   VSplitView     a pane beside the content: Inline / Overlay, compact or not;
//                  the app places widgets from PaneRect() and ContentRect()
//   VTwoPaneView   two areas side by side when wide, stacked when tall, with a
//                  draggable divider; Pane1Rect() / Pane2Rect()
//
// The virtual model has no parent-child tree: a container here is a widget
// that owns the geometry and the motion and reports both through OnLayout,
// exactly like VExpander and VNavView. The app's single layout() function
// places the children from the rects each container exposes.
// Z-order rule for VSplitView in an overlay mode: add it AFTER the content
// widgets and BEFORE the pane widgets, so its scrim covers the content and
// the pane's widgets paint on top of its pane.
// =============================================================================

#pragma once

#include "VirtualWidget.hpp"
#include "VControls.hpp"
#include "VDraw.hpp"
#include <cmath>
#include <functional>

namespace ChronoUI {

	// -------------------------------------------------------------------------
	class VScrollViewer : public VirtualWidgetImpl {
	public:
		using Painter = std::function<void(ID2D1RenderTarget*, const D2D1_RECT_F&)>;   // content space: (0, 0, w, h)
	private:
		Painter m_paint;
		float m_cw = 0.0f, m_ch = 0.0f, m_sx = 0.0f, m_sy = 0.0f, m_zoom = 1.0f;
		bool  m_zoomable = true, m_border = true;
		int   m_drag = 0, m_hover = 0;               // 1 = vertical thumb, 2 = horizontal thumb, 3 = pan
		float m_dragOff = 0.0f, m_lastX = 0.0f, m_lastY = 0.0f;
		static constexpr float kRail = 10.0f;

		float MaxX() const { return (std::max)(0.0f, m_cw * m_zoom - vd::W(m_bounds)); }
		float MaxY() const { return (std::max)(0.0f, m_ch * m_zoom - vd::H(m_bounds)); }
		void  Clamp()      { m_sx = vd::Clamp(m_sx, 0.0f, MaxX()); m_sy = vd::Clamp(m_sy, 0.0f, MaxY()); }
		D2D1_RECT_F VThumb() const {
			float H = vd::H(m_bounds) - 8.0f, th = (std::max)(24.0f, H * vd::H(m_bounds) / (m_ch * m_zoom));
			float ty = m_bounds.top + 4.0f + (H - th) * (MaxY() > 0.0f ? m_sy / MaxY() : 0.0f);
			return vd::Rect(m_bounds.right - 8.0f, ty, 5.0f, th);
		}
		D2D1_RECT_F HThumb() const {
			float W = vd::W(m_bounds) - 8.0f, tw = (std::max)(24.0f, W * vd::W(m_bounds) / (m_cw * m_zoom));
			float tx = m_bounds.left + 4.0f + (W - tw) * (MaxX() > 0.0f ? m_sx / MaxX() : 0.0f);
			return vd::Rect(tx, m_bounds.bottom - 8.0f, tw, 5.0f);
		}
		void ZoomAt(float x, float y, float factor) {
			float z = vd::Clamp(m_zoom * factor, 0.25f, 8.0f);
			float cx = (x - m_bounds.left + m_sx) / m_zoom, cy = (y - m_bounds.top + m_sy) / m_zoom;   // the content point under the cursor
			m_zoom = z; m_sx = cx * z - (x - m_bounds.left); m_sy = cy * z - (y - m_bounds.top); Clamp();
		}
	public:
		const char* GetTypeName() const override { return "VScrollViewer"; }
		bool CanFocus() const override { return true; }
		VScrollViewer& Content(float w, float h)   { m_cw = w; m_ch = h; Clamp(); return *this; }
		VScrollViewer& Paint(Painter p)            { m_paint = std::move(p); return *this; }
		VScrollViewer& Zoomable(bool z)            { m_zoomable = z; return *this; }
		VScrollViewer& Zoom(float z)               { m_zoom = vd::Clamp(z, 0.25f, 8.0f); Clamp(); return *this; }
		VScrollViewer& Border(bool b)              { m_border = b; return *this; }
		void  ScrollTo(float x, float y)           { m_sx = x; m_sy = y; Clamp(); }
		float ScrollX() const { return m_sx; }
		float ScrollY() const { return m_sy; }
		float ZoomFactor() const { return m_zoom; }

		VInputResult OnMouseMove(float x, float y) override {
			if (m_drag == 1) { float H = vd::H(m_bounds) - 8.0f, th = vd::H(VThumb()); m_sy = (H > th) ? (y - m_dragOff - m_bounds.top - 4.0f) / (H - th) * MaxY() : 0.0f; Clamp(); return VInputResult::Handled; }
			if (m_drag == 2) { float W = vd::W(m_bounds) - 8.0f, tw = vd::W(HThumb()); m_sx = (W > tw) ? (x - m_dragOff - m_bounds.left - 4.0f) / (W - tw) * MaxX() : 0.0f; Clamp(); return VInputResult::Handled; }
			if (m_drag == 3) { m_sx -= x - m_lastX; m_sy -= y - m_lastY; m_lastX = x; m_lastY = y; Clamp(); return VInputResult::Handled; }
			int h = (MaxY() > 0.0f && vd::Contains(vd::Inset(VThumb(), -4.0f, 0.0f), x, y)) ? 1 : ((MaxX() > 0.0f && vd::Contains(vd::Inset(HThumb(), 0.0f, -4.0f), x, y)) ? 2 : 0);
			if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = 0; return VInputResult::Handled; }
		VInputResult OnMouseDown(float x, float y, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			if (MaxY() > 0.0f && vd::Contains(vd::Inset(VThumb(), -4.0f, 0.0f), x, y)) { m_drag = 1; m_dragOff = y - VThumb().top;  return VInputResult::Capture; }
			if (MaxX() > 0.0f && vd::Contains(vd::Inset(HThumb(), 0.0f, -4.0f), x, y)) { m_drag = 2; m_dragOff = x - HThumb().left; return VInputResult::Capture; }
			m_drag = 3; m_lastX = x; m_lastY = y; return VInputResult::Capture;
		}
		VInputResult OnMouseUp(float, float, int) override { m_drag = 0; return VInputResult::Handled; }
		VInputResult OnMouseWheel(float delta, float x, float y) override {
			bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
			if (ctrl && m_zoomable) { ZoomAt(x, y, delta > 0 ? 1.15f : 1.0f / 1.15f); return VInputResult::Handled; }
			float step = -(delta / 120.0f) * 48.0f;
			if (shift) { if (MaxX() <= 0.0f) return VInputResult::NotHandled; m_sx += step; }
			else       { if (MaxY() <= 0.0f) return VInputResult::NotHandled; m_sy += step; }
			Clamp(); return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			switch (vk) {
				case VK_UP:    m_sy -= 40.0f; break;               case VK_DOWN:  m_sy += 40.0f; break;
				case VK_LEFT:  m_sx -= 40.0f; break;               case VK_RIGHT: m_sx += 40.0f; break;
				case VK_PRIOR: m_sy -= vd::H(m_bounds); break;     case VK_NEXT:  m_sy += vd::H(m_bounds); break;
				case VK_HOME:  m_sy = 0.0f; break;                 case VK_END:   m_sy = MaxY(); break;
				case VK_ADD: case VK_OEM_PLUS:      if (m_zoomable) ZoomAt(vd::CX(m_bounds), vd::CY(m_bounds), 1.15f); break;
				case VK_SUBTRACT: case VK_OEM_MINUS: if (m_zoomable) ZoomAt(vd::CX(m_bounds), vd::CY(m_bounds), 1.0f / 1.15f); break;
				default: return VInputResult::NotHandled;
			}
			Clamp(); return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			if (m_border) { vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 8.0f); vd::Stroke(rt, m_bounds, m_focused ? vd::Alpha(vctl::Blue(), 0.6f) : vd::Col(0xE5E7EB), 8.0f, 1.0f); }
			rt->PushAxisAlignedClip(vd::Inset(m_bounds, 1.0f, 1.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			if (m_paint) {
				D2D1_MATRIX_3X2_F old; rt->GetTransform(&old);
				rt->SetTransform(D2D1::Matrix3x2F::Scale(m_zoom, m_zoom) * D2D1::Matrix3x2F::Translation(m_bounds.left - m_sx, m_bounds.top - m_sy) * old);
				m_paint(rt, vd::Rect(0.0f, 0.0f, m_cw, m_ch));
				rt->SetTransform(old);
			}
			if (MaxY() > 0.0f) vd::Fill(rt, VThumb(), vd::Col(0x000000, m_hover == 1 || m_drag == 1 ? 0.4f : 0.22f), 2.5f);
			if (MaxX() > 0.0f) vd::Fill(rt, HThumb(), vd::Col(0x000000, m_hover == 2 || m_drag == 2 ? 0.4f : 0.22f), 2.5f);
			if (fabsf(m_zoom - 1.0f) > 0.01f) {
				D2D1_RECT_F pill = vd::Rect(m_bounds.left + 8.0f, m_bounds.bottom - 30.0f, 52.0f, 22.0f);
				vd::Fill(rt, pill, vd::Col(0x1F2937, 0.85f), 11.0f);
				vd::Text(rt, vd::Num(m_zoom * 100.0) + L"%", pill, vd::Col(0xFFFFFF), vd::Style().Size(11).Bold().Center());
			}
			rt->PopAxisAlignedClip();
		}
	};

	// -------------------------------------------------------------------------
	class VSplitView : public VirtualWidgetImpl {
	public:
		enum class Mode { Overlay, Inline, CompactOverlay, CompactInline };
	private:
		Mode  m_mode = Mode::Inline;
		bool  m_open = true;
		float m_anim = 1.0f, m_paneW = 300.0f, m_compact = 48.0f;
		D2D1_COLOR_F m_paneBg = vd::Col(0xF3F3F3);
		std::function<void()>     m_layout;
		std::function<void(bool)> m_cb;
		bool  Compact() const { return m_mode == Mode::CompactOverlay || m_mode == Mode::CompactInline; }
		bool  Overlay() const { return m_mode == Mode::Overlay || m_mode == Mode::CompactOverlay; }
		float ClosedW() const { return Compact() ? m_compact : 0.0f; }
		float ShownW() const  { return ClosedW() + (m_paneW - ClosedW()) * m_anim; }
	public:
		const char* GetTypeName() const override { return "VSplitView"; }
		VSplitView& SetMode(Mode m)                { m_mode = m; return *this; }
		VSplitView& PaneWidth(float w)             { m_paneW = w; return *this; }
		VSplitView& CompactWidth(float w)          { m_compact = w; return *this; }
		VSplitView& PaneBackground(D2D1_COLOR_F c) { m_paneBg = c; return *this; }
		VSplitView& Open(bool o) { if (m_open != o) { m_open = o; if (m_cb) m_cb(o); } return *this; }
		void Toggle()            { Open(!m_open); }
		bool IsOpen() const      { return m_open; }
		Mode GetMode() const     { return m_mode; }
		// Where the children go. PaneVisible: the pane has some width; PaneOpen:
		// it is (nearly) fully open, the moment to show its labels.
		D2D1_RECT_F PaneRect() const    { return vd::Rect(m_bounds.left, m_bounds.top, ShownW(), vd::H(m_bounds)); }
		D2D1_RECT_F ContentRect() const {
			float x = Overlay() ? ClosedW() : ShownW();
			return D2D1::RectF(m_bounds.left + x, m_bounds.top, m_bounds.right, m_bounds.bottom);
		}
		bool  PaneVisible() const { return ShownW() > 0.5f; }
		bool  PaneOpen() const    { return m_open && m_anim > 0.85f; }
		float PaneShownWidth() const { return ShownW(); }
		void OnLayout(std::function<void()> cb)     { m_layout = std::move(cb); }
		void OnChange(std::function<void(bool)> cb) { m_cb = std::move(cb); }

		bool HitTest(float x, float y) const override {
			if (Overlay() && m_open) return vd::Contains(m_bounds, x, y);       // the scrim takes the click
			return PaneVisible() && vd::Contains(PaneRect(), x, y);
		}
		bool OnUpdate(float dt) override {
			float t = m_open ? 1.0f : 0.0f;
			if (fabsf(m_anim - t) < 0.004f) { if (m_anim == t) return false; m_anim = t; if (m_layout) m_layout(); return true; }
			m_anim = vd::Approach(m_anim, t, dt, 16.0f);
			if (m_layout) m_layout();
			return true;
		}
		VInputResult OnMouseDown(float x, float y, int) override {
			if (Overlay() && m_open && !vd::Contains(PaneRect(), x, y)) { Open(false); return VInputResult::Handled; }
			return VInputResult::Handled;                                     // the pane's floor: nothing under it reacts
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			if (Overlay() && m_anim > 0.01f) vd::Fill(rt, ContentRect(), vd::Col(0x000000, 0.25f * m_anim));
			if (!PaneVisible()) return;
			D2D1_RECT_F p = PaneRect();
			if (Overlay() && m_anim > 0.01f) vd::Shadow(rt, p, 0.0f, 0.18f * m_anim, 5);
			vd::Fill(rt, p, m_paneBg);
			vd::Line(rt, p.right - 0.5f, p.top, p.right - 0.5f, p.bottom, vd::Col(0xE5E7EB), 1.0f);
		}
	};

	// -------------------------------------------------------------------------
	class VTwoPaneView : public VirtualWidgetImpl {
	public:
		enum class Mode { Auto, Wide, Tall };
	private:
		Mode  m_mode = Mode::Auto;
		float m_threshold = 720.0f, m_split = 0.5f, m_gap = 12.0f;
		bool  m_wide = true, m_drag = false, m_hover = false;
		std::function<void()> m_layout;
		D2D1_RECT_F Divider() const {
			if (Wide()) { float x = m_bounds.left + (vd::W(m_bounds) - m_gap) * m_split; return vd::Rect(x, m_bounds.top, m_gap, vd::H(m_bounds)); }
			float y = m_bounds.top + (vd::H(m_bounds) - m_gap) * m_split; return vd::Rect(m_bounds.left, y, vd::W(m_bounds), m_gap);
		}
	public:
		const char* GetTypeName() const override { return "VTwoPaneView"; }
		VTwoPaneView& SetMode(Mode m)          { m_mode = m; return *this; }
		VTwoPaneView& Threshold(float px)      { m_threshold = px; return *this; }   // Auto: side by side from this width
		VTwoPaneView& Split(float f)           { m_split = vd::Clamp(f, 0.15f, 0.85f); return *this; }
		VTwoPaneView& Gap(float g)             { m_gap = g; return *this; }
		bool  Wide() const { return m_mode == Mode::Wide || (m_mode == Mode::Auto && vd::W(m_bounds) >= m_threshold); }
		float SplitValue() const { return m_split; }
		D2D1_RECT_F Pane1Rect() const { D2D1_RECT_F d = Divider(); return Wide() ? D2D1::RectF(m_bounds.left, m_bounds.top, d.left, m_bounds.bottom) : D2D1::RectF(m_bounds.left, m_bounds.top, m_bounds.right, d.top); }
		D2D1_RECT_F Pane2Rect() const { D2D1_RECT_F d = Divider(); return Wide() ? D2D1::RectF(d.right, m_bounds.top, m_bounds.right, m_bounds.bottom) : D2D1::RectF(m_bounds.left, d.bottom, m_bounds.right, m_bounds.bottom); }
		void OnLayout(std::function<void()> cb) { m_layout = std::move(cb); }   // the mode flipped or the divider moved

		void SetBounds(const D2D1_RECT_F& r) override {
			VirtualWidgetImpl::SetBounds(r);
			bool w = Wide();
			if (w != m_wide) { m_wide = w; if (m_layout) m_layout(); }
		}
		bool HitTest(float x, float y) const override { return vd::Contains(vd::Inset(Divider(), Wide() ? -2.0f : 0.0f, Wide() ? 0.0f : -2.0f), x, y); }
		VInputResult OnMouseEnter() override { m_hover = true;  return VInputResult::Handled; }
		VInputResult OnMouseLeave() override { m_hover = false; return VInputResult::Handled; }
		VInputResult OnMouseDown(float, float, int btn) override { if (btn != 1) return VInputResult::NotHandled; m_drag = true; return VInputResult::Capture; }
		VInputResult OnMouseMove(float x, float y) override {
			if (!m_drag) return VInputResult::NotHandled;
			float f = Wide() ? (x - m_bounds.left - m_gap * 0.5f) / (vd::W(m_bounds) - m_gap) : (y - m_bounds.top - m_gap * 0.5f) / (vd::H(m_bounds) - m_gap);
			Split(f); if (m_layout) m_layout();
			return VInputResult::Handled;
		}
		VInputResult OnMouseUp(float, float, int) override { m_drag = false; return VInputResult::Handled; }
		void OnDraw(ID2D1RenderTarget* rt) override {
			D2D1_RECT_F d = Divider();
			float cx = vd::CX(d), cy = vd::CY(d);
			D2D1_COLOR_F c = (m_hover || m_drag) ? vctl::Blue() : vd::Col(0xD1D5DB);
			if (m_hover || m_drag) vd::Fill(rt, Wide() ? vd::Rect(cx - 1.0f, d.top, 2.0f, vd::H(d)) : vd::Rect(d.left, cy - 1.0f, vd::W(d), 2.0f), vd::Alpha(c, 0.5f));
			for (int k = -1; k <= 1; ++k)
				if (Wide()) vd::Circle(rt, cx, cy + 7.0f * (float)k, 1.8f, c); else vd::Circle(rt, cx + 7.0f * (float)k, cy, 1.8f, c);
		}
	};

} // namespace ChronoUI

// =============================================================================
// VText.hpp — the text family, WinUI 3 vocabulary.
//
//   VNumberBox    a numeric field: spin buttons, min / max / step, decimals,
//                 and it evaluates what you type ("2*(3+4)" commits 14)
//   VPasswordBox  bullets while typing, an eye that reveals while held
//   VRichText     a paragraph with **bold**, *italic*, `code` and [links](url)
//
// The two fields share vtext::Line, a one-line editor (text + caret + the keys
// every text field answers). VChatInput (VirtualChat.hpp) is the multi-line
// editor with selection and clipboard; these stay single-line on purpose.
// Same conventions as VControls.hpp: chainable setters, OnChange callbacks.
// =============================================================================

#pragma once

#include "VirtualWidget.hpp"
#include "VControls.hpp"
#include "VDraw.hpp"
#include <cmath>
#include <cwchar>
#include <functional>
#include <string>
#include <vector>

namespace ChronoUI {

	namespace vtext {
		// A one-line editor: the text, the caret, and the keys every field answers.
		struct Line {
			std::wstring text;
			int caret = 0;
			void Set(std::wstring s) { text = std::move(s); caret = (int)text.size(); }
			bool Key(UINT vk) {
				int n = (int)text.size();
				switch (vk) {
					case VK_LEFT:   if (caret > 0) --caret; return true;
					case VK_RIGHT:  if (caret < n) ++caret; return true;
					case VK_HOME:   caret = 0; return true;
					case VK_END:    caret = n; return true;
					case VK_BACK:   if (caret > 0) { text.erase((size_t)caret - 1, 1); --caret; } return true;
					case VK_DELETE: if (caret < n) text.erase((size_t)caret, 1); return true;
					default: return false;
				}
			}
			bool Char(wchar_t ch) {
				if (ch < 32 || ch == 127) return false;
				text.insert((size_t)caret, 1, ch); ++caret; return true;
			}
			// The caret index nearest to x when `shown` starts at x0.
			int IndexAt(float x, float x0, const std::wstring& shown, const vd::Style& st) const {
				int best = 0; float bd = 1e9f;
				for (int i = 0; i <= (int)shown.size(); ++i) {
					float d = fabsf(x0 + vd::TextWidth(shown.substr(0, (size_t)i), st) - x);
					if (d < bd) { bd = d; best = i; }
				}
				return best;
			}
		};

		// The text (or the placeholder) in r, and the caret when focused.
		inline void Draw(ID2D1RenderTarget* rt, const Line& ln, const std::wstring& shown, const std::wstring& placeholder,
		                 const D2D1_RECT_F& r, bool focused, D2D1_COLOR_F ink, const vd::Style& st)
		{
			rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			if (shown.empty()) vd::Text(rt, placeholder, r, vd::Col(0x9CA3AF), st);
			else               vd::Text(rt, shown, r, ink, st);
			if (focused) {
				float cx = r.left + vd::TextWidth(shown.substr(0, (size_t)ln.caret), st) + 0.5f;
				vd::Line(rt, cx, vd::CY(r) - st.size * 0.65f, cx, vd::CY(r) + st.size * 0.65f, ink, 1.0f);
			}
			rt->PopAxisAlignedClip();
		}

		// "2*(3+4)^2" -> 98. Numbers, + - * / % ^, parentheses, unary minus.
		struct Eval {
			const wchar_t* p; bool ok = true;
			void Ws() { while (*p == L' ') ++p; }
			double Primary() {
				Ws();
				if (*p == L'(') { ++p; double v = Expr(); Ws(); if (*p == L')') ++p; else ok = false; return v; }
				if (*p == L'-') { ++p; return -Primary(); }
				if (*p == L'+') { ++p; return Primary(); }
				wchar_t* e = nullptr; double v = wcstod(p, &e);
				if (e == p) { ok = false; return 0.0; }
				p = e; return v;
			}
			double Power() { double b = Primary(); Ws(); if (*p == L'^') { ++p; return pow(b, Power()); } return b; }
			double Term() {
				double v = Power();
				for (;;) {
					Ws();
					if (*p == L'*')      { ++p; v *= Power(); }
					else if (*p == L'/') { ++p; double d = Power(); if (d == 0.0) ok = false; else v /= d; }
					else if (*p == L'%') { ++p; double d = Power(); if (d == 0.0) ok = false; else v = fmod(v, d); }
					else return v;
				}
			}
			double Expr() {
				double v = Term();
				for (;;) { Ws(); if (*p == L'+') { ++p; v += Term(); } else if (*p == L'-') { ++p; v -= Term(); } else return v; }
			}
		};
		inline bool Evaluate(const std::wstring& s, double& out) {
			Eval e{ s.c_str() }; double v = e.Expr(); e.Ws();
			if (!e.ok || *e.p != 0 || !std::isfinite(v)) return false;
			out = v; return true;
		}
	}

	// -------------------------------------------------------------------------
	class VNumberBox : public VirtualWidgetImpl {
		vtext::Line m_ed;
		double m_v = 0.0, m_min = -1e12, m_max = 1e12, m_step = 1.0;
		int    m_dec = 0, m_hover = 0;                 // hover: -1 down, +1 up
		bool   m_spin = true, m_err = false;
		std::wstring m_placeholder = L"0";
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(double)> m_cb;
		static constexpr float kSpin = 26.0f;

		vd::Style St() const { return vd::Style().Size(13); }
		D2D1_RECT_F UpRect() const   { return vd::Rect(m_bounds.right - 2.0f * kSpin, m_bounds.top, kSpin, vd::H(m_bounds)); }
		D2D1_RECT_F DownRect() const { return vd::Rect(m_bounds.right - kSpin, m_bounds.top, kSpin, vd::H(m_bounds)); }
		D2D1_RECT_F TextRect() const { return D2D1::RectF(m_bounds.left + 10.0f, m_bounds.top, (m_spin ? UpRect().left : m_bounds.right) - 8.0f, m_bounds.bottom); }
		std::wstring Fmt(double v) const { wchar_t b[64]; swprintf_s(b, L"%.*f", m_dec, v); return b; }
		void Apply(double v, bool fire) {
			double k = pow(10.0, m_dec);
			v = round(v * k) / k;
			v = v < m_min ? m_min : (v > m_max ? m_max : v);
			bool changed = v != m_v;
			m_v = v; m_ed.Set(Fmt(v)); m_err = false;
			if (fire && changed && m_cb) m_cb(m_v);
		}
		void Commit() {
			if (m_ed.text.empty()) { m_ed.Set(Fmt(m_v)); m_err = false; return; }
			double v; if (vtext::Evaluate(m_ed.text, v)) Apply(v, true); else m_err = true;
		}
		void Nudge(int d) { Commit(); Apply(m_v + (double)d * m_step, true); }
	public:
		VNumberBox() { m_ed.Set(Fmt(0.0)); }
		const char* GetTypeName() const override { return "VNumberBox"; }
		bool CanFocus() const override { return true; }
		VNumberBox& Range(double lo, double hi)   { m_min = lo; m_max = hi; return *this; }
		VNumberBox& Step(double s)                { m_step = s; return *this; }
		VNumberBox& Decimals(int n)               { m_dec = (std::max)(0, (std::min)(6, n)); m_ed.Set(Fmt(m_v)); return *this; }
		VNumberBox& Set(double v)                 { Apply(v, false); return *this; }
		VNumberBox& Spin(bool on)                 { m_spin = on; return *this; }
		VNumberBox& Placeholder(std::wstring p)   { m_placeholder = std::move(p); return *this; }
		VNumberBox& Accent(D2D1_COLOR_F c)        { m_accent = c; return *this; }
		double Value() const                      { return m_v; }
		bool   HasError() const                   { return m_err; }
		void OnChange(std::function<void(double)> cb) { m_cb = std::move(cb); }

		void OnFocus(bool gained) override { if (!gained) Commit(); }
		VInputResult OnMouseMove(float x, float y) override {
			int h = m_spin ? (vd::Contains(UpRect(), x, y) ? 1 : (vd::Contains(DownRect(), x, y) ? -1 : 0)) : 0;
			if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = 0; return VInputResult::Handled; }
		VInputResult OnMouseDown(float x, float y, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			if (m_spin && vd::Contains(UpRect(), x, y))   { Nudge(+1); return VInputResult::Handled; }
			if (m_spin && vd::Contains(DownRect(), x, y)) { Nudge(-1); return VInputResult::Handled; }
			m_ed.caret = m_ed.IndexAt(x, TextRect().left, m_ed.text, St());
			return VInputResult::Handled;
		}
		VInputResult OnMouseWheel(float delta, float, float) override { Nudge(delta > 0 ? 1 : -1); return VInputResult::Handled; }
		VInputResult OnKeyDown(UINT vk) override {
			switch (vk) {
				case VK_UP:     Nudge(+1); return VInputResult::Handled;
				case VK_DOWN:   Nudge(-1); return VInputResult::Handled;
				case VK_RETURN: Commit(); return VInputResult::Handled;
				case VK_ESCAPE: m_ed.Set(Fmt(m_v)); m_err = false; return VInputResult::Handled;
				default:        return m_ed.Key(vk) ? VInputResult::Handled : VInputResult::NotHandled;
			}
		}
		VInputResult OnChar(wchar_t ch) override {
			if (!m_ed.Char(ch)) return VInputResult::NotHandled;
			m_err = false; return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 6.0f);
			D2D1_COLOR_F border = m_err ? vd::Col(0xDC2626) : (m_focused ? m_accent : vd::Col(0xD1D5DB));
			vd::Stroke(rt, m_bounds, border, 6.0f, m_focused || m_err ? 1.5f : 1.0f);
			vtext::Draw(rt, m_ed, m_ed.text, m_placeholder, TextRect(), m_focused, m_err ? vd::Col(0xDC2626) : vctl::Ink(), St());
			if (!m_spin) return;
			D2D1_RECT_F up = UpRect(), dn = DownRect();
			vd::Line(rt, up.left, m_bounds.top + 6.0f, up.left, m_bounds.bottom - 6.0f, vd::Col(0xE5E7EB), 1.0f);
			if (m_hover == 1)  vd::Fill(rt, vd::Inset(up, 3.0f, 4.0f), vctl::Track(), 4.0f);
			if (m_hover == -1) vd::Fill(rt, vd::Inset(dn, 3.0f, 4.0f), vctl::Track(), 4.0f);
			vd::Chevron(rt, vd::CX(up), vd::CY(up) + 1.0f, 180.0f, m_v < m_max ? vctl::Ink() : vd::Col(0xD1D5DB), 4.0f);
			vd::Chevron(rt, vd::CX(dn), vd::CY(dn) - 1.0f, 0.0f,   m_v > m_min ? vctl::Ink() : vd::Col(0xD1D5DB), 4.0f);
		}
	};

	// -------------------------------------------------------------------------
	class VPasswordBox : public VirtualWidgetImpl {
		vtext::Line m_ed;
		std::wstring m_placeholder = L"Password";
		bool m_reveal = false, m_hoverEye = false;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(const std::wstring&)> m_cb, m_submit;
		static constexpr float kEye = 32.0f;
		vd::Style St() const { return vd::Style().Size(13); }
		D2D1_RECT_F EyeRect() const  { return vd::Rect(m_bounds.right - kEye - 2.0f, m_bounds.top, kEye, vd::H(m_bounds)); }
		D2D1_RECT_F TextRect() const { return D2D1::RectF(m_bounds.left + 10.0f, m_bounds.top, EyeRect().left - 4.0f, m_bounds.bottom); }
		std::wstring Shown() const   { return m_reveal ? m_ed.text : std::wstring(m_ed.text.size(), L'\x25CF'); }
		void Fire() { if (m_cb) m_cb(m_ed.text); }
	public:
		const char* GetTypeName() const override { return "VPasswordBox"; }
		bool CanFocus() const override { return true; }
		VPasswordBox& Placeholder(std::wstring p) { m_placeholder = std::move(p); return *this; }
		VPasswordBox& Set(std::wstring s)         { m_ed.Set(std::move(s)); return *this; }
		VPasswordBox& Accent(D2D1_COLOR_F c)      { m_accent = c; return *this; }
		const std::wstring& Value() const         { return m_ed.text; }
		void OnChange(std::function<void(const std::wstring&)> cb) { m_cb = std::move(cb); }
		void OnSubmit(std::function<void(const std::wstring&)> cb) { m_submit = std::move(cb); }   // Enter

		VInputResult OnMouseMove(float x, float y) override {
			bool h = vd::Contains(EyeRect(), x, y);
			if (h == m_hoverEye) return VInputResult::NotHandled;
			m_hoverEye = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hoverEye = false; return VInputResult::Handled; }
		VInputResult OnMouseDown(float x, float y, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			if (vd::Contains(EyeRect(), x, y)) { m_reveal = true; return VInputResult::Capture; }   // shown while held
			m_ed.caret = m_ed.IndexAt(x, TextRect().left, Shown(), St());
			return VInputResult::Handled;
		}
		VInputResult OnMouseUp(float, float, int) override { m_reveal = false; return VInputResult::Handled; }
		VInputResult OnKeyDown(UINT vk) override {
			if (vk == VK_RETURN) { if (m_submit) m_submit(m_ed.text); return VInputResult::Handled; }
			if (!m_ed.Key(vk)) return VInputResult::NotHandled;
			if (vk == VK_BACK || vk == VK_DELETE) Fire();
			return VInputResult::Handled;
		}
		VInputResult OnChar(wchar_t ch) override {
			if (!m_ed.Char(ch)) return VInputResult::NotHandled;
			Fire(); return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 6.0f);
			vd::Stroke(rt, m_bounds, m_focused ? m_accent : vd::Col(0xD1D5DB), 6.0f, m_focused ? 1.5f : 1.0f);
			vtext::Draw(rt, m_ed, Shown(), m_placeholder, TextRect(), m_focused, vctl::Ink(), St());
			D2D1_RECT_F e = EyeRect();
			if (m_hoverEye || m_reveal) vd::Fill(rt, vd::Inset(e, 4.0f, 5.0f), m_reveal ? vd::Alpha(m_accent, 0.15f) : vctl::Track(), 4.0f);
			vd::Text(rt, m_reveal ? L"\xED1A" : L"\xE7B3", e, m_reveal ? m_accent : vctl::Muted(), vd::Style().Icon().Size(14).Center());
		}
	};

	// -------------------------------------------------------------------------
	// VRichText — a wrapped paragraph from a little markup: **bold**, *italic*,
	// `code`, [text](url), \n for a line break, a backslash escapes. Links
	// underline, light up under the mouse and fire OnLink with their url.
	// MeasureHeight(width) is for the app's layout.
	// -------------------------------------------------------------------------
	class VRichText : public VirtualWidgetImpl {
	public:
		struct Run { UINT32 start = 0, len = 0; bool bold = false, italic = false, code = false; std::wstring url; };
	private:
		std::wstring     m_text;
		std::vector<Run> m_runs;
		float m_size = 13.0f;
		int   m_hoverLink = -1;
		D2D1_COLOR_F m_ink = vctl::Ink(), m_accent = vctl::Blue();
		std::function<void(const std::wstring&)> m_cb;

		ComPtr<IDWriteTextLayout> Layout(float width) const {
			auto f = vd::Format(vd::Style().Size(m_size).Wrap().Top()); if (!f) return nullptr;
			ComPtr<IDWriteTextLayout> l;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(m_text.c_str(), (UINT32)m_text.size(), f.Get(), (std::max)(1.0f, width), 1e4f, &l);
			if (!l) return nullptr;
			for (const Run& r : m_runs) {
				DWRITE_TEXT_RANGE tr{ r.start, r.len };
				if (r.bold)         l->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, tr);
				if (r.italic)       l->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, tr);
				if (r.code)         { l->SetFontFamilyName(L"Consolas", tr); l->SetFontSize(m_size * 0.95f, tr); }
				if (!r.url.empty()) l->SetUnderline(TRUE, tr);
			}
			return l;
		}
		int LinkAt(float x, float y) const {
			auto l = Layout(vd::W(m_bounds)); if (!l) return -1;
			BOOL trailing = FALSE, inside = FALSE; DWRITE_HIT_TEST_METRICS m{};
			l->HitTestPoint(x - m_bounds.left, y - m_bounds.top, &trailing, &inside, &m);
			if (!inside) return -1;
			for (size_t i = 0; i < m_runs.size(); ++i) {
				const Run& r = m_runs[i];
				if (!r.url.empty() && m.textPosition >= r.start && m.textPosition < r.start + r.len) return (int)i;
			}
			return -1;
		}
	public:
		const char* GetTypeName() const override { return "VRichText"; }
		VRichText& FontSize(float px)      { m_size = px; return *this; }
		VRichText& Color(D2D1_COLOR_F c)   { m_ink = c; return *this; }
		VRichText& Accent(D2D1_COLOR_F c)  { m_accent = c; return *this; }
		const std::wstring& PlainText() const { return m_text; }
		void OnLink(std::function<void(const std::wstring&)> cb) { m_cb = std::move(cb); }

		VRichText& Set(const std::wstring& src) {
			m_text.clear(); m_runs.clear(); m_hoverLink = -1;
			bool bold = false, italic = false, code = false;
			size_t bStart = 0, iStart = 0, cStart = 0;
			auto push = [&](size_t start, bool b, bool i, bool c) {
				if (m_text.size() <= start) return;
				Run r; r.start = (UINT32)start; r.len = (UINT32)(m_text.size() - start); r.bold = b; r.italic = i; r.code = c;
				m_runs.push_back(std::move(r));
			};
			for (size_t i = 0; i < src.size(); ++i) {
				wchar_t ch = src[i];
				if (code) { if (ch == L'`') { push(cStart, false, false, true); code = false; } else m_text += ch; continue; }
				if (ch == L'`') { code = true; cStart = m_text.size(); continue; }
				if (ch == L'*' && i + 1 < src.size() && src[i + 1] == L'*') {
					if (bold) push(bStart, true, false, false); else bStart = m_text.size();
					bold = !bold; ++i; continue;
				}
				if (ch == L'*') {
					if (italic) push(iStart, false, true, false); else iStart = m_text.size();
					italic = !italic; continue;
				}
				if (ch == L'[') {
					size_t close = src.find(L']', i);
					size_t open  = (close != std::wstring::npos && close + 1 < src.size() && src[close + 1] == L'(') ? close + 1 : std::wstring::npos;
					size_t end   = open != std::wstring::npos ? src.find(L')', open) : std::wstring::npos;
					if (end != std::wstring::npos) {
						std::wstring label = src.substr(i + 1, close - i - 1);
						Run r; r.start = (UINT32)m_text.size(); r.len = (UINT32)label.size(); r.url = src.substr(open + 1, end - open - 1);
						m_text += label; m_runs.push_back(std::move(r));
						i = end; continue;
					}
				}
				if (ch == L'\\' && i + 1 < src.size()) { m_text += src[++i]; continue; }
				m_text += ch;
			}
			return *this;
		}
		float MeasureHeight(float width) const {
			auto l = Layout(width); if (!l) return 0.0f;
			DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m); return m.height;
		}

		VInputResult OnMouseMove(float x, float y) override {
			int h = LinkAt(x, y); if (h == m_hoverLink) return VInputResult::NotHandled;
			m_hoverLink = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hoverLink = -1; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			int i = LinkAt(x, y);
			if (btn != 1 || i < 0) return VInputResult::NotHandled;
			if (m_cb) m_cb(m_runs[(size_t)i].url);
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			auto l = Layout(vd::W(m_bounds)); if (!l) return;
			D2D1_POINT_2F o = D2D1::Point2F(m_bounds.left, m_bounds.top);
			// a light box behind each code run
			for (const Run& r : m_runs) {
				if (!r.code) continue;
				UINT32 n = 0; l->HitTestTextRange(r.start, r.len, o.x, o.y, nullptr, 0, &n);
				std::vector<DWRITE_HIT_TEST_METRICS> hm((size_t)(std::max)(1u, n));
				if (FAILED(l->HitTestTextRange(r.start, r.len, o.x, o.y, hm.data(), n, &n))) continue;
				for (UINT32 k = 0; k < n; ++k) vd::Fill(rt, vd::Rect(hm[k].left - 2.0f, hm[k].top, hm[k].width + 4.0f, hm[k].height), vd::Col(0xF3F4F6), 3.0f);
			}
			auto link = vd::Brush(rt, m_accent), hot = vd::Brush(rt, vd::Col(0x1D4ED8));
			for (size_t i = 0; i < m_runs.size(); ++i)
				if (!m_runs[i].url.empty()) l->SetDrawingEffect((int)i == m_hoverLink ? hot.Get() : link.Get(), DWRITE_TEXT_RANGE{ m_runs[i].start, m_runs[i].len });
			auto ink = vd::Brush(rt, m_ink);
			if (ink) rt->DrawTextLayout(o, l.Get(), ink.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}
	};

} // namespace ChronoUI

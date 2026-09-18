// =============================================================================
// Gallery.cpp — six generative artworks, a lightbox, and a keyboard.
//
// What this example teaches:
//   * Direct2D is a drawing API, not just a widget toolkit: gradients,
//     filled polygons, clipping, transforms. Every "artwork" below is a
//     short function that paints procedurally into a rectangle, and four of
//     them move with time.
//   * PushAxisAlignedClip keeps each tile inside its frame.
//   * A lightbox is an animated rectangle: on open the tile's rect eases
//     into the stage rect (vd::LerpRect over 0.35 s); on close it eases back.
//   * Keyboard focus: CanFocus() returns true, so a click gives the widget
//     the keyboard and OnKeyDown receives ← → Esc Enter.
//
// Build target: Gallery. Links ChronoUI only.
// =============================================================================

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

#include "VirtualWidget.hpp"
#include "VDraw.hpp"

using namespace ChronoUI;

static const D2D1_COLOR_F kBg    = vd::Col(0x0B0F19);
static const D2D1_COLOR_F kText  = vd::Col(0xE5E7EB);
static const D2D1_COLOR_F kMuted = vd::Col(0x8B93A7);

// Deterministic pseudo-random in [0,1): the same picture every run.
static float Hash(int i) { float v = sinf((float)i * 12.9898f + 78.233f) * 43758.5453f; return v - floorf(v); }

struct Art { std::wstring title, artist; int kind; };

// --- the artworks -------------------------------------------------------------
static void DrawSunset(ID2D1RenderTarget* rt, const D2D1_RECT_F& r) {
	vd::Gradient(rt, r, vd::Col(0xFDBA74), vd::Col(0x4C1D95));
	float sx = vd::CX(r), sy = r.top + vd::H(r) * 0.52f, sr = vd::W(r) * 0.14f;
	for (int i = 3; i >= 0; --i) vd::Circle(rt, sx, sy, sr + (float)i * 10.0f, vd::Col(0xFFF7ED, 0.10f + (i == 0 ? 0.8f : 0.0f)));
	for (int layer = 0; layer < 3; ++layer) {
		std::vector<D2D1_POINT_2F> p; p.push_back(D2D1::Point2F(r.left, r.bottom));
		float base = r.top + vd::H(r) * (0.62f + 0.12f * (float)layer);
		for (int i = 0; i <= 24; ++i) {
			float t = (float)i / 24.0f, x = r.left + vd::W(r) * t;
			float y = base - vd::H(r) * (0.09f * sinf(t * 7.0f + (float)layer) + 0.05f * sinf(t * 19.0f + (float)layer * 3.0f)) * (1.0f - 0.2f * (float)layer);
			p.push_back(D2D1::Point2F(x, y));
		}
		p.push_back(D2D1::Point2F(r.right, r.bottom));
		vd::Polyline(rt, p, vd::Col(0x1E1B4B, 0.55f + 0.2f * (float)layer), 0.0f, true);
	}
}
static void DrawWaves(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float time) {
	vd::Gradient(rt, r, vd::Col(0x38BDF8), vd::Col(0x1E3A8A));
	for (int layer = 0; layer < 4; ++layer) {
		std::vector<D2D1_POINT_2F> p; p.push_back(D2D1::Point2F(r.left, r.bottom));
		float base = r.top + vd::H(r) * (0.45f + 0.14f * (float)layer);
		float speed = 0.6f + 0.3f * (float)layer;
		for (int i = 0; i <= 40; ++i) {
			float t = (float)i / 40.0f, x = r.left + vd::W(r) * t;
			float y = base + vd::H(r) * 0.045f * sinf(t * 9.0f + time * speed + (float)layer * 1.7f) + vd::H(r) * 0.02f * sinf(t * 23.0f - time * 1.3f);
			p.push_back(D2D1::Point2F(x, y));
		}
		p.push_back(D2D1::Point2F(r.right, r.bottom));
		vd::Polyline(rt, p, vd::Col(0xF0F9FF, 0.10f + 0.12f * (float)layer), 0.0f, true);
	}
}
static void DrawAurora(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float time) {
	vd::Fill(rt, r, vd::Col(0x030712));
	for (int i = 0; i < 70; ++i) vd::Circle(rt, r.left + vd::W(r) * Hash(i), r.top + vd::H(r) * Hash(i + 500) * 0.7f, 0.6f + Hash(i + 900) * 1.2f, vd::Col(0xFFFFFF, 0.5f + 0.5f * Hash(i + 1300)));
	D2D1_COLOR_F cols[3] = { vd::Col(0x34D399), vd::Col(0xA78BFA), vd::Col(0x22D3EE) };
	for (int band = 0; band < 3; ++band)
		for (float x = r.left; x < r.right; x += 5.0f) {
			float t = (x - r.left) / vd::W(r);
			float top = r.top + vd::H(r) * (0.22f + 0.12f * (float)band + 0.10f * sinf(t * 5.0f + time * 0.5f + (float)band));
			float len = vd::H(r) * (0.25f + 0.15f * sinf(t * 11.0f - time * 0.8f + (float)band * 2.0f));
			vd::Line(rt, x, top, x, top + len, vd::Alpha(cols[band], 0.10f), 5.0f);
		}
	vd::Gradient(rt, D2D1::RectF(r.left, r.top + vd::H(r) * 0.78f, r.right, r.bottom), vd::Col(0x030712, 0.0f), vd::Col(0x0B1120));
}
static void DrawBubbles(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float time) {
	vd::Gradient(rt, r, vd::Col(0x111827), vd::Col(0x312E81));
	for (int i = 0; i < 26; ++i) {
		float speed = 0.02f + Hash(i + 40) * 0.05f;
		float py = fmodf(Hash(i) + time * speed, 1.0f);
		float x = r.left + vd::W(r) * Hash(i + 20), y = r.bottom - vd::H(r) * py, rad = 6.0f + Hash(i + 60) * 22.0f;
		vd::Circle(rt, x, y, rad, vd::Col(0x818CF8, 0.16f));
		vd::Ring(rt, x, y, rad, vd::Col(0xC7D2FE, 0.45f), 1.2f);
		vd::Circle(rt, x - rad * 0.35f, y - rad * 0.35f, rad * 0.22f, vd::Col(0xFFFFFF, 0.5f));
	}
}
static void DrawMondrian(ID2D1RenderTarget* rt, const D2D1_RECT_F& r) {
	vd::Fill(rt, r, vd::Col(0xF8FAFC));
	struct Cell { float x, y, w, h; uint32_t c; };
	const Cell cells[] = { {0,0,.30f,.55f,0xDC2626}, {.30f,0,.45f,.25f,0xF8FAFC}, {.75f,0,.25f,.40f,0xFACC15},
		{0,.55f,.30f,.45f,0xF8FAFC}, {.30f,.25f,.45f,.75f,0xF8FAFC}, {.75f,.40f,.25f,.35f,0x2563EB}, {.75f,.75f,.25f,.25f,0xF8FAFC} };
	for (const Cell& c : cells) {
		D2D1_RECT_F q = vd::Rect(r.left + vd::W(r) * c.x, r.top + vd::H(r) * c.y, vd::W(r) * c.w, vd::H(r) * c.h);
		vd::Fill(rt, q, vd::Col(c.c));
		vd::Stroke(rt, q, vd::Col(0x111827), 0.0f, 6.0f);
	}
}
static void DrawOrbits(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, float time) {
	vd::Gradient(rt, r, vd::Col(0x0F172A), vd::Col(0x020617));
	float cx = vd::CX(r), cy = vd::CY(r), maxR = (std::min)(vd::W(r), vd::H(r)) * 0.44f;
	vd::Circle(rt, cx, cy, 9.0f, vd::Col(0xFDE68A));
	for (int i = 1; i <= 5; ++i) {
		float rad = maxR * (float)i / 5.0f;
		vd::Ring(rt, cx, cy, rad, vd::Col(0xFFFFFF, 0.08f), 1.0f);
		float a = time * (1.6f / (float)i) + (float)i * 1.3f;
		float px = cx + rad * cosf(a), py = cy + rad * sinf(a);
		for (int k = 1; k <= 10; ++k) {   // trail
			float ta = a - (float)k * 0.06f;
			vd::Circle(rt, cx + rad * cosf(ta), cy + rad * sinf(ta), 3.5f - (float)k * 0.25f, vd::Col(0x7DD3FC, 0.35f - (float)k * 0.03f));
		}
		vd::Circle(rt, px, py, 4.5f, vd::Col(0xE0F2FE));
	}
}
static void DrawArt(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, int kind, float time) {
	rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	switch (kind) {
	case 0: DrawSunset(rt, r); break;
	case 1: DrawWaves(rt, r, time); break;
	case 2: DrawAurora(rt, r, time); break;
	case 3: DrawBubbles(rt, r, time); break;
	case 4: DrawMondrian(rt, r); break;
	default: DrawOrbits(rt, r, time); break;
	}
	rt->PopAxisAlignedClip();
}

// =============================================================================
class VGallery : public VirtualWidgetImpl {
	std::vector<Art> m_arts;
	float m_time = 0.0f;
	int   m_hover = -1;
	int   m_open = -1;          // index in the lightbox, -1 = grid
	float m_t = 1.0f;           // open/close animation clock
	bool  m_closing = false;

	static constexpr float kPad = 24.0f, kGap = 18.0f, kCaption = 34.0f;

	D2D1_RECT_F Tile(int i) const {
		int cols = 3, rows = 2;
		float w = (vd::W(m_bounds) - kPad * 2.0f - kGap * (float)(cols - 1)) / (float)cols;
		float h = (vd::H(m_bounds) - kPad * 2.0f - kGap * (float)(rows - 1)) / (float)rows;
		int c = i % cols, r = i / cols;
		return vd::Rect(m_bounds.left + kPad + (w + kGap) * (float)c, m_bounds.top + kPad + (h + kGap) * (float)r, w, h - kCaption);
	}
	D2D1_RECT_F Stage() const { return D2D1::RectF(m_bounds.left + 70.0f, m_bounds.top + 40.0f, m_bounds.right - 70.0f, m_bounds.bottom - 96.0f); }
	int TileAt(float x, float y) const {
		for (int i = 0; i < (int)m_arts.size(); ++i) if (vd::Contains(Tile(i), x, y)) return i;
		return -1;
	}
	void Open(int i) { m_open = i; m_t = 0.0f; m_closing = false; }
	void Close() { if (m_open >= 0) { m_closing = true; m_t = 0.0f; } }
	void Step(int d) { if (m_open >= 0) { m_open = (m_open + d + (int)m_arts.size()) % (int)m_arts.size(); m_t = 1.0f; } }

public:
	const char* GetTypeName() const override { return "VGallery"; }
	bool CanFocus() const override { return true; }
	void SetArts(std::vector<Art> a) { m_arts = std::move(a); }

	bool OnUpdate(float dt) override {
		m_time += dt;
		if (m_open >= 0 && m_t < 1.0f) {
			m_t = (std::min)(1.0f, m_t + dt / 0.35f);
			if (m_closing && m_t >= 1.0f) { m_open = -1; m_closing = false; }
		}
		return true;   // four artworks animate continuously
	}
	VInputResult OnMouseMove(float x, float y) override {
		int h = m_open >= 0 ? -1 : TileAt(x, y);
		if (h == m_hover) return VInputResult::NotHandled;
		m_hover = h; return VInputResult::Handled;
	}
	VInputResult OnMouseLeave() override { m_hover = -1; return VInputResult::Handled; }
	VInputResult OnMouseDown(float x, float y, int btn) override {
		if (btn != 1) return VInputResult::NotHandled;
		if (m_open >= 0) {
			D2D1_RECT_F st = Stage();
			if (x < st.left) Step(-1); else if (x > st.right) Step(+1); else Close();
			return VInputResult::Handled;
		}
		int i = TileAt(x, y);
		if (i >= 0) { Open(i); m_hover = -1; }
		return VInputResult::Handled;
	}
	VInputResult OnKeyDown(UINT vk) override {
		if (m_open < 0) { if (vk == VK_RETURN) Open(0); return VInputResult::Handled; }
		if (vk == VK_LEFT)  Step(-1);
		else if (vk == VK_RIGHT) Step(+1);
		else if (vk == VK_ESCAPE || vk == VK_RETURN) Close();
		return VInputResult::Handled;
	}

	void OnDraw(ID2D1RenderTarget* rt) override {
		for (int i = 0; i < (int)m_arts.size(); ++i) {
			D2D1_RECT_F r = Tile(i);
			bool hov = (i == m_hover);
			if (hov) r = vd::Inset(r, -4.0f, -4.0f);
			vd::Shadow(rt, r, 10.0f, hov ? 0.5f : 0.3f, 5);
			DrawArt(rt, r, m_arts[i].kind, m_time);
			vd::Stroke(rt, r, hov ? vd::Col(0xFFFFFF, 0.8f) : vd::Col(0xFFFFFF, 0.12f), 0.0f, hov ? 2.0f : 1.0f);
			vd::Text(rt, m_arts[i].title, vd::Rect(r.left, r.bottom + 6.0f, vd::W(r), 18.0f), kText, vd::Style().Size(13).Bold());
			vd::Text(rt, m_arts[i].artist, vd::Rect(r.left, r.bottom + 6.0f, vd::W(r), 18.0f), kMuted, vd::Style().Size(11).Right());
		}
		if (m_open < 0) return;

		// Lightbox: dim, then the animated rectangle between tile and stage.
		float e = vd::EaseInOut(m_closing ? 1.0f - m_t : m_t);
		vd::Fill(rt, m_bounds, vd::Col(0x000000, 0.72f * e));
		D2D1_RECT_F r = vd::LerpRect(Tile(m_open), Stage(), e);
		vd::Shadow(rt, r, 8.0f, 0.6f * e, 6);
		DrawArt(rt, r, m_arts[m_open].kind, m_time);
		vd::Stroke(rt, r, vd::Col(0xFFFFFF, 0.25f), 0.0f, 1.0f);
		if (e > 0.85f) {
			float a = (e - 0.85f) / 0.15f;
			const Art& art = m_arts[m_open];
			vd::Text(rt, art.title, vd::Rect(r.left, r.bottom + 10.0f, vd::W(r), 28.0f), vd::Col(0xFFFFFF, a), vd::Style().Size(20).Bold());
			vd::Text(rt, art.artist + L"  \x00B7  " + std::to_wstring(m_open + 1) + L" / " + std::to_wstring(m_arts.size())
				+ L"  \x00B7  \x2190 \x2192 navigate  \x00B7  Esc closes",
				vd::Rect(r.left, r.bottom + 10.0f, vd::W(r), 28.0f), vd::Alpha(kMuted, a), vd::Style().Size(13).Right());
		}
	}
};

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Gallery", 1100, 760)) return 1;
	win.SetBackground(kBg);

	auto* title = win.Add<VLabel>(); title->Text(L"Generative gallery").FontSize(20.0f).Color(kText);
	auto* hint  = win.Add<VLabel>(); hint->Text(L"Six pictures painted with Direct2D, four of them alive. Click one; arrows move, Esc closes.").FontSize(12.0f).Color(kMuted);
	auto* gallery = win.Add<VGallery>();
	gallery->SetArts({
		{ L"Last light",   L"gradients + polygons", 0 },
		{ L"Swell",        L"sine waves, animated", 1 },
		{ L"Aurora",       L"5 px lines, animated", 2 },
		{ L"Rising",       L"circles, animated",    3 },
		{ L"Composition",  L"rectangles",           4 },
		{ L"Orbits",       L"rings + trails, animated", 5 },
	});

	auto layout = [&] {
		RECT rc; GetClientRect(win.GetHWND(), &rc);
		float W = (float)rc.right, H = (float)rc.bottom;
		title->SetBounds(vd::Rect(24.0f, 14.0f, 500.0f, 28.0f));
		hint ->SetBounds(vd::Rect(24.0f, 42.0f, W - 48.0f, 20.0f));
		gallery->SetBounds(D2D1::RectF(0.0f, 60.0f, W, H));
		InvalidateRect(win.GetHWND(), NULL, FALSE);
	};
	win.OnResize(layout);
	layout();
	win.SetFocusWidget(gallery);
	return win.RunMessageLoop();
}

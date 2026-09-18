// =============================================================================
// Bounce.cpp — a physics toy: throw balls around a box.
//
// What this example teaches:
//   * OnUpdate(dt) is a real simulation step. The host hands every widget a
//     measured delta time each frame; integrate velocity, resolve walls and
//     collisions, return true, and the window repaints at the display rate.
//   * Mouse capture with velocity: grab a ball and it follows the cursor;
//     the velocity is whatever the cursor was doing when you let go, so you
//     can throw.
//   * Click on empty space spawns a ball. The keyboard toggles gravity (G),
//     clears (C) and adds ten (Space) — the widget has focus because
//     CanFocus() is true and the host gives focus on click.
//   * A little HUD text drawn in the same OnDraw.
//
// Build target: Bounce. Links ChronoUI only.
// =============================================================================

#include <windows.h>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "VirtualWidget.hpp"
#include "VDraw.hpp"

using namespace ChronoUI;

static const D2D1_COLOR_F kBg    = vd::Col(0x0F172A);
static const D2D1_COLOR_F kText  = vd::Col(0xE5E7EB);
static const D2D1_COLOR_F kMuted = vd::Col(0x8B93A7);

static float Rand(float lo, float hi) {
	static std::mt19937 g(2026);
	return std::uniform_real_distribution<float>(lo, hi)(g);
}

struct Ball { float x, y, vx, vy, r; D2D1_COLOR_F c; };

// =============================================================================
class VWorld : public VirtualWidgetImpl {
	std::vector<Ball> m_balls;
	bool  m_gravity = true;
	int   m_drag = -1;
	float m_mx = 0, m_my = 0;
	float m_fps = 60.0f, m_fpsAcc = 0.0f; int m_frames = 0;

	static constexpr const uint32_t kColors[6] = { 0x60A5FA, 0xF472B6, 0x34D399, 0xFBBF24, 0xA78BFA, 0xF87171 };

	int BallAt(float x, float y) const {
		for (int i = (int)m_balls.size() - 1; i >= 0; --i) {
			float dx = x - m_balls[i].x, dy = y - m_balls[i].y;
			if (dx * dx + dy * dy <= m_balls[i].r * m_balls[i].r) return i;
		}
		return -1;
	}

public:
	const char* GetTypeName() const override { return "VWorld"; }
	bool CanFocus() const override { return true; }

	void Spawn(float x, float y, float vx, float vy) {
		Ball b{ x, y, vx, vy, Rand(14.0f, 30.0f), vd::Col(kColors[m_balls.size() % 6]) };
		m_balls.push_back(b);
	}
	void AddRandom(int n) {
		for (int i = 0; i < n; ++i)
			Spawn(Rand(m_bounds.left + 40.0f, m_bounds.right - 40.0f), Rand(m_bounds.top + 40.0f, vd::CY(m_bounds)),
			      Rand(-300.0f, 300.0f), Rand(-200.0f, 100.0f));
	}
	void Clear() { m_balls.clear(); m_drag = -1; }
	void ToggleGravity() { m_gravity = !m_gravity; }
	bool Gravity() const { return m_gravity; }
	size_t Count() const { return m_balls.size(); }

	bool OnUpdate(float dt) override {
		dt = (std::min)(dt, 0.033f);
		m_frames++; m_fpsAcc += dt;
		if (m_fpsAcc >= 0.5f) { m_fps = (float)m_frames / m_fpsAcc; m_frames = 0; m_fpsAcc = 0.0f; }

		const float g = 1800.0f, bounce = 0.82f, air = 0.999f;
		for (int i = 0; i < (int)m_balls.size(); ++i) {
			Ball& b = m_balls[i];
			if (i == m_drag) {
				// Follow the cursor; the velocity we keep is what a throw uses.
				b.vx = (m_mx - b.x) / dt; b.vy = (m_my - b.y) / dt;
				b.x = m_mx; b.y = m_my;
				continue;
			}
			if (m_gravity) b.vy += g * dt;
			b.vx *= air; b.vy *= air;
			b.x += b.vx * dt; b.y += b.vy * dt;
			if (b.x - b.r < m_bounds.left)  { b.x = m_bounds.left + b.r;  b.vx = -b.vx * bounce; }
			if (b.x + b.r > m_bounds.right) { b.x = m_bounds.right - b.r; b.vx = -b.vx * bounce; }
			if (b.y - b.r < m_bounds.top)   { b.y = m_bounds.top + b.r;   b.vy = -b.vy * bounce; }
			if (b.y + b.r > m_bounds.bottom) {
				b.y = m_bounds.bottom - b.r; b.vy = -b.vy * bounce;
				b.vx *= 0.98f;                                     // rolling friction
				if (fabsf(b.vy) < 40.0f) b.vy = 0.0f;              // settle
			}
		}
		// Ball-ball collisions: equal mass, elastic, with position correction.
		for (size_t i = 0; i < m_balls.size(); ++i)
			for (size_t j = i + 1; j < m_balls.size(); ++j) {
				Ball& a = m_balls[i]; Ball& b = m_balls[j];
				float dx = b.x - a.x, dy = b.y - a.y, d2 = dx * dx + dy * dy, rr = a.r + b.r;
				if (d2 >= rr * rr || d2 < 1e-4f) continue;
				float d = sqrtf(d2), nx = dx / d, ny = dy / d, overlap = rr - d;
				if ((int)i != m_drag) { a.x -= nx * overlap * 0.5f; a.y -= ny * overlap * 0.5f; }
				if ((int)j != m_drag) { b.x += nx * overlap * 0.5f; b.y += ny * overlap * 0.5f; }
				float rel = (b.vx - a.vx) * nx + (b.vy - a.vy) * ny;
				if (rel > 0.0f) continue;
				float jn = -(1.0f + bounce) * rel * 0.5f;
				if ((int)i != m_drag) { a.vx -= jn * nx; a.vy -= jn * ny; }
				if ((int)j != m_drag) { b.vx += jn * nx; b.vy += jn * ny; }
			}
		return true;
	}

	VInputResult OnMouseDown(float x, float y, int btn) override {
		if (btn != 1) return VInputResult::NotHandled;
		int i = BallAt(x, y);
		m_mx = x; m_my = y;
		if (i >= 0) { m_drag = i; return VInputResult::Capture; }
		Spawn(x, y, Rand(-250.0f, 250.0f), Rand(-500.0f, -100.0f));
		return VInputResult::Handled;
	}
	VInputResult OnMouseMove(float x, float y) override {
		m_mx = x; m_my = y;
		return m_drag >= 0 ? VInputResult::Handled : VInputResult::NotHandled;
	}
	VInputResult OnMouseUp(float, float, int) override { m_drag = -1; return VInputResult::Handled; }
	VInputResult OnKeyDown(UINT vk) override {
		if (vk == 'G') ToggleGravity();
		else if (vk == 'C') Clear();
		else if (vk == VK_SPACE) AddRandom(10);
		else return VInputResult::NotHandled;
		return VInputResult::Handled;
	}

	void OnDraw(ID2D1RenderTarget* rt) override {
		vd::Gradient(rt, m_bounds, vd::Col(0x0F172A), vd::Col(0x1E293B));
		vd::Line(rt, m_bounds.left, m_bounds.bottom - 1.0f, m_bounds.right, m_bounds.bottom - 1.0f, vd::Col(0xFFFFFF, 0.15f), 2.0f);
		for (const Ball& b : m_balls) {
			vd::Circle(rt, b.x + 3.0f, b.y + 4.0f, b.r, vd::Col(0x000000, 0.25f));
			vd::Circle(rt, b.x, b.y, b.r, b.c);
			vd::Circle(rt, b.x - b.r * 0.32f, b.y - b.r * 0.32f, b.r * 0.28f, vd::Col(0xFFFFFF, 0.55f));
		}
		wchar_t hud[96];
		swprintf_s(hud, L"%zu balls   \x00B7   %.0f fps   \x00B7   gravity %s", m_balls.size(), m_fps, m_gravity ? L"on" : L"off");
		vd::Text(rt, hud, vd::Rect(m_bounds.left + 18.0f, m_bounds.top + 12.0f, 400.0f, 20.0f), kText, vd::Style().Size(13).Bold());
		vd::Text(rt, L"click: spawn   \x00B7   drag: throw   \x00B7   G gravity   \x00B7   Space +10   \x00B7   C clear",
			vd::Rect(m_bounds.left + 18.0f, m_bounds.top + 34.0f, 600.0f, 20.0f), kMuted, vd::Style().Size(12));
	}
};

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Bounce", 1000, 680)) return 1;
	win.SetBackground(kBg);

	auto* world = win.Add<VWorld>();
	auto* add   = win.Add<VButton>(); add->Text(L"+10 balls");
	auto* clear = win.Add<VButton>(); clear->Text(L"Clear");
	auto* grav  = win.Add<VButton>(); grav->Text(L"Gravity: on");
	auto ghost = [](VButton* b) { b->Face(vd::Col(0xFFFFFF, 0.10f)).FaceHover(vd::Col(0xFFFFFF, 0.18f)).FacePress(vd::Col(0xFFFFFF, 0.26f)); };
	ghost(clear); ghost(grav);
	add->OnClick([&] { world->AddRandom(10); });
	clear->OnClick([&] { world->Clear(); });
	grav->OnClick([&] { world->ToggleGravity(); grav->Text(world->Gravity() ? L"Gravity: on" : L"Gravity: off"); });

	auto layout = [&] {
		RECT rc; GetClientRect(win.GetHWND(), &rc);
		float W = (float)rc.right, H = (float)rc.bottom;
		world->SetBounds(D2D1::RectF(0.0f, 0.0f, W, H));
		grav ->SetBounds(vd::Rect(W - 18.0f - 110.0f, 12.0f, 110.0f, 32.0f));
		clear->SetBounds(vd::Rect(W - 18.0f - 110.0f - 10.0f - 80.0f, 12.0f, 80.0f, 32.0f));
		add  ->SetBounds(vd::Rect(W - 18.0f - 110.0f - 10.0f - 80.0f - 10.0f - 100.0f, 12.0f, 100.0f, 32.0f));
	};
	win.OnResize(layout);
	layout();
	world->AddRandom(12);
	win.SetFocusWidget(world);
	return win.RunMessageLoop();
}

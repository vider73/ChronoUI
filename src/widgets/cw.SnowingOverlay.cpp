#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>
#include <random>

#include "WidgetImpl.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")

using namespace ChronoUI;

#ifndef NOMINMAX
#define NOMINMAX
#endif

class SnowingOverlay : public ChronoUI::WidgetImpl
{
	struct Snowflake {
		float x;
		float y;
		float speed;
		float size;
		float wobblePhase;
		float opacity; // Added for visual depth
	};

	// --- Animation State ---
	bool m_isActive = true;
	int m_width = 0;
	int m_height = 0;

	// --- Configuration ---
	int m_frequency = 2; // spawn rate 
	int m_maxSize = 4;   // max radius

	// --- Particles ---
	std::vector<Snowflake> m_flakes;
	std::mt19937 m_rng;

	// --- Direct2D Resources ---
	ID2D1SolidColorBrush* m_pSnowBrush = nullptr;
	ID2D1RenderTarget* m_pLastRT = nullptr; // To detect device loss/change

public:
	SnowingOverlay() {
		// Initialize Random Number Generator
		std::random_device rd;
		m_rng = std::mt19937(rd());

		// Default property state
		SetProperty("active", "true");
		SetProperty("freq", "20");
		SetProperty("size", "4");
	}

	virtual ~SnowingOverlay() {
		SafeRelease(&m_pSnowBrush);
		// m_pLastRT is a weak reference, do not release
	}

	virtual void __stdcall SetBounds(int x, int y, int w, int h) override {
		m_width = w;
		m_height = h;

		if (m_hwnd) {
			SetWindowPos(m_hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

			// If active, ensure we are visible
			if (m_isActive)
				SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
			else
				SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
		}
	}

	const char* __stdcall GetControlName() override {
		return "SnowingOverlay";
	}

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "A hardware-accelerated overlay that renders a snowing effect. Mouse passes through.",
            "properties": [
                { "name": "active", "type": "boolean", "description": "Enable or disable the snow." },
                { "name": "freq", "type": "integer", "description": "Frequency of snowfall (1-20)." },
                { "name": "size", "type": "integer", "description": "Maximum size of snowflakes." }
            ]
        })json";
	}

	// --- Direct2D Drawing Logic ---
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		if (!m_isActive) return;

		// Manage Device Dependent Resources
		if (m_pSnowBrush == nullptr || m_pLastRT != pRT) {
			SafeRelease(&m_pSnowBrush);
			m_pLastRT = pRT;

			// Create a white brush. We will modulate opacity per flake.
			// Using a slightly off-white cool color for a modern look.
			pRT->CreateSolidColorBrush(D2D1::ColorF(0xA0E6EC, 1.0f), &m_pSnowBrush);
		}

		if (!m_pSnowBrush) return;

		// No background draw needed (transparency preserved)

		// High quality rendering
		pRT->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

		for (const auto& f : m_flakes) {
			// Modulate opacity based on flake properties for depth
			m_pSnowBrush->SetOpacity(f.opacity);

			D2D1_ELLIPSE flakeEllipse = D2D1::Ellipse(
				D2D1::Point2F(f.x, f.y),
				f.size,
				f.size
			);

			pRT->FillEllipse(flakeEllipse, m_pSnowBrush);
		}
	}

	// --- Update Logic ---
	bool OnUpdateAnimation(float deltaTime) override {
		if (!m_isActive) return false;
		if (m_width <= 0 || m_height <= 0) return true;

		// 1. Spawn new flakes
		// Normalized probability based on 60fps assumption to deltaTime
		std::uniform_int_distribution<int> spawnDist(0, 100);

		// Scale frequency probability roughly to frame time
		int threshold = (std::min)(100, m_frequency * 2);

		if (spawnDist(m_rng) < threshold) {
			SpawnFlake();
		}

		// 2. Move flakes
		// Use deltaTime to ensure smooth movement regardless of framerate
		// Base speed multiplier (60.0f normalizes standard logic to delta time)
		float timeScale = deltaTime * 60.0f;

		for (auto& f : m_flakes) {
			f.y += f.speed * timeScale;
			// Wobble effect: x + sin(y * factor + phase)
			f.x += std::sin(f.y * 0.05f + f.wobblePhase) * 0.5f * timeScale;
		}

		// 3. Remove flakes that are off-screen
		m_flakes.erase(
			std::remove_if(m_flakes.begin(), m_flakes.end(),
				[this](const Snowflake& f) { return f.y > (float)m_height + f.size; }),
			m_flakes.end()
		);

		return true; // Keep animating
	}

	void SpawnFlake() {
		std::uniform_real_distribution<float> xDist(0.0f, (float)m_width);
		std::uniform_real_distribution<float> sizeDist(1.0f, (float)(std::max)(1, m_maxSize));
		std::uniform_real_distribution<float> wobbleDist(0.0f, 6.28f);

		Snowflake f;
		f.x = xDist(m_rng);
		f.size = sizeDist(m_rng);
		f.y = -f.size * 2.0f; // Start just above screen

		// Modern look: Parallax simulation
		// Larger flakes fall faster and are more opaque (foreground)
		// Smaller flakes fall slower and are more transparent (background)
		float sizeRatio = f.size / (float)(std::max)(1, m_maxSize);

		f.speed = 1.5f + (sizeRatio * 2.5f); // Speed range 1.5 - 4.0
		f.wobblePhase = wobbleDist(m_rng);

		// Opacity range 0.4 - 0.95
		f.opacity = 0.4f + (sizeRatio * 0.55f);

		m_flakes.push_back(f);
	}

	// --- Message Handling ---
	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override
	{
		switch (msg) {
		case WM_PAINT:
			return false; // Let host call OnDrawWidget

		case WM_ERASEBKGND:
			return 1; // Prevent GDI flicker

		case WM_CREATE:
			if (m_isActive) {
				::SetTimer(m_hwnd, CHRONOUI_ANIM_TIMER, 16, NULL);
			}
			SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			return false;

		case WM_DESTROY:
			::KillTimer(m_hwnd, CHRONOUI_ANIM_TIMER);
			SafeRelease(&m_pSnowBrush);
			return false;

		case WM_NCHITTEST:
			// Passthrough magic
			return false;
		}

		return false;
	}

	void OnFocus(bool f) override {
		// Overlay shouldn't take focus
	}

	void OnPropertyChanged(const char* _key, const char* value) override {
		std::string key = _key;
		std::string val = value;

		if (key == "active") {
			bool oldActive = m_isActive;
			m_isActive = (val == "true" || val == "1" || val == "yes");

			if (m_isActive && !oldActive) {
				SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
				::SetTimer(m_hwnd, CHRONOUI_ANIM_TIMER, 16, NULL);
			}
			else if (!m_isActive && oldActive) {
				::KillTimer(m_hwnd, CHRONOUI_ANIM_TIMER);
				m_flakes.clear();
				SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);

				// Force parent repaint to clear tracks
				if (m_hwnd) {
					HWND parent = GetParent(m_hwnd);
					RECT r; GetWindowRect(m_hwnd, &r);
					MapWindowPoints(NULL, parent, (LPPOINT)&r, 2);
					InvalidateRect(parent, &r, TRUE);
				}
			}
		}
		else if (key == "freq") {
			try {
				m_frequency = (std::max)(0, std::stoi(val));
			}
			catch (...) {}
		}
		else if (key == "size") {
			try {
				m_maxSize = (std::max)(1, std::stoi(val));
			}
			catch (...) {}
		}
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new SnowingOverlay();
}
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <list>
#include <cmath>
#include <random>

#include "WidgetImpl.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")

using namespace ChronoUI;

class LightingStormOverlay : public ChronoUI::WidgetImpl
{
	struct RainDrop {
		float x;
		float y;
		float speed;
		float length;
	};

	struct LightningBolt {
		std::vector<D2D1_POINT_2F> segments;
		int lifeTime;       // How many frames the bolt remains
		int totalLife;
		float thickness;
	};

	// --- Animation State ---
	bool m_isActive = true;
	int m_width = 0;
	int m_height = 0;

	// --- Configuration ---
	int m_rainIntensity = 50;   // Number of raindrops (10-500)
	int m_stormFreq = 2;        // Chance of lightning (0-100)

	// --- Particles ---
	std::vector<RainDrop> m_rain;
	std::list<LightningBolt> m_bolts;
	std::mt19937 m_rng;

	// --- D2D Resources ---
	// We keep track of the RenderTarget used to create resources. 
	// If it changes (resize/device loss), we rebuild brushes.
	ID2D1RenderTarget* m_pLastRT = nullptr;
	ID2D1SolidColorBrush* m_pRainBrush = nullptr;
	ID2D1SolidColorBrush* m_pBoltBrush = nullptr;
	ID2D1SolidColorBrush* m_pFlashBrush = nullptr;
	ID2D1StrokeStyle* m_pRoundStrokeStyle = nullptr;

	// Current global flash intensity (0.0 - 1.0)
	float m_flashIntensity = 0.0f;

public:
	LightingStormOverlay() {
		// Initialize Random Number Generator
		std::random_device rd;
		m_rng = std::mt19937(rd());

		// Default property state
		SetProperty("active", "true");
		SetProperty("intensity", "100"); // rain count
		SetProperty("storm", "5");       // lightning frequency
	}

	virtual ~LightingStormOverlay() {
		DiscardDeviceResources();
	}

	void DiscardDeviceResources() {
		SafeRelease(&m_pRainBrush);
		SafeRelease(&m_pBoltBrush);
		SafeRelease(&m_pFlashBrush);
		SafeRelease(&m_pRoundStrokeStyle);
		m_pLastRT = nullptr;
	}

	// Helper to ensure resources exist for the current RenderTarget
	void CreateDeviceResources(ID2D1RenderTarget* pRT) {
		if (m_pLastRT != pRT) {
			DiscardDeviceResources();
			m_pLastRT = pRT;
		}

		if (!m_pRainBrush) {
			// Bluish-gray rain
			pRT->CreateSolidColorBrush(D2D1::ColorF(0.47f, 0.7f, 0.78f, 0.6f), &m_pRainBrush);
		}

		if (!m_pBoltBrush) {
			// Bright white lightning
			pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_pBoltBrush);
		}

		if (!m_pFlashBrush) {
			// White flash overlay
			pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f), &m_pFlashBrush);
		}

		if (!m_pRoundStrokeStyle) {
			D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
				D2D1_CAP_STYLE_ROUND,  // Start cap
				D2D1_CAP_STYLE_ROUND,  // End cap
				D2D1_CAP_STYLE_ROUND,  // Dash cap
				D2D1_LINE_JOIN_ROUND   // Line join
			);
			GetD2DFactory()->CreateStrokeStyle(&props, nullptr, 0, &m_pRoundStrokeStyle);
		}
	}

	// New Virtual Entry Point
	virtual void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		if (!m_isActive) return;

		CreateDeviceResources(pRT);

		D2D1_SIZE_F size = pRT->GetSize();
		// Update dimensions in case they drifted, though SetBounds handles integers
		m_width = static_cast<int>(size.width);
		m_height = static_cast<int>(size.height);

		// 1. Draw Flash (Background illumination)
		if (m_flashIntensity > 0.01f) {
			// Map 0.0-1.0 to alpha. Cap at 0.6 to preserve UI readability.
			float alpha = (std::min)(m_flashIntensity, 0.6f);
			m_pFlashBrush->SetOpacity(alpha);
			// Flash color: Very light cyan-white
			m_pFlashBrush->SetColor(D2D1::ColorF(0.9f, 0.95f, 1.0f, alpha));

			pRT->FillRectangle(D2D1::RectF(0, 0, size.width, size.height), m_pFlashBrush);
		}

		// 2. Draw Lightning Bolts
		for (const auto& bolt : m_bolts) {
			if (bolt.segments.size() > 1) {
				// Flicker effect
				float baseAlpha = (bolt.lifeTime % 2 == 0) ? 1.0f : 0.4f;

				// Pass 1: Glow (Thick, low alpha)
				m_pBoltBrush->SetColor(D2D1::ColorF(0.6f, 0.8f, 1.0f, baseAlpha * 0.3f));
				for (size_t i = 0; i < bolt.segments.size() - 1; ++i) {
					pRT->DrawLine(
						bolt.segments[i],
						bolt.segments[i + 1],
						m_pBoltBrush,
						bolt.thickness * 4.0f, // Wide glow
						m_pRoundStrokeStyle
					);
				}

				// Pass 2: Core (Thin, high alpha, white)
				m_pBoltBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, baseAlpha));
				for (size_t i = 0; i < bolt.segments.size() - 1; ++i) {
					pRT->DrawLine(
						bolt.segments[i],
						bolt.segments[i + 1],
						m_pBoltBrush,
						bolt.thickness,
						m_pRoundStrokeStyle
					);
				}
			}
		}

		// 3. Draw Rain
		m_pRainBrush->SetColor(D2D1::ColorF(0.47f, 0.7f, 0.78f, 0.8f));

		for (const auto& r : m_rain) {
			// Calculate tail position based on length
			D2D1_POINT_2F p1 = D2D1::Point2F(r.x, r.y);
			D2D1_POINT_2F p2 = D2D1::Point2F(r.x - 2.0f, r.y - r.length);

			// Vary opacity slightly by speed to simulate motion blur depth
			float alpha = (std::min)(1.0f, (r.speed / 40.0f));
			m_pRainBrush->SetOpacity(alpha * 0.6f);

			pRT->DrawLine(p1, p2, m_pRainBrush, 1.5f);
		}
	}

	virtual void __stdcall SetBounds(int x, int y, int w, int h) override {
		bool sizeChanged = (m_width != w || m_height != h);
		m_width = w;
		m_height = h;

		if (m_hwnd) {
			SetWindowPos(m_hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

			if (m_isActive) {
				SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
				if (sizeChanged) InitializeRain();
			}
			else {
				SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
			}
		}
	}

	const char* __stdcall GetControlName() override {
		return "LightingStormOverlay";
	}

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "Direct2D storm overlay with glow effects.",
            "properties": [
                { "name": "active", "type": "boolean", "description": "Enable or disable the storm." },
                { "name": "intensity", "type": "integer", "description": "Rain density (10-500)." },
                { "name": "storm", "type": "integer", "description": "Frequency of lightning strikes (0-50)." }
            ],
            "events": []
        })json";
	}

	// --- Update Logic ---
	bool OnUpdateAnimation(float deltaTime) override {
		if (!m_isActive) return false;
		if (m_width <= 0 || m_height <= 0) return true;

		// Ensure rain count matches config (lazy init)
		if (m_rain.size() != static_cast<size_t>(m_rainIntensity)) {
			InitializeRain();
		}

		// 1. Update Rain
		for (auto& r : m_rain) {
			r.y += r.speed;
			r.x -= 1.5f; // Slight wind to the left

			// Wrap around
			if (r.y > m_height + r.length) {
				ResetRainDrop(r);
				r.y = -r.length; // Start from top
			}
			if (r.x < 0) {
				r.x = (float)m_width;
			}
		}

		// 2. Update Lightning
		auto it = m_bolts.begin();
		while (it != m_bolts.end()) {
			it->lifeTime--;
			if (it->lifeTime <= 0) {
				it = m_bolts.erase(it);
			}
			else {
				++it;
			}
		}

		// Spawn new lightning
		if (m_stormFreq > 0) {
			std::uniform_int_distribution<int> dist(0, 1000);
			if (dist(m_rng) < m_stormFreq) {
				SpawnLightning();
			}
		}

		// 3. Update Flash Effect
		if (m_flashIntensity > 0.0f) {
			m_flashIntensity -= 0.15f; // Fade out speed
			if (m_flashIntensity < 0.0f) m_flashIntensity = 0.0f;
		}

		return true;
	}

	void InitializeRain() {
		m_rain.resize((std::max)(0, m_rainIntensity));
		for (auto& r : m_rain) {
			ResetRainDrop(r);
			// Randomize starting Y so they don't fall in a single sheet
			std::uniform_real_distribution<float> yDist(0.0f, (float)m_height);
			r.y = yDist(m_rng);
		}
	}

	void ResetRainDrop(RainDrop& r) {
		std::uniform_real_distribution<float> xDist(0.0f, (float)m_width);
		std::uniform_real_distribution<float> speedDist(15.0f, 35.0f); // Fast falling
		std::uniform_real_distribution<float> lenDist(10.0f, 25.0f);

		r.x = xDist(m_rng);
		r.speed = speedDist(m_rng);
		r.length = lenDist(m_rng);
	}

	void SpawnLightning() {
		LightningBolt bolt;
		bolt.lifeTime = 6; // Frames (approx 100ms)
		bolt.totalLife = 6;
		bolt.thickness = 2.0f + (m_rng() % 3);

		std::uniform_int_distribution<int> xStartDist(0, m_width);
		float currentX = (float)xStartDist(m_rng);
		float currentY = 0.0f;

		bolt.segments.push_back(D2D1::Point2F(currentX, currentY));

		// Procedural generation of jagged line
		while (currentY < m_height) {
			std::uniform_real_distribution<float> xOffset(-40.0f, 40.0f);
			std::uniform_real_distribution<float> yStep(10.0f, 70.0f);

			currentX += xOffset(m_rng);
			currentY += yStep(m_rng);

			// Clamp X
			currentX = (std::max)(0.0f, (std::min)(currentX, (float)m_width));

			bolt.segments.push_back(D2D1::Point2F(currentX, currentY));
		}

		m_bolts.push_back(bolt);

		// Trigger screen flash
		m_flashIntensity = 0.8f;
	}

	// --- Message Handling ---
	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override
	{
		switch (msg) {
		case WM_CREATE:
			if (m_isActive) {
				::SetTimer(m_hwnd, CHRONOUI_ANIM_TIMER, 16, NULL);
			}
			SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			return false;

		case WM_DESTROY:
			::KillTimer(m_hwnd, CHRONOUI_ANIM_TIMER);
			DiscardDeviceResources();
			return false;

		case WM_NCHITTEST:
			return HTTRANSPARENT;

		case WM_ERASEBKGND:
			return 1;
		}

		return false;
	}

	void OnFocus(bool f) override {}

	void OnPropertyChanged(const char* _key, const char* value) override {
		std::string key = _key;
		std::string val = value;

		if (key == "active") {
			bool oldActive = m_isActive;
			m_isActive = (val == "true" || val == "1" || val == "yes");

			if (m_isActive && !oldActive) {
				SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
				InitializeRain();
				::SetTimer(m_hwnd, CHRONOUI_ANIM_TIMER, 16, NULL);
			}
			else if (!m_isActive && oldActive) {
				::KillTimer(m_hwnd, CHRONOUI_ANIM_TIMER);
				m_bolts.clear();
				m_flashIntensity = 0;
				SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);

				// Trigger repaint to clear
				if (m_hwnd) {
					InvalidateRect(m_hwnd, NULL, FALSE);
				}
			}
		}
		else if (key == "intensity") {
			try {
				int v = std::stoi(val);
				m_rainIntensity = (std::max)(0, (std::min)(v, 1000));
				if (m_isActive) InitializeRain();
			}
			catch (...) {}
		}
		else if (key == "storm") {
			try {
				int v = std::stoi(val);
				m_stormFreq = (std::max)(0, (std::min)(v, 100));
			}
			catch (...) {}
		}
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new LightingStormOverlay();
}
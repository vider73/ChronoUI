#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>

#include "WidgetImpl.hpp"

// Link DWrite and D2D
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")

using namespace ChronoUI;

class AnimatedParticlesProgress : public ChronoUI::WidgetImpl
{
	// --- Particle System ---
	struct Particle {
		float x, y;
		float alpha;
		float lifeSpan;
	};

	float m_scannerPos = 0.0f;
	float m_scannerTime = 0.0f;

	D2D1_COLOR_F m_trailCol;
	D2D1_COLOR_F m_headCol;
	float m_yOffset;
	float m_particleSize; // Changed to float for D2D
	std::vector<Particle> m_particles;

public:
	AnimatedParticlesProgress() {
		m_yOffset = 0.85f;
		m_scannerTime = (float)(rand() % 628) / 100.0f;

		// Pre-calculate colors (Normalize 0-255 to 0.0f-1.0f)
		// Trail: RGB(220, 0, 0)
		m_trailCol = D2D1::ColorF(220.0f / 255.0f, 0.0f, 0.0f, 1.0f);
		// Head: RGB(255, 120, 120)
		m_headCol = D2D1::ColorF(1.0f, 120.0f / 255.0f, 120.0f / 255.0f, 1.0f);

		m_particleSize = 20.0f;
	}

	virtual ~AnimatedParticlesProgress() {
		// No explicit shutdown needed for D2D here
	}

	const char* __stdcall GetControlName() override {
		return "ColorWidget";
	}

	const char* __stdcall GetControlManifest() override {
		return R"json({
            "version": 2,
            "description": "An animated progress control with smart title alignment and overflow protection.",
            "properties": [
                { "name": "title", "type": "string", "description": "The text title displayed in the control" }
            ],
            "events": [
                { "name": "onClick", "type": "action", "description": "Triggered when the user clicks anywhere on the control surface" }
            ]
        })json";
	}

	void SpawnParticle(float width, float height) {
		if (width <= 0) return;
		Particle p;
		p.x = 20.0f + (m_scannerPos * (width - 40.0f));
		p.y = height * m_yOffset;
		p.alpha = 1.0f;
		p.lifeSpan = 0.06f;
		m_particles.push_back(p);
	}

	// --- 1. Direct2D Drawing Logic ---
	void OnDrawWidget(ID2D1RenderTarget* pRT) override {
		D2D1_SIZE_F size = pRT->GetSize();
		D2D1_RECT_F rect = D2D1::RectF(0, 0, size.width, size.height);

		// Use Helper for Background
		DrawWidgetBackground(pRT, rect);

		// Create a scratch brush for drawing particles
		ComPtr<ID2D1SolidColorBrush> pBrush;
		HRESULT hr = pRT->CreateSolidColorBrush(m_trailCol, &pBrush);

		if (SUCCEEDED(hr)) {
			// Draw Fading Trail
			for (const auto& p : m_particles) {
				// Modulate alpha
				D2D1_COLOR_F pColor = m_trailCol;
				pColor.a = (std::max)(0.0f, (std::min)(1.0f, p.alpha)); // Clamp alpha

				pBrush->SetColor(pColor);

				D2D1_RECT_F particleRect = D2D1::RectF(
					p.x - (m_particleSize / 2.0f),
					p.y,
					p.x + (m_particleSize / 2.0f),
					p.y + 3.0f
				);
				pRT->FillRectangle(particleRect, pBrush.Get());
			}

			// Draw Scanner Head
			float headX = 20.0f + (m_scannerPos * (size.width - 40.0f));
			pBrush->SetColor(m_headCol);

			D2D1_RECT_F headRect = D2D1::RectF(
				headX - (m_particleSize / 2.0f),
				(size.height * m_yOffset) - 1.0f,
				headX + (m_particleSize / 2.0f),
				(size.height * m_yOffset) + 4.0f // +4 makes height 5.0f (y-1 to y+4)
			);
			pRT->FillRectangle(headRect, pBrush.Get());

			// Focus Border
			if (m_focused) {
				// Color(80, 255, 255, 255) -> Alpha ~0.31
				pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 80.0f / 255.0f));

				// Draw inside the bounds
				D2D1_RECT_F focusRect = D2D1::RectF(0.5f, 0.5f, size.width - 0.5f, size.height - 0.5f);
				pRT->DrawRectangle(focusRect, pBrush.Get(), 1.0f);
			}
		}

		// Text with flexible layout & overflow protection via Helper
		std::string titleProp = GetProperty("title");
		if (!titleProp.empty()) {
			DrawTextStyled(pRT, titleProp, rect);
		}
	}

	// --- 2. Update Logic ---
	bool OnUpdateAnimation(float deltaTime) override {
		// Update Scanner Physics
		m_scannerTime += 0.05f; // Logic kept from original
		m_scannerPos = (std::sin(m_scannerTime) + 1.0f) * 0.5f;

		// Get dimensions for spawning
		RECT rc;
		GetClientRect(m_hwnd, &rc);
		SpawnParticle((float)rc.right, (float)rc.bottom);

		// Update Particles
		for (size_t i = 0; i < m_particles.size(); ) {
			m_particles[i].alpha -= m_particles[i].lifeSpan;
			if (m_particles[i].alpha <= 0.0f) {
				m_particles.erase(m_particles.begin() + i);
			}
			else {
				++i;
			}
		}
		return true;
	}

	// --- 3. Message Handling (Hooks) ---
	bool OnMessage(UINT msg, WPARAM wp, LPARAM lp) override {
		switch (msg) {
		case WM_CREATE:
			// Start the animation timer
			::SetTimer(m_hwnd, CHRONOUI_ANIM_TIMER, 16, NULL);
			return false; // Let base continue

		case WM_DESTROY:
			::KillTimer(m_hwnd, CHRONOUI_ANIM_TIMER);
			return false;

		case WM_LBUTTONDOWN:
			::SetFocus(m_hwnd);
			FireEvent("onClick", "{}");
			return true; // We handled it
		}
		return false; // Not handled, let Base handle it
	}

	// 3. Focus Logic
	void OnFocus(bool f) override {
		// Old StaticText behavior: notify parent of focus change via custom message
		HWND parent = GetParent(m_hwnd);
		if (f && parent) {
			SendMessage(parent, WM_USER + 200, 0, (LPARAM)m_hwnd);
		}
		FireEvent(f ? "onFocus" : "onBlur", "{}");
	}
};

extern "C" __declspec(dllexport) ChronoUI::IWidget* __stdcall CreateInstance() {
	return new AnimatedParticlesProgress();
}
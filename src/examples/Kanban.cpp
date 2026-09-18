// =============================================================================
// Kanban.cpp — drag cards between columns, and watch the others make room.
//
// What this example teaches:
//   * One widget can own a whole interactive model. VBoard keeps the columns
//     and cards as plain data and paints them; there is no widget per card.
//   * Mouse capture: OnMouseDown returns VInputResult::Capture, and from then
//     on every move and the final release reach this widget even when the
//     cursor leaves its bounds.
//   * Motion sells the interaction. Each card has a `y` and a `targetY`;
//     OnUpdate eases y towards targetY, so when a card is lifted or dropped
//     its neighbours slide instead of jumping.
//   * Return Handled from OnMouseMove only when something visual changed.
//
// Build target: Kanban. Links ChronoUI only.
// =============================================================================

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

#include "VirtualWidget.hpp"
#include "VDraw.hpp"

using namespace ChronoUI;

static const D2D1_COLOR_F kBg     = vd::Col(0xF3F4F6);
static const D2D1_COLOR_F kColumn = vd::Col(0xE9ECF1);
static const D2D1_COLOR_F kText   = vd::Col(0x111827);
static const D2D1_COLOR_F kMuted  = vd::Col(0x6B7280);
static const D2D1_COLOR_F kAccent = vd::Col(0x4A90E2);

struct Card {
	std::wstring title, tag;
	D2D1_COLOR_F tagColor;
	float y = 0.0f, targetY = 0.0f;
	bool  placed = false;          // false = snap to targetY on next layout
};
struct Column {
	std::wstring name;
	std::vector<Card> cards;
};

static D2D1_COLOR_F TagColor(const std::wstring& tag) {
	if (tag == L"Bug")     return vd::Col(0xEF4444);
	if (tag == L"Design")  return vd::Col(0x8B5CF6);
	if (tag == L"Docs")    return vd::Col(0x10B981);
	if (tag == L"Infra")   return vd::Col(0xF59E0B);
	return vd::Col(0x3B82F6);      // Feature
}

// =============================================================================
class VBoard : public VirtualWidgetImpl {
	std::vector<Column> m_cols;

	// Drag state. The lifted card leaves its column and lives in m_drag until
	// it is dropped; m_overCol / m_insert are where it would land right now.
	bool  m_dragging = false;
	Card  m_drag;
	float m_grabX = 0, m_grabY = 0, m_mx = 0, m_my = 0;
	int   m_overCol = -1, m_insert = -1;
	int   m_hoverCol = -1, m_hoverIdx = -1;

	static constexpr float kPad = 18.0f, kHead = 48.0f, kCardH = 74.0f, kGap = 10.0f;

	D2D1_RECT_F ColRect(int c) const {
		float n = (float)m_cols.size();
		float w = (vd::W(m_bounds) - kPad * (n + 1.0f)) / n;
		float x = m_bounds.left + kPad + (w + kPad) * (float)c;
		return D2D1::RectF(x, m_bounds.top + kPad, x + w, m_bounds.bottom - kPad);
	}
	D2D1_RECT_F CardRect(int c, float y) const {
		D2D1_RECT_F col = ColRect(c);
		return D2D1::RectF(col.left + 10.0f, y, col.right - 10.0f, y + kCardH);
	}
	float SlotY(int c, int slot) const { return ColRect(c).top + kHead + (float)slot * (kCardH + kGap); }

	// Every card gets a target slot; in the column under a drag, cards at or
	// after the insertion point shift one slot down to open a gap.
	void Relayout(bool snap = false) {
		for (int c = 0; c < (int)m_cols.size(); ++c) {
			int slot = 0;
			for (size_t i = 0; i < m_cols[c].cards.size(); ++i, ++slot) {
				if (m_dragging && c == m_overCol && (int)i == m_insert) ++slot;
				Card& k = m_cols[c].cards[i];
				k.targetY = SlotY(c, slot);
				if (snap || !k.placed) { k.y = k.targetY; k.placed = true; }
			}
		}
	}
	bool HitCard(float x, float y, int& col, int& idx) const {
		for (int c = 0; c < (int)m_cols.size(); ++c)
			for (size_t i = 0; i < m_cols[c].cards.size(); ++i)
				if (vd::Contains(CardRect(c, m_cols[c].cards[i].y), x, y)) { col = c; idx = (int)i; return true; }
		return false;
	}
	int ColumnAt(float x) const {
		int best = 0; float bestD = 1e9f;
		for (int c = 0; c < (int)m_cols.size(); ++c) {
			float d = fabsf(vd::CX(ColRect(c)) - x);
			if (d < bestD) { bestD = d; best = c; }
		}
		return best;
	}
	int InsertIndex(int col, float y) const {
		int idx = 0;
		for (const Card& k : m_cols[col].cards) if (k.targetY + kCardH * 0.5f < y) ++idx;
		return (std::min)(idx, (int)m_cols[col].cards.size());
	}

	void DrawCard(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, const Card& k, bool lifted) {
		vd::Shadow(rt, r, 10.0f, lifted ? 0.28f : 0.10f, lifted ? 6 : 3);
		vd::Fill(rt, r, vd::Col(0xFFFFFF), 10.0f);
		vd::Stroke(rt, r, vd::Col(0xE5E7EB), 10.0f);
		vd::Style pillStyle = vd::Style().Size(10).Bold().Center();
		float pw = vd::TextWidth(k.tag, pillStyle) + 14.0f;
		D2D1_RECT_F pill = vd::Rect(r.left + 12.0f, r.top + 10.0f, pw, 18.0f);
		vd::Fill(rt, pill, vd::Alpha(k.tagColor, 0.16f), 9.0f);
		vd::Text(rt, k.tag, pill, k.tagColor, pillStyle);
		vd::Text(rt, k.title, D2D1::RectF(r.left + 12.0f, r.top + 32.0f, r.right - 12.0f, r.bottom - 8.0f), kText, vd::Style().Size(13).Top().Wrap());
	}

public:
	const char* GetTypeName() const override { return "VBoard"; }

	void SetBounds(const D2D1_RECT_F& r) override { VirtualWidgetImpl::SetBounds(r); Relayout(true); }

	void Reset() {
		m_cols = {
			{ L"To do",       { { L"Design the onboarding flow", L"Design", TagColor(L"Design") },
			                    { L"Rate limiter returns 500 under load", L"Bug", TagColor(L"Bug") },
			                    { L"Write the widget catalog page", L"Docs", TagColor(L"Docs") },
			                    { L"Dark palette for virtual widgets", L"Feature", TagColor(L"Feature") },
			                    { L"Nightly build on a tag", L"Infra", TagColor(L"Infra") } } },
			{ L"In progress", { { L"Kanban board example", L"Feature", TagColor(L"Feature") },
			                    { L"Tooltip clipped at the window edge", L"Bug", TagColor(L"Bug") },
			                    { L"Screenshot driver: drag steps", L"Infra", TagColor(L"Infra") } } },
			{ L"Done",        { { L"Custom title bar drags again", L"Bug", TagColor(L"Bug") },
			                    { L"VirtualShowcase tour", L"Feature", TagColor(L"Feature") },
			                    { L"Build with no vcpkg", L"Infra", TagColor(L"Infra") },
			                    { L"README front page", L"Docs", TagColor(L"Docs") } } },
		};
		m_dragging = false; m_hoverCol = m_hoverIdx = -1;
		Relayout(true);
	}
	void AddCard(const std::wstring& title, const std::wstring& tag) {
		Card k{ title, tag, TagColor(tag) };
		k.y = SlotY(0, (int)m_cols[0].cards.size()) - 24.0f;   // drop in from above
		k.placed = true;
		m_cols[0].cards.push_back(k);
		Relayout();
	}
	int CardCount() const { int n = 0; for (auto& c : m_cols) n += (int)c.cards.size(); return n; }

	bool OnUpdate(float dt) override {
		bool moving = false;
		for (auto& col : m_cols)
			for (Card& k : col.cards) {
				if (fabsf(k.y - k.targetY) < 0.3f) { k.y = k.targetY; continue; }
				k.y = vd::Approach(k.y, k.targetY, dt, 16.0f);
				moving = true;
			}
		return moving || m_dragging;
	}

	VInputResult OnMouseDown(float x, float y, int btn) override {
		int c, i;
		if (btn != 1 || !HitCard(x, y, c, i)) return VInputResult::NotHandled;
		Card& k = m_cols[c].cards[(size_t)i];
		D2D1_RECT_F r = CardRect(c, k.y);
		m_drag = k; m_grabX = x - r.left; m_grabY = y - r.top; m_mx = x; m_my = y;
		m_cols[c].cards.erase(m_cols[c].cards.begin() + i);
		m_dragging = true; m_overCol = c; m_insert = i; m_hoverCol = m_hoverIdx = -1;
		Relayout();
		return VInputResult::Capture;
	}
	VInputResult OnMouseMove(float x, float y) override {
		if (m_dragging) {
			m_mx = x; m_my = y;
			int col = ColumnAt(x), ins = InsertIndex(col, y);
			if (col != m_overCol || ins != m_insert) { m_overCol = col; m_insert = ins; Relayout(); }
			return VInputResult::Handled;
		}
		int c = -1, i = -1; HitCard(x, y, c, i);
		if (c == m_hoverCol && i == m_hoverIdx) return VInputResult::NotHandled;
		m_hoverCol = c; m_hoverIdx = i;
		return VInputResult::Handled;
	}
	VInputResult OnMouseUp(float, float, int) override {
		if (!m_dragging) return VInputResult::NotHandled;
		m_dragging = false;
		Card k = m_drag;
		k.y = m_my - m_grabY; k.placed = true;          // glide from where it was dropped
		auto& cards = m_cols[m_overCol].cards;
		cards.insert(cards.begin() + (std::min)(m_insert, (int)cards.size()), k);
		Relayout();
		return VInputResult::Handled;
	}
	VInputResult OnMouseLeave() override { m_hoverCol = m_hoverIdx = -1; return VInputResult::Handled; }

	void OnDraw(ID2D1RenderTarget* rt) override {
		for (int c = 0; c < (int)m_cols.size(); ++c) {
			D2D1_RECT_F col = ColRect(c);
			vd::Fill(rt, col, kColumn, 14.0f);
			vd::Text(rt, m_cols[c].name, vd::Rect(col.left + 14.0f, col.top + 12.0f, vd::W(col) - 60.0f, 24.0f), kText, vd::Style().Size(14).Bold());
			std::wstring n = std::to_wstring(m_cols[c].cards.size() + ((m_dragging && c == m_overCol) ? 1 : 0));
			D2D1_RECT_F pill = vd::Rect(col.right - 44.0f, col.top + 14.0f, 30.0f, 20.0f);
			vd::Fill(rt, pill, vd::Col(0xD1D5DB), 10.0f);
			vd::Text(rt, n, pill, kMuted, vd::Style().Size(11).Bold().Center());

			if (m_dragging && c == m_overCol) {
				D2D1_RECT_F gap = CardRect(c, SlotY(c, m_insert));
				vd::Fill(rt, gap, vd::Alpha(kAccent, 0.08f), 10.0f);
				vd::Stroke(rt, gap, vd::Alpha(kAccent, 0.6f), 10.0f, 1.5f);
			}
			for (size_t i = 0; i < m_cols[c].cards.size(); ++i) {
				const Card& k = m_cols[c].cards[i];
				bool hov = (c == m_hoverCol && (int)i == m_hoverIdx);
				D2D1_RECT_F r = CardRect(c, k.y);
				if (hov) r = vd::Inset(r, -2.0f, -2.0f);
				DrawCard(rt, r, k, hov);
			}
		}
		if (m_dragging) {
			// The ghost: same card, slightly tilted and lifted, drawn last so it
			// floats above everything.
			D2D1_RECT_F r = CardRect(m_overCol, 0.0f);
			r = D2D1::RectF(m_mx - m_grabX, m_my - m_grabY, m_mx - m_grabX + vd::W(r), m_my - m_grabY + kCardH);
			rt->SetTransform(D2D1::Matrix3x2F::Rotation(2.5f, D2D1::Point2F(vd::CX(r), vd::CY(r))));
			DrawCard(rt, r, m_drag, true);
			rt->SetTransform(D2D1::Matrix3x2F::Identity());
		}
	}
};

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Kanban", 1100, 700)) return 1;
	win.SetBackground(kBg);

	auto* title = win.Add<VLabel>(); title->Text(L"Sprint board").FontSize(20.0f).Color(kText);
	auto* hint  = win.Add<VLabel>(); hint->Text(L"Drag a card to another column. The others make room.").FontSize(12.0f).Color(kMuted);
	auto* count = win.Add<VLabel>(); count->FontSize(12.0f).Color(kMuted);
	auto* add   = win.Add<VButton>(); add->Text(L"+ Add card");
	auto* reset = win.Add<VButton>(); reset->Text(L"Reset");
	reset->Face(vd::Col(0xE5E7EB)).FaceHover(vd::Col(0xD1D5DB)).FacePress(vd::Col(0x9CA3AF)).TextColor(kText);
	auto* board = win.Add<VBoard>();
	board->Reset();

	static const wchar_t* kTitles[] = { L"Ship the dashboard example", L"Investigate slow first paint",
		L"Translate the last Spanish comments", L"Add a dark palette", L"Profile the mind map on 2k nodes",
		L"Write the release notes" };
	static const wchar_t* kTags[] = { L"Feature", L"Bug", L"Docs", L"Design", L"Infra", L"Docs" };
	int next = 0;
	auto refreshCount = [&] { count->Text(std::to_wstring(board->CardCount()) + L" cards"); };
	add->OnClick([&] { board->AddCard(kTitles[next % 6], kTags[next % 6]); ++next; refreshCount(); });
	reset->OnClick([&] { board->Reset(); refreshCount(); });

	auto layout = [&] {
		RECT rc; GetClientRect(win.GetHWND(), &rc);
		float W = (float)rc.right, H = (float)rc.bottom;
		title->SetBounds(vd::Rect(24.0f, 14.0f, 400.0f, 28.0f));
		hint ->SetBounds(vd::Rect(24.0f, 42.0f, 600.0f, 20.0f));
		count->SetBounds(vd::Rect(W - 24.0f - 96.0f - 12.0f - 110.0f - 12.0f - 80.0f, 22.0f, 80.0f, 34.0f));
		reset->SetBounds(vd::Rect(W - 24.0f - 96.0f - 12.0f - 110.0f, 22.0f, 110.0f, 34.0f));
		add  ->SetBounds(vd::Rect(W - 24.0f - 96.0f, 22.0f, 96.0f, 34.0f));
		board->SetBounds(D2D1::RectF(6.0f, 66.0f, W - 6.0f, H));
		InvalidateRect(win.GetHWND(), NULL, FALSE);
	};
	win.OnResize(layout);
	layout();
	refreshCount();
	return win.RunMessageLoop();
}

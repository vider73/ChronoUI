// =============================================================================
// Table.cpp — a sortable, filterable data grid with a detail panel.
//
// What this example teaches:
//   * A grid is one widget over a vector of records. Columns are data
//     (title, weight, alignment), rows are painted on the fly, and only the
//     visible ones — 10,000 rows cost the same as 20.
//   * Its own scrolling: the widget keeps a pixel offset, reacts to the
//     wheel, clips its rows with PushAxisAlignedClip and paints a thumb.
//   * Sorting and filtering never touch the data: a `view` of indices is
//     rebuilt when a header is clicked or the search box changes, and the
//     selection is remembered by record id so it survives both.
//   * Master-detail: the table reports a selection, the panel shows the
//     record, and three buttons mutate the data and ask the table to refresh.
//
// Build target: Table. Links ChronoUI only.
// =============================================================================

#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "VirtualWidget.hpp"
#include "VirtualChat.hpp"
#include "VDraw.hpp"

using namespace ChronoUI;

static const D2D1_COLOR_F kBg     = vd::Col(0xF3F4F6);
static const D2D1_COLOR_F kPanel  = vd::Col(0xFFFFFF);
static const D2D1_COLOR_F kLine   = vd::Col(0xE5E7EB);
static const D2D1_COLOR_F kText   = vd::Col(0x111827);
static const D2D1_COLOR_F kMuted  = vd::Col(0x6B7280);
static const D2D1_COLOR_F kAccent = vd::Col(0x4A90E2);

enum class Status { Paid, Pending, Overdue };
struct Invoice {
	int          id;
	std::wstring number, customer, country, date;
	double       amount;
	Status       status;
};
static const wchar_t* StatusName(Status s) { return s == Status::Paid ? L"Paid" : (s == Status::Pending ? L"Pending" : L"Overdue"); }
static D2D1_COLOR_F   StatusColor(Status s) { return s == Status::Paid ? vd::Col(0x059669) : (s == Status::Pending ? vd::Col(0xD97706) : vd::Col(0xDC2626)); }

static std::vector<Invoice> MakeInvoices(int n) {
	static const wchar_t* customers[] = { L"Atelier Norte", L"Bluepeak Logistics", L"Northwind Traders", L"Vega Dental", L"Lumen Studio",
		L"Orion Robotics", L"Cobalt Foods", L"Helix Labs", L"Marlin Marine", L"Sable & Co", L"Quill Press", L"Tessera Tiles" };
	static const wchar_t* countries[] = { L"Spain", L"Portugal", L"France", L"Germany", L"Italy", L"Netherlands", L"Ireland" };
	std::mt19937 g(42);
	std::vector<Invoice> v;
	for (int i = 0; i < n; ++i) {
		Invoice in;
		in.id = i + 1;
		wchar_t num[16]; swprintf_s(num, L"INV-%04d", 1180 + i);
		in.number = num;
		in.customer = customers[g() % 12];
		in.country = countries[g() % 7];
		int day = 1 + (int)(g() % 28), month = 1 + (int)(g() % 9);
		wchar_t date[16]; swprintf_s(date, L"2026-%02d-%02d", month, day);
		in.date = date;
		in.amount = 120.0 + (double)(g() % 98000) / 10.0;
		int r = (int)(g() % 10);
		in.status = r < 6 ? Status::Paid : (r < 9 ? Status::Pending : Status::Overdue);
		v.push_back(in);
	}
	return v;
}

// =============================================================================
class VTable : public VirtualWidgetImpl {
public:
	struct Column { std::wstring title; float weight; bool right; };
private:
	std::vector<Column>   m_cols;
	std::vector<Invoice>* m_rows = nullptr;
	std::vector<int>      m_view;          // indices into *m_rows, filtered + sorted
	int   m_sortCol = 0; bool m_sortAsc = true;
	std::wstring m_filter;
	int   m_selectedId = -1, m_hoverRow = -1;
	float m_scroll = 0.0f;
	std::function<void(const Invoice*)> m_onSelect;

	static constexpr float kHead = 38.0f, kRow = 34.0f, kPadX = 14.0f;

	D2D1_RECT_F Body() const { return D2D1::RectF(m_bounds.left, m_bounds.top + kHead, m_bounds.right, m_bounds.bottom); }
	float ColX(int c) const {
		float total = 0; for (auto& col : m_cols) total += col.weight;
		float x = m_bounds.left, w = vd::W(m_bounds) - 12.0f;
		for (int i = 0; i < c; ++i) x += w * m_cols[(size_t)i].weight / total;
		return x;
	}
	float ColW(int c) const {
		float total = 0; for (auto& col : m_cols) total += col.weight;
		return (vd::W(m_bounds) - 12.0f) * m_cols[(size_t)c].weight / total;
	}
	float MaxScroll() const { return (std::max)(0.0f, (float)m_view.size() * kRow - vd::H(Body())); }
	int RowAt(float y) const {
		if (y < Body().top) return -1;
		int i = (int)((y - Body().top + m_scroll) / kRow);
		return (i >= 0 && i < (int)m_view.size()) ? i : -1;
	}
	static std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
	std::wstring Cell(const Invoice& r, int c) const {
		switch (c) {
		case 0: return r.number; case 1: return r.customer; case 2: return r.country;
		case 3: return r.date; case 4: return L"\x20AC" + vd::Num(r.amount, 2); default: return StatusName(r.status);
		}
	}

public:
	const char* GetTypeName() const override { return "VTable"; }
	bool CanFocus() const override { return true; }

	void Bind(std::vector<Invoice>* rows, std::vector<Column> cols) { m_rows = rows; m_cols = std::move(cols); Rebuild(); }
	void OnSelect(std::function<void(const Invoice*)> cb) { m_onSelect = std::move(cb); }
	void SetFilter(const std::wstring& f) { m_filter = Lower(f); Rebuild(); }
	int  VisibleCount() const { return (int)m_view.size(); }
	const Invoice* Selected() const {
		if (!m_rows) return nullptr;
		for (auto& r : *m_rows) if (r.id == m_selectedId) return &r;
		return nullptr;
	}
	void Select(int id) { m_selectedId = id; if (m_onSelect) m_onSelect(Selected()); }
	// Re-derive the view after the data or the criteria changed.
	void Rebuild() {
		m_view.clear();
		if (!m_rows) return;
		for (int i = 0; i < (int)m_rows->size(); ++i) {
			const Invoice& r = (*m_rows)[(size_t)i];
			if (!m_filter.empty() && Lower(r.customer).find(m_filter) == std::wstring::npos
				&& Lower(r.country).find(m_filter) == std::wstring::npos && Lower(r.number).find(m_filter) == std::wstring::npos) continue;
			m_view.push_back(i);
		}
		int c = m_sortCol; bool asc = m_sortAsc;
		std::stable_sort(m_view.begin(), m_view.end(), [&](int a, int b) {
			const Invoice& x = (*m_rows)[(size_t)a]; const Invoice& y = (*m_rows)[(size_t)b];
			bool less;
			if (c == 4) less = x.amount < y.amount;
			else if (c == 5) less = (int)x.status < (int)y.status;
			else less = Cell(x, c) < Cell(y, c);
			return asc ? less : (c == 4 ? x.amount > y.amount : (c == 5 ? (int)x.status > (int)y.status : Cell(x, c) > Cell(y, c)));
		});
		m_scroll = (std::min)(m_scroll, MaxScroll());
		if (Selected() == nullptr && m_selectedId != -1) Select(-1);
	}
	void ScrollTo(int id) {
		for (size_t i = 0; i < m_view.size(); ++i)
			if ((*m_rows)[(size_t)m_view[i]].id == id) {
				float y = (float)i * kRow;
				if (y < m_scroll) m_scroll = y;
				else if (y + kRow > m_scroll + vd::H(Body())) m_scroll = y + kRow - vd::H(Body());
				return;
			}
	}

	VInputResult OnMouseWheel(float delta, float, float) override {
		m_scroll = vd::Clamp(m_scroll - delta / 120.0f * kRow * 3.0f, 0.0f, MaxScroll());
		return VInputResult::Handled;
	}
	VInputResult OnMouseMove(float, float y) override {
		int r = RowAt(y); if (r == m_hoverRow) return VInputResult::NotHandled;
		m_hoverRow = r; return VInputResult::Handled;
	}
	VInputResult OnMouseLeave() override { m_hoverRow = -1; return VInputResult::Handled; }
	VInputResult OnMouseDown(float x, float y, int btn) override {
		if (btn != 1) return VInputResult::NotHandled;
		if (y < m_bounds.top + kHead) {
			for (int c = 0; c < (int)m_cols.size(); ++c)
				if (x >= ColX(c) && x < ColX(c) + ColW(c)) {
					if (m_sortCol == c) m_sortAsc = !m_sortAsc; else { m_sortCol = c; m_sortAsc = true; }
					Rebuild();
				}
			return VInputResult::Handled;
		}
		int r = RowAt(y);
		if (r >= 0) Select((*m_rows)[(size_t)m_view[(size_t)r]].id);
		return VInputResult::Handled;
	}
	VInputResult OnKeyDown(UINT vk) override {
		if (vk != VK_UP && vk != VK_DOWN) return VInputResult::NotHandled;
		int at = -1;
		for (size_t i = 0; i < m_view.size(); ++i) if ((*m_rows)[(size_t)m_view[i]].id == m_selectedId) at = (int)i;
		if (m_view.empty()) return VInputResult::Handled;
		int next = (std::max)(0, (std::min)((int)m_view.size() - 1, at + (vk == VK_DOWN ? 1 : -1)));
		int id = (*m_rows)[(size_t)m_view[(size_t)next]].id;
		Select(id); ScrollTo(id);
		return VInputResult::Handled;
	}

	void OnDraw(ID2D1RenderTarget* rt) override {
		vd::Fill(rt, m_bounds, kPanel, 12.0f);
		vd::Stroke(rt, m_bounds, kLine, 12.0f);
		// Header.
		for (int c = 0; c < (int)m_cols.size(); ++c) {
			D2D1_RECT_F h = vd::Rect(ColX(c) + kPadX, m_bounds.top, ColW(c) - kPadX * 2.0f, kHead);
			std::wstring t = m_cols[(size_t)c].title;
			if (c == m_sortCol) t += m_sortAsc ? L" \x25B4" : L" \x25BE";
			vd::Text(rt, t, h, c == m_sortCol ? kText : kMuted, m_cols[(size_t)c].right ? vd::Style().Size(12).Bold().Right() : vd::Style().Size(12).Bold());
		}
		vd::Line(rt, m_bounds.left, m_bounds.top + kHead, m_bounds.right, m_bounds.top + kHead, kLine);
		// Rows: only the visible band.
		D2D1_RECT_F body = Body();
		rt->PushAxisAlignedClip(D2D1::RectF(body.left, body.top, body.right, body.bottom - 1.0f), D2D1_ANTIALIAS_MODE_ALIASED);
		int first = (int)(m_scroll / kRow), last = (std::min)((int)m_view.size(), first + (int)(vd::H(body) / kRow) + 2);
		for (int i = first; i < last; ++i) {
			const Invoice& r = (*m_rows)[(size_t)m_view[(size_t)i]];
			float y = body.top + (float)i * kRow - m_scroll;
			D2D1_RECT_F rr = D2D1::RectF(body.left + 1.0f, y, body.right - 12.0f, y + kRow);
			if (r.id == m_selectedId)   vd::Fill(rt, rr, vd::Alpha(kAccent, 0.14f));
			else if (i == m_hoverRow)   vd::Fill(rt, rr, vd::Col(0xF3F4F6));
			else if (i % 2 == 1)        vd::Fill(rt, rr, vd::Col(0xFAFAFB));
			if (r.id == m_selectedId)   vd::Fill(rt, D2D1::RectF(rr.left, rr.top, rr.left + 3.0f, rr.bottom), kAccent);
			for (int c = 0; c < (int)m_cols.size(); ++c) {
				D2D1_RECT_F cell = vd::Rect(ColX(c) + kPadX, y, ColW(c) - kPadX * 2.0f, kRow);
				if (c == 5) {
					D2D1_COLOR_F sc = StatusColor(r.status);
					vd::Style st = vd::Style().Size(11).Bold().Center();
					float pw = vd::TextWidth(StatusName(r.status), st) + 16.0f;
					D2D1_RECT_F pill = vd::Rect(cell.left, y + 8.0f, pw, 18.0f);
					vd::Fill(rt, pill, vd::Alpha(sc, 0.14f), 9.0f);
					vd::Text(rt, StatusName(r.status), pill, sc, st);
				} else {
					vd::Text(rt, Cell(r, c), cell, c == 1 ? kText : kMuted, m_cols[(size_t)c].right ? vd::Style().Size(13).Right() : vd::Style().Size(13));
				}
			}
		}
		rt->PopAxisAlignedClip();
		// Scroll thumb.
		if (MaxScroll() > 0.0f) {
			float trackH = vd::H(body) - 8.0f, thumbH = (std::max)(24.0f, trackH * vd::H(body) / ((float)m_view.size() * kRow));
			float ty = body.top + 4.0f + (trackH - thumbH) * (m_scroll / MaxScroll());
			vd::Fill(rt, vd::Rect(body.right - 9.0f, ty, 5.0f, thumbH), vd::Col(0xD1D5DB), 2.5f);
		}
		if (m_view.empty())
			vd::Text(rt, L"No invoices match \x201C" + m_filter + L"\x201D", body, kMuted, vd::Style().Size(13).Center());
	}
};

// =============================================================================
// VDetail — the selected record, or an empty state.
// =============================================================================
class VDetail : public VirtualWidgetImpl {
	const Invoice* m_r = nullptr;
public:
	const char* GetTypeName() const override { return "VDetail"; }
	void Set(const Invoice* r) { m_r = r; }
	void OnDraw(ID2D1RenderTarget* rt) override {
		vd::Fill(rt, m_bounds, kPanel, 12.0f);
		vd::Stroke(rt, m_bounds, kLine, 12.0f);
		D2D1_RECT_F in = vd::Inset(m_bounds, 20.0f, 16.0f);
		if (!m_r) {
			vd::Text(rt, L"Select an invoice to see its details.", in, kMuted, vd::Style().Size(13).Center().Wrap());
			return;
		}
		vd::Text(rt, m_r->number, vd::Rect(in.left, in.top, vd::W(in) - 90.0f, 28.0f), kText, vd::Style().Size(20).Bold());
		D2D1_COLOR_F sc = StatusColor(m_r->status);
		vd::Style st = vd::Style().Size(11).Bold().Center();
		float pw = vd::TextWidth(StatusName(m_r->status), st) + 18.0f;
		D2D1_RECT_F pill = vd::Rect(in.right - pw, in.top + 5.0f, pw, 20.0f);
		vd::Fill(rt, pill, vd::Alpha(sc, 0.14f), 10.0f);
		vd::Text(rt, StatusName(m_r->status), pill, sc, st);
		vd::Text(rt, L"\x20AC" + vd::Num(m_r->amount, 2), vd::Rect(in.left, in.top + 34.0f, vd::W(in), 40.0f), kText, vd::Style().Size(30).Light());

		struct KV { const wchar_t* k; std::wstring v; };
		KV rows[] = { { L"Customer", m_r->customer }, { L"Country", m_r->country }, { L"Issued", m_r->date },
		              { L"Due", m_r->date.substr(0, 8) + L"28" }, { L"Reference", L"PO-" + std::to_wstring(4000 + m_r->id) } };
		float y = in.top + 96.0f;
		for (const KV& kv : rows) {
			vd::Line(rt, in.left, y, in.right, y, kLine);
			vd::Text(rt, kv.k, vd::Rect(in.left, y, 110.0f, 34.0f), kMuted, vd::Style().Size(12));
			vd::Text(rt, kv.v, vd::Rect(in.left + 110.0f, y, vd::W(in) - 110.0f, 34.0f), kText, vd::Style().Size(13));
			y += 34.0f;
		}
		vd::Line(rt, in.left, y, in.right, y, kLine);
	}
};

// =============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
	ChronoControllerImpl::Instance();

	VirtualWindow win;
	if (!win.Create(hInstance, L"ChronoUI \x2014 Table", 1180, 720)) return 1;
	win.SetBackground(kBg);
	HWND hwnd = win.GetHWND();
	auto repaint = [hwnd] { InvalidateRect(hwnd, NULL, FALSE); };

	std::vector<Invoice> data = MakeInvoices(60);
	int nextId = 61;

	auto* heading = win.Add<VLabel>(); heading->Text(L"Invoices").FontSize(20.0f).Color(kText);
	auto* status  = win.Add<VLabel>(); status->FontSize(12.0f).Color(kMuted);
	auto* search  = win.Add<VChatInput>(); search->SetSingleLine(true).SetPlaceholder(L"Search customer, country or number\x2026");
	auto* table   = win.Add<VTable>();
	auto* detail  = win.Add<VDetail>();
	auto* markPaid = win.Add<VButton>(); markPaid->Text(L"Mark as paid");
	auto* dup      = win.Add<VButton>(); dup->Text(L"Duplicate");
	auto* del      = win.Add<VButton>(); del->Text(L"Delete");
	auto ghost = [](VButton* b) { b->Face(vd::Col(0xE5E7EB)).FaceHover(vd::Col(0xD1D5DB)).FacePress(vd::Col(0x9CA3AF)).TextColor(kText); };
	ghost(dup);
	del->Face(vd::Col(0xFEE2E2)).FaceHover(vd::Col(0xFECACA)).FacePress(vd::Col(0xFCA5A5)).TextColor(vd::Col(0xB91C1C));

	table->Bind(&data, { { L"Number", 1.1f, false }, { L"Customer", 1.8f, false }, { L"Country", 1.1f, false },
	                     { L"Date", 1.0f, false }, { L"Amount", 1.1f, true }, { L"Status", 0.9f, false } });

	auto refreshStatus = [&] {
		double total = 0; int overdue = 0;
		for (auto& r : data) { total += r.amount; if (r.status == Status::Overdue) ++overdue; }
		status->Text(std::to_wstring(table->VisibleCount()) + L" of " + std::to_wstring(data.size()) + L" invoices  \x00B7  \x20AC"
			+ vd::Num(total, 2) + L" total  \x00B7  " + std::to_wstring(overdue) + L" overdue");
	};
	auto onSelect = [&](const Invoice* r) {
		detail->Set(r);
		bool has = r != nullptr;
		markPaid->Enabled(has && r->status != Status::Paid); dup->Enabled(has); del->Enabled(has);
		repaint();
	};
	table->OnSelect(onSelect);
	search->OnTextChanged([&] { table->SetFilter(search->GetText()); refreshStatus(); repaint(); });

	markPaid->OnClick([&] {
		const Invoice* r = table->Selected(); if (!r) return;
		for (auto& x : data) if (x.id == r->id) x.status = Status::Paid;
		table->Rebuild(); table->Select(r->id); refreshStatus();
	});
	dup->OnClick([&] {
		const Invoice* r = table->Selected(); if (!r) return;
		Invoice copy = *r; copy.id = nextId++;
		wchar_t num[16]; swprintf_s(num, L"INV-%04d", 1180 + copy.id - 1); copy.number = num;
		copy.status = Status::Pending;
		for (size_t i = 0; i < data.size(); ++i) if (data[i].id == r->id) { data.insert(data.begin() + (ptrdiff_t)i + 1, copy); break; }
		table->Rebuild(); table->Select(copy.id); table->ScrollTo(copy.id); refreshStatus();
	});
	del->OnClick([&] {
		const Invoice* r = table->Selected(); if (!r) return;
		int id = r->id;
		data.erase(std::remove_if(data.begin(), data.end(), [id](const Invoice& x) { return x.id == id; }), data.end());
		table->Rebuild(); table->Select(-1); refreshStatus();
	});

	auto layout = [&] {
		RECT rc; GetClientRect(hwnd, &rc);
		float W = (float)rc.right, H = (float)rc.bottom, pad = 24.0f;
		heading->SetBounds(vd::Rect(pad, 16.0f, 300.0f, 28.0f));
		status ->SetBounds(vd::Rect(pad, 44.0f, 600.0f, 20.0f));
		search ->SetBounds(vd::Rect(W - pad - 320.0f, 20.0f, 320.0f, 38.0f));
		float top = 78.0f, detailW = 340.0f;
		table ->SetBounds(D2D1::RectF(pad, top, W - pad - detailW - 16.0f, H - pad));
		detail->SetBounds(D2D1::RectF(W - pad - detailW, top, W - pad, H - pad - 62.0f));
		float bx = W - pad - detailW, by = H - pad - 48.0f;
		markPaid->SetBounds(vd::Rect(bx, by, 118.0f, 36.0f));
		dup     ->SetBounds(vd::Rect(bx + 126.0f, by, 100.0f, 36.0f));
		del     ->SetBounds(vd::Rect(bx + detailW - 80.0f, by, 80.0f, 36.0f));
		repaint();
	};
	win.OnResize(layout);
	layout();
	refreshStatus();
	table->Select(data[2].id);
	win.SetFocusWidget(table);
	return win.RunMessageLoop();
}

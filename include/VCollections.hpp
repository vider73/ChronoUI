// =============================================================================
// VCollections.hpp — lists of things, as virtual widgets.
//
//   VListView   rows with a glyph, a title and a subtitle; single or multi
//               selection (click, Ctrl+click, Shift+click, Ctrl+A), keyboard,
//               its own wheel scroll and thumb
//   VTreeView   nested nodes with a chevron per branch; open/closed state
//               lives in the node, arrows walk and fold the tree
//   VGridView   tiles painted by a callback, with captions and the same
//               selection gestures as the list
//   VFlipView   one item at a time, painted by a callback; arrows at the
//               edges, keys and wheel flip, the pages slide
//   VPipsPager  the dots under a flip view: one per page, the current one lit
//   VAnnotatedScrollBar  a tall scrollbar with labels along it (months, letters)
//               and a tooltip while dragging; drives any scrolling widget
//
// Both paint only the rows inside their bounds, so cost follows what is
// visible, not the size of the data. Same conventions as VControls.hpp.
// See src/examples/Settings.cpp for both in use.
// =============================================================================

#pragma once

#include "VirtualWidget.hpp"
#include "VControls.hpp"
#include "VDraw.hpp"
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace ChronoUI {

	// -------------------------------------------------------------------------
	// VSelection — the selection gestures a list and a grid share: click,
	// Ctrl+click toggles, Shift+click extends from the anchor, Ctrl+A.
	// -------------------------------------------------------------------------
	struct VSelection {
		std::vector<char> on;
		int  focus = -1, anchor = -1;
		bool multi = true;
		void Reset(size_t n) { on.assign(n, 0); focus = anchor = -1; }
		void Click(int i, bool ctrl, bool shift) {
			if (!multi) { ctrl = false; shift = false; }
			if (shift && anchor >= 0) {
				if (!ctrl) std::fill(on.begin(), on.end(), (char)0);
				for (int k = (std::min)(anchor, i); k <= (std::max)(anchor, i); ++k) on[(size_t)k] = 1;
			} else if (ctrl) {
				on[(size_t)i] = !on[(size_t)i]; anchor = i;
			} else {
				std::fill(on.begin(), on.end(), (char)0); on[(size_t)i] = 1; anchor = i;
			}
			focus = i;
		}
		void All(bool v) { std::fill(on.begin(), on.end(), (char)(v ? 1 : 0)); }
		void Erase(int i) { on.erase(on.begin() + i); focus = anchor = -1; }
		bool Is(int i) const { return on[(size_t)i] != 0; }
		int  Count() const { int n = 0; for (char c : on) n += c ? 1 : 0; return n; }
		std::vector<int> Indices() const { std::vector<int> v; for (size_t i = 0; i < on.size(); ++i) if (on[i]) v.push_back((int)i); return v; }
		static bool Key(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }
	};

	// -------------------------------------------------------------------------
	class VListView : public VirtualWidgetImpl {
	public:
		struct Item { std::wstring title, subtitle, glyph; bool unread = false; };   // unread = dot + heavier title
	private:
		std::vector<Item> m_items;
		VSelection m_s;
		int   m_hover = -1;
		bool  m_avatars = false;
		float m_scroll = 0.0f, m_rowH = 56.0f;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void()>    m_cb;
		std::function<void(int)> m_act;

		int   Count() const     { return (int)m_items.size(); }
		float MaxScroll() const { return (std::max)(0.0f, m_rowH * (float)Count() - vd::H(m_bounds)); }
		void  ClampScroll()     { m_scroll = vd::Clamp(m_scroll, 0.0f, MaxScroll()); }
		D2D1_RECT_F RowRect(int i) const { return vd::Rect(m_bounds.left, m_bounds.top + m_rowH * (float)i - m_scroll, vd::W(m_bounds), m_rowH); }
		int RowAt(float x, float y) const {
			if (!HitTest(x, y)) return -1;
			int i = (int)((y - m_bounds.top + m_scroll) / m_rowH);
			return (i >= 0 && i < Count()) ? i : -1;
		}
		void Reveal(int i) {
			D2D1_RECT_F r = RowRect(i);
			if (r.top < m_bounds.top) m_scroll -= m_bounds.top - r.top;
			else if (r.bottom > m_bounds.bottom) m_scroll += r.bottom - m_bounds.bottom;
			ClampScroll();
		}
		void Click(int i, bool ctrl, bool shift) { m_s.Click(i, ctrl, shift); Reveal(i); if (m_cb) m_cb(); }
	public:
		const char* GetTypeName() const override { return "VListView"; }
		bool CanFocus() const override { return true; }
		VListView& Items(std::vector<Item> v) {
			m_items = std::move(v); m_s.Reset(m_items.size());
			m_hover = -1; m_scroll = 0.0f; return *this;
		}
		VListView& Multi(bool m)           { m_s.multi = m; return *this; }
		VListView& Avatars(bool a)         { m_avatars = a; return *this; }   // initials circle from the title instead of the glyph
		VListView& RowHeight(float h)      { m_rowH = h; return *this; }
		VListView& Accent(D2D1_COLOR_F c)  { m_accent = c; return *this; }
		int  Size() const                  { return Count(); }
		const Item& At(int i) const        { return m_items[(size_t)i]; }
		Item&       Edit(int i)            { return m_items[(size_t)i]; }   // patch a row in place (unread, subtitle...)
		void Select(int i)                 { if (i >= 0 && i < Count()) Click(i, false, false); }
		void Clear()                       { m_s.All(false); m_s.focus = m_s.anchor = -1; if (m_cb) m_cb(); }
		bool IsSelected(int i) const       { return m_s.Is(i); }
		std::vector<int> Selected() const  { return m_s.Indices(); }
		int  SelectedCount() const         { return m_s.Count(); }
		void SelectAll(bool on)            { m_s.All(on); if (m_cb) m_cb(); }
		// Remove rows by index (any order). Selection and focus are dropped.
		void Erase(std::vector<int> idx) {
			std::sort(idx.begin(), idx.end(), std::greater<int>());
			for (int i : idx) if (i >= 0 && i < Count()) { m_items.erase(m_items.begin() + i); m_s.Erase(i); }
			m_hover = -1; ClampScroll();
			if (m_cb) m_cb();
		}
		void OnSelection(std::function<void()> cb)   { m_cb = std::move(cb); }
		void OnActivate(std::function<void(int)> cb) { m_act = std::move(cb); }   // Enter on the focused row

		VInputResult OnMouseMove(float x, float y) override {
			int h = RowAt(x, y); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = -1; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			int i = RowAt(x, y);
			if (btn != 1 || i < 0) return VInputResult::NotHandled;
			Click(i, VSelection::Key(VK_CONTROL), VSelection::Key(VK_SHIFT));
			return VInputResult::Handled;
		}
		VInputResult OnMouseWheel(float delta, float, float) override {
			if (MaxScroll() <= 0.0f) return VInputResult::NotHandled;
			m_scroll -= (delta > 0 ? 1.0f : -1.0f) * m_rowH * 1.5f; ClampScroll();
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (Count() == 0) return VInputResult::NotHandled;
			int last = Count() - 1, f = m_s.focus;
			switch (vk) {
				case VK_DOWN:   Click((std::min)(last, f + 1), false, VSelection::Key(VK_SHIFT)); break;
				case VK_UP:     Click((std::max)(0, f - 1), false, VSelection::Key(VK_SHIFT)); break;
				case VK_HOME:   Click(0, false, VSelection::Key(VK_SHIFT)); break;
				case VK_END:    Click(last, false, VSelection::Key(VK_SHIFT)); break;
				case VK_SPACE:  if (f >= 0) Click(f, true, false); break;
				case VK_RETURN: if (f >= 0 && m_act) m_act(f); break;
				case 'A':       if (VSelection::Key(VK_CONTROL) && m_s.multi) SelectAll(true); else return VInputResult::NotHandled; break;
				default: return VInputResult::NotHandled;
			}
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 8.0f);
			vd::Stroke(rt, m_bounds, vd::Col(0xE5E7EB), 8.0f, 1.0f);
			rt->PushAxisAlignedClip(vd::Inset(m_bounds, 1.0f, 1.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			int first = (std::max)(0, (int)(m_scroll / m_rowH));
			int lastV = (std::min)(Count() - 1, (int)((m_scroll + vd::H(m_bounds)) / m_rowH));
			for (int i = first; i <= lastV; ++i) {
				const Item& it = m_items[(size_t)i];
				D2D1_RECT_F r = RowRect(i), in = vd::Inset(r, 4.0f, 2.0f);
				bool sel = m_s.Is(i);
				float cy = vd::CY(r);
				if (sel)                 vd::Fill(rt, in, vd::Alpha(m_accent, 0.08f), 6.0f);
				else if (i == m_hover)   vd::Fill(rt, in, vd::Col(0x000000, 0.04f), 6.0f);
				if (sel)                 vd::Fill(rt, vd::Rect(r.left + 4.0f, cy - 10.0f, 3.0f, 20.0f), m_accent, 1.5f);
				if (m_focused && i == m_s.focus) vd::Stroke(rt, in, vd::Alpha(m_accent, 0.5f), 6.0f, 1.0f);
				float tx = r.left + 20.0f;
				if (it.unread) vd::Circle(rt, r.left + 12.0f, cy, 3.5f, m_accent);
				if (m_avatars) {
					vd::Avatar(rt, r.left + 38.0f, cy, 18.0f, it.title);
					tx = r.left + 68.0f;
				} else if (!it.glyph.empty()) {
					vd::Circle(rt, r.left + 38.0f, cy, 18.0f, sel ? vd::Alpha(m_accent, 0.15f) : vctl::Track());
					vd::Text(rt, it.glyph, vd::Rect(r.left + 20.0f, cy - 18.0f, 36.0f, 36.0f), sel ? m_accent : vctl::Muted(), vd::Style().Icon().Size(15).Center());
					tx = r.left + 68.0f;
				}
				if (it.subtitle.empty()) {
					vd::Text(rt, it.title, D2D1::RectF(tx, r.top, r.right - 16.0f, r.bottom), vctl::Ink(), vd::Style().Size(13).Bold());
				} else {
					vd::Text(rt, it.title,    D2D1::RectF(tx, r.top + 9.0f,  r.right - 16.0f, r.top + 30.0f), vctl::Ink(),   it.unread ? vd::Style().Size(13).Heavy() : vd::Style().Size(13).Bold());
					vd::Text(rt, it.subtitle, D2D1::RectF(tx, r.top + 29.0f, r.right - 16.0f, r.top + 48.0f), vctl::Muted(), vd::Style().Size(12));
				}
			}
			float ms = MaxScroll();
			if (ms > 0.0f) {
				float H = vd::H(m_bounds) - 8.0f, th = (std::max)(24.0f, H * H / (m_rowH * (float)Count()));
				float ty = m_bounds.top + 4.0f + (H - th) * (m_scroll / ms);
				vd::Fill(rt, vd::Rect(m_bounds.right - 7.0f, ty, 3.0f, th), vd::Col(0x000000, 0.22f), 1.5f);
			}
			rt->PopAxisAlignedClip();
		}
	};

	// -------------------------------------------------------------------------
	class VTreeView : public VirtualWidgetImpl {
	public:
		struct Node {
			std::wstring      label, glyph;   // glyph L"" = folder / page by whether it has kids
			std::vector<Node> kids;
			bool  open = false;
			float anim = 0.0f;                // chevron rotation, follows `open`
		};
	private:
		struct Row { Node* n; Node* parent; int depth; };
		std::vector<Node> m_roots;
		std::vector<Row>  m_rows;             // the visible rows, rebuilt on every fold
		Node* m_sel = nullptr;
		int   m_hover = -1;
		float m_scroll = 0.0f;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(Node&)> m_cb;
		static constexpr float kRow = 34.0f, kIndent = 22.0f;

		void Walk(std::vector<Node>& v, Node* parent, int depth) {
			for (Node& n : v) { m_rows.push_back({ &n, parent, depth }); if (n.open) Walk(n.kids, &n, depth + 1); }
		}
		void  Flatten()         { m_rows.clear(); Walk(m_roots, nullptr, 0); ClampScroll(); }
		int   Count() const     { return (int)m_rows.size(); }
		float MaxScroll() const { return (std::max)(0.0f, kRow * (float)Count() - vd::H(m_bounds)); }
		void  ClampScroll()     { m_scroll = vd::Clamp(m_scroll, 0.0f, MaxScroll()); }
		D2D1_RECT_F RowRect(int i) const  { return vd::Rect(m_bounds.left, m_bounds.top + kRow * (float)i - m_scroll, vd::W(m_bounds), kRow); }
		D2D1_RECT_F ChevRect(int i) const { D2D1_RECT_F r = RowRect(i); float x = r.left + 10.0f + kIndent * (float)m_rows[(size_t)i].depth; return vd::Rect(x, vd::CY(r) - 11.0f, 22.0f, 22.0f); }
		int RowAt(float x, float y) const {
			if (!HitTest(x, y)) return -1;
			int i = (int)((y - m_bounds.top + m_scroll) / kRow);
			return (i >= 0 && i < Count()) ? i : -1;
		}
		int IndexOf(const Node* n) const { for (int i = 0; i < Count(); ++i) if (m_rows[(size_t)i].n == n) return i; return -1; }
		void Reveal(int i) {
			if (i < 0) return;
			D2D1_RECT_F r = RowRect(i);
			if (r.top < m_bounds.top) m_scroll -= m_bounds.top - r.top;
			else if (r.bottom > m_bounds.bottom) m_scroll += r.bottom - m_bounds.bottom;
			ClampScroll();
		}
		void Toggle(Node* n) { if (!n || n->kids.empty()) return; n->open = !n->open; Flatten(); }
		void Select(Node* n) { if (m_sel == n) return; m_sel = n; Reveal(IndexOf(n)); if (m_cb && n) m_cb(*n); }
	public:
		const char* GetTypeName() const override { return "VTreeView"; }
		bool CanFocus() const override { return true; }
		VTreeView& Set(std::vector<Node> roots) { m_roots = std::move(roots); m_sel = nullptr; m_scroll = 0.0f; Flatten(); return *this; }
		VTreeView& Accent(D2D1_COLOR_F c)       { m_accent = c; return *this; }
		Node* Selected() const { return m_sel; }
		int   RowCount() const { return Count(); }
		void OnSelect(std::function<void(Node&)> cb) { m_cb = std::move(cb); }

		bool OnUpdate(float dt) override {
			bool busy = false;
			for (Row& r : m_rows) {
				float t = r.n->open ? 1.0f : 0.0f;
				if (fabsf(r.n->anim - t) > 0.01f) { r.n->anim = vd::Approach(r.n->anim, t, dt, 18.0f); busy = true; }
				else r.n->anim = t;
			}
			return busy;
		}
		VInputResult OnMouseMove(float x, float y) override {
			int h = RowAt(x, y); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = -1; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			int i = RowAt(x, y);
			if (btn != 1 || i < 0) return VInputResult::NotHandled;
			Node* n = m_rows[(size_t)i].n;
			if (!n->kids.empty() && vd::Contains(ChevRect(i), x, y)) Toggle(n);
			else if (m_sel == n) Toggle(n);      // a second click on the selected branch folds it
			else Select(n);
			return VInputResult::Handled;
		}
		VInputResult OnMouseWheel(float delta, float, float) override {
			if (MaxScroll() <= 0.0f) return VInputResult::NotHandled;
			m_scroll -= (delta > 0 ? 1.0f : -1.0f) * kRow * 2.0f; ClampScroll();
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (Count() == 0) return VInputResult::NotHandled;
			int i = IndexOf(m_sel);
			if (i < 0) { if (vk == VK_DOWN || vk == VK_UP) { Select(m_rows[0].n); return VInputResult::Handled; } return VInputResult::NotHandled; }
			Row row = m_rows[(size_t)i];
			switch (vk) {
				case VK_DOWN:  if (i + 1 < Count()) Select(m_rows[(size_t)i + 1].n); break;
				case VK_UP:    if (i > 0) Select(m_rows[(size_t)i - 1].n); break;
				case VK_RIGHT: if (!row.n->kids.empty()) { if (!row.n->open) Toggle(row.n); else Select(&row.n->kids[0]); } break;
				case VK_LEFT:  if (row.n->open) Toggle(row.n); else if (row.parent) Select(row.parent); break;
				case VK_RETURN: case VK_SPACE: Toggle(row.n); break;
				default: return VInputResult::NotHandled;
			}
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, vd::Col(0xFFFFFF), 8.0f);
			vd::Stroke(rt, m_bounds, vd::Col(0xE5E7EB), 8.0f, 1.0f);
			rt->PushAxisAlignedClip(vd::Inset(m_bounds, 1.0f, 1.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			int first = (std::max)(0, (int)(m_scroll / kRow));
			int lastV = (std::min)(Count() - 1, (int)((m_scroll + vd::H(m_bounds)) / kRow));
			for (int i = first; i <= lastV; ++i) {
				const Row& row = m_rows[(size_t)i];
				const Node& n = *row.n;
				D2D1_RECT_F r = RowRect(i), in = vd::Inset(r, 4.0f, 1.0f);
				bool sel = row.n == m_sel, branch = !n.kids.empty();
				float cy = vd::CY(r), x = r.left + 10.0f + kIndent * (float)row.depth;
				if (sel)               vd::Fill(rt, in, vd::Alpha(m_accent, 0.08f), 6.0f);
				else if (i == m_hover) vd::Fill(rt, in, vd::Col(0x000000, 0.04f), 6.0f);
				if (sel)               vd::Fill(rt, vd::Rect(r.left + 4.0f, cy - 8.0f, 3.0f, 16.0f), m_accent, 1.5f);
				if (sel && m_focused)  vd::Stroke(rt, in, vd::Alpha(m_accent, 0.5f), 6.0f, 1.0f);
				if (branch) vd::Chevron(rt, x + 11.0f, cy, -90.0f + 90.0f * n.anim, vctl::Muted(), 4.0f);
				const std::wstring& g = !n.glyph.empty() ? n.glyph : std::wstring(branch ? (n.open ? L"\xE838" : L"\xE8B7") : L"\xE7C3");
				vd::Text(rt, g, vd::Rect(x + 24.0f, cy - 11.0f, 22.0f, 22.0f), branch ? vd::Col(0xD97706) : vctl::Muted(), vd::Style().Icon().Size(14).Center());
				vd::Text(rt, n.label, D2D1::RectF(x + 52.0f, r.top, r.right - 16.0f, r.bottom), vctl::Ink(), vd::Style().Size(13));
			}
			float ms = MaxScroll();
			if (ms > 0.0f) {
				float H = vd::H(m_bounds) - 8.0f, th = (std::max)(24.0f, H * H / (kRow * (float)Count()));
				float ty = m_bounds.top + 4.0f + (H - th) * (m_scroll / ms);
				vd::Fill(rt, vd::Rect(m_bounds.right - 7.0f, ty, 3.0f, th), vd::Col(0x000000, 0.22f), 1.5f);
			}
			rt->PopAxisAlignedClip();
		}
	};

	// -------------------------------------------------------------------------
	class VGridView : public VirtualWidgetImpl {
	public:
		struct Tile { std::wstring title, caption; };
		using Painter = std::function<void(ID2D1RenderTarget*, const D2D1_RECT_F&, int)>;   // paints tile i inside the rect
	private:
		std::vector<Tile> m_tiles;
		VSelection m_s;
		Painter m_paint;
		int   m_hover = -1;
		bool  m_captions = true;
		float m_tw = 180.0f, m_th = 150.0f, m_gap = 12.0f, m_scroll = 0.0f;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void()>    m_cb;
		std::function<void(int)> m_act;
		static constexpr float kCaption = 44.0f;

		int   Count() const     { return (int)m_tiles.size(); }
		int   Cols() const      { return (std::max)(1, (int)((vd::W(m_bounds) - m_gap) / (m_tw + m_gap))); }
		float TileH() const     { return m_th + (m_captions ? kCaption : 0.0f); }
		float MaxScroll() const { int rows = (Count() + Cols() - 1) / Cols(); return (std::max)(0.0f, (float)rows * (TileH() + m_gap) + m_gap - vd::H(m_bounds)); }
		void  ClampScroll()     { m_scroll = vd::Clamp(m_scroll, 0.0f, MaxScroll()); }
		D2D1_RECT_F TileRect(int i) const {
			int c = i % Cols(), r = i / Cols();
			return vd::Rect(m_bounds.left + m_gap + (m_tw + m_gap) * (float)c, m_bounds.top + m_gap + (TileH() + m_gap) * (float)r - m_scroll, m_tw, TileH());
		}
		int TileAt(float x, float y) const {
			if (!HitTest(x, y)) return -1;
			for (int i = 0; i < Count(); ++i) if (vd::Contains(TileRect(i), x, y)) return i;
			return -1;
		}
		void Reveal(int i) {
			D2D1_RECT_F r = TileRect(i);
			if (r.top < m_bounds.top + m_gap) m_scroll -= m_bounds.top + m_gap - r.top;
			else if (r.bottom > m_bounds.bottom - m_gap) m_scroll += r.bottom - (m_bounds.bottom - m_gap);
			ClampScroll();
		}
		void Click(int i, bool ctrl, bool shift) { m_s.Click(i, ctrl, shift); Reveal(i); if (m_cb) m_cb(); }
	public:
		const char* GetTypeName() const override { return "VGridView"; }
		bool CanFocus() const override { return true; }
		VGridView& Tiles(std::vector<Tile> v)    { m_tiles = std::move(v); m_s.Reset(m_tiles.size()); m_hover = -1; ClampScroll(); return *this; }
		VGridView& Paint(Painter p)              { m_paint = std::move(p); return *this; }
		VGridView& TileSize(float w, float h)    { m_tw = w; m_th = h; return *this; }
		VGridView& Gap(float g)                  { m_gap = g; return *this; }
		VGridView& Multi(bool m)                 { m_s.multi = m; return *this; }
		VGridView& Captions(bool on)             { m_captions = on; ClampScroll(); return *this; }
		VGridView& Accent(D2D1_COLOR_F c)        { m_accent = c; return *this; }
		int  Size() const                        { return Count(); }
		const Tile& At(int i) const              { return m_tiles[(size_t)i]; }
		bool IsSelected(int i) const             { return m_s.Is(i); }
		std::vector<int> Selected() const        { return m_s.Indices(); }
		int  SelectedCount() const               { return m_s.Count(); }
		void Select(int i)                       { if (i >= 0 && i < Count()) Click(i, false, false); }
		void SelectAll(bool on)                  { m_s.All(on); if (m_cb) m_cb(); }
		void Clear()                             { m_s.All(false); m_s.focus = m_s.anchor = -1; if (m_cb) m_cb(); }
		void OnSelection(std::function<void()> cb)   { m_cb = std::move(cb); }
		void OnActivate(std::function<void(int)> cb) { m_act = std::move(cb); }

		VInputResult OnMouseMove(float x, float y) override {
			int h = TileAt(x, y); if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override { m_hover = -1; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			int i = TileAt(x, y);
			if (btn != 1) return VInputResult::NotHandled;
			if (i < 0) { if (HitTest(x, y) && !VSelection::Key(VK_CONTROL)) Clear(); return VInputResult::Handled; }
			Click(i, VSelection::Key(VK_CONTROL), VSelection::Key(VK_SHIFT));
			return VInputResult::Handled;
		}
		VInputResult OnMouseWheel(float delta, float, float) override {
			if (MaxScroll() <= 0.0f) return VInputResult::NotHandled;
			m_scroll -= (delta > 0 ? 1.0f : -1.0f) * 90.0f; ClampScroll();
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (Count() == 0) return VInputResult::NotHandled;
			int last = Count() - 1, f = (std::max)(0, m_s.focus), cols = Cols();
			bool sh = VSelection::Key(VK_SHIFT);
			switch (vk) {
				case VK_RIGHT: Click((std::min)(last, f + 1), false, sh); break;
				case VK_LEFT:  Click((std::max)(0, f - 1), false, sh); break;
				case VK_DOWN:  Click((std::min)(last, f + cols), false, sh); break;
				case VK_UP:    Click((std::max)(0, f - cols), false, sh); break;
				case VK_HOME:  Click(0, false, sh); break;
				case VK_END:   Click(last, false, sh); break;
				case VK_SPACE: if (m_s.focus >= 0) Click(m_s.focus, true, false); break;
				case VK_RETURN: if (m_s.focus >= 0 && m_act) m_act(m_s.focus); break;
				case 'A':      if (VSelection::Key(VK_CONTROL) && m_s.multi) SelectAll(true); else return VInputResult::NotHandled; break;
				default: return VInputResult::NotHandled;
			}
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			rt->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			for (int i = 0; i < Count(); ++i) {
				D2D1_RECT_F r = TileRect(i);
				if (r.bottom < m_bounds.top || r.top > m_bounds.bottom) continue;
				bool sel = m_s.Is(i), hov = i == m_hover;
				if (hov && !sel) vd::Shadow(rt, r, 8.0f, 0.12f, 3);
				vd::Fill(rt, r, vd::Col(0xFFFFFF), 8.0f);
				D2D1_RECT_F pic = D2D1::RectF(r.left, r.top, r.right, r.top + m_th);
				if (m_paint) {
					rt->PushAxisAlignedClip(vd::Inset(pic, 1.0f, 1.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
					m_paint(rt, pic, i);
					rt->PopAxisAlignedClip();
				}
				if (m_captions) {
					const Tile& t = m_tiles[(size_t)i];
					vd::Text(rt, t.title,   D2D1::RectF(r.left + 10.0f, pic.bottom + 6.0f,  r.right - 10.0f, pic.bottom + 24.0f), vctl::Ink(),   vd::Style().Size(12).Bold());
					vd::Text(rt, t.caption, D2D1::RectF(r.left + 10.0f, pic.bottom + 24.0f, r.right - 10.0f, pic.bottom + 40.0f), vctl::Muted(), vd::Style().Size(11));
				}
				vd::Stroke(rt, r, sel ? m_accent : vd::Col(0xE5E7EB), 8.0f, sel ? 2.0f : 1.0f);
				if (m_focused && i == m_s.focus && !sel) vd::Stroke(rt, r, vd::Alpha(m_accent, 0.5f), 8.0f, 1.0f);
				if (sel) {
					vd::Circle(rt, r.right - 16.0f, r.top + 16.0f, 11.0f, m_accent);
					vd::Text(rt, L"\xE73E", vd::Rect(r.right - 27.0f, r.top + 5.0f, 22.0f, 22.0f), vd::Col(0xFFFFFF), vd::Style().Icon().Size(11).Center());
				}
			}
			float ms = MaxScroll();
			if (ms > 0.0f) {
				float H = vd::H(m_bounds) - 8.0f, th = (std::max)(24.0f, H * H / (ms + vd::H(m_bounds)));
				float ty = m_bounds.top + 4.0f + (H - th) * (m_scroll / ms);
				vd::Fill(rt, vd::Rect(m_bounds.right - 7.0f, ty, 3.0f, th), vd::Col(0x000000, 0.22f), 1.5f);
			}
			rt->PopAxisAlignedClip();
		}
	};

	// -------------------------------------------------------------------------
	class VFlipView : public VirtualWidgetImpl {
	public:
		using Painter = std::function<void(ID2D1RenderTarget*, const D2D1_RECT_F&, int)>;   // paints page i inside the rect
	private:
		int     m_count = 0, m_sel = 0, m_hover = 0;    // hover: -1 previous arrow, +1 next arrow
		float   m_pos = 0.0f;                           // the page currently shown, fractional while sliding
		bool    m_vertical = false;
		Painter m_paint;
		std::function<void(int)> m_cb;
		static constexpr float kArrow = 36.0f;
		D2D1_RECT_F ArrowRect(int dir) const {
			if (m_vertical) return vd::Rect(vd::CX(m_bounds) - kArrow * 0.5f, dir < 0 ? m_bounds.top + 6.0f : m_bounds.bottom - kArrow - 6.0f, kArrow, kArrow);
			return vd::Rect(dir < 0 ? m_bounds.left + 6.0f : m_bounds.right - kArrow - 6.0f, vd::CY(m_bounds) - kArrow * 0.5f, kArrow, kArrow);
		}
		bool CanGo(int dir) const { return dir < 0 ? m_sel > 0 : m_sel < m_count - 1; }
		void Go(int i) { i = (std::max)(0, (std::min)(m_count - 1, i)); if (i == m_sel) return; m_sel = i; if (m_cb) m_cb(i); }
	public:
		const char* GetTypeName() const override { return "VFlipView"; }
		bool CanFocus() const override { return true; }
		VFlipView& Count(int n)        { m_count = n; m_sel = (std::min)(m_sel, (std::max)(0, n - 1)); m_pos = (float)m_sel; return *this; }
		VFlipView& Paint(Painter p)    { m_paint = std::move(p); return *this; }
		VFlipView& Vertical(bool v)    { m_vertical = v; return *this; }
		VFlipView& Select(int i)       { Go(i); return *this; }                 // slides there; Count() sets the start page
		int  Value() const { return m_sel; }
		int  Size() const  { return m_count; }
		void Next()        { Go(m_sel + 1); }
		void Previous()    { Go(m_sel - 1); }
		void OnChange(std::function<void(int)> cb) { m_cb = std::move(cb); }

		bool OnUpdate(float dt) override {
			if (fabsf(m_pos - (float)m_sel) < 0.002f) { m_pos = (float)m_sel; return false; }
			m_pos = vd::Approach(m_pos, (float)m_sel, dt, 14.0f);
			return true;
		}
		VInputResult OnMouseEnter() override { m_hovered = true;  return VInputResult::Handled; }
		VInputResult OnMouseLeave() override { m_hovered = false; m_hover = 0; return VInputResult::Handled; }
		VInputResult OnMouseMove(float x, float y) override {
			int h = vd::Contains(ArrowRect(-1), x, y) ? -1 : (vd::Contains(ArrowRect(1), x, y) ? 1 : 0);
			if (h == m_hover) return VInputResult::NotHandled;
			m_hover = h; return VInputResult::Handled;
		}
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			if (vd::Contains(ArrowRect(-1), x, y)) Previous(); else if (vd::Contains(ArrowRect(1), x, y)) Next();
			return VInputResult::Handled;
		}
		VInputResult OnMouseWheel(float delta, float, float) override { if (delta > 0) Previous(); else Next(); return VInputResult::Handled; }
		VInputResult OnKeyDown(UINT vk) override {
			bool prev = m_vertical ? vk == VK_UP : vk == VK_LEFT, next = m_vertical ? vk == VK_DOWN : vk == VK_RIGHT;
			if (prev) Previous(); else if (next) Next(); else if (vk == VK_HOME) Go(0); else if (vk == VK_END) Go(m_count - 1); else return VInputResult::NotHandled;
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			vd::Fill(rt, m_bounds, vd::Col(0x111827), 8.0f);
			rt->PushAxisAlignedClip(m_bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			if (m_paint && m_count > 0) {
				float W = vd::W(m_bounds), H = vd::H(m_bounds);
				int first = (int)floorf(m_pos), last = (int)ceilf(m_pos);
				for (int i = first; i <= last && i < m_count; ++i) {
					float off = ((float)i - m_pos) * (m_vertical ? H : W);
					D2D1_RECT_F r = m_vertical ? vd::Rect(m_bounds.left, m_bounds.top + off, W, H) : vd::Rect(m_bounds.left + off, m_bounds.top, W, H);
					rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
					m_paint(rt, r, i);
					rt->PopAxisAlignedClip();
				}
			}
			for (int dir = -1; dir <= 1; dir += 2) {
				if (!CanGo(dir) || !(m_hovered || m_focused)) continue;
				D2D1_RECT_F a = ArrowRect(dir);
				vd::Fill(rt, a, vd::Col(0x000000, m_hover == dir ? 0.55f : 0.35f), kArrow * 0.5f);
				vd::Chevron(rt, vd::CX(a), vd::CY(a), m_vertical ? (dir < 0 ? 180.0f : 0.0f) : (dir < 0 ? 90.0f : -90.0f), vd::Col(0xFFFFFF), 5.0f, 1.8f);
			}
			if (m_focused) vd::Stroke(rt, vd::Inset(m_bounds, 1.5f, 1.5f), vd::Alpha(vctl::Blue(), 0.7f), 7.0f, 1.5f);
			rt->PopAxisAlignedClip();
		}
	};

	// -------------------------------------------------------------------------
	class VPipsPager : public VirtualWidgetImpl {
		int  m_count = 0, m_sel = 0, m_hover = -1;
		bool m_arrows = false, m_vertical = false;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(int)> m_cb;
		static constexpr float kPitch = 18.0f;
		float Start() const { float len = kPitch * (float)m_count; return (m_vertical ? vd::CY(m_bounds) : vd::CX(m_bounds)) - len * 0.5f + kPitch * 0.5f; }
		D2D1_POINT_2F Dot(int i) const { float s = Start() + kPitch * (float)i; return m_vertical ? D2D1::Point2F(vd::CX(m_bounds), s) : D2D1::Point2F(s, vd::CY(m_bounds)); }
		D2D1_RECT_F Arrow(int dir) const {
			float s = Start() - kPitch * 0.5f, e = Start() + kPitch * (float)m_count - kPitch * 0.5f;
			float a = dir < 0 ? s - 22.0f : e + 2.0f;
			return m_vertical ? vd::Rect(vd::CX(m_bounds) - 10.0f, a, 20.0f, 20.0f) : vd::Rect(a, vd::CY(m_bounds) - 10.0f, 20.0f, 20.0f);
		}
		int At(float x, float y) const {
			for (int i = 0; i < m_count; ++i) { D2D1_POINT_2F d = Dot(i); if (fabsf(x - d.x) < kPitch * 0.5f && fabsf(y - d.y) < kPitch * 0.5f) return i; }
			if (m_arrows && vd::Contains(Arrow(-1), x, y)) return -2;
			if (m_arrows && vd::Contains(Arrow(1), x, y))  return -3;
			return -1;
		}
		void Go(int i) { i = (std::max)(0, (std::min)(m_count - 1, i)); if (i == m_sel) return; m_sel = i; if (m_cb) m_cb(i); }
	public:
		const char* GetTypeName() const override { return "VPipsPager"; }
		bool CanFocus() const override { return true; }
		VPipsPager& Count(int n)          { m_count = n; m_sel = (std::min)(m_sel, (std::max)(0, n - 1)); return *this; }
		VPipsPager& Select(int i)         { m_sel = (std::max)(0, (std::min)(m_count - 1, i)); return *this; }
		VPipsPager& Arrows(bool on)       { m_arrows = on; return *this; }
		VPipsPager& Vertical(bool v)      { m_vertical = v; return *this; }
		VPipsPager& Accent(D2D1_COLOR_F c){ m_accent = c; return *this; }
		int Value() const { return m_sel; }
		void OnChange(std::function<void(int)> cb) { m_cb = std::move(cb); }
		VInputResult OnMouseMove(float x, float y) override { int h = At(x, y); if (h == m_hover) return VInputResult::NotHandled; m_hover = h; return VInputResult::Handled; }
		VInputResult OnMouseLeave() override { m_hover = -1; return VInputResult::Handled; }
		VInputResult OnMouseUp(float x, float y, int btn) override {
			int i = At(x, y);
			if (btn != 1 || i == -1) return VInputResult::NotHandled;
			if (i == -2) Go(m_sel - 1); else if (i == -3) Go(m_sel + 1); else Go(i);
			return VInputResult::Handled;
		}
		VInputResult OnKeyDown(UINT vk) override {
			if (vk == VK_LEFT || vk == VK_UP) Go(m_sel - 1); else if (vk == VK_RIGHT || vk == VK_DOWN) Go(m_sel + 1); else return VInputResult::NotHandled;
			return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			for (int i = 0; i < m_count; ++i) {
				D2D1_POINT_2F d = Dot(i);
				bool on = i == m_sel;
				vd::Circle(rt, d.x, d.y, on ? 4.5f : (i == m_hover ? 3.5f : 2.5f), on ? m_accent : (i == m_hover ? vctl::Muted() : vd::Col(0xC4C9D2)));
			}
			if (m_arrows) for (int dir = -1; dir <= 1; dir += 2) {
				D2D1_RECT_F a = Arrow(dir);
				bool can = dir < 0 ? m_sel > 0 : m_sel < m_count - 1, lit = m_hover == (dir < 0 ? -2 : -3);
				if (lit && can) vd::Fill(rt, a, vd::Col(0x000000, 0.06f), 10.0f);
				vd::Chevron(rt, vd::CX(a), vd::CY(a), m_vertical ? (dir < 0 ? 180.0f : 0.0f) : (dir < 0 ? 90.0f : -90.0f), can ? vctl::Ink() : vd::Col(0xD1D5DB), 4.0f);
			}
			if (m_focused) { D2D1_POINT_2F d = Dot(m_sel); vd::Ring(rt, d.x, d.y, 8.0f, vd::Alpha(m_accent, 0.5f), 1.0f); }
		}
	};

	// -------------------------------------------------------------------------
	// VAnnotatedScrollBar — Value() is 0..1 of the way through the content;
	// Labels mark positions along the rail ("2024", "March", "M"); while the
	// thumb is dragged a tooltip names the nearest label. Wire OnChange to the
	// scrolling widget and call Set() from its scroll so both stay in step.
	// -------------------------------------------------------------------------
	class VAnnotatedScrollBar : public VirtualWidgetImpl {
	public:
		struct Label { float at; std::wstring text; };   // at: 0..1
	private:
		std::vector<Label> m_labels;
		float m_v = 0.0f, m_thumb = 0.2f;                // thumb: viewport / content, 0..1
		bool  m_drag = false, m_hover = false;
		float m_dragOff = 0.0f;
		D2D1_COLOR_F m_accent = vctl::Blue();
		std::function<void(float)> m_cb;
		static constexpr float kRail = 6.0f, kPad = 4.0f;
		float RailX() const { return m_bounds.right - 14.0f; }
		float RailH() const { return vd::H(m_bounds) - kPad * 2.0f; }
		float ThumbH() const { return (std::max)(24.0f, RailH() * m_thumb); }
		D2D1_RECT_F Thumb() const { return vd::Rect(RailX() - kRail * 0.5f, m_bounds.top + kPad + (RailH() - ThumbH()) * m_v, kRail, ThumbH()); }
		float YOf(float at) const { return m_bounds.top + kPad + RailH() * at; }
		const Label* Nearest(float v) const {
			const Label* best = nullptr; float bd = 1e9f;
			for (const Label& l : m_labels) { float d = fabsf(l.at - v); if (d < bd) { bd = d; best = &l; } }
			return best;
		}
		void SetFromY(float y) { float t = RailH() - ThumbH(); float v = t > 0.0f ? vd::Clamp01((y - m_dragOff - m_bounds.top - kPad) / t) : 0.0f; if (v != m_v) { m_v = v; if (m_cb) m_cb(v); } }
	public:
		const char* GetTypeName() const override { return "VAnnotatedScrollBar"; }
		bool CanFocus() const override { return true; }
		VAnnotatedScrollBar& Labels(std::vector<Label> v) { m_labels = std::move(v); return *this; }
		VAnnotatedScrollBar& Set(float v)                { m_v = vd::Clamp01(v); return *this; }
		VAnnotatedScrollBar& ThumbSize(float f)          { m_thumb = vd::Clamp(f, 0.02f, 1.0f); return *this; }   // viewport / content
		VAnnotatedScrollBar& Accent(D2D1_COLOR_F c)      { m_accent = c; return *this; }
		float Value() const { return m_v; }
		void OnChange(std::function<void(float)> cb) { m_cb = std::move(cb); }

		VInputResult OnMouseEnter() override { m_hover = true;  return VInputResult::Handled; }
		VInputResult OnMouseLeave() override { m_hover = false; return VInputResult::Handled; }
		VInputResult OnMouseDown(float x, float y, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			D2D1_RECT_F t = Thumb();
			m_dragOff = (y >= t.top && y <= t.bottom) ? y - t.top : ThumbH() * 0.5f;
			m_drag = true; SetFromY(y); (void)x;
			return VInputResult::Capture;
		}
		VInputResult OnMouseMove(float, float y) override { if (!m_drag) return VInputResult::NotHandled; SetFromY(y); return VInputResult::Handled; }
		VInputResult OnMouseUp(float, float, int) override { m_drag = false; return VInputResult::Handled; }
		VInputResult OnMouseWheel(float delta, float, float) override { m_v = vd::Clamp01(m_v - (delta / 120.0f) * m_thumb * 0.5f); if (m_cb) m_cb(m_v); return VInputResult::Handled; }
		VInputResult OnKeyDown(UINT vk) override {
			float step = m_thumb * 0.5f;
			switch (vk) {
				case VK_UP:    m_v = vd::Clamp01(m_v - step * 0.25f); break;   case VK_DOWN:  m_v = vd::Clamp01(m_v + step * 0.25f); break;
				case VK_PRIOR: m_v = vd::Clamp01(m_v - step); break;           case VK_NEXT:  m_v = vd::Clamp01(m_v + step); break;
				case VK_HOME:  m_v = 0.0f; break;                              case VK_END:   m_v = 1.0f; break;
				default: return VInputResult::NotHandled;
			}
			if (m_cb) m_cb(m_v); return VInputResult::Handled;
		}
		void OnDraw(ID2D1RenderTarget* rt) override {
			float rx = RailX();
			vd::Fill(rt, vd::Rect(rx - kRail * 0.5f, m_bounds.top + kPad, kRail, RailH()), vd::Col(0xE5E7EB), kRail * 0.5f);
			for (const Label& l : m_labels) {
				float y = YOf(l.at);
				vd::Line(rt, rx - 10.0f, y, rx - 5.0f, y, vd::Col(0x9CA3AF), 1.0f);
				vd::Text(rt, l.text, D2D1::RectF(m_bounds.left, y - 9.0f, rx - 14.0f, y + 9.0f), vctl::Muted(), vd::Style().Size(11).Right());
			}
			D2D1_RECT_F t = Thumb();
			vd::Fill(rt, t, (m_drag || m_hover || m_focused) ? m_accent : vd::Col(0x9CA3AF), kRail * 0.5f);
			if (m_drag) {
				const Label* l = Nearest(m_v + m_thumb * 0.5f);
				std::wstring s = l ? l->text : vd::Num(m_v * 100.0) + L"%";
				float w = vd::TextWidth(s, vd::Style().Size(12).Bold()) + 20.0f;
				D2D1_RECT_F pill = vd::Rect(rx - 18.0f - w, vd::CY(t) - 13.0f, w, 26.0f);
				vd::Fill(rt, pill, vd::Col(0x1F2937, 0.95f), 6.0f);
				vd::Text(rt, s, pill, vd::Col(0xFFFFFF), vd::Style().Size(12).Bold().Center());
			}
		}
	};

} // namespace ChronoUI

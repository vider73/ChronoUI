// =============================================================================
// chrono_dwrite.hpp — DirectWrite, the part ChronoUI uses, on Pango.
//
// IDWriteTextFormat is a font description plus paragraph settings; an
// IDWriteTextLayout adds the string, the box and per-range formatting, and
// owns a PangoLayout built lazily from all of it. The two models line up
// closely; the differences this file absorbs:
//
//   * Positions. DirectWrite counts characters of the caller's string, Pango
//     counts UTF-8 bytes. Every layout keeps the byte offset of each character
//     so hit-tests and ranges convert both ways.
//   * The box. DirectWrite aligns text inside maxWidth x maxHeight even when it
//     does not wrap; Pango only aligns inside a wrap width. Unwrapped text is
//     laid out free and shifted by (xoff, yoff) to where DirectWrite puts it;
//     vertical (paragraph) alignment is always that shift.
//   * Fonts. Windows family names map to free ones (Segoe UI -> Noto Sans,
//     Consolas -> JetBrains Mono / Noto Sans Mono), and Segoe MDL2 Assets to
//     ChronoUI Icons, the same code points cut from Fluent UI System Icons.
// =============================================================================

#pragma once

#include "chrono_win32.hpp"
#include <algorithm>
#include <cmath>
#include <fontconfig/fontconfig.h>
#include <pango/pangofc-fontmap.h>

#define E_NOT_SUFFICIENT_BUFFER ((HRESULT)0x8007007A)

enum DWRITE_FACTORY_TYPE { DWRITE_FACTORY_TYPE_SHARED = 0, DWRITE_FACTORY_TYPE_ISOLATED = 1 };
enum DWRITE_FONT_WEIGHT {
	DWRITE_FONT_WEIGHT_THIN = 100, DWRITE_FONT_WEIGHT_EXTRA_LIGHT = 200, DWRITE_FONT_WEIGHT_ULTRA_LIGHT = 200,
	DWRITE_FONT_WEIGHT_LIGHT = 300, DWRITE_FONT_WEIGHT_SEMI_LIGHT = 350, DWRITE_FONT_WEIGHT_NORMAL = 400,
	DWRITE_FONT_WEIGHT_REGULAR = 400, DWRITE_FONT_WEIGHT_MEDIUM = 500, DWRITE_FONT_WEIGHT_DEMI_BOLD = 600,
	DWRITE_FONT_WEIGHT_SEMI_BOLD = 600, DWRITE_FONT_WEIGHT_BOLD = 700, DWRITE_FONT_WEIGHT_EXTRA_BOLD = 800,
	DWRITE_FONT_WEIGHT_ULTRA_BOLD = 800, DWRITE_FONT_WEIGHT_BLACK = 900, DWRITE_FONT_WEIGHT_HEAVY = 900,
	DWRITE_FONT_WEIGHT_EXTRA_BLACK = 950, DWRITE_FONT_WEIGHT_ULTRA_BLACK = 950
};
enum DWRITE_FONT_STYLE   { DWRITE_FONT_STYLE_NORMAL = 0, DWRITE_FONT_STYLE_OBLIQUE = 1, DWRITE_FONT_STYLE_ITALIC = 2 };
enum DWRITE_FONT_STRETCH {
	DWRITE_FONT_STRETCH_UNDEFINED = 0, DWRITE_FONT_STRETCH_ULTRA_CONDENSED = 1, DWRITE_FONT_STRETCH_EXTRA_CONDENSED = 2,
	DWRITE_FONT_STRETCH_CONDENSED = 3, DWRITE_FONT_STRETCH_SEMI_CONDENSED = 4, DWRITE_FONT_STRETCH_NORMAL = 5,
	DWRITE_FONT_STRETCH_MEDIUM = 5, DWRITE_FONT_STRETCH_SEMI_EXPANDED = 6, DWRITE_FONT_STRETCH_EXPANDED = 7,
	DWRITE_FONT_STRETCH_EXTRA_EXPANDED = 8, DWRITE_FONT_STRETCH_ULTRA_EXPANDED = 9
};
enum DWRITE_TEXT_ALIGNMENT      { DWRITE_TEXT_ALIGNMENT_LEADING = 0, DWRITE_TEXT_ALIGNMENT_TRAILING = 1,
                                  DWRITE_TEXT_ALIGNMENT_CENTER = 2, DWRITE_TEXT_ALIGNMENT_JUSTIFIED = 3 };
enum DWRITE_PARAGRAPH_ALIGNMENT { DWRITE_PARAGRAPH_ALIGNMENT_NEAR = 0, DWRITE_PARAGRAPH_ALIGNMENT_FAR = 1,
                                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER = 2 };
enum DWRITE_WORD_WRAPPING       { DWRITE_WORD_WRAPPING_WRAP = 0, DWRITE_WORD_WRAPPING_NO_WRAP = 1,
                                  DWRITE_WORD_WRAPPING_EMERGENCY_BREAK = 2, DWRITE_WORD_WRAPPING_WHOLE_WORD = 3,
                                  DWRITE_WORD_WRAPPING_CHARACTER = 4 };
enum DWRITE_READING_DIRECTION   { DWRITE_READING_DIRECTION_LEFT_TO_RIGHT = 0, DWRITE_READING_DIRECTION_RIGHT_TO_LEFT = 1 };
enum DWRITE_FLOW_DIRECTION      { DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM = 0 };
enum DWRITE_TRIMMING_GRANULARITY { DWRITE_TRIMMING_GRANULARITY_NONE = 0, DWRITE_TRIMMING_GRANULARITY_CHARACTER = 1,
                                   DWRITE_TRIMMING_GRANULARITY_WORD = 2 };
enum DWRITE_LINE_SPACING_METHOD { DWRITE_LINE_SPACING_METHOD_DEFAULT = 0, DWRITE_LINE_SPACING_METHOD_UNIFORM = 1,
                                  DWRITE_LINE_SPACING_METHOD_PROPORTIONAL = 2 };
enum DWRITE_MEASURING_MODE      { DWRITE_MEASURING_MODE_NATURAL = 0, DWRITE_MEASURING_MODE_GDI_CLASSIC = 1,
                                  DWRITE_MEASURING_MODE_GDI_NATURAL = 2 };

struct DWRITE_TEXT_RANGE { UINT32 startPosition, length; };
struct DWRITE_TRIMMING   { DWRITE_TRIMMING_GRANULARITY granularity; UINT32 delimiter, delimiterCount; };
struct DWRITE_TEXT_METRICS {
	FLOAT left, top, width, widthIncludingTrailingWhitespace, height, layoutWidth, layoutHeight;
	UINT32 maxBidiReorderingDepth, lineCount;
};
struct DWRITE_LINE_METRICS {
	UINT32 length, trailingWhitespaceLength, newlineLength;
	FLOAT height, baseline; BOOL isTrimmed;
};
struct DWRITE_HIT_TEST_METRICS {
	UINT32 textPosition, length;
	FLOAT left, top, width, height;
	UINT32 bidiLevel; BOOL isText, isTrimmed;
};
struct DWRITE_OVERHANG_METRICS { FLOAT left, top, right, bottom; };

struct IDWriteInlineObject : IUnknown {};
struct IDWriteFontCollection : IUnknown {
	HRESULT FindFamilyName(const WCHAR*, UINT32* index, BOOL* exists) { if (index) *index = 0; if (exists) *exists = TRUE; return S_OK; }
	UINT32  GetFontFamilyCount() { return 1; }
};

namespace chrono_linux {

	// The fonts the app ships (<exe dir>/assets/fonts/**.ttf), visible to
	// fontconfig for this process only: the icon fonts are not installed system-wide.
	inline void RegisterAppFonts() {
		static bool done = false;
		if (done) return;
		done = true;
		char exe[4096]; ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
		if (n <= 0) return;
		std::string dir(exe, (size_t)n);
		dir = dir.substr(0, dir.find_last_of('/') + 1) + "assets/fonts";
		FcConfig* cfg = FcConfigGetCurrent();
		if (FcConfigAppFontAddDir(cfg, (const FcChar8*)dir.c_str()))
			pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(pango_cairo_font_map_get_default()));
	}

	inline PangoContext* Pango() {
		static PangoContext* ctx = [] {
			RegisterAppFonts();
			PangoContext* c = pango_font_map_create_context(pango_cairo_font_map_get_default());
			cairo_font_options_t* o = cairo_font_options_create();
			cairo_font_options_set_antialias(o, CAIRO_ANTIALIAS_GRAY);
			cairo_font_options_set_hint_style(o, CAIRO_HINT_STYLE_SLIGHT);
			cairo_font_options_set_hint_metrics(o, CAIRO_HINT_METRICS_OFF);   // positions independent of the transform, like DWrite
			pango_cairo_context_set_font_options(c, o);
			cairo_font_options_destroy(o);
			pango_context_set_round_glyph_positions(c, FALSE);
			return c;
		}();
		return ctx;
	}

	inline bool IsIconFamily(const std::wstring& f) {
		return f == L"Segoe MDL2 Assets" || f == L"Segoe Fluent Icons";
	}
	inline std::string MapFamily(const std::wstring& f) {
		// assets/fonts/ChronoUIIcons.ttf answers the MDL2 code points (tools/make_icon_font.py).
		if (IsIconFamily(f)) return "ChronoUI Icons";
		if (f.empty() || f.rfind(L"Segoe UI", 0) == 0 || f == L"Arial" || f == L"Tahoma" || f == L"Calibri")
			return f == L"Segoe UI Emoji" ? "Noto Color Emoji" : "Noto Sans";
		if (f == L"Consolas" || f.rfind(L"Cascadia", 0) == 0 || f == L"Courier New" || f == L"Lucida Console")
			return "JetBrains Mono,Noto Sans Mono,DejaVu Sans Mono,monospace";
		if (f == L"Georgia" || f == L"Times New Roman" || f == L"Cambria") return "Noto Serif,DejaVu Serif,serif";
		return ToUtf8(f);
	}
	inline PangoWeight  ToPango(DWRITE_FONT_WEIGHT w)  { return (PangoWeight)(int)w; }
	inline PangoStyle   ToPango(DWRITE_FONT_STYLE s)   { return s == DWRITE_FONT_STYLE_ITALIC ? PANGO_STYLE_ITALIC : s == DWRITE_FONT_STYLE_OBLIQUE ? PANGO_STYLE_OBLIQUE : PANGO_STYLE_NORMAL; }
	inline PangoStretch ToPango(DWRITE_FONT_STRETCH s) { int k = (int)s - 1; return (PangoStretch)(k < 0 ? 4 : k > 8 ? 8 : k); }
	constexpr double kPS = (double)PANGO_SCALE;
}

// =============================================================================
struct IDWriteTextFormat : IUnknown {
	std::wstring family, locale;
	DWRITE_FONT_WEIGHT   weight  = DWRITE_FONT_WEIGHT_NORMAL;
	DWRITE_FONT_STYLE    style   = DWRITE_FONT_STYLE_NORMAL;
	DWRITE_FONT_STRETCH  stretch = DWRITE_FONT_STRETCH_NORMAL;
	FLOAT                size    = 14.0f;
	DWRITE_TEXT_ALIGNMENT      textAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
	DWRITE_PARAGRAPH_ALIGNMENT paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
	DWRITE_WORD_WRAPPING       wrapping  = DWRITE_WORD_WRAPPING_WRAP;
	DWRITE_TRIMMING            trimming  = { DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0 };
	DWRITE_LINE_SPACING_METHOD spacingMethod = DWRITE_LINE_SPACING_METHOD_DEFAULT;
	FLOAT                      lineSpacing = 0.0f, baseline = 0.0f;
	DWRITE_READING_DIRECTION   readingDir = DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;

	virtual void Changed() {}

	// Field-by-field: IUnknown's refcount must not travel with the settings.
	void CopySettings(const IDWriteTextFormat& o) {
		family = o.family; locale = o.locale; weight = o.weight; style = o.style; stretch = o.stretch; size = o.size;
		textAlign = o.textAlign; paraAlign = o.paraAlign; wrapping = o.wrapping; trimming = o.trimming;
		spacingMethod = o.spacingMethod; lineSpacing = o.lineSpacing; baseline = o.baseline; readingDir = o.readingDir;
	}

	HRESULT SetTextAlignment(DWRITE_TEXT_ALIGNMENT a)           { textAlign = a; Changed(); return S_OK; }
	HRESULT SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT a) { paraAlign = a; Changed(); return S_OK; }
	HRESULT SetWordWrapping(DWRITE_WORD_WRAPPING w)             { wrapping = w; Changed(); return S_OK; }
	HRESULT SetReadingDirection(DWRITE_READING_DIRECTION d)     { readingDir = d; Changed(); return S_OK; }
	HRESULT SetFlowDirection(DWRITE_FLOW_DIRECTION)             { return S_OK; }
	HRESULT SetIncrementalTabStop(FLOAT)                        { return S_OK; }
	HRESULT SetTrimming(const DWRITE_TRIMMING* t, IDWriteInlineObject*) { if (t) trimming = *t; Changed(); return S_OK; }
	HRESULT SetLineSpacing(DWRITE_LINE_SPACING_METHOD m, FLOAT spacing, FLOAT base) {
		spacingMethod = m; lineSpacing = spacing; baseline = base; Changed(); return S_OK;
	}
	DWRITE_TEXT_ALIGNMENT      GetTextAlignment() const      { return textAlign; }
	DWRITE_PARAGRAPH_ALIGNMENT GetParagraphAlignment() const { return paraAlign; }
	DWRITE_WORD_WRAPPING       GetWordWrapping() const       { return wrapping; }
	FLOAT                      GetFontSize() const           { return size; }
	DWRITE_FONT_WEIGHT         GetFontWeight() const         { return weight; }
	DWRITE_FONT_STYLE          GetFontStyle() const          { return style; }
	DWRITE_FONT_STRETCH        GetFontStretch() const        { return stretch; }
	UINT32                     GetFontFamilyNameLength() const { return (UINT32)family.size(); }
	HRESULT GetFontFamilyName(WCHAR* buf, UINT32 n) const {
		if (!buf || n <= family.size()) return E_NOT_SUFFICIENT_BUFFER;
		wmemcpy(buf, family.c_str(), family.size() + 1); return S_OK;
	}
	HRESULT GetFontCollection(IDWriteFontCollection** c) const { if (c) *c = nullptr; return S_OK; }
};

// =============================================================================
struct IDWriteTextLayout : IDWriteTextFormat {
	std::wstring        text;
	std::string         utf8;
	std::vector<int>    offs;          // offs[i] = byte index of character i; offs[n] = utf8.size()
	FLOAT               maxW = 0.0f, maxH = 0.0f;

	struct Range { enum Kind { Size, Weight, Style, Stretch, Family, Underline, Strike, Effect } kind;
	               UINT32 start, len; float f; int i; std::wstring s; IUnknown* effect; };
	std::vector<Range>  ranges;

	PangoLayout*        lay = nullptr;
	double              xoff = 0.0, yoff = 0.0;   // where the Pango layout sits inside the DWrite box

	~IDWriteTextLayout() override {
		if (lay) g_object_unref(lay);
		for (auto& r : ranges) if (r.effect) r.effect->Release();
	}
	void Changed() override { if (lay) { g_object_unref(lay); lay = nullptr; } }

	void SetText(const WCHAR* s, UINT32 n) {
		text.assign(s ? s : L"", s ? n : 0);
		utf8.clear(); offs.assign(text.size() + 1, 0);
		for (size_t i = 0; i < text.size(); ++i) { offs[i] = (int)utf8.size(); utf8 += chrono_linux::ToUtf8(&text[i], 1); }
		offs[text.size()] = (int)utf8.size();
	}
	int ByteOf(UINT32 pos) const { return offs[(std::min)((size_t)pos, offs.size() - 1)]; }
	UINT32 CharOf(int byte) const {
		auto it = std::upper_bound(offs.begin(), offs.end(), byte);
		return (UINT32)((it - offs.begin()) - 1);
	}

	PangoLayout* L() {
		using namespace chrono_linux;
		if (lay) return lay;
		lay = pango_layout_new(Pango());
		pango_layout_set_text(lay, utf8.c_str(), (int)utf8.size());

		PangoFontDescription* fd = pango_font_description_new();
		pango_font_description_set_family(fd, MapFamily(family).c_str());
		pango_font_description_set_weight(fd, ToPango(weight));
		pango_font_description_set_style(fd, ToPango(style));
		pango_font_description_set_stretch(fd, ToPango(stretch));
		pango_font_description_set_absolute_size(fd, (double)size * kPS);
		pango_layout_set_font_description(lay, fd);
		pango_font_description_free(fd);

		PangoAttrList* attrs = pango_attr_list_new();
		auto add = [&](PangoAttribute* a, UINT32 start, UINT32 len) {
			a->start_index = (guint)ByteOf(start);
			a->end_index = (guint)ByteOf(len > text.size() ? (UINT32)text.size() : start + len);
			if (len == 0xFFFFFFFFu || (UINT64)start + len > text.size()) a->end_index = (guint)utf8.size();
			pango_attr_list_insert(attrs, a);
		};
		for (auto& r : ranges) switch (r.kind) {
			case Range::Size:      add(pango_attr_size_new_absolute((int)(r.f * kPS)), r.start, r.len); break;
			case Range::Weight:    add(pango_attr_weight_new((PangoWeight)r.i), r.start, r.len); break;
			case Range::Style:     add(pango_attr_style_new(ToPango((DWRITE_FONT_STYLE)r.i)), r.start, r.len); break;
			case Range::Stretch:   add(pango_attr_stretch_new(ToPango((DWRITE_FONT_STRETCH)r.i)), r.start, r.len); break;
			case Range::Family:    add(pango_attr_family_new(MapFamily(r.s).c_str()), r.start, r.len); break;
			case Range::Underline: add(pango_attr_underline_new(r.i ? PANGO_UNDERLINE_SINGLE : PANGO_UNDERLINE_NONE), r.start, r.len); break;
			case Range::Strike:    add(pango_attr_strikethrough_new(r.i != 0), r.start, r.len); break;
			case Range::Effect:    break;   // colours are applied at draw time
		}
		if (spacingMethod == DWRITE_LINE_SPACING_METHOD_UNIFORM && lineSpacing > 0.0f)
			add(pango_attr_line_height_new_absolute((int)(lineSpacing * kPS)), 0, 0xFFFFFFFFu);
		else if (spacingMethod == DWRITE_LINE_SPACING_METHOD_PROPORTIONAL && lineSpacing > 0.0f)
			add(pango_attr_line_height_new(lineSpacing), 0, 0xFFFFFFFFu);
		pango_layout_set_attributes(lay, attrs);
		pango_attr_list_unref(attrs);

		PangoAlignment al = textAlign == DWRITE_TEXT_ALIGNMENT_TRAILING ? PANGO_ALIGN_RIGHT
		                  : textAlign == DWRITE_TEXT_ALIGNMENT_CENTER   ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT;
		pango_layout_set_alignment(lay, al);
		pango_layout_set_justify(lay, textAlign == DWRITE_TEXT_ALIGNMENT_JUSTIFIED);
		bool wraps = wrapping != DWRITE_WORD_WRAPPING_NO_WRAP;
		bool trims = trimming.granularity != DWRITE_TRIMMING_GRANULARITY_NONE;
		bool boxed = (wraps || trims) && maxW > 0.0f && maxW < 1e6f;
		if (boxed) {
			pango_layout_set_width(lay, (int)(maxW * kPS));
			pango_layout_set_wrap(lay, wrapping == DWRITE_WORD_WRAPPING_CHARACTER ? PANGO_WRAP_CHAR
			                         : wrapping == DWRITE_WORD_WRAPPING_WHOLE_WORD ? PANGO_WRAP_WORD : PANGO_WRAP_WORD_CHAR);
			if (trims) {
				pango_layout_set_ellipsize(lay, PANGO_ELLIPSIZE_END);
				pango_layout_set_height(lay, wraps && maxH > 0.0f && maxH < 1e6f ? (int)(maxH * kPS) : -1);
			}
		} else {
			pango_layout_set_width(lay, -1);
		}

		PangoRectangle ink, log;
		pango_layout_get_extents(lay, &ink, &log);
		double w = log.width / kPS, h = log.height / kPS;
		double k = textAlign == DWRITE_TEXT_ALIGNMENT_CENTER ? 0.5 : textAlign == DWRITE_TEXT_ALIGNMENT_TRAILING ? 1.0 : 0.0;
		xoff = boxed ? 0.0 : (maxW < 1e6f ? (maxW - w) * k : 0.0);
		double v = paraAlign == DWRITE_PARAGRAPH_ALIGNMENT_CENTER ? 0.5 : paraAlign == DWRITE_PARAGRAPH_ALIGNMENT_FAR ? 1.0 : 0.0;
		yoff = maxH < 1e6f ? (maxH - h) * v : 0.0;
		return lay;
	}

	// --- per-range formatting ---
	HRESULT AddRange(Range r) { ranges.push_back(std::move(r)); Changed(); return S_OK; }
	HRESULT SetFontSize(FLOAT s, DWRITE_TEXT_RANGE r)            { return AddRange({ Range::Size, r.startPosition, r.length, s, 0, {}, nullptr }); }
	HRESULT SetFontWeight(DWRITE_FONT_WEIGHT w, DWRITE_TEXT_RANGE r) { return AddRange({ Range::Weight, r.startPosition, r.length, 0, (int)w, {}, nullptr }); }
	HRESULT SetFontStyle(DWRITE_FONT_STYLE s, DWRITE_TEXT_RANGE r)   { return AddRange({ Range::Style, r.startPosition, r.length, 0, (int)s, {}, nullptr }); }
	HRESULT SetFontStretch(DWRITE_FONT_STRETCH s, DWRITE_TEXT_RANGE r) { return AddRange({ Range::Stretch, r.startPosition, r.length, 0, (int)s, {}, nullptr }); }
	HRESULT SetFontFamilyName(const WCHAR* f, DWRITE_TEXT_RANGE r)   { return AddRange({ Range::Family, r.startPosition, r.length, 0, 0, f ? f : L"", nullptr }); }
	HRESULT SetUnderline(BOOL on, DWRITE_TEXT_RANGE r)              { return AddRange({ Range::Underline, r.startPosition, r.length, 0, on ? 1 : 0, {}, nullptr }); }
	HRESULT SetStrikethrough(BOOL on, DWRITE_TEXT_RANGE r)          { return AddRange({ Range::Strike, r.startPosition, r.length, 0, on ? 1 : 0, {}, nullptr }); }
	HRESULT SetDrawingEffect(IUnknown* e, DWRITE_TEXT_RANGE r) {
		if (e) e->AddRef();
		ranges.push_back({ Range::Effect, r.startPosition, r.length, 0, 0, {}, e });
		return S_OK;   // no relayout: colour only
	}
	HRESULT SetLocaleName(const WCHAR*, DWRITE_TEXT_RANGE) { return S_OK; }
	HRESULT SetTypography(IUnknown*, DWRITE_TEXT_RANGE)    { return S_OK; }
	HRESULT SetMaxWidth(FLOAT w)  { maxW = w; Changed(); return S_OK; }
	HRESULT SetMaxHeight(FLOAT h) { maxH = h; Changed(); return S_OK; }
	FLOAT   GetMaxWidth() const   { return maxW; }
	FLOAT   GetMaxHeight() const  { return maxH; }

	// --- measuring ---
	HRESULT GetMetrics(DWRITE_TEXT_METRICS* m) {
		using chrono_linux::kPS;
		if (!m) return E_POINTER;
		PangoLayout* l = L();
		PangoRectangle ink, log;
		pango_layout_get_extents(l, &ink, &log);
		// Width without trailing whitespace: the widest line measured up to its last visible character.
		double tight = 0.0;
		for (GSList* it = pango_layout_get_lines_readonly(l); it; it = it->next) {
			auto* line = (PangoLayoutLine*)it->data;
			int start = line->start_index, end = line->start_index + line->length;
			while (end > start && strchr(" \t\r\n", utf8[(size_t)end - 1])) --end;
			if (end <= start) continue;
			int* r = nullptr; int n = 0;
			pango_layout_line_get_x_ranges(line, start, end, &r, &n);
			int lo = INT32_MAX, hi = INT32_MIN;
			for (int i = 0; i < n; ++i) { lo = (std::min)(lo, r[i * 2]); hi = (std::max)(hi, r[i * 2 + 1]); }
			g_free(r);
			if (n) tight = (std::max)(tight, (hi - lo) / kPS);
		}
		m->left = (FLOAT)(xoff + log.x / kPS);
		m->top = (FLOAT)(yoff + log.y / kPS);
		m->width = (FLOAT)tight;
		m->widthIncludingTrailingWhitespace = (FLOAT)(log.width / kPS);
		m->height = (FLOAT)(log.height / kPS);
		m->layoutWidth = maxW; m->layoutHeight = maxH;
		m->maxBidiReorderingDepth = 1;
		m->lineCount = (UINT32)pango_layout_get_line_count(l);
		return S_OK;
	}
	HRESULT GetLineMetrics(DWRITE_LINE_METRICS* out, UINT32 maxCount, UINT32* actual) {
		using chrono_linux::kPS;
		PangoLayout* l = L();
		UINT32 n = (UINT32)pango_layout_get_line_count(l);
		if (actual) *actual = n;
		if (!out || maxCount < n) return E_NOT_SUFFICIENT_BUFFER;
		PangoLayoutIter* it = pango_layout_get_iter(l);
		UINT32 i = 0;
		do {
			PangoLayoutLine* line = pango_layout_iter_get_line_readonly(it);
			PangoRectangle log; pango_layout_iter_get_line_extents(it, nullptr, &log);
			int startB = line->start_index, endB = line->start_index + line->length;
			// Pango leaves the paragraph separator out of the line; DWrite counts it.
			int nl = 0;
			if ((size_t)endB < utf8.size() && utf8[(size_t)endB] == '\r') ++nl;
			if ((size_t)(endB + nl) < utf8.size() && utf8[(size_t)(endB + nl)] == '\n') ++nl;
			int ws = endB; while (ws > startB && (utf8[(size_t)ws - 1] == ' ' || utf8[(size_t)ws - 1] == '\t')) --ws;
			UINT32 c0 = CharOf(startB), c1 = CharOf(endB), cw = CharOf(ws);
			out[i] = DWRITE_LINE_METRICS{ c1 - c0 + (UINT32)nl, (c1 - cw) + (UINT32)nl, (UINT32)nl,
			                              (FLOAT)(log.height / kPS), (FLOAT)((pango_layout_iter_get_baseline(it) - log.y) / kPS),
			                              pango_layout_is_ellipsized(l) };
			++i;
		} while (i < n && pango_layout_iter_next_line(it));
		pango_layout_iter_free(it);
		return S_OK;
	}
	HRESULT GetOverhangMetrics(DWRITE_OVERHANG_METRICS* o) { if (o) *o = DWRITE_OVERHANG_METRICS{ 0, 0, 0, 0 }; return S_OK; }
	HRESULT DetermineMinWidth(FLOAT* w) {
		using chrono_linux::kPS;
		if (!w) return E_POINTER;
		PangoLayout* l = pango_layout_copy(L());
		pango_layout_set_width(l, 1);
		pango_layout_set_wrap(l, PANGO_WRAP_WORD);
		int pw, ph; pango_layout_get_size(l, &pw, &ph);
		g_object_unref(l);
		*w = (FLOAT)(pw / kPS);
		return S_OK;
	}

	// --- hit-testing ---
	void FillMetrics(DWRITE_HIT_TEST_METRICS* m, UINT32 pos, UINT32 len, const PangoRectangle& r) const {
		if (!m) return;
		int x = r.width < 0 ? r.x + r.width : r.x, w = r.width < 0 ? -r.width : r.width;
		*m = DWRITE_HIT_TEST_METRICS{ pos, len, (FLOAT)(xoff + x / chrono_linux::kPS), (FLOAT)(yoff + r.y / chrono_linux::kPS),
		                              (FLOAT)(w / chrono_linux::kPS), (FLOAT)(r.height / chrono_linux::kPS), 0, TRUE, FALSE };
	}
	HRESULT HitTestPoint(FLOAT x, FLOAT y, BOOL* trailing, BOOL* inside, DWRITE_HIT_TEST_METRICS* m) {
		using chrono_linux::kPS;
		PangoLayout* l = L();
		int idx = 0, tr = 0;
		gboolean in = pango_layout_xy_to_index(l, (int)((x - xoff) * kPS), (int)((y - yoff) * kPS), &idx, &tr);
		UINT32 pos = CharOf(idx);
		if (pos >= text.size() && !text.empty()) { pos = (UINT32)text.size() - 1; tr = 1; }
		if (trailing) *trailing = tr > 0;
		if (inside) *inside = in;
		PangoRectangle r; pango_layout_index_to_pos(l, ByteOf(pos), &r);
		FillMetrics(m, pos, text.empty() ? 0 : 1, r);
		return S_OK;
	}
	HRESULT HitTestTextPosition(UINT32 pos, BOOL trailing, FLOAT* px, FLOAT* py, DWRITE_HIT_TEST_METRICS* m) {
		using chrono_linux::kPS;
		PangoLayout* l = L();
		if (pos > text.size()) pos = (UINT32)text.size();
		PangoRectangle r; pango_layout_index_to_pos(l, ByteOf(pos), &r);
		double x = r.x + ((trailing && pos < text.size()) ? r.width : 0);
		if (px) *px = (FLOAT)(xoff + x / kPS);
		if (py) *py = (FLOAT)(yoff + r.y / kPS);
		FillMetrics(m, pos, pos < text.size() ? 1 : 0, r);
		return S_OK;
	}
	HRESULT HitTestTextRange(UINT32 pos, UINT32 len, FLOAT ox, FLOAT oy, DWRITE_HIT_TEST_METRICS* out, UINT32 maxCount, UINT32* actual) {
		using chrono_linux::kPS;
		PangoLayout* l = L();
		int b0 = ByteOf(pos), b1 = ByteOf((UINT64)pos + len > text.size() ? (UINT32)text.size() : pos + len);
		std::vector<DWRITE_HIT_TEST_METRICS> res;
		PangoLayoutIter* it = pango_layout_get_iter(l);
		do {
			PangoLayoutLine* line = pango_layout_iter_get_line_readonly(it);
			int ls = line->start_index, le = line->start_index + line->length;
			bool endsNl = (size_t)le < utf8.size() && (utf8[(size_t)le] == '\n' || utf8[(size_t)le] == '\r');
			int s = (std::max)(b0, ls), e = (std::min)(b1, le);
			bool coversNl = endsNl && b0 <= le && b1 > le;   // the selection runs over this line's break
			if (s > e || (s == e && !coversNl && !(len == 0 && b0 >= ls && b0 <= le))) continue;
			PangoRectangle log; pango_layout_iter_get_line_extents(it, nullptr, &log);
			double left, right;
			if (s < e) {
				int* r = nullptr; int n = 0;
				pango_layout_line_get_x_ranges(line, s, e, &r, &n);
				int lo = INT32_MAX, hi = INT32_MIN;
				for (int i = 0; i < n; ++i) { lo = (std::min)(lo, r[i * 2]); hi = (std::max)(hi, r[i * 2 + 1]); }
				g_free(r);
				if (!n) { lo = hi = log.x; }
				left = lo / kPS; right = hi / kPS;
			} else {
				int x; pango_layout_line_index_to_x(line, s, 0, &x);
				left = right = x / kPS;
			}
			if (coversNl) right += size * 0.3;   // DWrite shows a selected line break as a sliver
			UINT32 c0 = CharOf(s), c1 = CharOf(e);
			res.push_back(DWRITE_HIT_TEST_METRICS{ c0, c1 - c0 + (coversNl ? 1u : 0u),
				(FLOAT)(ox + xoff + left), (FLOAT)(oy + yoff + log.y / kPS),
				(FLOAT)(right - left), (FLOAT)(log.height / kPS), 0, TRUE, FALSE });
		} while (pango_layout_iter_next_line(it));
		pango_layout_iter_free(it);
		if (actual) *actual = (UINT32)res.size();
		if (!out || maxCount < res.size()) return E_NOT_SUFFICIENT_BUFFER;
		std::copy(res.begin(), res.end(), out);
		return S_OK;
	}
};

// =============================================================================
struct IDWriteFactory : IUnknown {
	HRESULT CreateTextFormat(const WCHAR* family, IDWriteFontCollection*, DWRITE_FONT_WEIGHT w, DWRITE_FONT_STYLE s,
	                         DWRITE_FONT_STRETCH st, FLOAT size, const WCHAR* locale, IDWriteTextFormat** out) {
		if (!out) return E_POINTER;
		auto* f = new IDWriteTextFormat();
		f->family = family ? family : L"Segoe UI"; f->locale = locale ? locale : L"";
		f->weight = w; f->style = s; f->stretch = st; f->size = size > 0.0f ? size : 1.0f;
		*out = f;
		return S_OK;
	}
	HRESULT CreateTextLayout(const WCHAR* str, UINT32 len, IDWriteTextFormat* fmt, FLOAT maxW, FLOAT maxH, IDWriteTextLayout** out) {
		if (!out || !fmt) return E_INVALIDARG;
		auto* l = new IDWriteTextLayout();
		l->CopySettings(*fmt);
		l->maxW = maxW; l->maxH = maxH;
		l->SetText(str, len);
		*out = l;
		return S_OK;
	}
	HRESULT CreateEllipsisTrimmingSign(IDWriteTextFormat*, IDWriteInlineObject** out) { if (!out) return E_POINTER; *out = new IDWriteInlineObject(); return S_OK; }
	HRESULT GetSystemFontCollection(IDWriteFontCollection** out, BOOL = FALSE) { if (!out) return E_POINTER; *out = new IDWriteFontCollection(); return S_OK; }
};

inline HRESULT DWriteCreateFactory(DWRITE_FACTORY_TYPE, REFIID, IUnknown** out) {
	if (!out) return E_POINTER;
	*out = new IDWriteFactory();
	return S_OK;
}

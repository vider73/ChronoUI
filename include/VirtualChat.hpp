// =============================================================================
// VirtualChat.hpp — virtual widgets for a Claude Code-style chat UI.
//
// Built on top of the VirtualWidget infrastructure. Includes:
//
//   VChatBubble     — a message bubble. Knows whether it's a user or assistant
//                     message and renders the appropriate alignment + palette.
//                     Wraps text using DirectWrite; can measure its own height.
//   VCodeBlock      — a monospace code block with a dark theme and an optional
//                     language label. No syntax highlighting (yet); the surface
//                     is small enough that the user can paste real code in.
//   VTypingIndicator — three animated dots, driven by the heartbeat. Sits at
//                      the bottom of the message list while the assistant is
//                      "thinking."
//   VSendButton     — a styled action button (Send / Stop). Reuses VButton's
//                     interaction model with a chat-appropriate palette.
//   VChatLayout     — a tiny layout helper that stacks a vector of widgets
//                     vertically with consistent gaps, measures the total
//                     height, and reports it to the host for scrolling.
//
// Everything is virtual — no HWND per widget. The whole conversation list
// renders into the host's single ID2D1HwndRenderTarget.
// =============================================================================

#pragma once

#include "VirtualWidget.hpp"
#include "ChatImage.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <functional>

namespace ChronoUI {

	// ---------------------------------------------------------------------
	// Helper: measure a wrapped DWrite text block. Returns the height that
	// the text needs to render fully at the given width.
	// ---------------------------------------------------------------------
	inline float MeasureWrappedTextHeight(const std::wstring& text,
		const wchar_t* fontFamily, float fontSize,
		DWRITE_FONT_WEIGHT weight, float maxWidth)
	{
		if (text.empty() || maxWidth <= 0.0f) return 0.0f;
		ComPtr<IDWriteTextFormat> pTextFormat;
		HRESULT hr = ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
			fontFamily, NULL, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			fontSize, L"en-us", &pTextFormat);
		if (FAILED(hr) || !pTextFormat) return 0.0f;
		pTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

		ComPtr<IDWriteTextLayout> pLayout;
		hr = ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
			text.c_str(), (UINT32)text.size(),
			pTextFormat.Get(), maxWidth, 4096.0f, &pLayout);
		if (FAILED(hr) || !pLayout) return 0.0f;

		DWRITE_TEXT_METRICS m{};
		pLayout->GetMetrics(&m);
		return m.height;
	}


	// ---------------------------------------------------------------------
	// Inline markdown parser. Recognized syntax:
	//   **bold**        — DWRITE_FONT_WEIGHT_BOLD
	//   *italic*        — DWRITE_FONT_STYLE_ITALIC
	//   `inline code`   — font family swapped to Consolas
	// Nesting is permitted between bold and italic (e.g. ***both***). Code spans
	// are literal — markers inside them are not parsed. Unmatched markers are
	// silently swallowed: their content still appears in the clean text, no run
	// is emitted. That's forgiving when a stream target ends mid-marker.
	//
	// The parser produces (a) a "clean" string with the markers removed and
	// (b) a list of MdRuns that reference positions in the clean string. Those
	// runs are stable for a given input, so they survive streaming reveal
	// (just clip length to the revealed prefix when applying).
	// ---------------------------------------------------------------------
	struct MdRun {
		enum Kind : uint8_t { Bold = 1, Italic = 2, Code = 4 };
		UINT32  start;
		UINT32  length;
		uint8_t kind;     // single bit from Kind
	};

	inline void ParseInlineMarkdown(const std::wstring& raw,
		std::wstring& outClean, std::vector<MdRun>& outRuns)
	{
		outClean.clear();
		outRuns.clear();
		struct Open { uint8_t kind; UINT32 startInClean; };
		std::vector<Open> stack;

		auto topIs = [&](uint8_t k) -> int {
			for (int i = (int)stack.size() - 1; i >= 0; --i)
				if (stack[(size_t)i].kind == k) return i;
			return -1;
		};
		auto closeAt = [&](int idx, uint8_t k) {
			UINT32 start = stack[(size_t)idx].startInClean;
			UINT32 len   = (UINT32)outClean.size() - start;
			if (len > 0) outRuns.push_back({ start, len, k });
			stack.erase(stack.begin() + idx);
		};

		bool inCode = false;
		size_t i = 0;
		while (i < raw.size()) {
			wchar_t c = raw[i];
			// Inside a code span, only ` toggles — everything else is literal.
			if (inCode) {
				if (c == L'`') {
					int idx = topIs(MdRun::Code);
					if (idx >= 0) closeAt(idx, MdRun::Code);
					inCode = false;
					++i; continue;
				}
				outClean.push_back(c);
				++i; continue;
			}
			if (c == L'`') {
				stack.push_back({ MdRun::Code, (UINT32)outClean.size() });
				inCode = true;
				++i; continue;
			}
			if (c == L'*') {
				// Double-star is bold; single is italic. Greedy match on **.
				if (i + 1 < raw.size() && raw[i+1] == L'*') {
					int idx = topIs(MdRun::Bold);
					if (idx >= 0) closeAt(idx, MdRun::Bold);
					else          stack.push_back({ MdRun::Bold, (UINT32)outClean.size() });
					i += 2; continue;
				}
				// Glob-aware italic detection: only treat a single `*` as
				// an italic marker when it's actually decorating word text.
				//   - opener: next char must be alphanumeric (so `*foo*` opens,
				//     but `*.jpg` / `*test*` stays literal as a glob).
				//   - closer: prev char in outClean must be alphanumeric (so
				//     `text*` doesn't close a non-existent italic).
				// Failing either check, the `*` is appended as a literal so
				// the user sees their glob pattern intact.
				auto isAlnum = [](wchar_t ch) {
					return (ch >= L'0' && ch <= L'9')
					    || (ch >= L'A' && ch <= L'Z')
					    || (ch >= L'a' && ch <= L'z');
				};
				int idx = topIs(MdRun::Italic);
				if (idx < 0) {
					bool nextOk = (i + 1 < raw.size()) && isAlnum(raw[i+1]);
					if (!nextOk) { outClean.push_back(c); ++i; continue; }
					stack.push_back({ MdRun::Italic, (UINT32)outClean.size() });
				} else {
					bool prevOk = !outClean.empty() && isAlnum(outClean.back());
					if (!prevOk) { outClean.push_back(c); ++i; continue; }
					closeAt(idx, MdRun::Italic);
				}
				++i; continue;
			}
			outClean.push_back(c);
			++i;
		}
		// Unclosed spans are discarded — text already in outClean, no run emitted.
	}

	// Apply parsed runs to a freshly-built layout. Length is clipped to
	// 'textLen' so streaming bubbles only format their revealed substring —
	// the rest of a half-revealed run gets the formatting later as it fills in.
	inline void ApplyMdRuns(IDWriteTextLayout* layout,
		const std::vector<MdRun>& runs, size_t textLen)
	{
		if (!layout) return;
		for (const auto& r : runs) {
			if (r.start >= textLen) continue;
			UINT32 len = r.length;
			if ((size_t)r.start + len > textLen) len = (UINT32)(textLen - r.start);
			if (len == 0) continue;
			DWRITE_TEXT_RANGE tr = { r.start, len };
			switch (r.kind) {
				case MdRun::Bold:   layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, tr); break;
				case MdRun::Italic: layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, tr); break;
				case MdRun::Code:   layout->SetFontFamilyName(L"Consolas", tr); break;
			}
		}
	}

	// ---------------------------------------------------------------------
	// Tiny clipboard helper — push a wide string to CF_UNICODETEXT. Used by
	// every text-bearing widget (bubbles, code blocks, input) so they share
	// the same paste/copy guarantees.
	// ---------------------------------------------------------------------
	inline void SetClipboardWideText(const std::wstring& s) {
		if (!OpenClipboard(NULL)) return;
		EmptyClipboard();
		size_t bytes = (s.size() + 1) * sizeof(wchar_t);
		HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
		if (g) {
			if (wchar_t* p = (wchar_t*)GlobalLock(g)) {
				memcpy(p, s.c_str(), bytes);
				GlobalUnlock(g);
				SetClipboardData(CF_UNICODETEXT, g);
			}
		}
		CloseClipboard();
	}

	// ---------------------------------------------------------------------
	// Native popup menu with Copy / Select All. Returns the chosen id or 0
	// if the user dismissed the menu. Coordinates are in SCREEN space — the
	// caller passes pre-translated coords (typically from GetCursorPos()).
	// ---------------------------------------------------------------------
	// Context menu command IDs. Copy/SelectAll are the original "any
	// selectable widget" actions. The kCtxBubble* family is bubble-only —
	// they appear under a separator after the copy items when the menu
	// is invoked on a VChatBubble. Other widgets (VCodeBlock etc) pass
	// `bubbleActions=false` to keep the menu minimal.
	enum : int {
		kCtxCopy            = 1,
		kCtxSelectAll       = 2,
		kCtxBubbleEdit      = 10,
		kCtxBubbleDelete    = 11,
		kCtxBubbleRestart   = 12,
	};
	inline int ShowCopyContextMenu(HWND hwnd, int screenX, int screenY,
		bool hasSelection, bool hasContent = true,
		bool bubbleActions = false)
	{
		HMENU menu = CreatePopupMenu();
		if (!menu) return 0;
		AppendMenuW(menu, MF_STRING | (hasSelection ? 0 : MF_GRAYED), kCtxCopy,       L"Copy\tCtrl+C");
		AppendMenuW(menu, MF_STRING | (hasContent   ? 0 : MF_GRAYED), kCtxSelectAll,  L"Select All\tCtrl+A");
		if (bubbleActions) {
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(menu, MF_STRING, kCtxBubbleEdit,    L"Edit message");
			AppendMenuW(menu, MF_STRING, kCtxBubbleDelete,  L"Delete message");
			AppendMenuW(menu, MF_STRING, kCtxBubbleRestart, L"Restart from here\tCtrl+R");
		}
		int cmd = TrackPopupMenu(menu,
			TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
			screenX, screenY, 0, hwnd, NULL);
		DestroyMenu(menu);
		return cmd;
	}

	// Convert a content-local mouse coordinate to screen space without needing
	// to know the host's scrollY: just ask the OS where the cursor is RIGHT NOW.
	// Used by right-click handlers since the popup wants screen coords.
	inline POINT CursorScreenPos() {
		POINT pt = { 0, 0 };
		GetCursorPos(&pt);
		return pt;
	}

	// ---------------------------------------------------------------------
	// BubbleSegment — un trozo del contenido de una burbuja. Las respuestas de
	// los LLMs tienen forma de texto markdown intercalado con bloques de código
	// fenced (```lang...```). Para renderizar correctamente, partimos el raw
	// en segmentos alternados: Text (markdown inline) y Code (monospace +
	// fondo oscuro).
	//
	// Cada segmento conoce su posición en la concatenación visible global
	// (globalStart) para que selección y hit-test (que viven en m_text plano)
	// puedan mapear índices locales ↔ globales.
	// ---------------------------------------------------------------------
	struct BubbleSegment {
		enum Kind { Text, Code };
		Kind               kind = Text;
		std::wstring       text;
		std::wstring       lang;          // sólo Code (lenguaje, opcional)
		std::vector<MdRun> runs;          // sólo Text (runs inline)
		size_t             globalStart = 0;
	};

	// Parser de markdown con fenced code blocks. Detecta ```lang seguido de
	// salto de línea hasta el siguiente ``` también en su propia línea (o el
	// final del texto). Lo que queda fuera de los fences se parsea como inline
	// markdown vía ParseInlineMarkdown.
	//
	// Si el texto no contiene ningún fence, el resultado es un único segmento
	// Text — el comportamiento es idéntico al de antes.
	inline void ParseBubbleSegments(const std::wstring& raw,
		std::vector<BubbleSegment>& out, std::wstring* outConcat = nullptr)
	{
		out.clear();
		if (outConcat) outConcat->clear();

		auto pushText = [&](const std::wstring& s) {
			BubbleSegment seg;
			seg.kind = BubbleSegment::Text;
			seg.globalStart = outConcat ? outConcat->size() : 0;
			ParseInlineMarkdown(s, seg.text, seg.runs);
			if (outConcat) outConcat->append(seg.text);
			out.push_back(std::move(seg));
		};
		auto pushCode = [&](const std::wstring& code, const std::wstring& lang) {
			BubbleSegment seg;
			seg.kind = BubbleSegment::Code;
			seg.text = code;
			seg.lang = lang;
			seg.globalStart = outConcat ? outConcat->size() : 0;
			if (outConcat) outConcat->append(code);
			out.push_back(std::move(seg));
		};

		size_t i = 0;
		const size_t N = raw.size();
		std::wstring buf;   // texto acumulado fuera de fences
		while (i < N) {
			// Busca ``` al principio de línea (o inicio de texto). Aceptamos
			// también ``` precedida por whitespace inline porque algunos
			// modelos no respetan la convención estricta de "fence en su propia
			// línea".
			bool isLineStart = (i == 0) || (raw[i - 1] == L'\n');
			if (isLineStart && i + 2 < N
			    && raw[i] == L'`' && raw[i + 1] == L'`' && raw[i + 2] == L'`')
			{
				// Cierra el segmento de texto pendiente.
				if (!buf.empty()) { pushText(buf); buf.clear(); }
				// Lee la lang (resto de la línea tras ```).
				size_t j = i + 3;
				while (j < N && raw[j] != L'\n') ++j;
				std::wstring lang(raw, i + 3, j - (i + 3));
				// Trim de espacios.
				while (!lang.empty() && (lang.front() == L' ' || lang.front() == L'\t')) lang.erase(0, 1);
				while (!lang.empty() && (lang.back()  == L' ' || lang.back()  == L'\t')) lang.pop_back();
				if (j < N) ++j;   // salta el \n
				// Busca el cierre ``` también al principio de línea.
				size_t codeStart = j;
				size_t codeEnd   = N;
				size_t closeFence = std::wstring::npos;
				while (j < N) {
					bool lineStart = (j == 0) || (raw[j - 1] == L'\n');
					if (lineStart && j + 2 < N
					    && raw[j] == L'`' && raw[j + 1] == L'`' && raw[j + 2] == L'`')
					{
						closeFence = j;
						codeEnd = j;
						// Trim un \n final del code body si lo tiene (la línea
						// que precede al fence).
						if (codeEnd > codeStart && raw[codeEnd - 1] == L'\n') --codeEnd;
						break;
					}
					++j;
				}
				std::wstring code(raw, codeStart, codeEnd - codeStart);
				pushCode(code, lang);
				if (closeFence == std::wstring::npos) {
					// Fence sin cerrar — consumimos hasta el final.
					i = N;
				} else {
					// Salta el ``` de cierre y el \n posterior si lo hay.
					i = closeFence + 3;
					if (i < N && raw[i] == L'\n') ++i;
				}
				continue;
			}
			buf.push_back(raw[i]);
			++i;
		}
		if (!buf.empty()) pushText(buf);
	}

	// ---------------------------------------------------------------------
	// VChatBubble — a wrapped-text message bubble with role-aware styling.
	//
	// Roles:
	//   User      — right-aligned, blue gradient-ish solid fill, white text.
	//   Assistant — left-aligned, light gray fill, dark text.
	//
	// The bubble is positioned by the caller (the chat layout sets its bounds).
	// Internally the bubble draws a rounded rectangle constrained to a max
	// width (~70% of host width is conventional) and the text inside with
	// padding. Call MeasureHeight(maxWidth) before SetBounds() to figure out
	// how tall the bubble needs to be.
	// ---------------------------------------------------------------------
	class VChatBubble : public VirtualWidgetImpl {
	public:
		enum Role { User, Assistant };

		// Right-click actions surfaced by the bubble's context menu.
		// The host (Chat.cpp) installs a single callback via
		// ActionCallback() and is responsible for finding the bubble in
		// its message list and performing the action. Keeping the hook
		// at class scope (instead of per-bubble) means AppendBubble()
		// doesn't need to wire a callback on every newly-created bubble.
		enum class Action { Edit, Delete, RestartHere };
		using ActionFn = std::function<void(VChatBubble*, Action)>;
		static ActionFn& ActionCallback() {
			static ActionFn cb;
			return cb;
		}
	private:
		Role         m_role = Assistant;
		std::wstring m_text;
		float        m_fontSize     = 14.0f;
		float        m_padding      = 14.0f;
		float        m_cornerRadius = 12.0f;

		// Palette. Defaults match a light/neutral theme; swap if you want dark mode.
		D2D1_COLOR_F m_userFill        = D2D1::ColorF(0x4A90E2);
		D2D1_COLOR_F m_userText        = D2D1::ColorF(0xFFFFFF);
		D2D1_COLOR_F m_assistantFill   = D2D1::ColorF(0xF1F1F2);
		D2D1_COLOR_F m_assistantText   = D2D1::ColorF(0x1A1A1A);
		D2D1_COLOR_F m_assistantBorder = D2D1::ColorF(0xE0E0E2);

		// Streaming state — set by StreamFrom(), advanced by OnUpdate().
		// NOTE: m_streamTarget holds the CLEAN parsed text (markers removed),
		// not the raw input. That way streaming reveals one visible character
		// at a time, never a half-typed marker.
		std::wstring m_streamTarget;
		size_t       m_streamRevealed = 0;
		float        m_streamAccum = 0.0f;
		float        m_streamCps   = 80.0f;

		// Inline-markdown formatting runs into m_text. Stable for the lifetime
		// of the current text — parsed once at SetText / StreamFrom time and
		// applied (clipped to revealed length) every draw. m_runs es el
		// concat global aplicado a m_text (la concatenación visible de TODOS
		// los segments).
		std::vector<MdRun> m_runs;

		// Segmentos del contenido. Cada uno es Text o Code. Si el raw no tiene
		// fences ```, m_segments tiene un único elemento Text — el comportamiento
		// es idéntico al del demo original. Si el LLM responde con código, los
		// fences se renderizan como mini-VCodeBlocks apilados verticalmente.
		std::vector<BubbleSegment> m_segments;

		// Gap entre segmentos consecutivos dentro de la burbuja.
		float m_segmentGap = 8.0f;
		// Padding interno del code block (relativo al segment, dentro del
		// padding general de la burbuja).
		float m_codeInnerPad = 10.0f;

		// Imágenes adjuntas al mensaje. Se renderizan en una tira horizontal
		// ARRIBA del texto, dentro del padding de la burbuja. Pasadas via
		// SetImages() tras enviar (lo hace Chat.cpp tomándolas de la barra
		// de attachments).
		std::vector<std::shared_ptr<ChatImage>> m_images;
		float m_imgThumb     = 96.0f;   // tamaño visible máx por imagen en bubble
		float m_imgGap       = 6.0f;
		float m_imgStripGap  = 8.0f;    // gap entre la tira y el primer segmento

		// True when the cursor sits on the copy-button overlay specifically
		// (independent of the broader bubble hover). Updated in OnMouseMove
		// and consumed by DrawCopyButton to swap to a brighter fill + show
		// a "Copy" mini-tooltip.
		bool m_copyHover = false;
		// Same pattern for the Edit ✎, Delete 🗑 and Restart ↺ buttons
		// that sit to the LEFT of Copy. They only render when
		// ActionCallback is registered (Chat.cpp does that at
		// startup), so widgets used outside Cowork-chat-style hosts
		// stay clean.
		bool m_editHover    = false;
		bool m_deleteHover  = false;
		bool m_restartHover = false;

		// Selection state. m_caret is the active end (where Shift+arrow / drag
		// updates); m_anchor is the other end. They're equal when there's no
		// selection. Indices reference m_text — for streaming bubbles, we clamp
		// to m_text.size() each frame so selection can't dangle past the
		// revealed text.
		size_t m_caret  = 0;
		size_t m_anchor = 0;
		bool   m_dragging = false;
		D2D1_COLOR_F m_selectionFill = D2D1::ColorF(0xCBD9FF);  // light blue, slightly darker on assistant

	public:
		const char* GetTypeName() const override { return "VChatBubble"; }

		VChatBubble& SetRole(Role r)              { m_role = r; return *this; }
		Role         GetRole() const              { return m_role; }
		// SetText parsea el texto completo en segmentos (Text e potenciales
		// Code blocks ```...```). m_text es la concatenación visible de TODOS
		// los segmentos, así Selection / Ctrl+C / Ctrl+A siguen funcionando
		// sobre la burbuja entera independientemente de cuántos segmentos
		// tenga. m_runs son los runs inline ya offset-eados a posiciones
		// globales de m_text.
		VChatBubble& SetText(const std::wstring& raw) {
			ParseBubbleSegments(raw, m_segments, &m_text);
			RebuildGlobalRuns();
			if (m_caret  > m_text.size()) m_caret  = m_text.size();
			if (m_anchor > m_text.size()) m_anchor = m_text.size();
			return *this;
		}
		VChatBubble& SetFontSize(float px)        { m_fontSize = px; return *this; }
		VChatBubble& SetCornerRadius(float r)     { m_cornerRadius = r; return *this; }

		// Adjunta imágenes al mensaje. Las imágenes se pintan en una tira
		// horizontal sobre el texto. Cada thumbnail mantiene aspect-ratio,
		// limitado a m_imgThumb en cualquier dimensión. La burbuja crece en
		// altura para acomodarlas.
		VChatBubble& SetImages(std::vector<std::shared_ptr<ChatImage>> imgs) {
			m_images = std::move(imgs);
			return *this;
		}
		const std::vector<std::shared_ptr<ChatImage>>& Images() const { return m_images; }

		// Calcula la altura que la tira de imágenes ocupará dentro del
		// inner rect, dada una width disponible. Las imágenes pueden
		// ajustarse en una sola fila si caben, o en filas adicionales si
		// no. Devuelve 0 si no hay imágenes.
		float MeasureImageStripHeight(float innerWidth) const {
			if (m_images.empty()) return 0.0f;
			float x = 0.0f;
			float y = 0.0f;
			float rowH = 0.0f;
			for (const auto& img : m_images) {
				if (!img || img->width <= 0 || img->height <= 0) continue;
				float scale = (std::min)(
					m_imgThumb / (float)img->width,
					m_imgThumb / (float)img->height);
				float w = (float)img->width  * scale;
				float h = (float)img->height * scale;
				if (x > 0 && x + w > innerWidth) {
					// Nueva fila.
					y += rowH + m_imgGap;
					x = 0.0f;
					rowH = 0.0f;
				}
				x += w + m_imgGap;
				if (h > rowH) rowH = h;
			}
			return y + rowH;
		}

		// Returns the height the bubble needs at a given external width. The
		// caller picks an externalWidth (the column the bubble can live in);
		// the bubble figures out how much vertical space its wrapped text needs
		// plus padding. Use this before SetBounds() to size the bubble.
		//
		// Itera todos los segmentos (Text y Code) y suma sus alturas. Text se
		// mide con Segoe UI + word-wrap. Code se mide con Consolas + word-wrap
		// (a diferencia del VCodeBlock standalone, dentro de una burbuja sí
		// queremos wrap — la burbuja tiene ancho fijo y no scrollea).
		float MeasureHeight(float externalWidth) const {
			float innerWidth = externalWidth - (m_padding * 2.0f);
			if (innerWidth < 40.0f) innerWidth = 40.0f;
			float total = 0.0f;
			// Tira de imágenes, si la hay.
			float imgH = MeasureImageStripHeight(innerWidth);
			if (imgH > 0) total += imgH + m_imgStripGap;
			bool first = true;
			for (const auto& seg : m_segments) {
				if (!first) total += m_segmentGap;
				first = false;
				total += MeasureSegmentHeight(seg, innerWidth);
			}
			return total + (m_padding * 2.0f);
		}

		// Helper: altura de un segmento individual.
		float MeasureSegmentHeight(const BubbleSegment& seg, float innerWidth) const {
			if (seg.kind == BubbleSegment::Text) {
				return MeasureWrappedTextHeight(seg.text, L"Segoe UI",
					m_fontSize, DWRITE_FONT_WEIGHT_NORMAL, innerWidth);
			}
			// Code: padding interno + label si hay lang.
			float codeInnerW = innerWidth - (m_codeInnerPad * 2.0f);
			if (codeInnerW < 32.0f) codeInnerW = 32.0f;
			float bodyH = MeasureWrappedTextHeight(seg.text, L"Consolas",
				m_fontSize - 1.0f, DWRITE_FONT_WEIGHT_NORMAL, codeInnerW);
			float labelH = seg.lang.empty() ? 0.0f : 18.0f;
			return bodyH + (m_codeInnerPad * 2.0f) + labelH;
		}

		// --- Streaming support ---
		// StreamFrom(text, charsPerSecond) starts revealing 'text' one character
		// at a time via the heartbeat. The bubble's bounds are NOT changed —
		// the caller should already have measured + positioned the bubble for
		// the FULL final text (see MeasureHeight). Only the visible substring
		// grows. Returns true while the stream is still revealing.
		void StreamFrom(const std::wstring& rawFull, float charsPerSecond = 80.0f) {
			// Parse ONCE en el FULL target. m_streamTarget es la concatenación
			// visible final completa. m_segments contiene los segmentos del
			// target — pero NO se usan para rendering durante el stream
			// heartbeat (el reveal carácter a carácter se hace sobre m_text
			// "plano" via OnUpdate). Los segments sí se usan para MeasureHeight
			// (medimos contra el full final), igual que con el comportamiento
			// streaming original.
			ParseBubbleSegments(rawFull, m_segments, &m_streamTarget);
			RebuildGlobalRuns();
			m_streamRevealed = 0;
			m_streamAccum    = 0.0f;
			m_streamCps      = (charsPerSecond > 0.0f) ? charsPerSecond : 1.0f;
			m_text.clear();
		}
		bool IsStreaming() const { return m_streamRevealed < m_streamTarget.size(); }

		bool OnUpdate(float dt) override {
			if (!IsStreaming()) return false;
			m_streamAccum += dt * m_streamCps;
			size_t add = (size_t)m_streamAccum;
			if (add == 0) return false;
			m_streamAccum -= (float)add;
			m_streamRevealed += add;
			if (m_streamRevealed > m_streamTarget.size())
				m_streamRevealed = m_streamTarget.size();
			m_text.assign(m_streamTarget.c_str(), m_streamRevealed);
			return true;  // ask host for InvalidateRect
		}

		// Bubble is selectable for copy-to-clipboard. Receiving focus is what
		// lets Ctrl+C be routed here.
		bool CanFocus() const override { return true; }

		void OnDraw(ID2D1RenderTarget* pRT) override {
			D2D1_RECT_F r = m_bounds;
			D2D1_COLOR_F fill = (m_role == User) ? m_userFill : m_assistantFill;

			ComPtr<ID2D1SolidColorBrush> pFill;
			pRT->CreateSolidColorBrush(fill, &pFill);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, m_cornerRadius, m_cornerRadius);
			if (pFill) pRT->FillRoundedRectangle(rr, pFill.Get());

			// Subtle border on the assistant bubble to lift it off the page.
			if (m_role == Assistant) {
				ComPtr<ID2D1SolidColorBrush> pBorder;
				pRT->CreateSolidColorBrush(m_assistantBorder, &pBorder);
				if (pBorder) pRT->DrawRoundedRectangle(rr, pBorder.Get(), 1.0f);
			}

			if (m_segments.empty() && m_text.empty() && m_images.empty()) return;

			D2D1_RECT_F inner = D2D1::RectF(
				r.left + m_padding,  r.top + m_padding,
				r.right - m_padding, r.bottom - m_padding);
			float innerW = inner.right - inner.left;
			float y = inner.top;

			// Tira de imágenes ARRIBA del texto. Cada imagen se escala con
			// aspect-ratio preservado a m_imgThumb (max), envuelve a múltiples
			// filas si no caben en una línea. Si una imagen escalada supera
			// el inner width (burbuja muy estrecha), reducimos más para que
			// quepa siempre — nunca dibujamos fuera del padding interno.
			if (!m_images.empty()) {
				// Clip de seguridad al inner del bubble — protege contra
				// errores de cálculo en el wrap y, sobre todo, contra
				// imágenes muy anchas en burbujas estrechas.
				pRT->PushAxisAlignedClip(
					D2D1::RectF(inner.left, inner.top, inner.right, inner.bottom),
					D2D1_ANTIALIAS_MODE_ALIASED);
				float x = inner.left;
				float rowTop = y;
				float rowH = 0.0f;
				for (const auto& img : m_images) {
					if (!img || img->width <= 0 || img->height <= 0) continue;
					float scale = (std::min)(
						m_imgThumb / (float)img->width,
						m_imgThumb / (float)img->height);
					float w = (float)img->width  * scale;
					float h = (float)img->height * scale;
					// Si aun así excede el inner width (burbuja muy estrecha),
					// reducimos hasta que quepa.
					if (w > innerW) {
						float s = innerW / w;
						w *= s; h *= s;
					}
					if (x > inner.left && x + w > inner.right) {
						rowTop += rowH + m_imgGap;
						x = inner.left;
						rowH = 0.0f;
					}
					D2D1_RECT_F dst = D2D1::RectF(x, rowTop, x + w, rowTop + h);
					// Fondo gris claro por si el bitmap aún no se resolvió.
					ComPtr<ID2D1SolidColorBrush> pPh;
					pRT->CreateSolidColorBrush(D2D1::ColorF(0xE5E7EB), &pPh);
					if (pPh) {
						D2D1_ROUNDED_RECT rr2 = D2D1::RoundedRect(dst, 6.0f, 6.0f);
						pRT->FillRoundedRectangle(rr2, pPh.Get());
					}
					ID2D1Bitmap* bmp = img->EnsureBitmap(pRT);
					if (bmp) {
						pRT->DrawBitmap(bmp, dst, 1.0f,
							D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
					}
					x += w + m_imgGap;
					if (h > rowH) rowH = h;
				}
				y = rowTop + rowH + m_imgStripGap;
				pRT->PopAxisAlignedClip();
			}

			// Durante el reveal carácter-a-carácter (StreamFrom + heartbeat),
			// m_text < m_streamTarget. En ese caso renderizamos el m_text
			// plano como un único bloque Text — los segmentos sólo son
			// definitivos cuando la respuesta está completa. Para el caso
			// común (Ollama real, SetText directo), m_segments y m_text están
			// sincronizados y se renderiza segment by segment.
			const bool partial = IsStreaming();
			if (partial && !m_text.empty()) {
				DrawTextSpan(pRT, m_text, m_runs, 0, m_text.size(),
				             inner.left, y, innerW, /*outBlockH=*/nullptr);
			} else {
				for (size_t si = 0; si < m_segments.size(); ++si) {
					const auto& seg = m_segments[si];
					if (si > 0) y += m_segmentGap;
					float segH = MeasureSegmentHeight(seg, innerW);
					if (seg.kind == BubbleSegment::Text) {
						DrawTextSpan(pRT, seg.text, seg.runs,
						             seg.globalStart, seg.text.size(),
						             inner.left, y, innerW, nullptr);
					} else {
						DrawCodeSegment(pRT, seg, inner.left, y, innerW, segH);
					}
					y += segH;
				}
			}

			// Hover-revealed action buttons in the top-right corner.
			// Order (left → right): [ ↺ ][ 🗑 ][ ✎ ][ 📋 ]. Restart /
			// Edit / Delete only appear when the host has registered
			// an ActionCallback — stand-alone embeds of VChatBubble
			// keep the original Copy-only chrome. None of them draw
			// while the bubble is streaming (copying / mutating a
			// half-typed reply is rarely what the user wants).
			if (m_hovered && !IsStreaming() && !m_text.empty()) {
				if (BubbleActionsActive()) {
					DrawRestartButton(pRT);
					DrawDeleteButton(pRT);
					DrawEditButton(pRT);
				}
				DrawCopyButton(pRT);
			}
		}

		// Dibuja un span de texto en (x, y) con ancho 'width'. 'globalOffset'
		// es la posición del inicio del span en m_text plano (para mapear la
		// selección global). Si outBlockH != nullptr, escribe la altura
		// renderizada.
		void DrawTextSpan(ID2D1RenderTarget* pRT, const std::wstring& text,
			const std::vector<MdRun>& runs, size_t globalOffset, size_t /*len*/,
			float x, float y, float width, float* outBlockH) const
		{
			D2D1_COLOR_F textColor = (m_role == User) ? m_userText : m_assistantText;
			ComPtr<IDWriteTextFormat> fmt;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				L"Segoe UI", NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				m_fontSize, L"en-us", &fmt);
			if (!fmt) { if (outBlockH) *outBlockH = 0.0f; return; }
			fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
			fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
			fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

			ComPtr<IDWriteTextLayout> layout;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
				text.c_str(), (UINT32)text.size(),
				fmt.Get(), width, 4096.0f, &layout);
			if (!layout) { if (outBlockH) *outBlockH = 0.0f; return; }
			ApplyMdRuns(layout.Get(), runs, text.size());

			// Selección que cae dentro de este span.
			if (HasSelection()) {
				size_t selA = SelStart(), selB = SelEnd();
				size_t spanA = globalOffset, spanB = globalOffset + text.size();
				size_t a = (std::max)(selA, spanA);
				size_t b = (std::min)(selB, spanB);
				if (a < b) {
					DrawSelectionRange(pRT, layout.Get(), x, y,
					                   (UINT32)(a - spanA), (UINT32)(b - a));
				}
			}

			ComPtr<ID2D1SolidColorBrush> pBrush;
			pRT->CreateSolidColorBrush(textColor, &pBrush);
			if (pBrush) pRT->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), pBrush.Get());

			if (outBlockH) {
				DWRITE_TEXT_METRICS m{};
				layout->GetMetrics(&m);
				*outBlockH = m.height;
			}
		}

		// Dibuja un mini-code-block dentro de la burbuja: fondo oscuro + texto
		// Consolas + opcional label de lenguaje en la esquina.
		void DrawCodeSegment(ID2D1RenderTarget* pRT, const BubbleSegment& seg,
			float x, float y, float width, float height) const
		{
			D2D1_RECT_F box = D2D1::RectF(x, y, x + width, y + height);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(box, 6.0f, 6.0f);

			ComPtr<ID2D1SolidColorBrush> pBg, pLabel, pCode;
			pRT->CreateSolidColorBrush(D2D1::ColorF(0x1E1E1E), &pBg);
			pRT->CreateSolidColorBrush(D2D1::ColorF(0x9CA3AF), &pLabel);
			pRT->CreateSolidColorBrush(D2D1::ColorF(0xE6E6E6), &pCode);
			if (pBg) pRT->FillRoundedRectangle(rr, pBg.Get());

			float labelH = seg.lang.empty() ? 0.0f : 18.0f;
			float codeTop = y + m_codeInnerPad;

			if (!seg.lang.empty()) {
				ComPtr<IDWriteTextFormat> lf;
				ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
					L"Segoe UI", NULL,
					DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
					11.0f, L"en-us", &lf);
				if (lf && pLabel) {
					lf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
					D2D1_RECT_F lr = D2D1::RectF(x + m_codeInnerPad, y + 4.0f,
					                              x + width - m_codeInnerPad, y + 4.0f + labelH);
					pRT->DrawText(seg.lang.c_str(), (UINT32)seg.lang.size(),
						lf.Get(), lr, pLabel.Get());
				}
				codeTop += labelH;
			}

			ComPtr<IDWriteTextFormat> cf;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				L"Consolas", NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				m_fontSize - 1.0f, L"en-us", &cf);
			if (cf) {
				cf->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
				cf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
				cf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

				float codeInnerW = width - (m_codeInnerPad * 2.0f);
				ComPtr<IDWriteTextLayout> layout;
				ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
					seg.text.c_str(), (UINT32)seg.text.size(),
					cf.Get(), codeInnerW, 4096.0f, &layout);

				// Selección que cae dentro de este code segment.
				if (layout && HasSelection()) {
					size_t selA = SelStart(), selB = SelEnd();
					size_t spanA = seg.globalStart, spanB = seg.globalStart + seg.text.size();
					size_t a = (std::max)(selA, spanA);
					size_t b = (std::min)(selB, spanB);
					if (a < b) {
						DrawSelectionRange(pRT, layout.Get(),
						                   x + m_codeInnerPad, codeTop,
						                   (UINT32)(a - spanA), (UINT32)(b - a));
					}
				}

				if (layout && pCode) {
					pRT->DrawTextLayout(D2D1::Point2F(x + m_codeInnerPad, codeTop),
						layout.Get(), pCode.Get());
				}
			}
		}

		// Helper: dibuja el highlight de una selección [start, start+len) sobre
		// un layout posicionado en (x, y). Generaliza el DrawSelectionHighlight
		// original a un origen arbitrario.
		void DrawSelectionRange(ID2D1RenderTarget* pRT, IDWriteTextLayout* layout,
			float x, float y, UINT32 start, UINT32 len) const
		{
			if (len == 0) return;
			UINT32 actualCount = 0;
			layout->HitTestTextRange(start, len, 0.0f, 0.0f, nullptr, 0, &actualCount);
			if (actualCount == 0) return;
			std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
			if (FAILED(layout->HitTestTextRange(start, len, 0.0f, 0.0f,
				metrics.data(), actualCount, &actualCount))) return;
			D2D1_COLOR_F selColor = (m_role == User)
				? D2D1::ColorF(0x6FA8EF)
				: m_selectionFill;
			ComPtr<ID2D1SolidColorBrush> pSel;
			pRT->CreateSolidColorBrush(selColor, &pSel);
			if (!pSel) return;
			for (const auto& m : metrics) {
				D2D1_RECT_F sr = D2D1::RectF(
					x + m.left,
					y + m.top,
					x + m.left + m.width,
					y + m.top  + m.height);
				pRT->FillRectangle(sr, pSel.Get());
			}
		}

		// ----------------- Input: drag-to-select + copy -----------------
		// Track hover so the copy button can fade in/out. The matching
		// OnMouseLeave lives further down — it also clears m_copyHover
		// and the drag flag.
		VInputResult OnMouseEnter() override { m_hovered = true;  return VInputResult::Handled; }

		VInputResult OnMouseDown(float x, float y, int btn) override {
			if (btn == 1) {
				// Hover-revealed buttons take priority over selection
				// drag. Check left-to-right (Restart, Delete, Edit,
				// Copy) so the dispatch order matches visual order.
				if (HitRestartButton(x, y)) {
					if (auto& cb = ActionCallback()) cb(this, Action::RestartHere);
					return VInputResult::Handled;
				}
				if (HitDeleteButton(x, y)) {
					if (auto& cb = ActionCallback()) cb(this, Action::Delete);
					return VInputResult::Handled;
				}
				if (HitEditButton(x, y)) {
					if (auto& cb = ActionCallback()) cb(this, Action::Edit);
					return VInputResult::Handled;
				}
				if (HitCopyButton(x, y)) {
					SetClipboardWideText(m_text);
					return VInputResult::Handled;
				}
				size_t pos = HitTestToCaret(x, y);
				m_caret = pos;
				if (!(GetKeyState(VK_SHIFT) & 0x8000)) m_anchor = pos;
				m_dragging = true;
				return VInputResult::Capture;   // capture so drag outside the bubble still updates the caret
			}
			if (btn == 2) {
				// Swallow the right-DOWN so the host doesn't treat it as a focus
				// change while the menu is about to come up on UP. Selection is
				// preserved — we do NOT collapse it on right-click.
				return VInputResult::Handled;
			}
			return VInputResult::NotHandled;
		}
		VInputResult OnMouseMove(float x, float y) override {
			// Update all four hover-button states each move so the
			// overlays light up the instant the cursor crosses in.
			// Any change should force a repaint.
			bool wasCopyHover    = m_copyHover;
			bool wasEditHover    = m_editHover;
			bool wasDeleteHover  = m_deleteHover;
			bool wasRestartHover = m_restartHover;
			m_copyHover    = HitCopyButton(x, y);
			m_editHover    = HitEditButton(x, y);
			m_deleteHover  = HitDeleteButton(x, y);
			m_restartHover = HitRestartButton(x, y);
			bool hoverChanged =
				(wasCopyHover    != m_copyHover)    ||
				(wasEditHover    != m_editHover)    ||
				(wasDeleteHover  != m_deleteHover)  ||
				(wasRestartHover != m_restartHover);
			if (!m_dragging) {
				return hoverChanged
					? VInputResult::Handled : VInputResult::NotHandled;
			}
			m_caret = HitTestToCaret(x, y);
			return VInputResult::Handled;
		}
		VInputResult OnMouseLeave() override {
			m_hovered      = false;
			m_dragging     = false;
			m_copyHover    = false;
			m_editHover    = false;
			m_deleteHover  = false;
			m_restartHover = false;
			return VInputResult::Handled;
		}
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn == 1) {
				if (m_dragging) m_caret = HitTestToCaret(x, y);
				m_dragging = false;
				return VInputResult::Handled;
			}
			if (btn == 2) {
				// Context menu fires on UP (matches Explorer / browser convention).
				// Bubble-only actions (Edit / Delete / Restart) appear
				// when an ActionCallback is registered — Chat.cpp does
				// that on startup. Without a callback we don't surface
				// the items (they'd be no-ops).
				POINT scr = CursorScreenPos();
				const bool hasCb = (bool)ActionCallback();
				int cmd = ShowCopyContextMenu(GetActiveWindow(), scr.x, scr.y,
					HasSelection(), !m_text.empty(), /*bubbleActions=*/hasCb);
				if (cmd == kCtxCopy) {
					CopySelectionToClipboard();
				} else if (cmd == kCtxSelectAll) {
					m_anchor = 0;
					m_caret  = m_text.size();
				} else if (cmd == kCtxBubbleEdit && hasCb) {
					ActionCallback()(this, Action::Edit);
				} else if (cmd == kCtxBubbleDelete && hasCb) {
					ActionCallback()(this, Action::Delete);
				} else if (cmd == kCtxBubbleRestart && hasCb) {
					ActionCallback()(this, Action::RestartHere);
				}
				return VInputResult::Handled;
			}
			return VInputResult::NotHandled;
		}

		VInputResult OnKeyDown(UINT vk) override {
			const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
			const bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
			switch (vk) {
			case 'A':
				if (ctrl) { m_anchor = 0; m_caret = m_text.size(); return VInputResult::Handled; }
				break;
			case 'C':
				if (ctrl) {
					if (HasSelection()) CopySelectionToClipboard();
					return VInputResult::Handled;
				}
				break;
			case VK_LEFT:
				if (m_caret > 0) m_caret--;
				if (!shift) m_anchor = m_caret;
				return VInputResult::Handled;
			case VK_RIGHT:
				if (m_caret < m_text.size()) m_caret++;
				if (!shift) m_anchor = m_caret;
				return VInputResult::Handled;
			case VK_HOME:
				m_caret = 0;
				if (!shift) m_anchor = m_caret;
				return VInputResult::Handled;
			case VK_END:
				m_caret = m_text.size();
				if (!shift) m_anchor = m_caret;
				return VInputResult::Handled;
			case VK_ESCAPE:
				m_anchor = m_caret;             // collapse selection
				return VInputResult::Handled;
			}
			return VInputResult::NotHandled;
		}

	private:
		bool   HasSelection() const {
			// Clamp to revealed text — important while streaming so a selection
			// can't reference indices past the visible substring.
			size_t a = m_anchor, b = m_caret;
			if (a > m_text.size()) a = m_text.size();
			if (b > m_text.size()) b = m_text.size();
			return a != b;
		}
		size_t SelStart() const {
			size_t a = m_anchor > m_text.size() ? m_text.size() : m_anchor;
			size_t b = m_caret  > m_text.size() ? m_text.size() : m_caret;
			return (a < b) ? a : b;
		}
		size_t SelEnd() const {
			size_t a = m_anchor > m_text.size() ? m_text.size() : m_anchor;
			size_t b = m_caret  > m_text.size() ? m_text.size() : m_caret;
			return (a < b) ? b : a;
		}

		void CopySelectionToClipboard() const {
			if (!HasSelection()) return;
			SetClipboardWideText(m_text.substr(SelStart(), SelEnd() - SelStart()));
		}

		// Map a bubble-local mouse position to a character index in m_text via
		// DWrite's wrapped-text hit-testing. Itera segmentos vertically para
		// localizar el correspondiente, hace hit-test local en su layout, y
		// devuelve el índice global en m_text (= globalStart + posición local).
		size_t HitTestToCaret(float clickX, float clickY) const {
			if (m_text.empty()) return 0;
			float innerW = (m_bounds.right - m_bounds.left) - (m_padding * 2.0f);
			if (innerW < 40.0f) innerW = 40.0f;
			float y = m_bounds.top + m_padding;
			float relX = clickX - (m_bounds.left + m_padding);
			if (relX < 0) relX = 0;

			for (size_t si = 0; si < m_segments.size(); ++si) {
				const auto& seg = m_segments[si];
				if (si > 0) y += m_segmentGap;
				float segH = MeasureSegmentHeight(seg, innerW);
				float segTop = y, segBot = y + segH;
				y = segBot;
				// Si el click cae dentro de este segmento (o el último y el
				// click es por debajo), hacemos hit-test aquí.
				bool isLast = (si == m_segments.size() - 1);
				if ((clickY >= segTop && clickY < segBot) || (isLast && clickY >= segBot)) {
					float localY = clickY - segTop;
					if (localY < 0) localY = 0;
					if (seg.kind == BubbleSegment::Text) {
						return seg.globalStart + HitTestInLayout(
							seg.text, seg.runs, /*isCode=*/false,
							relX, localY, innerW);
					} else {
						// Dentro del code box: descontamos paddings internos.
						float codeX = relX - m_codeInnerPad;
						float codeY = localY - m_codeInnerPad;
						if (!seg.lang.empty()) codeY -= 18.0f;
						if (codeX < 0) codeX = 0;
						if (codeY < 0) codeY = 0;
						return seg.globalStart + HitTestInLayout(
							seg.text, seg.runs, /*isCode=*/true,
							codeX, codeY, innerW - m_codeInnerPad * 2.0f);
					}
				}
			}
			return m_text.size();
		}

		// Helper interno: hit-test dentro de un layout construido con los
		// mismos parámetros que el paint del segmento correspondiente.
		size_t HitTestInLayout(const std::wstring& text,
			const std::vector<MdRun>& runs, bool isCode,
			float relX, float relY, float innerW) const
		{
			if (text.empty()) return 0;
			ComPtr<IDWriteTextFormat> fmt;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				isCode ? L"Consolas" : L"Segoe UI", NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				isCode ? (m_fontSize - 1.0f) : m_fontSize, L"en-us", &fmt);
			if (!fmt) return text.size();
			fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
			ComPtr<IDWriteTextLayout> layout;
			if (FAILED(ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
				text.c_str(), (UINT32)text.size(), fmt.Get(),
				innerW, 4096.0f, &layout)) || !layout) return text.size();
			if (!isCode) ApplyMdRuns(layout.Get(), runs, text.size());
			BOOL isTrailing = FALSE, isInside = FALSE;
			DWRITE_HIT_TEST_METRICS hm{};
			if (FAILED(layout->HitTestPoint(relX, relY, &isTrailing, &isInside, &hm)))
				return text.size();
			size_t pos = hm.textPosition + (isTrailing ? hm.length : 0);
			if (pos > text.size()) pos = text.size();
			return pos;
		}

		// Reconstruye m_runs como concatenación offset-eada de los runs de
		// cada Text segment. Útil como referencia global (no se usa en el
		// path principal de paint/hit-test, pero ApplyMdRuns lo necesitaría
		// si alguien construyera un layout sobre m_text plano completo).
		void RebuildGlobalRuns() {
			m_runs.clear();
			for (const auto& seg : m_segments) {
				if (seg.kind != BubbleSegment::Text) continue;
				for (const auto& r : seg.runs) {
					MdRun shifted = r;
					shifted.start = (UINT32)(r.start + seg.globalStart);
					m_runs.push_back(shifted);
				}
			}
		}

		// --- Copy button overlay -----------------------------------------
		// Rectangle is tucked into the bubble's top-right corner. It's only
		// hit-testable when the bubble is hovered AND not streaming (we don't
		// expose copy until the reply is complete).
		D2D1_RECT_F CopyButtonRect() const {
			const float pad = 6.0f, w = 28.0f, h = 22.0f;
			return D2D1::RectF(
				m_bounds.right - pad - w, m_bounds.top + pad,
				m_bounds.right - pad,     m_bounds.top + pad + h);
		}
		// Edit + Delete sit to the LEFT of Copy in that order:
		//   [ 🗑 ][ ✎ ][ 📋 ]
		// Same 28×22 box, 4 px gap. Only visible when ActionCallback
		// is wired so we don't show dead UI for embedded use.
		bool BubbleActionsActive() const {
			return (bool)ActionCallback();
		}
		D2D1_RECT_F EditButtonRect() const {
			const float gap = 4.0f, w = 28.0f, h = 22.0f, pad = 6.0f;
			D2D1_RECT_F copy = CopyButtonRect();
			return D2D1::RectF(
				copy.left - gap - w, m_bounds.top + pad,
				copy.left - gap,     m_bounds.top + pad + h);
		}
		D2D1_RECT_F DeleteButtonRect() const {
			const float gap = 4.0f, w = 28.0f, h = 22.0f, pad = 6.0f;
			D2D1_RECT_F edit = EditButtonRect();
			return D2D1::RectF(
				edit.left - gap - w, m_bounds.top + pad,
				edit.left - gap,     m_bounds.top + pad + h);
		}
		D2D1_RECT_F RestartButtonRect() const {
			const float gap = 4.0f, w = 28.0f, h = 22.0f, pad = 6.0f;
			D2D1_RECT_F del = DeleteButtonRect();
			return D2D1::RectF(
				del.left - gap - w, m_bounds.top + pad,
				del.left - gap,     m_bounds.top + pad + h);
		}
		bool HitCopyButton(float x, float y) const {
			if (!m_hovered || IsStreaming() || m_text.empty()) return false;
			D2D1_RECT_F r = CopyButtonRect();
			return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
		}
		bool HitEditButton(float x, float y) const {
			if (!m_hovered || IsStreaming() || !BubbleActionsActive()) return false;
			D2D1_RECT_F r = EditButtonRect();
			return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
		}
		bool HitDeleteButton(float x, float y) const {
			if (!m_hovered || IsStreaming() || !BubbleActionsActive()) return false;
			D2D1_RECT_F r = DeleteButtonRect();
			return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
		}
		bool HitRestartButton(float x, float y) const {
			if (!m_hovered || IsStreaming() || !BubbleActionsActive()) return false;
			D2D1_RECT_F r = RestartButtonRect();
			return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
		}

		void DrawCopyButton(ID2D1RenderTarget* pRT) const {
			D2D1_RECT_F r = CopyButtonRect();
			// Hover state intensifies the fill so the button feels "armed"
			// just before click. Border + glyph match.
			D2D1_COLOR_F fill, line;
			if (m_copyHover) {
				fill = (m_role == User)
					? D2D1::ColorF(D2D1::ColorF::White, 0.55f)
					: D2D1::ColorF(0xE5EBF2);
				line = (m_role == User)
					? D2D1::ColorF(D2D1::ColorF::White, 0.95f)
					: D2D1::ColorF(0x2563EB);
			} else {
				fill = (m_role == User)
					? D2D1::ColorF(D2D1::ColorF::White, 0.25f)
					: D2D1::ColorF(D2D1::ColorF::White, 0.85f);
				line = (m_role == User)
					? D2D1::ColorF(D2D1::ColorF::White, 0.55f)
					: D2D1::ColorF(0x8A8F98);
			}
			ComPtr<ID2D1SolidColorBrush> pFill, pLine;
			pRT->CreateSolidColorBrush(fill, &pFill);
			pRT->CreateSolidColorBrush(line, &pLine);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 6.0f, 6.0f);
			if (pFill) pRT->FillRoundedRectangle(rr, pFill.Get());
			if (pLine) pRT->DrawRoundedRectangle(rr, pLine.Get(), 1.0f);

			// Two overlapping rounded rects = a tiny "two-page" copy glyph.
			// Centered inside the button.
			float cx = (r.left + r.right) * 0.5f;
			float cy = (r.top  + r.bottom) * 0.5f;
			D2D1_RECT_F back  = D2D1::RectF(cx - 4.0f, cy - 6.0f, cx + 5.0f, cy + 3.0f);
			D2D1_RECT_F front = D2D1::RectF(cx - 5.0f, cy - 4.0f, cx + 4.0f, cy + 5.0f);
			D2D1_ROUNDED_RECT brr = D2D1::RoundedRect(back,  1.5f, 1.5f);
			D2D1_ROUNDED_RECT frr = D2D1::RoundedRect(front, 1.5f, 1.5f);
			if (pLine) {
				pRT->DrawRoundedRectangle(brr, pLine.Get(), 1.0f);
				pRT->DrawRoundedRectangle(frr, pLine.Get(), 1.0f);
			}

			// Hover label rendered BELOW the button (was floating to the
			// left — that ran off the bubble for narrow widths and
			// collided with the sibling Edit / Delete / Restart labels
			// once we added them). Same "below" convention for every
			// hover button via DrawBubbleLabelBelow.
			if (m_copyHover) {
				DrawBubbleLabelBelow(pRT, r, L"Copy",
					(m_role == User)
						? D2D1::ColorF(D2D1::ColorF::White, 0.95f)
						: D2D1::ColorF(0x2563EB));
			}
		}

		// Shared label drawer — used by Copy / Delete / Restart hover
		// states. Renders a centered, semi-bold mini-label directly
		// below the button rect. Centred-align stays inside the
		// bubble's right edge even at narrow widths because every
		// button is already inset 6 px from the bubble border.
		void DrawBubbleLabelBelow(ID2D1RenderTarget* pRT,
		                           const D2D1_RECT_F& btnRect,
		                           const wchar_t* text,
		                           D2D1_COLOR_F colour) const
		{
			ComPtr<IDWriteTextFormat> fmt;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				L"Segoe UI", NULL,
				DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-us", &fmt);
			if (!fmt) return;
			fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
			ComPtr<ID2D1SolidColorBrush> pTxt;
			pRT->CreateSolidColorBrush(colour, &pTxt);
			if (!pTxt) return;
			// Stretch the label rect a bit wider than the button so
			// 8-char words ("Restart") fit; horizontal-centred keeps
			// the visual relationship.
			float cx  = (btnRect.left + btnRect.right) * 0.5f;
			D2D1_RECT_F lab = D2D1::RectF(
				cx - 30.0f, btnRect.bottom + 2.0f,
				cx + 30.0f, btnRect.bottom + 16.0f);
			pRT->DrawText(text, (UINT32)wcslen(text), fmt.Get(),
				lab, pTxt.Get());
		}

		// Shared chrome for the Edit / Delete sibling buttons. Same
		// hover-fill convention as Copy: stronger fill + accent-coloured
		// glyph when armed. `glyphAccent` lets Delete go red on hover
		// without leaking that colour into the resting state.
		void DrawBubbleActionButton(ID2D1RenderTarget* pRT,
		                             const D2D1_RECT_F& r,
		                             bool hover,
		                             D2D1_COLOR_F glyphAccent,
		                             std::function<void(
		                                ID2D1RenderTarget*,
		                                ID2D1SolidColorBrush*,
		                                float cx, float cy)> drawGlyph) const
		{
			D2D1_COLOR_F fill, line;
			if (hover) {
				fill = (m_role == User)
					? D2D1::ColorF(D2D1::ColorF::White, 0.55f)
					: D2D1::ColorF(0xE5EBF2);
				line = glyphAccent;
			} else {
				fill = (m_role == User)
					? D2D1::ColorF(D2D1::ColorF::White, 0.25f)
					: D2D1::ColorF(D2D1::ColorF::White, 0.85f);
				line = (m_role == User)
					? D2D1::ColorF(D2D1::ColorF::White, 0.55f)
					: D2D1::ColorF(0x8A8F98);
			}
			ComPtr<ID2D1SolidColorBrush> pFill, pLine;
			pRT->CreateSolidColorBrush(fill, &pFill);
			pRT->CreateSolidColorBrush(line, &pLine);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 6.0f, 6.0f);
			if (pFill) pRT->FillRoundedRectangle(rr, pFill.Get());
			if (pLine) pRT->DrawRoundedRectangle(rr, pLine.Get(), 1.0f);
			if (pLine && drawGlyph) {
				float cx = (r.left + r.right) * 0.5f;
				float cy = (r.top  + r.bottom) * 0.5f;
				drawGlyph(pRT, pLine.Get(), cx, cy);
			}
		}

		void DrawEditButton(ID2D1RenderTarget* pRT) const {
			D2D1_RECT_F r = EditButtonRect();
			// Edit glyph = a small pencil: diagonal stroke + a short
			// tip mark. Drawn with three thin lines for a clean look
			// at 28×22 px.
			DrawBubbleActionButton(pRT, r, m_editHover,
				D2D1::ColorF(0x2563EB),
				[](ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
				    float cx, float cy)
			{
				// Pencil body diagonal (NE → SW)
				rt->DrawLine(D2D1::Point2F(cx - 5, cy + 5),
				             D2D1::Point2F(cx + 5, cy - 5),
				             br, 1.4f);
				// Pencil tip notch (small triangle hint)
				rt->DrawLine(D2D1::Point2F(cx - 5, cy + 5),
				             D2D1::Point2F(cx - 3, cy + 3),
				             br, 1.4f);
				rt->DrawLine(D2D1::Point2F(cx - 5, cy + 5),
				             D2D1::Point2F(cx - 6.5f, cy + 6.5f),
				             br, 1.0f);
				// Eraser end cap
				rt->DrawLine(D2D1::Point2F(cx + 3, cy - 5),
				             D2D1::Point2F(cx + 5, cy - 3),
				             br, 1.4f);
			});
		}

		void DrawDeleteButton(ID2D1RenderTarget* pRT) const {
			D2D1_RECT_F r = DeleteButtonRect();
			// Delete glyph = miniature trash can: lid line + body
			// rectangle + two vertical hint bars inside.
			DrawBubbleActionButton(pRT, r, m_deleteHover,
				D2D1::ColorF(0xDC2626),    // red accent on hover
				[](ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
				    float cx, float cy)
			{
				// Lid (horizontal line + tiny handle in the middle)
				rt->DrawLine(D2D1::Point2F(cx - 5, cy - 4),
				             D2D1::Point2F(cx + 5, cy - 4),
				             br, 1.4f);
				rt->DrawLine(D2D1::Point2F(cx - 1.5f, cy - 5.5f),
				             D2D1::Point2F(cx + 1.5f, cy - 5.5f),
				             br, 1.2f);
				// Body — slightly tapered rectangle drawn as 4 lines
				rt->DrawLine(D2D1::Point2F(cx - 4, cy - 3),
				             D2D1::Point2F(cx - 3.5f, cy + 5),
				             br, 1.2f);
				rt->DrawLine(D2D1::Point2F(cx + 4, cy - 3),
				             D2D1::Point2F(cx + 3.5f, cy + 5),
				             br, 1.2f);
				rt->DrawLine(D2D1::Point2F(cx - 3.5f, cy + 5),
				             D2D1::Point2F(cx + 3.5f, cy + 5),
				             br, 1.2f);
				// Two vertical hint bars (trash slots)
				rt->DrawLine(D2D1::Point2F(cx - 1.5f, cy - 1),
				             D2D1::Point2F(cx - 1.5f, cy + 3),
				             br, 1.0f);
				rt->DrawLine(D2D1::Point2F(cx + 1.5f, cy - 1),
				             D2D1::Point2F(cx + 1.5f, cy + 3),
				             br, 1.0f);
			});

			// Hover label "Delete" BELOW the button (was floating to
			// the left — caused overlap with the Edit / Copy chrome
			// and ran off narrow bubbles).
			if (m_deleteHover) {
				DrawBubbleLabelBelow(pRT, r, L"Delete",
					D2D1::ColorF(0xDC2626));
			}
		}

		void DrawRestartButton(ID2D1RenderTarget* pRT) const {
			D2D1_RECT_F r = RestartButtonRect();
			// Restart glyph = a counter-clockwise rounded arrow (↺).
			// Drawn as a partial circle (4 chord segments approximating
			// 270° of arc) + a small arrowhead. Keeps the chrome
			// readable at 28×22 px without leaning on a font glyph.
			DrawBubbleActionButton(pRT, r, m_restartHover,
				D2D1::ColorF(0xA855F7),    // purple accent on hover
				[](ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
				    float cx, float cy)
			{
				// Approximate a 3/4 circle starting at angle ~30°
				// (right-top), sweeping CCW through left to right-
				// bottom. Drawn as polyline so we don't pay for a
				// PathGeometry per paint.
				constexpr int N = 14;
				constexpr double kStart = -3.14159265 * 0.15;  // ~ -27°
				constexpr double kEnd   =  3.14159265 * 1.45;  // ~ 261°
				const double radius = 5.0;
				D2D1_POINT_2F prev = D2D1::Point2F(
					cx + (float)(radius * std::cos(kStart)),
					cy + (float)(radius * std::sin(kStart)));
				for (int i = 1; i <= N; ++i) {
					double t = kStart + (kEnd - kStart) * (double)i / N;
					D2D1_POINT_2F cur = D2D1::Point2F(
						cx + (float)(radius * std::cos(t)),
						cy + (float)(radius * std::sin(t)));
					rt->DrawLine(prev, cur, br, 1.4f);
					prev = cur;
				}
				// Arrowhead at the END (right-top start). Small "V"
				// pointing back into the arc direction so it reads as
				// "this arrow is coming around".
				D2D1_POINT_2F head = D2D1::Point2F(
					cx + (float)(radius * std::cos(kStart)),
					cy + (float)(radius * std::sin(kStart)));
				rt->DrawLine(head,
					D2D1::Point2F(head.x - 2.5f, head.y - 1.8f),
					br, 1.4f);
				rt->DrawLine(head,
					D2D1::Point2F(head.x - 0.6f, head.y + 3.0f),
					br, 1.4f);
			});

			if (m_restartHover) {
				DrawBubbleLabelBelow(pRT, r, L"Restart",
					D2D1::ColorF(0xA855F7));
			}
		}

		// El highlight de selección ahora se dibuja per-segment dentro de
		// DrawTextSpan / DrawCodeSegment, via DrawSelectionRange. El método
		// "global" antiguo ya no es necesario.
	};


	// ---------------------------------------------------------------------
	// VCodeBlock — monospace block with dark theme. No syntax highlighting.
	// Optional language label drawn in the top-left corner.
	//
	// Selection / copy: same drag-to-select + Ctrl+C / Ctrl+A pattern as
	// VChatBubble, but laid out with Consolas and NO_WRAP word wrapping.
	// Right-click pops a Copy / Select All menu; a hover-revealed copy button
	// in the top-right corner copies the entire block.
	// ---------------------------------------------------------------------
	class VCodeBlock : public VirtualWidgetImpl {
		std::wstring m_code;
		std::wstring m_language;
		float        m_fontSize = 13.0f;
		float        m_padding  = 14.0f;
		float        m_radius   = 10.0f;
		float        m_labelHeight = 22.0f;

		D2D1_COLOR_F m_bgColor    = D2D1::ColorF(0x1E1E1E);
		D2D1_COLOR_F m_textColor  = D2D1::ColorF(0xE6E6E6);
		D2D1_COLOR_F m_labelColor = D2D1::ColorF(0x9CA3AF);
		D2D1_COLOR_F m_selectionFill = D2D1::ColorF(0x375A88);   // VS Code dark selection blue

		// Selection state — mirror of VChatBubble.
		size_t m_caret  = 0;
		size_t m_anchor = 0;
		bool   m_dragging = false;
		bool   m_copyHover = false;   // see VChatBubble for semantics

	public:
		const char* GetTypeName() const override { return "VCodeBlock"; }
		bool CanFocus() const override { return true; }

		VCodeBlock& SetCode(const std::wstring& s)     { m_code = s; m_caret = m_anchor = 0; return *this; }
		VCodeBlock& SetLanguage(const std::wstring& s) { m_language = s; return *this; }
		VCodeBlock& SetFontSize(float px)              { m_fontSize = px; return *this; }

		float MeasureHeight(float externalWidth) const {
			float innerWidth = externalWidth - (m_padding * 2.0f);
			if (innerWidth < 40.0f) innerWidth = 40.0f;
			float textH = MeasureWrappedTextHeight(m_code, L"Consolas",
				m_fontSize, DWRITE_FONT_WEIGHT_NORMAL, innerWidth);
			return textH + (m_padding * 2.0f) + (m_language.empty() ? 0.0f : m_labelHeight);
		}

		void OnDraw(ID2D1RenderTarget* pRT) override {
			D2D1_RECT_F r = m_bounds;

			ComPtr<ID2D1SolidColorBrush> pBg;
			pRT->CreateSolidColorBrush(m_bgColor, &pBg);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, m_radius, m_radius);
			if (pBg) pRT->FillRoundedRectangle(rr, pBg.Get());

			float textTop = r.top + m_padding;
			if (!m_language.empty()) {
				ComPtr<IDWriteTextFormat> pLangFormat;
				ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
					L"Segoe UI", NULL,
					DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
					11.0f, L"en-us", &pLangFormat);
				if (pLangFormat) {
					pLangFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
					ComPtr<ID2D1SolidColorBrush> pLangBrush;
					pRT->CreateSolidColorBrush(m_labelColor, &pLangBrush);
					if (pLangBrush) {
						D2D1_RECT_F labelRect = D2D1::RectF(
							r.left + m_padding, r.top + 6.0f,
							r.right - m_padding, r.top + 6.0f + m_labelHeight);
						pRT->DrawText(m_language.c_str(), (UINT32)m_language.size(),
							pLangFormat.Get(), labelRect, pLangBrush.Get());
					}
				}
				textTop += m_labelHeight;
			}

			if (m_code.empty()) return;
			ComPtr<IDWriteTextFormat> pTextFormat;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				L"Consolas", NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				m_fontSize, L"en-us", &pTextFormat);
			if (!pTextFormat) return;
			pTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
			pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
			pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

			D2D1_RECT_F inner = D2D1::RectF(
				r.left + m_padding,  textTop,
				r.right - m_padding, r.bottom - m_padding);

			// One layout, shared between selection highlight + glyph paint, so
			// they can't drift sub-pixel — same trick as VChatBubble.
			ComPtr<IDWriteTextLayout> layout;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
				m_code.c_str(), (UINT32)m_code.size(),
				pTextFormat.Get(),
				inner.right - inner.left, inner.bottom - inner.top, &layout);

			if (layout && HasSelection()) DrawSelectionHighlight(pRT, layout.Get(), inner);

			ComPtr<ID2D1SolidColorBrush> pTextBrush;
			pRT->CreateSolidColorBrush(m_textColor, &pTextBrush);
			if (pTextBrush) {
				if (layout) {
					pRT->DrawTextLayout(D2D1::Point2F(inner.left, inner.top),
						layout.Get(), pTextBrush.Get());
				} else {
					pRT->DrawText(m_code.c_str(), (UINT32)m_code.size(),
						pTextFormat.Get(), inner, pTextBrush.Get());
				}
			}

			if (m_hovered && !m_code.empty()) DrawCopyButton(pRT);
		}

		// --- Input ----------------------------------------------------------
		VInputResult OnMouseEnter() override { m_hovered = true;  return VInputResult::Handled; }
		VInputResult OnMouseLeave() override {
			m_hovered   = false;
			m_dragging  = false;
			m_copyHover = false;
			return VInputResult::Handled;
		}

		VInputResult OnMouseDown(float x, float y, int btn) override {
			if (btn == 1) {
				if (HitCopyButton(x, y)) {
					SetClipboardWideText(m_code);
					return VInputResult::Handled;
				}
				size_t pos = HitTestToCaret(x, y);
				m_caret = pos;
				if (!(GetKeyState(VK_SHIFT) & 0x8000)) m_anchor = pos;
				m_dragging = true;
				return VInputResult::Capture;
			}
			if (btn == 2) return VInputResult::Handled;  // wait for UP
			return VInputResult::NotHandled;
		}
		VInputResult OnMouseMove(float x, float y) override {
			bool wasCopyHover = m_copyHover;
			m_copyHover = HitCopyButton(x, y);
			if (!m_dragging) {
				return (wasCopyHover != m_copyHover)
					? VInputResult::Handled : VInputResult::NotHandled;
			}
			m_caret = HitTestToCaret(x, y);
			return VInputResult::Handled;
		}
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn == 1) {
				if (m_dragging) m_caret = HitTestToCaret(x, y);
				m_dragging = false;
				return VInputResult::Handled;
			}
			if (btn == 2) {
				POINT scr = CursorScreenPos();
				int cmd = ShowCopyContextMenu(GetActiveWindow(), scr.x, scr.y,
					HasSelection(), !m_code.empty());
				if (cmd == kCtxCopy) {
					CopySelectionToClipboard();
				} else if (cmd == kCtxSelectAll) {
					m_anchor = 0;
					m_caret  = m_code.size();
				}
				return VInputResult::Handled;
			}
			return VInputResult::NotHandled;
		}

		VInputResult OnKeyDown(UINT vk) override {
			const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
			const bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
			switch (vk) {
			case 'A':
				if (ctrl) { m_anchor = 0; m_caret = m_code.size(); return VInputResult::Handled; }
				break;
			case 'C':
				if (ctrl) { if (HasSelection()) CopySelectionToClipboard(); return VInputResult::Handled; }
				break;
			case VK_LEFT:
				if (m_caret > 0) m_caret--;
				if (!shift) m_anchor = m_caret;
				return VInputResult::Handled;
			case VK_RIGHT:
				if (m_caret < m_code.size()) m_caret++;
				if (!shift) m_anchor = m_caret;
				return VInputResult::Handled;
			case VK_HOME:
				m_caret = 0;
				if (!shift) m_anchor = m_caret;
				return VInputResult::Handled;
			case VK_END:
				m_caret = m_code.size();
				if (!shift) m_anchor = m_caret;
				return VInputResult::Handled;
			case VK_ESCAPE:
				m_anchor = m_caret;
				return VInputResult::Handled;
			}
			return VInputResult::NotHandled;
		}

	private:
		bool   HasSelection() const {
			size_t a = m_anchor, b = m_caret;
			if (a > m_code.size()) a = m_code.size();
			if (b > m_code.size()) b = m_code.size();
			return a != b;
		}
		size_t SelStart() const {
			size_t a = m_anchor > m_code.size() ? m_code.size() : m_anchor;
			size_t b = m_caret  > m_code.size() ? m_code.size() : m_caret;
			return (a < b) ? a : b;
		}
		size_t SelEnd() const {
			size_t a = m_anchor > m_code.size() ? m_code.size() : m_anchor;
			size_t b = m_caret  > m_code.size() ? m_code.size() : m_caret;
			return (a < b) ? b : a;
		}
		void CopySelectionToClipboard() const {
			if (!HasSelection()) return;
			SetClipboardWideText(m_code.substr(SelStart(), SelEnd() - SelStart()));
		}

		// Map a code-block-local mouse position to a character index in m_code.
		// Layout uses Consolas + NO_WRAP — same parameters as paint so caret
		// positions match what the user sees.
		size_t HitTestToCaret(float clickX, float clickY) const {
			if (m_code.empty()) return 0;
			ComPtr<IDWriteTextFormat> fmt;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				L"Consolas", NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				m_fontSize, L"en-us", &fmt);
			if (!fmt) return m_code.size();
			fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

			float textTopOffset = m_padding + (m_language.empty() ? 0.0f : m_labelHeight);
			float innerW = (m_bounds.right - m_bounds.left) - (m_padding * 2.0f);
			float innerH = (m_bounds.bottom - m_bounds.top) - textTopOffset - m_padding;
			ComPtr<IDWriteTextLayout> layout;
			if (FAILED(ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
				m_code.c_str(), (UINT32)m_code.size(), fmt.Get(),
				innerW, innerH, &layout)) || !layout) return m_code.size();

			float relX = clickX - (m_bounds.left + m_padding);
			float relY = clickY - (m_bounds.top  + textTopOffset);
			if (relX < 0) relX = 0;
			if (relY < 0) relY = 0;
			BOOL isTrailing = FALSE, isInside = FALSE;
			DWRITE_HIT_TEST_METRICS hm{};
			if (FAILED(layout->HitTestPoint(relX, relY, &isTrailing, &isInside, &hm)))
				return m_code.size();
			size_t pos = hm.textPosition + (isTrailing ? hm.length : 0);
			if (pos > m_code.size()) pos = m_code.size();
			return pos;
		}

		void DrawSelectionHighlight(ID2D1RenderTarget* pRT, IDWriteTextLayout* layout,
			const D2D1_RECT_F& inner) const
		{
			UINT32 a = (UINT32)SelStart();
			UINT32 len = (UINT32)(SelEnd() - SelStart());
			if (len == 0) return;
			UINT32 actualCount = 0;
			layout->HitTestTextRange(a, len, 0.0f, 0.0f, nullptr, 0, &actualCount);
			if (actualCount == 0) return;
			std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
			if (FAILED(layout->HitTestTextRange(a, len, 0.0f, 0.0f,
				metrics.data(), actualCount, &actualCount))) return;
			ComPtr<ID2D1SolidColorBrush> pSel;
			pRT->CreateSolidColorBrush(m_selectionFill, &pSel);
			if (!pSel) return;
			for (const auto& m : metrics) {
				D2D1_RECT_F sr = D2D1::RectF(
					inner.left + m.left,
					inner.top  + m.top,
					inner.left + m.left + m.width,
					inner.top  + m.top  + m.height);
				pRT->FillRectangle(sr, pSel.Get());
			}
		}

		// Hover-revealed copy button — same visual treatment as VChatBubble but
		// pre-tinted for the dark code background.
		D2D1_RECT_F CopyButtonRect() const {
			const float pad = 6.0f, w = 28.0f, h = 22.0f;
			return D2D1::RectF(
				m_bounds.right - pad - w, m_bounds.top + pad,
				m_bounds.right - pad,     m_bounds.top + pad + h);
		}
		bool HitCopyButton(float x, float y) const {
			if (!m_hovered || m_code.empty()) return false;
			D2D1_RECT_F r = CopyButtonRect();
			return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
		}
		void DrawCopyButton(ID2D1RenderTarget* pRT) const {
			D2D1_RECT_F r = CopyButtonRect();
			// Hover: brighter fill + accent border so it pops against the
			// dark code background.
			D2D1_COLOR_F fill = m_copyHover
				? D2D1::ColorF(0x3B82F6, 0.25f)
				: D2D1::ColorF(0xFFFFFF, 0.10f);
			D2D1_COLOR_F line = m_copyHover
				? D2D1::ColorF(0x60A5FA)
				: D2D1::ColorF(0xCFD3D8, 0.85f);
			ComPtr<ID2D1SolidColorBrush> pFill, pLine;
			pRT->CreateSolidColorBrush(fill, &pFill);
			pRT->CreateSolidColorBrush(line, &pLine);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 6.0f, 6.0f);
			if (pFill) pRT->FillRoundedRectangle(rr, pFill.Get());
			if (pLine) pRT->DrawRoundedRectangle(rr, pLine.Get(), 1.0f);
			float cx = (r.left + r.right) * 0.5f;
			float cy = (r.top  + r.bottom) * 0.5f;
			D2D1_RECT_F back  = D2D1::RectF(cx - 4.0f, cy - 6.0f, cx + 5.0f, cy + 3.0f);
			D2D1_RECT_F front = D2D1::RectF(cx - 5.0f, cy - 4.0f, cx + 4.0f, cy + 5.0f);
			D2D1_ROUNDED_RECT brr = D2D1::RoundedRect(back,  1.5f, 1.5f);
			D2D1_ROUNDED_RECT frr = D2D1::RoundedRect(front, 1.5f, 1.5f);
			if (pLine) {
				pRT->DrawRoundedRectangle(brr, pLine.Get(), 1.0f);
				pRT->DrawRoundedRectangle(frr, pLine.Get(), 1.0f);
			}
			// "Copy" inline label on hover — same idea as VChatBubble.
			if (m_copyHover) {
				ComPtr<IDWriteTextFormat> fmt;
				ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
					L"Segoe UI", NULL,
					DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
					11.0f, L"en-us", &fmt);
				if (fmt) {
					fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
					fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
					D2D1_RECT_F lab = D2D1::RectF(
						r.left - 52.0f, r.top,
						r.left - 6.0f,  r.bottom);
					ComPtr<ID2D1SolidColorBrush> pTxt;
					pRT->CreateSolidColorBrush(D2D1::ColorF(0x60A5FA), &pTxt);
					if (pTxt) pRT->DrawText(L"Copy", 4, fmt.Get(), lab, pTxt.Get());
				}
			}
		}
	};


	// ---------------------------------------------------------------------
	// VTypingIndicator — three dots, each bouncing on a phase-shifted sine
	// wave. Drives itself off the heartbeat (returns true from OnUpdate).
	// Position the bubble where you want the indicator to appear; the dots
	// are drawn inside m_bounds.
	// ---------------------------------------------------------------------
	class VTypingIndicator : public VirtualWidgetImpl {
		float m_time = 0.0f;
		D2D1_COLOR_F m_fill   = D2D1::ColorF(0xF1F1F2);
		D2D1_COLOR_F m_dotCol = D2D1::ColorF(0x4A5567);
	public:
		const char* GetTypeName() const override { return "VTypingIndicator"; }

		bool OnUpdate(float dt) override {
			m_time += dt;
			return true;  // animated every frame while visible
		}

		void OnDraw(ID2D1RenderTarget* pRT) override {
			// Background bubble — match the assistant bubble palette.
			ComPtr<ID2D1SolidColorBrush> pFill;
			pRT->CreateSolidColorBrush(m_fill, &pFill);
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(m_bounds, 12.0f, 12.0f);
			if (pFill) pRT->FillRoundedRectangle(rr, pFill.Get());

			// --- Three bouncing mini-suricatas (minimalist mascot) ----------
			// Replaces the earlier full-body sentinel — that one dominated
			// the typing bubble. Each glyph is a tiny upright capsule (the
			// standing body silhouette) with two dot-ears on top: enough
			// shape to read as "meerkat", small enough to feel like the
			// classic three-bouncing-dots indicator. Phase-shifted sine
			// bounce; brand gradient fill.
			const float W  = m_bounds.right - m_bounds.left;
			const float Hh = m_bounds.bottom - m_bounds.top;
			if (W < 8.0f || Hh < 8.0f) return;
			const float cx = m_bounds.left + W  * 0.5f;
			const float cy = m_bounds.top  + Hh * 0.5f;

			// Glyph geometry. Tuned so the strip is roughly the same
			// visual weight as three dots at this bubble size, but the
			// silhouette still reads as a tiny standing meerkat.
			const float glyphW    = 6.0f;
			const float glyphH    = 11.0f;
			const float gap       = 8.0f;
			const float earR      = 1.5f;
			const float bounceAmp = 3.5f;

			// Brand gradient brush spans the full bounce travel so each
			// glyph picks up its slice of the teal→violet→pink ramp as
			// it hops. Falls back to a flat violet if creation fails.
			ComPtr<ID2D1Brush> body;
			{
				D2D1_GRADIENT_STOP gs[3] = {
					{ 0.00f, D2D1::ColorF(0x21E6C1) },
					{ 0.55f, D2D1::ColorF(0x7C5CFF) },
					{ 1.00f, D2D1::ColorF(0xFF5DA2) },
				};
				ComPtr<ID2D1GradientStopCollection> stops;
				pRT->CreateGradientStopCollection(gs, 3, &stops);
				if (stops) {
					ComPtr<ID2D1LinearGradientBrush> lg;
					pRT->CreateLinearGradientBrush(
						D2D1::LinearGradientBrushProperties(
							D2D1::Point2F(cx, cy - glyphH),
							D2D1::Point2F(cx, cy + glyphH)),
						stops.Get(), &lg);
					if (lg) body = lg;
				}
				if (!body) {
					ComPtr<ID2D1SolidColorBrush> flat;
					pRT->CreateSolidColorBrush(D2D1::ColorF(0x7C5CFF), &flat);
					if (flat) body = flat;
				}
			}
			if (!body) return;

			// Three glyphs centered horizontally; each takes its own
			// phase offset so the hop staggers across the strip like
			// the classic typing dots.
			const float pitch = glyphW + gap;
			for (int i = 0; i < 3; ++i) {
				float gx = cx + ((float)i - 1.0f) * pitch;
				// Sine wave; damp the negative half so the "landing"
				// reads as ground contact instead of a symmetric sway.
				float phase = m_time * 4.0f + (float)i * 0.7f;
				float lift  = std::sin(phase);
				if (lift < 0.0f) lift *= 0.20f;
				float gy = cy - lift * bounceAmp;

				// Body: tiny upright capsule.
				D2D1_ROUNDED_RECT b = D2D1::RoundedRect(
					D2D1::RectF(gx - glyphW * 0.5f, gy - glyphH * 0.5f,
					            gx + glyphW * 0.5f, gy + glyphH * 0.5f),
					glyphW * 0.5f, glyphW * 0.5f);
				pRT->FillRoundedRectangle(b, body.Get());

				// Two ears — small dots sitting on top of the head.
				pRT->FillEllipse(D2D1::Ellipse(
					D2D1::Point2F(gx - glyphW * 0.42f,
					               gy - glyphH * 0.55f),
					earR, earR), body.Get());
				pRT->FillEllipse(D2D1::Ellipse(
					D2D1::Point2F(gx + glyphW * 0.42f,
					               gy - glyphH * 0.55f),
					earR, earR), body.Get());
			}
		}
	};


	// ---------------------------------------------------------------------
	// VChatInput — single-line text editor for the chat bottom strip.
	//
	// What it does:
	//   * Click to position caret. Drag to position too.
	//   * Type to insert characters (WM_CHAR).
	//   * Backspace / Delete remove characters.
	//   * Left / Right / Home / End navigate.
	//   * Ctrl+V pastes from the clipboard.
	//   * Enter fires OnSubmit(text) and clears the input.
	//   * Caret blinks at 2Hz while focused.
	//   * Placeholder text shown when empty + unfocused.
	//
	// What it deliberately does NOT do (deferred for later widgets):
	//   * Selection / shift-arrow / mouse-drag-select / cut / copy.
	//   * Multi-line. (Shift+Enter could submit-with-newline in a future pass.)
	//   * IME composition. Latin keyboards work; CJK input is broken.
	//   * Undo / redo.
	// ---------------------------------------------------------------------
	class VChatInput : public VirtualWidgetImpl {
		std::wstring m_text;
		std::wstring m_placeholder = L"Type a message…";

		// Caret = the active end (where typing inserts / arrows move from).
		// Anchor = the other end of the selection. They're equal when there is
		// no selection. Mouse drag and Shift+keys move the caret while pinning
		// the anchor; a plain click or arrow collapses the anchor onto the caret.
		size_t       m_caret  = 0;
		size_t       m_anchor = 0;
		bool         m_dragging = false;   // true between OnMouseDown and OnMouseUp

		float        m_padding   = 14.0f;
		float        m_fontSize  = 14.0f;
		float        m_radius    = 10.0f;

		// Font family for the field text. Default Segoe UI (chat/UI inputs); set
		// to L"Consolas" for a monospaced code/JSON editor. This widget only.
		std::wstring m_fontFamily = L"Segoe UI";

		// Modos:
		//   * SingleLine: NO_WRAP, paragraph alignment CENTER, hscroll horizontal
		//     interno que sigue al caret. Ideal para URLs, números, etc.
		//   * Multi-line (default): WRAP, paragraph NEAR. Crece verticalmente
		//     con el contenido hasta MaxLines; pasados los MaxLines mantiene
		//     altura fija y aparece scroll vertical interno.
		bool  m_singleLine = false;
		int   m_maxLines   = 0;     // 0 = ilimitado
		bool  m_password   = false; // render bullets instead of the real text

		// Scroll interno del editor. m_scrollX para single-line (px), m_scrollY
		// para multi-line (px). Mantenido vía ScrollCaretIntoView() tras cada
		// movimiento del caret o edición.
		float m_scrollX = 0.0f;
		float m_scrollY = 0.0f;

		// Blink state — separate from m_focused so we can pause blinking
		// briefly after typing (caret stays visible while the user is active).
		float m_blinkAccum   = 0.0f;
		bool  m_caretVisible = true;
		float m_freshTypingS = 0.0f;   // seconds since last keystroke

		D2D1_COLOR_F m_bg          = D2D1::ColorF(0xF7F8F9);
		D2D1_COLOR_F m_bgFocused   = D2D1::ColorF(0xFFFFFF);
		D2D1_COLOR_F m_border      = D2D1::ColorF(0xD8DBDF);
		D2D1_COLOR_F m_borderFocus = D2D1::ColorF(0x2563EB);
		D2D1_COLOR_F m_textColor   = D2D1::ColorF(0x111111);
		D2D1_COLOR_F m_placeholderColor = D2D1::ColorF(0x9AA0A6);
		D2D1_COLOR_F m_selectionFill    = D2D1::ColorF(0xB4D5FE);  // Office-blue selection

		std::function<void(const std::wstring&)> m_onSubmit;
		std::function<void()> m_onTextChanged;   // fires after every text mutation

	public:
		const char* GetTypeName() const override { return "VChatInput"; }
		bool CanFocus() const override { return true; }

		VChatInput& SetText(const std::wstring& s) {
			m_text = s;
			if (m_caret > m_text.size()) m_caret = m_text.size();
			m_anchor = m_caret;          // collapse selection on programmatic set
			NotifyTextChanged();         // scrollea el caret a la vista y notifica
			return *this;
		}
		const std::wstring& GetText() const { return m_text; }

		// Insert text at the caret (replacing any selection); advances the caret and
		// notifies. Used by pickers that drop a token (e.g. an @column) into a prompt.
		VChatInput& InsertAtCaret(const std::wstring& s) {
			size_t a = (m_caret < m_anchor) ? m_caret : m_anchor;
			size_t b = (m_caret < m_anchor) ? m_anchor : m_caret;
			if (a != b) { m_text.erase(a, b - a); m_caret = a; }   // replace selection
			if (m_caret > m_text.size()) m_caret = m_text.size();
			m_text.insert(m_caret, s);
			m_caret += s.size(); m_anchor = m_caret;
			NotifyTextChanged();
			return *this;
		}

		VChatInput& SetPlaceholder(const std::wstring& s) { m_placeholder = s; return *this; }
		VChatInput& SetFontSize(float px) { m_fontSize = px; return *this; }
		// Opt-in monospace (or any family). Empty -> Segoe UI. Default unchanged,
		// so existing chat inputs are unaffected.
		VChatInput& SetFontFamily(const std::wstring& f) { m_fontFamily = f.empty() ? L"Segoe UI" : f; return *this; }

		// Single-line: el input no crece verticalmente; texto en una línea con
		// centrado vertical y scroll horizontal si excede el ancho.
		VChatInput& SetSingleLine(bool v) { m_singleLine = v; m_scrollY = 0.0f; return *this; }
		// Password mode: the field still STORES the real text (GetText returns
		// it) but renders one bullet per character. Caret/selection indices map
		// 1:1 (one bullet per char), so editing still works.
		VChatInput& SetPassword(bool v) { m_password = v; return *this; }
		bool IsPassword() const { return m_password; }
		// Máximo de líneas en modo multi-line. 0 = sin límite (crece infinitamente).
		// Pasado el límite, el input mantiene su altura y aparece scroll vertical
		// interno; el caret sigue siendo visible.
		VChatInput& SetMaxLines(int n) { m_maxLines = (n < 0) ? 0 : n; return *this; }

		// Called when the user presses Enter (the input also auto-clears).
		void OnSubmit(std::function<void(const std::wstring&)> cb) { m_onSubmit = std::move(cb); }

		// Fired after every text mutation (typing, paste, backspace, etc.).
		// Demos hook this to re-run their layout pass so the input can grow
		// upward as wrapping requires more vertical space.
		void OnTextChanged(std::function<void()> cb) { m_onTextChanged = std::move(cb); }

		// Altura que el input necesita al ancho dado. Comportamiento por modo:
		//   * SingleLine: siempre una sola línea + padding. No crece.
		//   * Multi-line: alto del contenido (wrapped) + padding, capado por
		//     MaxLines * lineHeight + padding si MaxLines > 0.
		float MeasureHeight(float externalWidth) const {
			float innerW = externalWidth - (m_padding * 2.0f);
			if (innerW < 40.0f) innerW = 40.0f;
			float lineH = OneLineHeight();
			if (m_singleLine) {
				return lineH + (m_padding * 2.0f);
			}
			float contentH = ContentHeightWrapped(innerW);
			if (m_maxLines > 0) {
				float maxH = lineH * (float)m_maxLines;
				if (contentH > maxH) contentH = maxH;
			}
			if (contentH < lineH) contentH = lineH;
			return contentH + (m_padding * 2.0f);
		}

		// Altura de una sola línea con la fuente actual. Cacheable, pero el
		// cálculo es barato (~20µs) y evita state que se desincronice.
		float OneLineHeight() const {
			ComPtr<IDWriteTextFormat> fmt;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				m_fontFamily.c_str(), NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				m_fontSize, L"en-us", &fmt);
			if (!fmt) return m_fontSize * 1.2f;
			ComPtr<IDWriteTextLayout> layout;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
				L" ", 1, fmt.Get(), 1024.0f, 4096.0f, &layout);
			if (!layout) return m_fontSize * 1.2f;
			DWRITE_TEXT_METRICS m{};
			layout->GetMetrics(&m);
			return m.height;
		}

		// Altura del texto envuelto al ancho dado, con la misma config del paint.
		float ContentHeightWrapped(float innerW) const {
			const wchar_t* probe = m_text.empty() ? L" " : m_text.c_str();
			UINT32 len = m_text.empty() ? 1 : (UINT32)m_text.size();
			ComPtr<IDWriteTextFormat> fmt;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				m_fontFamily.c_str(), NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				m_fontSize, L"en-us", &fmt);
			if (!fmt) return m_fontSize * 1.2f;
			fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
			ComPtr<IDWriteTextLayout> layout;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
				probe, len, fmt.Get(), innerW, 65535.0f, &layout);
			if (!layout) return m_fontSize * 1.2f;
			DWRITE_TEXT_METRICS m{};
			layout->GetMetrics(&m);
			return m.height;
		}

		// --- Heartbeat: blink the caret while focused ---
		bool OnUpdate(float dt) override {
			if (!m_focused) return false;
			m_freshTypingS += dt;
			m_blinkAccum   += dt;
			// Keep the caret solid for 0.4s after a keystroke so it's easy to follow.
			if (m_freshTypingS < 0.4f) {
				if (!m_caretVisible) { m_caretVisible = true; return true; }
				return false;
			}
			if (m_blinkAccum >= 0.5f) {
				m_blinkAccum = 0.0f;
				m_caretVisible = !m_caretVisible;
				return true;
			}
			return false;
		}

		// --- Paint ---
		// Modos:
		//   single-line → DWRITE_WORD_WRAPPING_NO_WRAP + PARAGRAPH_ALIGNMENT_CENTER
		//                  + scroll horizontal interno (m_scrollX).
		//   multi-line  → WORD_WRAPPING_WRAP + PARAGRAPH_ALIGNMENT_NEAR
		//                  + scroll vertical interno (m_scrollY) cuando el
		//                    contenido excede el alto del input.
		// El texto se dibuja dentro de un PushAxisAlignedClip al rect interno
		// para que el scroll no se vea desbordado en los bordes del input.
		void OnDraw(ID2D1RenderTarget* pRT) override {
			D2D1_RECT_F r = m_bounds;

			// Background + border. Border thickens + recolors when focused.
			D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, m_radius, m_radius);
			ComPtr<ID2D1SolidColorBrush> pBg;
			pRT->CreateSolidColorBrush(m_focused ? m_bgFocused : m_bg, &pBg);
			if (pBg) pRT->FillRoundedRectangle(rr, pBg.Get());
			ComPtr<ID2D1SolidColorBrush> pBorder;
			pRT->CreateSolidColorBrush(m_focused ? m_borderFocus : m_border, &pBorder);
			if (pBorder) pRT->DrawRoundedRectangle(rr, pBorder.Get(), m_focused ? 1.5f : 1.0f);

			bool showPlaceholder = m_text.empty() && !m_focused;
			// Password mode renders one bullet per character. Same length as
			// m_text so caret/selection positions stay valid.
			std::wstring masked;
			if (m_password && !m_text.empty())
				masked.assign(m_text.size(), L'\x2022');   // •
			const std::wstring& visible = showPlaceholder ? m_placeholder
			                            : (m_password ? masked : m_text);

			D2D1_RECT_F inner = D2D1::RectF(
				r.left + m_padding,  r.top + m_padding,
				r.right - m_padding, r.bottom - m_padding);

			ComPtr<IDWriteTextFormat> pTextFormat;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				m_fontFamily.c_str(), NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				m_fontSize, L"en-us", &pTextFormat);
			if (!pTextFormat) return;
			if (m_singleLine) {
				pTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
				pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			} else {
				pTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
				pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
			}
			pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

			// Layout sobre el contenido (o placeholder). El maxWidth se usa
			// para el wrap en multi-line; en single-line da igual lo grande
			// que sea — DWrite no parte líneas con NO_WRAP.
			float layoutW = m_singleLine ? 65535.0f : (inner.right - inner.left);
			ComPtr<IDWriteTextLayout> layout;
			if (!visible.empty()) {
				ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
					visible.c_str(), (UINT32)visible.size(),
					pTextFormat.Get(), layoutW, 65535.0f, &layout);
			}

			// Origin del layout en pantalla, descontando el scroll interno.
			float drawX = inner.left - m_scrollX;
			float drawY = inner.top  - m_scrollY;
			if (m_singleLine) {
				// PARAGRAPH_ALIGNMENT_CENTER centra dentro del maxHeight del
				// layout. Usamos el alto del CONTROL completo (no inner) para
				// que aunque el inputH del caller sea justo, el texto quede
				// vertically centrado respecto al input, no metido contra el
				// padding superior.
				if (layout) layout->SetMaxHeight(r.bottom - r.top);
				drawY = r.top;
			}

			// Clip al rect del CONTROL (no al inner) para que el texto no
			// rebose por los bordes redondeados. Si clippeáramos al inner,
			// un padding generoso vs un control de altura justa podría
			// recortar el propio texto verticalmente — lo que queremos es
			// recortar SÓLO en los bordes externos del input.
			pRT->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);

			// Selección bajo el texto.
			if (layout && m_focused && !showPlaceholder && HasSelection()) {
				DrawSelectionAtOrigin(pRT, layout.Get(), drawX, drawY);
			}

			// Texto / placeholder.
			ComPtr<ID2D1SolidColorBrush> pTextBrush;
			pRT->CreateSolidColorBrush(showPlaceholder ? m_placeholderColor : m_textColor, &pTextBrush);
			if (pTextBrush && layout) {
				pRT->DrawTextLayout(D2D1::Point2F(drawX, drawY), layout.Get(), pTextBrush.Get());
			}

			// Caret.
			if (m_focused && m_caretVisible && !showPlaceholder && !HasSelection()) {
				ComPtr<IDWriteTextLayout> caretLayout = layout;
				if (!caretLayout) {
					ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
						L" ", 1, pTextFormat.Get(), layoutW, 65535.0f, &caretLayout);
					if (m_singleLine && caretLayout) caretLayout->SetMaxHeight(r.bottom - r.top);
				}
				if (caretLayout) {
					DWRITE_HIT_TEST_METRICS metrics{};
					FLOAT cX = 0, cY = 0;
					caretLayout->HitTestTextPosition((UINT32)m_caret, FALSE, &cX, &cY, &metrics);
					float caretX = drawX + cX;
					float caretYTop = drawY + cY;
					float caretYBot = caretYTop + metrics.height;
					ComPtr<ID2D1SolidColorBrush> pCaret;
					pRT->CreateSolidColorBrush(m_textColor, &pCaret);
					if (pCaret) {
						pRT->DrawLine(D2D1::Point2F(caretX, caretYTop),
						              D2D1::Point2F(caretX, caretYBot),
						              pCaret.Get(), 1.5f);
					}
				}
			}

			pRT->PopAxisAlignedClip();
		}

		// Dibuja highlight de selección con un layout cuyo origen está en
		// (originX, originY) — versión generalizada del helper original.
		void DrawSelectionAtOrigin(ID2D1RenderTarget* pRT, IDWriteTextLayout* layout,
			float originX, float originY) const
		{
			if (!HasSelection() || !layout) return;
			UINT32 a = (UINT32)SelStart();
			UINT32 len = (UINT32)(SelEnd() - SelStart());
			if (len == 0) return;
			UINT32 actualCount = 0;
			layout->HitTestTextRange(a, len, 0.0f, 0.0f, nullptr, 0, &actualCount);
			if (actualCount == 0) return;
			std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
			if (FAILED(layout->HitTestTextRange(a, len, 0.0f, 0.0f,
				metrics.data(), actualCount, &actualCount))) return;
			ComPtr<ID2D1SolidColorBrush> pSel;
			pRT->CreateSolidColorBrush(m_selectionFill, &pSel);
			if (!pSel) return;
			for (const auto& m : metrics) {
				D2D1_RECT_F sr = D2D1::RectF(
					originX + m.left,
					originY + m.top,
					originX + m.left + m.width,
					originY + m.top  + m.height);
				pRT->FillRectangle(sr, pSel.Get());
			}
		}

		// Tras un cambio de caret (typing, arrow keys, click), ajusta m_scrollX
		// (single-line) o m_scrollY (multi-line) para que el caret quede
		// visible dentro del rect interno.
		void ScrollCaretIntoView() {
			float innerW = (m_bounds.right - m_bounds.left) - (m_padding * 2.0f);
			float innerH = (m_bounds.bottom - m_bounds.top) - (m_padding * 2.0f);
			if (innerW <= 0 || innerH <= 0) return;
			ComPtr<IDWriteTextFormat> fmt;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				m_fontFamily.c_str(), NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				m_fontSize, L"en-us", &fmt);
			if (!fmt) return;
			fmt->SetWordWrapping(m_singleLine
				? DWRITE_WORD_WRAPPING_NO_WRAP : DWRITE_WORD_WRAPPING_WRAP);
			const wchar_t* probe = m_text.empty() ? L" " : m_text.c_str();
			UINT32 len = m_text.empty() ? 1 : (UINT32)m_text.size();
			ComPtr<IDWriteTextLayout> layout;
			float maxW = m_singleLine ? 65535.0f : innerW;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
				probe, len, fmt.Get(), maxW, 65535.0f, &layout);
			if (!layout) return;
			DWRITE_HIT_TEST_METRICS hm{};
			FLOAT cX = 0, cY = 0;
			layout->HitTestTextPosition((UINT32)m_caret, FALSE, &cX, &cY, &hm);

			if (m_singleLine) {
				const float margin = 4.0f;
				if (cX - m_scrollX < margin) m_scrollX = cX - margin;
				if (cX - m_scrollX > innerW - margin) m_scrollX = cX - innerW + margin;
				if (m_scrollX < 0.0f) m_scrollX = 0.0f;
			} else {
				const float margin = 2.0f;
				if (cY - m_scrollY < margin) m_scrollY = cY - margin;
				float caretBot = cY + hm.height;
				if (caretBot - m_scrollY > innerH - margin) m_scrollY = caretBot - innerH + margin;
				if (m_scrollY < 0.0f) m_scrollY = 0.0f;
			}
		}

		// --- Mouse ---
		VInputResult OnMouseDown(float x, float y, int btn) override {
			// btn == 2 → right click. UX shortcut for partner demos:
			// a right-click on an EMPTY input pastes the clipboard
			// content and selects all of it, so they can edit or
			// delete with one keystroke. Non-empty inputs ignore the
			// right click (future: open a proper context menu).
			if (btn == 2) {
				if (m_text.empty()) {
					PasteFromClipboard();
					m_anchor = 0;
					m_caret  = m_text.size();
					m_caretVisible = true;
					m_blinkAccum = 0.0f;
					ScrollCaretIntoView();
				}
				return VInputResult::Handled;
			}
			if (btn != 1) return VInputResult::NotHandled;
			size_t pos = HitTestToCaret(x, y);
			m_caret = pos;
			// Shift+click extends the existing selection; plain click collapses it.
			if (!(GetKeyState(VK_SHIFT) & 0x8000)) m_anchor = pos;
			m_dragging = true;
			m_caretVisible = true;
			m_blinkAccum = 0.0f;
			ScrollCaretIntoView();
			return VInputResult::Capture;  // host captures the mouse so drag works outside our bounds
		}

		// Mouse wheel scrolls the multi-line viewport. Single-line
		// mode (one-line URL/cron fields) returns NotHandled so the
		// wheel falls through to the host (e.g. the conversations
		// panel can keep scrolling when the wheel happens to land
		// over a single-line input).
		VInputResult OnMouseWheel(float delta, float, float) override {
			if (m_singleLine) return VInputResult::NotHandled;
			float lineH = OneLineHeight();
			if (lineH <= 0) lineH = m_fontSize * 1.2f;
			// One notch (120 units) = 3 lines, same convention as
			// VConversationsPanel / VRightPanel. Negative delta = scroll
			// down (content moves up = m_scrollY increases).
			float lines = -(delta / 120.0f) * 3.0f;
			m_scrollY += lines * lineH;
			if (m_scrollY < 0.0f) m_scrollY = 0.0f;
			// Clamp to the content height so we don't scroll past the end.
			float innerW = (m_bounds.right - m_bounds.left)
				- (m_padding * 2.0f);
			float innerH = (m_bounds.bottom - m_bounds.top)
				- (m_padding * 2.0f);
			if (innerW > 0 && innerH > 0) {
				float contentH = ContentHeightWrapped(innerW);
				float maxScroll = contentH > innerH
					? (contentH - innerH) : 0.0f;
				if (m_scrollY > maxScroll) m_scrollY = maxScroll;
			}
			return VInputResult::Handled;
		}
		VInputResult OnMouseMove(float x, float y) override {
			if (!m_dragging) return VInputResult::NotHandled;
			// Drag with button held: caret follows the cursor; anchor stays put,
			// forming the selection.
			m_caret = HitTestToCaret(x, y);
			ScrollCaretIntoView();
			return VInputResult::Handled;
		}
		VInputResult OnMouseUp(float x, float y, int btn) override {
			if (btn != 1) return VInputResult::NotHandled;
			if (m_dragging) m_caret = HitTestToCaret(x, y);
			m_dragging = false;
			ScrollCaretIntoView();
			return VInputResult::Handled;
		}

		// --- Keyboard ---
		VInputResult OnKeyDown(UINT vk) override {
			m_freshTypingS = 0.0f;
			m_caretVisible = true;
			const bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
			const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

			switch (vk) {
			case VK_LEFT:
				// Ctrl+Left = previous word boundary. Skips whitespace
				// then current word — matches Notepad / VSCode behaviour.
				if (ctrl) m_caret = WordLeftFrom(m_caret);
				else if (m_caret > 0) m_caret--;
				if (!shift) m_anchor = m_caret;
				ScrollCaretIntoView();
				return VInputResult::Handled;
			case VK_RIGHT:
				// Ctrl+Right = next word boundary. Same convention.
				if (ctrl) m_caret = WordRightFrom(m_caret);
				else if (m_caret < m_text.size()) m_caret++;
				if (!shift) m_anchor = m_caret;
				ScrollCaretIntoView();
				return VInputResult::Handled;
			case VK_HOME:
				m_caret = 0;
				if (!shift) m_anchor = m_caret;
				ScrollCaretIntoView();
				return VInputResult::Handled;
			case VK_END:
				m_caret = m_text.size();
				if (!shift) m_anchor = m_caret;
				ScrollCaretIntoView();
				return VInputResult::Handled;

			// Up / Down: line navigation. Skipped in single-line mode
			// (no second line to jump to). Reuses the DirectWrite
			// layout the same way ScrollCaretIntoView does — get the
			// caret's (x, y), shift y by ±lineHeight, then HitTestPoint
			// to map back to a caret position on the adjacent line.
			// Native-feeling: x is preserved across line jumps until
			// the next horizontal-affecting keystroke.
			case VK_UP:
			case VK_DOWN:
				if (!m_singleLine) {
					float innerW = (m_bounds.right - m_bounds.left)
						- (m_padding * 2.0f);
					if (innerW > 0) {
						ComPtr<IDWriteTextFormat> fmt;
						ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
							m_fontFamily.c_str(), NULL,
							DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
							DWRITE_FONT_STRETCH_NORMAL,
							m_fontSize, L"en-us", &fmt);
						if (fmt) {
							fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
							const wchar_t* probe = m_text.empty()
								? L" " : m_text.c_str();
							UINT32 len = m_text.empty()
								? 1 : (UINT32)m_text.size();
							ComPtr<IDWriteTextLayout> layout;
							ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
								probe, len, fmt.Get(),
								innerW, 65535.0f, &layout);
							if (layout) {
								DWRITE_HIT_TEST_METRICS hm{};
								FLOAT cX = 0, cY = 0;
								layout->HitTestTextPosition(
									(UINT32)m_caret, FALSE, &cX, &cY, &hm);
								float lineH = hm.height > 0
									? hm.height : OneLineHeight();
								float targetY = (vk == VK_UP)
									? cY - lineH * 0.5f
									: cY + lineH * 1.5f;
								if (targetY < 0) targetY = 0;
								BOOL isTrailing = FALSE, isInside = FALSE;
								DWRITE_HIT_TEST_METRICS hm2{};
								if (SUCCEEDED(layout->HitTestPoint(
									cX, targetY,
									&isTrailing, &isInside, &hm2)))
								{
									size_t pos = hm2.textPosition
										+ (isTrailing ? hm2.length : 0);
									if (pos > m_text.size())
										pos = m_text.size();
									m_caret = pos;
									if (!shift) m_anchor = m_caret;
								}
							}
						}
					}
					ScrollCaretIntoView();
				}
				return VInputResult::Handled;

			// PgUp / PgDn: page-sized vertical jump. Computes how many
			// lines fit in the visible area, walks that many lines via
			// repeated VK_UP / VK_DOWN logic. Useful for the larger
			// multi-line inputs (system prompt editor, etc.).
			case VK_PRIOR:
			case VK_NEXT:
				if (!m_singleLine) {
					float innerH = (m_bounds.bottom - m_bounds.top)
						- (m_padding * 2.0f);
					float lineH = OneLineHeight();
					int n = (lineH > 0) ? (int)(innerH / lineH) : 1;
					if (n < 1) n = 1;
					int dy = (vk == VK_PRIOR) ? -n : n;
					m_scrollY += dy * lineH;
					if (m_scrollY < 0) m_scrollY = 0;
					// Keep caret near the new scroll origin.
					ScrollCaretIntoView();
				}
				return VInputResult::Handled;

			case VK_BACK:
				if (HasSelection()) {
					DeleteSelection();
				} else if (m_caret > 0) {
					m_text.erase(m_caret - 1, 1);
					m_caret--; m_anchor = m_caret;
					NotifyTextChanged();
				}
				return VInputResult::Handled;
			case VK_DELETE:
				// Ctrl+Delete = cut selection to clipboard. Classic
				// Windows accelerator (along with the Shift+Del cut and
				// Shift+Ins paste pair).
				if (ctrl) {
					if (HasSelection()) {
						CopySelectionToClipboard();
						DeleteSelection();
					}
					return VInputResult::Handled;
				}
				if (HasSelection()) {
					DeleteSelection();
				} else if (m_caret < m_text.size()) {
					m_text.erase(m_caret, 1);
					NotifyTextChanged();
				}
				return VInputResult::Handled;

			// Classic Windows clipboard accelerators (predate Ctrl+C/V/X
			// and still alive in Notepad, Far Manager, terminal apps).
			// Adding them costs nothing and helps power-users on laptops
			// where the Insert key is more reachable than C/V/X.
			case VK_INSERT:
				if (ctrl) {
					if (HasSelection()) CopySelectionToClipboard();
					return VInputResult::Handled;
				}
				if (shift) {
					PasteFromClipboard();
					return VInputResult::Handled;
				}
				return VInputResult::Handled;

			case VK_RETURN: {
				// Shift+Enter → newline (sólo multi-line). Plain Enter → submit.
				// En single-line, ambos disparan submit; un \n suelto sería
				// invisible y rompería la suposición de "una sola línea".
				if (shift && !m_singleLine) {
					if (HasSelection()) DeleteSelection();
					m_text.insert(m_caret, 1, L'\n');
					m_caret++; m_anchor = m_caret;
					NotifyTextChanged();
					return VInputResult::Handled;
				}
				std::wstring submitted = m_text;
				m_text.clear();
				m_caret = 0; m_anchor = 0;
				NotifyTextChanged();
				if (m_onSubmit) m_onSubmit(submitted);
				return VInputResult::Handled;
			}

			// --- Ctrl combos ---
			case 'A':
				if (ctrl) { m_anchor = 0; m_caret = m_text.size(); return VInputResult::Handled; }
				break;
			case 'C':
				if (ctrl) {
					if (HasSelection()) CopySelectionToClipboard();
					return VInputResult::Handled;
				}
				break;
			case 'X':
				if (ctrl) {
					if (HasSelection()) { CopySelectionToClipboard(); DeleteSelection(); }
					return VInputResult::Handled;
				}
				break;
			case 'V':
				if (ctrl) { PasteFromClipboard(); return VInputResult::Handled; }
				break;
			}
			return VInputResult::NotHandled;
		}
		VInputResult OnChar(wchar_t ch) override {
			// Filter control chars (we handle Enter via OnKeyDown). Allow tab? No.
			if (ch < 32 || ch == 127) return VInputResult::NotHandled;
			// Replace selection on typing — this is how Backspace+type or
			// type-over-selection works in every text widget.
			if (HasSelection()) DeleteSelection();
			m_text.insert(m_caret, 1, ch);
			m_caret++; m_anchor = m_caret;
			m_freshTypingS = 0.0f;
			m_caretVisible = true;
			NotifyTextChanged();
			return VInputResult::Handled;
		}

	private:
		void NotifyTextChanged() {
			ScrollCaretIntoView();
			if (m_onTextChanged) m_onTextChanged();
		}

		// --- Word-boundary helpers (Ctrl+Left / Ctrl+Right) -----------
		// Treats letters/digits/'_' as "word" characters and anything
		// else as "separator". The classic VS / Notepad rule: pressing
		// Ctrl+Left at the start of a word jumps to the start of the
		// previous word; Ctrl+Right at the end of a word jumps PAST the
		// trailing whitespace to the start of the next word. Operates
		// on UTF-16 wchar_t units, so multi-byte runes count as one
		// step — fine for typical chat text.
		static bool IsWordChar(wchar_t c) {
			return iswalnum((wint_t)c) != 0 || c == L'_';
		}
		size_t WordLeftFrom(size_t pos) const {
			if (pos == 0) return 0;
			// Step back over separators first…
			while (pos > 0 && !IsWordChar(m_text[pos - 1])) --pos;
			// …then over the word itself.
			while (pos > 0 &&  IsWordChar(m_text[pos - 1])) --pos;
			return pos;
		}
		size_t WordRightFrom(size_t pos) const {
			const size_t n = m_text.size();
			if (pos >= n) return n;
			// Skip current word…
			while (pos < n &&  IsWordChar(m_text[pos])) ++pos;
			// …then any separator run so caret lands on the next word.
			while (pos < n && !IsWordChar(m_text[pos])) ++pos;
			return pos;
		}

		// --- Selection helpers ---
		bool   HasSelection() const { return m_anchor != m_caret; }
		size_t SelStart() const { return (m_anchor < m_caret) ? m_anchor : m_caret; }
		size_t SelEnd()   const { return (m_anchor < m_caret) ? m_caret  : m_anchor; }
		void   DeleteSelection() {
			if (!HasSelection()) return;
			size_t a = SelStart(), b = SelEnd();
			m_text.erase(a, b - a);
			m_caret = a; m_anchor = a;
			NotifyTextChanged();
		}
		void CopySelectionToClipboard() {
			if (!HasSelection()) return;
			std::wstring sel = m_text.substr(SelStart(), SelEnd() - SelStart());
			if (!OpenClipboard(NULL)) return;
			EmptyClipboard();
			size_t bytes = (sel.size() + 1) * sizeof(wchar_t);
			HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
			if (g) {
				if (wchar_t* p = (wchar_t*)GlobalLock(g)) {
					memcpy(p, sel.c_str(), bytes);
					GlobalUnlock(g);
					SetClipboardData(CF_UNICODETEXT, g);
				}
			}
			CloseClipboard();
		}

		// El highlight de selección ahora lo dibuja DrawSelectionAtOrigin
		// (generalizado a un origen arbitrario para soportar scroll interno).
		// El método antiguo "DrawSelectionHighlight(layout, inner)" se quitó
		// porque era dead code tras el refactor de single-line / multi-line.
	public:

		void OnFocus(bool gained) override {
			m_focused = gained;
			m_caretVisible = gained;
			m_blinkAccum = 0.0f;
		}

	private:
		// --- DirectWrite-driven caret X measurement ---
		// Lays out the current text prefix [0..caret] and returns the x offset
		// of the caret relative to the inner rect's left edge.
		float MeasureCaretX(IDWriteTextFormat* fmt, float maxWidth) const {
			if (m_caret == 0 || m_text.empty()) return 0.0f;
			ComPtr<IDWriteTextLayout> layout;
			HRESULT hr = ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
				m_text.c_str(), (UINT32)m_text.size(),
				fmt, maxWidth, 4096.0f, &layout);
			if (FAILED(hr) || !layout) return 0.0f;

			DWRITE_HIT_TEST_METRICS hitMetrics{};
			FLOAT caretX = 0, caretY = 0;
			hr = layout->HitTestTextPosition((UINT32)m_caret, FALSE, &caretX, &caretY, &hitMetrics);
			if (FAILED(hr)) return 0.0f;
			return caretX;
		}

		// --- DirectWrite-driven hit test: pixel (x,y) -> caret index ---
		// Tiene en cuenta el scroll interno (m_scrollX en single-line,
		// m_scrollY en multi-line) y el modo de wrap activo.
		size_t HitTestToCaret(float clickX, float clickY) const {
			if (m_text.empty()) return 0;
			ComPtr<IDWriteTextFormat> fmt;
			ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextFormat(
				m_fontFamily.c_str(), NULL,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				m_fontSize, L"en-us", &fmt);
			if (!fmt) return m_text.size();
			fmt->SetWordWrapping(m_singleLine
				? DWRITE_WORD_WRAPPING_NO_WRAP : DWRITE_WORD_WRAPPING_WRAP);
			float innerW = (m_bounds.right - m_bounds.left) - (m_padding * 2.0f);
			float innerH = (m_bounds.bottom - m_bounds.top) - (m_padding * 2.0f);
			float maxW = m_singleLine ? 65535.0f : innerW;
			ComPtr<IDWriteTextLayout> layout;
			HRESULT hr = ChronoControllerImpl::Instance().m_pDWriteFactory->CreateTextLayout(
				m_text.c_str(), (UINT32)m_text.size(),
				fmt.Get(), maxW, 65535.0f, &layout);
			if (FAILED(hr) || !layout) return m_text.size();

			float relX = clickX - (m_bounds.left + m_padding) + m_scrollX;
			float relY = clickY - (m_bounds.top  + m_padding) + m_scrollY;
			if (m_singleLine) {
				// El centrado vertical hace que el origen Y "lógico" sea 0;
				// cualquier click dentro del strip mapea a la misma línea.
				relY = 0.0f;
			}
			if (relX < 0) relX = 0;
			if (relY < 0) relY = 0;
			BOOL isTrailing = FALSE, isInside = FALSE;
			DWRITE_HIT_TEST_METRICS m{};
			hr = layout->HitTestPoint(relX, relY, &isTrailing, &isInside, &m);
			if (FAILED(hr)) return m_text.size();
			size_t pos = m.textPosition + (isTrailing ? m.length : 0);
			if (pos > m_text.size()) pos = m_text.size();
			(void)innerH;
			return pos;
		}

		// --- Clipboard paste ---
		void PasteFromClipboard() {
			if (!OpenClipboard(NULL)) return;
			HANDLE h = GetClipboardData(CF_UNICODETEXT);
			if (h) {
				const wchar_t* p = (const wchar_t*)GlobalLock(h);
				if (p) {
					std::wstring s = p;
					GlobalUnlock(h);
					// Normalize CRLF → LF (we use \n for line breaks).
					std::wstring normalized;
					normalized.reserve(s.size());
					for (size_t i = 0; i < s.size(); ++i) {
						if (s[i] == L'\r') {
							normalized.push_back(L'\n');
							if (i + 1 < s.size() && s[i+1] == L'\n') ++i;
						} else {
							normalized.push_back(s[i]);
						}
					}
					if (HasSelection()) DeleteSelection();
					m_text.insert(m_caret, normalized);
					m_caret += normalized.size();
					m_anchor = m_caret;
					m_freshTypingS = 0.0f;
					m_caretVisible = true;
					NotifyTextChanged();
				}
			}
			CloseClipboard();
		}
	};


	// ---------------------------------------------------------------------
	// VChatLayout — non-widget helper. Stacks a list of virtual widgets
	// vertically with a constant gap, computes each widget's required height
	// from MeasureHeight() if it provides one (use the supplied callback to
	// teach VChatLayout how to measure your widget), and reports total content
	// height so the host can configure scrolling.
	//
	// This is intentionally minimal — a full ILayout-equivalent for virtual
	// widgets is a future session.
	// ---------------------------------------------------------------------
	struct VStackItem {
		IVirtualWidget* widget;
		float           height;   // pre-measured for this column width
	};

	inline float StackVertical(std::vector<VStackItem>& items,
		float left, float top, float width, float gap)
	{
		float y = top;
		for (auto& it : items) {
			it.widget->SetBounds(D2D1::RectF(left, y, left + width, y + it.height));
			y += it.height + gap;
		}
		return y - gap;  // bottom of the stack
	}

} // namespace ChronoUI

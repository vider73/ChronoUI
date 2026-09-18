// =============================================================================
// Welcome.cpp — a reference example for the modernized ChronoUI.
//
// What this file demonstrates, end to end:
//   * Custom title bar with theme toggle and close button (no Win32 chrome)
//   * Reactive UI via ChronoObservable<T> — one value drives multiple widgets
//   * Class-based styling via StyleManager + correct insertion-order cascade
//     (btn applied first, btn-primary second → btn-primary wins, as in real CSS)
//   * Runtime theme swap via StyleManager::LoadCSSReplace() — re-cascades onto
//     every widget tracked by AddClass without recreating any window
//   * Modern CSS features the parser now supports: rgb(), named colors,
//     font-weight as both keyword and numeric, margin shorthand
//
// What this file deliberately does NOT do:
//   * It does not pull in funMessageBox or open the resource pak, so the
//     example has no runtime dependencies beyond the CSS files. Add those
//     back if you want modal dialogs.
//   * It does not call SetTimer / CHRONOUI_ANIM_TIMER. If you wanted an
//     animated widget, call its StartAnimation() — the central heartbeat in
//     ChronoControllerImpl will tick it with a real deltaTime.
//
// Keep this file short — it is meant to be the kind of thing you copy-paste
// when starting a new ChronoUI app. Anything more than a few hundred lines
// stops being a useful template.
// =============================================================================

#include <memory>
#include <string>
#include <windows.h>

#include "ChronoUI.hpp"
#include "ChronoStyles.hpp"

using namespace ChronoUI;

// -----------------------------------------------------------------------------
// Theme handling
// -----------------------------------------------------------------------------
// The base bootstrap stylesheet is loaded once. The light/dark overlay is
// re-loaded on toggle via LoadCSSReplace, which clears the previous overlay,
// loads the new one, and calls ReapplyAll() so every AddClass'd widget gets a
// fresh cascade. Buttons painted with .btn-primary etc. will swap colors live.
namespace {
	bool g_isDarkTheme = false;

	void ApplyTheme() {
		// Reset clears whatever was loaded before. We then re-load the base
		// stylesheet plus the active overlay, then ReapplyAll() re-cascades
		// onto tracked widgets. LoadCSSReplace does Reset+Load+ReapplyAll in
		// one call — we do it manually here so we can chain two files.
		StyleManager::Reset();
		StyleManager::LoadCSSFile("assets\\bootstrap_lite.css");
		StyleManager::LoadCSSFile(g_isDarkTheme
			? "assets\\welcome_dark.css"
			: "assets\\welcome_light.css");
		StyleManager::ReapplyAll();
	}
}

// -----------------------------------------------------------------------------
// Builders — small helpers to keep wWinMain readable.
// -----------------------------------------------------------------------------
static IWidget* MakeLabel(const char* title, const char* align = "left", int fontSize = 11) {
	IWidget* w = WidgetFactory::Create("cw.StaticText.dll");
	w->SetProperty("title", title);
	w->SetProperty("text-align", align);
	w->SetProperty("font-size", std::to_string(fontSize).c_str());
	w->SetProperty("border-width", "0");
	return w;
}

static IWidget* MakeClassedButton(const char* title, const char* extraClass) {
	IWidget* btn = WidgetFactory::Create("cw.Button.dll");
	btn->SetProperty("title", title);
	btn->SetProperty("width", "110");
	// Button's own constructor already calls AddClass(this, "btn"). Adding a
	// modifier here exercises the order-preserving cascade: btn was applied
	// first, btn-<variant> is applied second, so the variant wins on conflicts.
	StyleManager::AddClass(btn->GetContextNode(), extraClass);
	return btn;
}

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	// 1. Styles. Load base, then the active theme overlay.
	StyleManager::LoadCSSFile("assets\\bootstrap_lite.css");
	StyleManager::LoadCSSFile("assets\\welcome_light.css");

	// 2. Reactive state — three observables drive the entire UI.
	auto brightness = std::make_shared<ChronoObservable<int>>(65);
	auto isOn       = std::make_shared<ChronoObservable<bool>>(true);
	auto userName   = std::make_shared<ChronoObservable<std::string>>("Jose");

	// 3. Window + 4-row root layout.
	auto* win = CreateChronoContainer(0, L"Welcome to ChronoUI", 760, 480, /*customTitleBar*/ true);

	// Drag the window from anywhere that isn't an interactive widget. This makes
	// every non-clickable surface (cell backgrounds, labels, passive displays
	// that opt in via drag-through) act as a window grab handle, the way macOS
	// dialogs work. Interactive widgets (Button, Slider, EditBox, Switch) still
	// consume their own clicks and won't initiate drag.
	win->SetProperty("drag-anywhere", "true");

	auto* root = win->CreateRootLayout(4, 1);
	root->SetRow(0, WidgetSize::Fixed(36));   // title bar
	root->SetRow(1, WidgetSize::Fixed(80));   // hero
	root->SetRow(2, WidgetSize::Fill());      // main split
	root->SetRow(3, WidgetSize::Fixed(64));   // footer (was 56 — a bit more breathing room)

	// Modest vertical gap between the four root sections.
	root->SetProperty("row-gap", "4px");

	// ---------------------------------------------------------------------
	// Row 0 — title bar
	// ---------------------------------------------------------------------
	// Title bar gets a small left pad and a comfortable gap between the trailing buttons.
	root->GetCell(0, 0)->SetProperty("padding-left", "12px");
	root->GetCell(0, 0)->SetProperty("padding-right", "8px");

	auto* tb = root->GetCell(0, 0)->CreateLayout(1, 3);
	tb->SetCol(0, WidgetSize::Fill());
	tb->SetCol(1, WidgetSize::Fixed(80));
	tb->SetCol(2, WidgetSize::Fixed(40));
	tb->SetProperty("col-gap", "6px");

	tb->GetCell(0, 0)->SetProperty("align-items", "center");
	tb->GetCell(0, 0)->AddWidget(MakeLabel("ChronoUI Welcome Demo", "left", 11));

	IWidget* themeBtn = WidgetFactory::Create("cw.Button.dll");
	themeBtn->SetProperty("title", "Theme");
	themeBtn->SetProperty("width", "72");
	themeBtn->addEventHandler("onClick", [](IWidget*, const char*) {
		g_isDarkTheme = !g_isDarkTheme;
		ApplyTheme();
	});
	tb->GetCell(0, 1)->AddWidget(themeBtn);

	IWidget* closeBtn = WidgetFactory::Create("cw.Button.dll");
	closeBtn->SetProperty("title", "X");
	closeBtn->SetProperty("width", "32");
	closeBtn->SetColor("color",       RGB(200, 50, 50));
	closeBtn->SetColor("color:hover", RGB(220, 80, 80));
	closeBtn->addEventHandler("onClick", [](IWidget*, const char*) { PostQuitMessage(0); });
	tb->GetCell(0, 2)->AddWidget(closeBtn);

	// ---------------------------------------------------------------------
	// Row 1 — hero (welcome headline + subtitle)
	// ---------------------------------------------------------------------
	auto* hero = root->GetCell(1, 0)->CreateLayout(2, 1);
	hero->SetRow(0, WidgetSize::Fixed(42));
	hero->SetRow(1, WidgetSize::Fill());
	hero->GetCell(0, 0)->SetProperty("justify-content", "center");
	hero->GetCell(0, 0)->SetProperty("align-items", "center");
	hero->GetCell(1, 0)->SetProperty("justify-content", "center");
	hero->GetCell(1, 0)->SetProperty("align-items", "center");

	IWidget* heroTitle = MakeLabel("Welcome to ChronoUI", "center", 22);
	heroTitle->SetProperty("font-weight", "700");   // exercises new font-weight parsing
	hero->GetCell(0, 0)->AddWidget(heroTitle);
	hero->GetCell(1, 0)->AddWidget(MakeLabel("A modern Direct2D framework for native C++ UI.", "center", 11));

	// ---------------------------------------------------------------------
	// Row 2 — main content: settings on the left, preview on the right.
	// Each side is its own nested layout so we don't fight CommandBar mode.
	// ---------------------------------------------------------------------
	// Pad the main split's outer cell so contents don't touch the window edge.
	root->GetCell(2, 0)->SetProperty("padding-left",  "16px");
	root->GetCell(2, 0)->SetProperty("padding-right", "16px");

	auto* main = root->GetCell(2, 0)->CreateLayout(1, 2);
	main->SetCol(0, WidgetSize::Percent(55));
	main->SetCol(1, WidgetSize::Percent(45));
	main->SetProperty("col-gap", "20px");   // breathing room between settings + preview

	// --- Settings (left column) — 3 labeled controls bound to observables ---
	auto* settings = main->GetCell(0, 0)->CreateLayout(3, 1);
	settings->SetRow(0, WidgetSize::Fixed(48));
	settings->SetRow(1, WidgetSize::Fixed(48));
	settings->SetRow(2, WidgetSize::Fixed(48));
	settings->SetProperty("row-gap", "8px");

	auto buildLabelledRow = [&](ILayout* parent, int row, const char* labelText, IWidget* control) {
		auto* line = parent->GetCell(row, 0)->CreateLayout(1, 2);
		line->SetCol(0, WidgetSize::Fixed(110));
		line->SetCol(1, WidgetSize::Fill());
		line->SetProperty("col-gap", "12px");      // gap between label cell and control cell
		line->GetCell(0, 0)->SetProperty("align-items", "center");
		line->GetCell(0, 0)->AddWidget(MakeLabel(labelText));
		line->GetCell(0, 1)->AddWidget(control);
	};

	IWidget* slider = WidgetFactory::Create("cw.SliderControl.dll");
	slider->SetProperty("min", "0");
	slider->SetProperty("max", "100");
	slider->Bind("value", brightness);                // two-way: drag updates `brightness`
	buildLabelledRow(settings, 0, "Brightness", slider);

	IWidget* sw = WidgetFactory::Create("cw.SwitchButton.dll");
	sw->Bind("checked", isOn);                        // two-way: toggle updates `isOn`
	buildLabelledRow(settings, 1, "Enabled", sw);

	IWidget* ed = WidgetFactory::Create("cw.EditBox.dll");
	ed->Bind("value", userName);
	buildLabelledRow(settings, 2, "Username", ed);

	// --- Preview (right column) — visualizations bound to brightness ---
	auto* preview = main->GetCell(0, 1)->CreateLayout(2, 1);
	preview->SetRow(0, WidgetSize::Fill());
	preview->SetRow(1, WidgetSize::Fixed(40));
	preview->SetProperty("row-gap", "8px");

	IWidget* gauge = WidgetFactory::Create("cw.GaugeBatteryLevelControl.dll");
	gauge->SetProperty("drag-through", "true");       // passive display — let the window be grabbed here
	gauge->Bind("value", brightness);                 // gauge follows the slider
	preview->GetCell(0, 0)->AddWidget(gauge);

	IWidget* prog = WidgetFactory::Create("cw.Progress.dll");
	prog->SetProperty("min", "0");
	prog->SetProperty("max", "100");
	prog->SetProperty("show_text", "true");
	prog->SetProperty("drag-through", "true");        // passive display — drag-through
	prog->Bind("value", brightness);                  // same observable, second view
	preview->GetCell(1, 0)->AddWidget(prog);

	// ---------------------------------------------------------------------
	// Row 3 — footer: three class-styled buttons
	// ---------------------------------------------------------------------
	auto* footer = root->GetCell(3, 0);
	footer->SetStackMode(StackMode::CommandBar);
	footer->SetProperty("justify-content", "center");
	footer->SetProperty("gap", "12px");       // visible space between footer buttons

	footer->AddWidget(MakeClassedButton("Primary", "btn-primary"));
	footer->AddWidget(MakeClassedButton("Success", "btn-success"));

	// Danger button also has a side-effect: snap the brightness observable to 0.
	// The slider, gauge, and progress bar will all update because they all bind
	// to the same observable.
	IWidget* dangerBtn = MakeClassedButton("Reset", "btn-danger");
	dangerBtn->addEventHandler("onClick", [brightness](IWidget*, const char*) {
		*brightness = 0;
	});
	footer->AddWidget(dangerBtn);

	// 4. Run.
	win->DoModal();

	// 5. Destructor cleans up all child widgets and StyleManager tracking.
	delete win;
	return 0;
}

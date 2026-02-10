#include <windows.h>
#include <commdlg.h> // Required for ChooseColor
#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

// Include the JSON library
#include "nlohmann/json.hpp"
using json = nlohmann::json;

namespace fs = std::filesystem;

#include "ChronoUI.hpp"
#include "ChronoStyles.hpp"

using namespace ChronoUI;

// --- Global State Management ---
struct AppState {
	ICell* stageCell = nullptr;
	ICell* inspectorCell = nullptr;
	ICell* eventMonitorCell = nullptr; // New cell for event log

	IWidget* currentWidget = nullptr;
	ICell* eventLogOutput = nullptr; // The text box showing logs

	bool isEventMonitorActive = true;

	// Map property names to their editor widgets for quick updates
	std::map<std::string, IWidget*> propertyMap;

	// We keep track of generic widgets to clean up reference counting if needed
	std::vector<IWidget*> inspectorControls;

	void ClearInspector() {
		if (!inspectorCell) return;
		inspectorControls.clear();
		propertyMap.clear();
		// The layout system handles destroying the actual C++ objects when cleared
	}

	void ClearStage() {
		if (!stageCell || !currentWidget) return;
		stageCell->RemoveWidget(currentWidget);
		ChronoUI::WidgetFactory::Destroy(currentWidget);
		currentWidget = nullptr;
		stageCell->UpdateWidgets();
	}

	void AppendLog(const std::string& evtName, const std::string& payload) {
		if (!eventLogOutput || !isEventMonitorActive) return;

		// Get Time
		auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::stringstream ss;
		ss << std::put_time(std::localtime(&now), "%H:%M:%S");

		std::stringstream logLine;
		logLine << "[" << ss.str() << "] " << evtName << ": " << payload;

		eventLogOutput->EnableScroll(true);
		eventLogOutput->AddWidget(WidgetFactory::Create("cw.StaticText.dll"))
			->SetProperty("width", "auto")
			->SetProperty("height", "20")
			->SetProperty("text-align", "left")
			->SetProperty("title", logLine.str().c_str());
		eventLogOutput->UpdateWidgets();
	}
};

static AppState g_App;

// --- Helper to get Executable Directory ---
std::string GetExeDirectory() {
	char buffer[MAX_PATH];
	GetModuleFileNameA(NULL, buffer, MAX_PATH);
	std::string::size_type pos = std::string(buffer).find_last_of("\\/");
	return std::string(buffer).substr(0, pos);
}

// --- Color Conversion Helpers ---
COLORREF HexToColorRef(const std::string& hex) {
	if (hex.empty()) return RGB(0, 0, 0);
	std::string clean = (hex[0] == '#') ? hex.substr(1) : hex;
	if (clean.length() == 3) {
		std::string temp;
		for (char c : clean) { temp += c; temp += c; }
		clean = temp;
	}
	if (clean.length() != 6) return RGB(0, 0, 0);

	int r, g, b;
	std::stringstream ss;
	ss << std::hex << clean.substr(0, 2); ss >> r; ss.clear();
	ss << std::hex << clean.substr(2, 2); ss >> g; ss.clear();
	ss << std::hex << clean.substr(4, 2); ss >> b;
	return RGB(r, g, b);
}

std::string ColorRefToHex(COLORREF color) {
	std::stringstream ss;
	ss << "#" << std::hex << std::setfill('0') << std::setw(2) << (int)GetRValue(color)
		<< std::setw(2) << (int)GetGValue(color)
		<< std::setw(2) << (int)GetBValue(color);
	return ss.str();
}

// --- Inspector Styling Helper ---
void ApplyInspectorStyle(IWidget* w) {
	if (!w) return;
	w->SetProperty("background-color", "#ffffff");
	w->SetProperty("border-color", "#d0d0d0");
	w->SetProperty("border-width", "1");
	w->SetProperty("border-radius", "3");
	w->SetProperty("font-family", "Segoe UI");
	w->SetProperty("font-size", "12");
	w->SetProperty("padding-left", "4");
}

// --- Helper: Open Color Picker ---
void HandleColorPicker(IWidget* editorWidget, const std::string& propertyKey) {
	static COLORREF acrCustClr[16];

	const char* currVal = editorWidget->GetProperty("value");
	COLORREF initialColor = HexToColorRef(currVal ? currVal : "#ffffff");

	CHOOSECOLORA cc = { 0 };
	cc.lStructSize = sizeof(cc);
	cc.hwndOwner = GetActiveWindow();
	cc.lpCustColors = (LPDWORD)acrCustClr;
	cc.rgbResult = initialColor;
	cc.Flags = CC_FULLOPEN | CC_RGBINIT;

	if (ChooseColorA(&cc)) {
		std::string newHex = ColorRefToHex(cc.rgbResult);
		editorWidget->SetProperty("value", newHex.c_str());
		if (g_App.currentWidget) {
			g_App.currentWidget->SetProperty(propertyKey.c_str(), newHex.c_str());
		}
	}
}

// --- Refresh Inspector Values ---
// Updates the UI of the inspector to match the current state of the Widget
void RefreshInspectorValues() {
	if (!g_App.currentWidget) return;

	for (auto& [propName, editor] : g_App.propertyMap) {
		const char* actualValue = g_App.currentWidget->GetProperty(propName.c_str());
		std::string sVal = actualValue ? actualValue : "";

		// Handle SwitchButton specific property (it uses "checked", not "value")
		// But the editor widget type matters.
		// Simplified: We assume standard editors use "value", switches use "checked".
		// To be safe, we check the type of editor or just try setting both/correct one.
		// For this sample, we just set "value" unless we know better, or update the specific 'checked' logic inside BuildInspector.

		// However, the BuildInspector mapped them specifically.
		// Let's rely on the fact that we can set "value" on text boxes and "checked" on switches.
		// A generic approach:
		editor->SetProperty("value", sVal.c_str());
		if (sVal == "true" || sVal == "false") {
			editor->SetProperty("checked", sVal.c_str());
		}
	}
}

// --- Inspector Builder ---
void BuildInspector(const char* manifestRaw) {
	g_App.ClearInspector();

	if (!g_App.inspectorCell || !g_App.currentWidget) return;

	try {
		auto manifest = json::parse(manifestRaw);
		std::string desc = manifest.value("description", "Widget properties");

		// --- Event Subscription ---
		if (manifest.contains("events")) {
			for (const auto& evt : manifest["events"]) {
				std::string evtName = evt["name"];
				g_App.currentWidget->addEventHandler(evtName.c_str(), [evtName](IWidget* w, const char* payload) {
					if (g_App.isEventMonitorActive) {
						g_App.AppendLog(evtName, payload ? payload : "null");
						RefreshInspectorValues();
					}
				});
			}
		}

		// --- Property List Preparation ---
		struct PropItem { std::string name; std::string type; std::string desc; };
		std::vector<PropItem> props;

		if (manifest.contains("properties")) {
			for (const auto& prop : manifest["properties"]) {
				props.push_back({ prop["name"], prop["type"], prop.value("description", "") });
			}
		}

		std::vector<std::string> commonCss = {
			"width", "height", "margin", "padding",
			"background-color", "foreground-color", "color",
			"border-color", "border-width", "border-radius",
			"font-family", "font-size", "text-align", "justify-content"
		};
		for (const auto& css : commonCss) {
			props.push_back({ css, "string", "CSS Property" });
		}

		// --- Layout Construction ---
		auto* mainLayout = g_App.inspectorCell->CreateLayout(2, 1);
		mainLayout->SetRow(0, WidgetSize::Fixed(180));
		mainLayout->SetRow(1, WidgetSize::Fill());

		ICell* headerCell = mainLayout->GetCell(0, 0);
		IWidget* header = WidgetFactory::Create("cw.TitleDescCard.dll")
			->SetProperty("title", "Inspector")
			->SetProperty("description", desc.c_str())
			->SetProperty("height", "auto")
			->SetProperty("background-color", "#e3f2fd")
			->SetProperty("foreground-color", "#1565c0");
		headerCell->AddWidget(header);
		g_App.inspectorControls.push_back(header);

		ICell* gridContainer = mainLayout->GetCell(1, 0);
		gridContainer->EnableScroll(true);

		auto* grid = gridContainer->CreateLayout((int)props.size(), 3);
		grid->SetCol(0, WidgetSize::Fixed(110));
		grid->SetCol(1, WidgetSize::Fill());
		grid->SetCol(2, WidgetSize::Fixed(30));

		for (size_t i = 0; i < props.size(); i++) {
			const auto& p = props[i];

			// Label
			ICell* cLabel = grid->GetCell((int)i, 0);
			cLabel->SetProperty("padding-top", "6");
			cLabel->SetProperty("padding-right", "5");
			IWidget* wLabel = WidgetFactory::Create("cw.StaticText.dll");
			std::string title = p.name + ":";
			wLabel->SetProperty("title", p.name.c_str());
			wLabel->SetProperty("text-align", "right");
			wLabel->SetProperty("font-size", "11");
			wLabel->SetProperty("foreground-color", "#555555");
			cLabel->AddWidget(wLabel);
			g_App.inspectorControls.push_back(wLabel);

			// Editor
			ICell* cEditor = grid->GetCell((int)i, 1);
			cEditor->SetProperty("padding", "2");

			IWidget* wEditor = nullptr;
			bool isBool = (p.type == "boolean" || p.type == "bool");
			bool isAction = (p.type == "action");
			bool showHelpButton = true;

			if (isAction) {
				wEditor = WidgetFactory::Create("cw.Button.dll");
				wEditor->SetProperty("title", "Trigger");
				wEditor->addEventHandler("onClick", [name = p.name](IWidget*, const char*) {
					if (g_App.currentWidget) g_App.currentWidget->SetProperty(name.c_str(), "true");
				});
				showHelpButton = false;				
			}
			else if (isBool) {
				wEditor = WidgetFactory::Create("cw.SwitchButton.dll");
				const char* val = g_App.currentWidget->GetProperty(p.name.c_str());
				wEditor->SetProperty("checked", val ? val : "false");
				wEditor->addEventHandler("onChange", [name = p.name](IWidget*, const char* json) {
					if (g_App.currentWidget) g_App.currentWidget->SetProperty(name.c_str(), json);
				});
				showHelpButton = false;
				// Add to map for auto-updates
				g_App.propertyMap[p.name] = wEditor;
			}
			else {
				wEditor = WidgetFactory::Create("cw.EditBox.dll");
				wEditor->SetProperty("height", "24");
				wEditor->SetProperty("placeholder", p.desc.c_str());
				ApplyInspectorStyle(wEditor);
				const char* val = g_App.currentWidget->GetProperty(p.name.c_str());
				wEditor->SetProperty("value", val ? val : "");
				wEditor->addEventHandler("onInput", [name = p.name](IWidget* s, const char*) {
					if (g_App.currentWidget) g_App.currentWidget->SetProperty(name.c_str(), s->GetProperty("value"));
				});
				// Add to map for auto-updates
				g_App.propertyMap[p.name] = wEditor;
			}

			if (wEditor) {
				cEditor->AddWidget(wEditor);
				g_App.inspectorControls.push_back(wEditor);
			}

			// Helper Button
			if (showHelpButton) {
				ICell* cBtn = grid->GetCell((int)i, 2);
				cBtn->SetProperty("padding", "2");
				IWidget* wBtn = WidgetFactory::Create("cw.Button.dll");
				wBtn->SetProperty("width", "24");
				wBtn->SetProperty("height", "24");

				bool isColorProp = (p.name.find("color") != std::string::npos);
				bool hasDescription = !(p.desc.empty());
				if (isColorProp) {
					wBtn->SetProperty("title", "...");
					wBtn->SetProperty("tt_title", "Pick Color");
					wBtn->SetProperty("background-color", "#eeeeee");
					wBtn->addEventHandler("onClick", [wEditor, name = p.name](IWidget*, const char*) {
						if (wEditor) HandleColorPicker(wEditor, name);
					});
				}
				else if (hasDescription) {
					wBtn->SetProperty("title", "i");
					wEditor->SetProperty("tt_title", p.desc.c_str());
				}
				else {
					wBtn->SetProperty("tt_title", "Clear Value");
					wBtn->addEventHandler("onClick", [wEditor, name = p.name](IWidget*, const char*) {
						if (wEditor && g_App.currentWidget) {
							wEditor->SetProperty("value", "");
							g_App.currentWidget->SetProperty(name.c_str(), "");
						}
					});
				}
				cBtn->AddWidget(wBtn);
				g_App.inspectorControls.push_back(wBtn);
			}
		}
	}
	catch (json::parse_error& e) {
		auto* errLayout = g_App.inspectorCell->CreateLayout(1, 1);
		IWidget* err = WidgetFactory::Create("cw.StaticText.dll");
		std::string msg = "JSON Error: "; msg += e.what();
		err->SetProperty("title", msg.c_str());
		errLayout->GetCell(0, 0)->AddWidget(err);
		g_App.inspectorControls.push_back(err);
	}
	g_App.inspectorCell->UpdateWidgets();
}

// --- Main Application ---
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	StyleManager::LoadCSSFile("..\\assets\\bootstrap_lite.css");

	auto* win = CreateChronoContainer(GetDesktopWindow(), L"Dynamic Widget Tester", 1280, 800, true);
	HWND hwnd = win->GetHWND();

	// Root: TitleBar (Row 0), Content (Row 1)
	auto* root = win->CreateRootLayout(2, 1);
	root->SetRow(0, WidgetSize::Fixed(40));
	root->SetRow(1, WidgetSize::Fill());

	// Title Bar
	ICell* titleBarCell = root->GetCell(0, 0);
	titleBarCell->SetProperty("background-color", "#f0f0f0");
	
	auto* titleLayout = titleBarCell->CreateLayout(1, 2);
	titleLayout->SetCol(0, WidgetSize::Fill());
	titleLayout->SetCol(1, WidgetSize::Fixed(240));

	titleLayout->GetCell(0, 0)->SetProperty("title", "Dynamic Widget Tester");

	ICell* rightBar = titleLayout->GetCell(0, 1);
	rightBar->SetStackMode(StackMode::CommandBar);
	rightBar->SetProperty("justify-content", "right");
	rightBar->AddWidget(WidgetFactory::Create("cw.Button.dll")->SetProperty("title", "_")->SetProperty("width", "40")->addEventHandler("onClick", [hwnd](auto...) { ShowWindow(hwnd, SW_MINIMIZE); }));
	rightBar->AddWidget(WidgetFactory::Create("cw.Button.dll")->SetProperty("title", "[]")->SetProperty("width", "40")->addEventHandler("onClick", [hwnd](auto...) { ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); }));
	rightBar->AddWidget(WidgetFactory::Create("cw.Button.dll")->SetProperty("title", "X")->SetProperty("width", "40")->SetColor("color", RGB(200, 50, 50))->addEventHandler("onClick", [](auto...) { PostQuitMessage(0); }));

	// Content: Sidebar (Col 0), StageArea (Col 1), Inspector (Col 2)
	ICell* contentCell = root->GetCell(1, 0);
	auto* mainCols = contentCell->CreateLayout(1, 3);
	mainCols->SetCol(0, WidgetSize::Fixed(240));
	mainCols->SetCol(1, WidgetSize::Fill(), true);
	mainCols->SetCol(2, WidgetSize::Fill(), false, WidgetSize::Fixed(300));

	// -- Sidebar --
	ICell* sidebar = mainCols->GetCell(0, 0);
	sidebar->SetStackMode(ChronoUI::StackMode::Vertical);
	sidebar->EnableScroll(true);
	sidebar->SetProperty("background-color", "#f8f9fa");
	sidebar->SetProperty("border-right", "1px solid #e0e0e0");

	// -- Stage Area (Split into Stage and Event Monitor) --
	ICell* stageAreaCell = mainCols->GetCell(0, 1);
	auto* stageLayout = stageAreaCell->CreateLayout(2, 1);
	stageLayout->SetRow(0, WidgetSize::Percent(50),true);       // The actual widget stage
	stageLayout->SetRow(1, WidgetSize::Fill(200));   // Event Monitor Panel

	g_App.stageCell = stageLayout->GetCell(0, 0);
	g_App.stageCell->SetProperty("background-color", "#eaeff2");
	g_App.stageCell->SetProperty("padding", "20");

	// -- Event Monitor Setup --
	g_App.eventMonitorCell = stageLayout->GetCell(1, 0);
	g_App.eventMonitorCell->SetProperty("background-color", "#fafafa");
	g_App.eventMonitorCell->SetProperty("border-top", "1px solid #cccccc");
	g_App.eventMonitorCell->SetProperty("padding", "5");

	// Event Monitor Header Layout
	auto* evtLayout = g_App.eventMonitorCell->CreateLayout(2, 1);
	evtLayout->SetRow(0, WidgetSize::Fixed(30)); // Header
	evtLayout->SetRow(1, WidgetSize::Fill());    // Text Log

	// Event Header
	ICell* evtHeader = evtLayout->GetCell(0, 0);
	auto* headerRow = evtHeader->CreateLayout(1, 2);

	headerRow->SetCol(0, WidgetSize::Fixed(120)); // Title
	headerRow->SetCol(1, WidgetSize::Fill());

	headerRow->GetCell(0, 0)->AddWidget(WidgetFactory::Create("cw.StaticText.dll")
		->SetProperty("title", "Event Monitor")
		->SetProperty("font-weight", "bold"));

	headerRow->GetCell(0, 1)->SetStackMode(StackMode::CommandBar);
	headerRow->GetCell(0, 1)->SetProperty("justify-content", "right");

	// Clear Button
	IWidget* btnClear = WidgetFactory::Create("cw.Button.dll");
	btnClear->SetProperty("title", "Clear Log");
	btnClear->SetProperty("width", "120");
	btnClear->addEventHandler("onClick", [](IWidget*, const char*) {
		if (g_App.eventLogOutput) {
			g_App.eventLogOutput->RemoveAllWidgets();
		}
	});
	headerRow->GetCell(0, 1)->AddWidget(btnClear);

	// Active Switch
	IWidget* swActive = WidgetFactory::Create("cw.SwitchButton.dll");
	swActive->SetProperty("checked", "true");
	swActive->SetProperty("title", "Listening");
	swActive->SetProperty("width", "120");
	swActive->addEventHandler("onChange", [](IWidget*, const char* val) {
		g_App.isEventMonitorActive = (std::string(val) == "true");
	});
	headerRow->GetCell(0, 1)->AddWidget(swActive);

	g_App.eventLogOutput = evtLayout->GetCell(1, 0);


	// -- Inspector --
	g_App.inspectorCell = mainCols->GetCell(0, 2);
	g_App.inspectorCell->SetProperty("background-color", "#ffffff");
	g_App.inspectorCell->SetProperty("border-left", "1px solid #e0e0e0");

	// Populate Sidebar
	sidebar->AddWidget(WidgetFactory::Create("cw.StaticText.dll")
		->SetProperty("title", "Widget Library")
		->SetProperty("font-weight", "bold")
		->SetProperty("font-size", "18")
		->SetProperty("margin", "10"));

	std::string exePath = GetExeDirectory();
	std::vector<std::string> widgetsFound;
	try {
		for (const auto& entry : fs::directory_iterator(exePath)) {
			std::string f = entry.path().filename().string();
			if (f.find("cw.") == 0 && f.find(".dll") != std::string::npos) widgetsFound.push_back(f);
		}
	}
	catch (...) {}
	if (widgetsFound.empty()) widgetsFound = { "cw.Button.dll", "cw.EditBox.dll" };

	for (const auto& filename : widgetsFound) {
		IWidget* btn = WidgetFactory::Create("cw.Button.dll");
		btn->SetProperty("title", filename.substr(0, filename.length() - 4).c_str());
		btn->SetProperty("align", "left");

		btn->addEventHandler("onClick", [filename](IWidget*, const char*) {
			g_App.ClearStage();
			IWidget* newWidget = WidgetFactory::Create(filename.c_str());
			if (newWidget) {
				std::string w(newWidget->GetProperty("width"));
				if (w != "auto") {
					newWidget->SetProperty("width", "250");
					newWidget->SetProperty("height", "80");
				}
				newWidget->SetProperty("background-color", "#ffffff");
				newWidget->SetProperty("border-width", "1");
				newWidget->SetProperty("border-color", "#aaaaaa");

				g_App.stageCell->AddWidget(newWidget);
				g_App.currentWidget = newWidget;
				BuildInspector(newWidget->GetControlManifest());
				g_App.stageCell->UpdateWidgets();
			}
		});
		sidebar->AddWidget(btn);
	}

	win->CenterToParent();
	win->Show();
	win->RunMessageLoop();
	delete win;
	return 0;
}
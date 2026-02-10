#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <map>

namespace fs = std::filesystem;

#include "ChronoUI.hpp"
#include "ChronoStyles.hpp"

using namespace ChronoUI;

// --- Global State ---
struct EditorState {
	// The main container for the layout being built
	ICell* canvasRoot = nullptr;

	// The currently selected cell in the hierarchy
	ICell* selectedCell = nullptr;

	// Sidebar UI references to update them dynamically
	ICell* propertiesPanel = nullptr;
	IWidget* statusLabel = nullptr;

	// Defines the "Select Me" button style
	void StylePlaceholder(IWidget* btn, bool selected) {
		btn->SetProperty("border-style", "dashed");
		btn->SetProperty("border-width", "2");
		btn->SetProperty("border-radius", "4");

		if (selected) {
			btn->SetProperty("background-color", "#e3f2fd"); // Light Blue
			btn->SetProperty("border-color", "#2196f3");     // Solid Blue Border
			btn->SetProperty("foreground-color", "#1565c0");
		}
		else {
			btn->SetProperty("background-color", "#fafafa");
			btn->SetProperty("border-color", "#cccccc");
			btn->SetProperty("foreground-color", "#888888");
		}
	}
};

static EditorState g_Editor;

// Forward declarations
void RebuildPropertiesPanel();
void SetupCellAsPlaceholder(ICell* cell);
void SetupCellWithWidget(ICell* cell, const std::string& widgetName);

// --- Selection Logic ---

void SelectCell(ICell* cell) {
	g_Editor.selectedCell = cell;

	// In a real app, we would traverse the tree to update visual states of all placeholders.
	// Here, we rely on the specific widget's click handler to update itself, 
	// but strictly speaking, we should refresh the 'selected' visual state of the previous selection.
	// For this example, we simply rebuild the properties panel to reflect the new selection.

	if (g_Editor.statusLabel) {
		g_Editor.statusLabel->SetProperty("title", "Selection Changed");
	}
	RebuildPropertiesPanel();
}

// --- Cell Content Generators ---

// 1. Empty Cell (Placeholder)
// Creates a button inside the cell that says "[ Empty ]". Clicking it selects the cell.
void SetupCellAsPlaceholder(ICell* cell) {
	if (!cell) return;
	cell->RemoveAllWidgets();
	cell->SetStackMode(StackMode::Vertical);
	cell->SetProperty("padding", "5");

	IWidget* placeholder = WidgetFactory::Create("cw.Button.dll");
	placeholder->SetProperty("title", "[ Empty Cell ]\r\nClick to Configure");
	placeholder->SetProperty("height", "100%"); // Fill the cell
	placeholder->SetProperty("width", "100%");

	g_Editor.StylePlaceholder(placeholder, false);

	// Click Event: Mark this cell as selected
	placeholder->addEventHandler("onClick", [cell, placeholder](IWidget* s, const char* json) {
		SelectCell(cell);
		// Visual feedback
		g_Editor.StylePlaceholder(placeholder, true);
	});

	cell->AddWidget(placeholder);
	cell->UpdateWidgets();
}

// 2. Widget Cell (Occupied)
// Wraps the generic widget in a layout that includes a small "Header" strip
// so you can still select the cell later to delete/configure it.
void SetupCellWithWidget(ICell* cell, const std::string& widgetName) {
	if (!cell) return;
	cell->RemoveAllWidgets();

	// Create a wrapper layout: Row 0 = Tools, Row 1 = Content
	auto* wrapper = cell->CreateLayout(2, 1);
	wrapper->SetRow(0, WidgetSize::Fixed(24));
	wrapper->SetRow(1, WidgetSize::Fill());

	// -- Row 0: Header (Selection Strip) --
	ICell* header = wrapper->GetCell(0, 0);
	header->SetProperty("background-color", "#ddd");
	header->SetStackMode(StackMode::Horizontal);

	IWidget* selBtn = WidgetFactory::Create("cw.Button.dll");
	selBtn->SetProperty("title", ("Layout Cell: " + widgetName).c_str());
	selBtn->SetProperty("font-size", "9");
	selBtn->SetProperty("align", "left");
	selBtn->SetProperty("background-color", "#ddd");
	selBtn->SetProperty("border-width", "0");

	selBtn->addEventHandler("onClick", [cell](auto...) {
		SelectCell(cell);
	});
	header->AddWidget(selBtn);

	// -- Row 1: The Widget --
	ICell* content = wrapper->GetCell(1, 0);
	IWidget* w = WidgetFactory::Create(widgetName.c_str());
	if (w) {
		w->SetProperty("width", "100%");
		w->SetProperty("height", "100%");
		content->AddWidget(w);
	}
	else {
		content->AddWidget(WidgetFactory::Create("cw.StaticText.dll")->SetProperty("title", "Error loading widget"));
	}
}

// --- Layout Modifiers ---

// Splits the selected cell into N rows or N columns
void SplitSelectedCell(int count, bool isVertical) {
	if (!g_Editor.selectedCell) return;

	// ChronoUI Layout Creation
	// CreateLayout(Rows, Cols)
	ILayout* newLayout = nullptr;

	if (isVertical) {
		// Vertical Split = Multiple Rows, 1 Column
		newLayout = g_Editor.selectedCell->CreateLayout(count, 1);
		for (int i = 0; i < count; i++) {
			newLayout->SetRow(i, WidgetSize::Fill(), true); // Enable splitter
			SetupCellAsPlaceholder(newLayout->GetCell(i, 0));
		}
	}
	else {
		// Horizontal Split = 1 Row, Multiple Columns
		newLayout = g_Editor.selectedCell->CreateLayout(1, count);
		for (int i = 0; i < count; i++) {
			newLayout->SetCol(i, WidgetSize::Fill(), true); // Enable splitter
			SetupCellAsPlaceholder(newLayout->GetCell(0, i));
		}
	}

	// After splitting, the 'selectedCell' is now a container. 
	// We deselect it so the user has to click a new child.
	g_Editor.selectedCell = nullptr;
	RebuildPropertiesPanel();
}

// --- Properties Panel UI Builder ---

void AddSectionHeader(ICell* cell, const char* title) {
	cell->AddWidget(
		WidgetFactory::Create("cw.StaticText.dll")
		->SetProperty("title", title)
		->SetProperty("font-weight", "bold")
		->SetProperty("background-color", "#eee")
		->SetProperty("margin-top", "10")
		->SetProperty("padding", "5")
	);
}

void RebuildPropertiesPanel() {
	if (!g_Editor.propertiesPanel) return;
	g_Editor.propertiesPanel->RemoveAllWidgets();

	if (!g_Editor.selectedCell) {
		g_Editor.propertiesPanel->AddWidget(
			WidgetFactory::Create("cw.TitleDescCard.dll")
			->SetProperty("title", "No Selection")
			->SetProperty("description", "Click a cell in the layout canvas to edit it.")
		);
		g_Editor.propertiesPanel->UpdateWidgets();
		return;
	}

	// -- Info Card --
	g_Editor.propertiesPanel->AddWidget(
		WidgetFactory::Create("cw.TitleDescCard.dll")
		->SetProperty("title", "Cell Selected")
		->SetProperty("description", "Choose an action below to modify this container.")
		->SetColor("background-color", RGB(220, 245, 220))
	);

	// -- Section: Structure --
	AddSectionHeader(g_Editor.propertiesPanel, "Layout Structure");

	// Split Row Buttons
	ICell* rowSplitBox = g_Editor.propertiesPanel; // Simply stacking vertically

	auto CreateActionBtn = [](const char* title, std::function<void()> onClick) {
		IWidget* btn = WidgetFactory::Create("cw.Button.dll");
		btn->SetProperty("title", title);
		btn->SetProperty("margin", "2");
		btn->addEventHandler("onClick", [onClick](auto...) { onClick(); });
		return btn;
	};

	g_Editor.propertiesPanel->AddWidget(
		CreateActionBtn("Split Vertically (2 Rows)", []() { SplitSelectedCell(2, true); })
	);
	g_Editor.propertiesPanel->AddWidget(
		CreateActionBtn("Split Vertically (3 Rows)", []() { SplitSelectedCell(3, true); })
	);

	g_Editor.propertiesPanel->AddWidget(
		CreateActionBtn("Split Horizontally (2 Cols)", []() { SplitSelectedCell(2, false); })
	);
	g_Editor.propertiesPanel->AddWidget(
		CreateActionBtn("Split Horizontally (3 Cols)", []() { SplitSelectedCell(3, false); })
	);

	g_Editor.propertiesPanel->AddWidget(
		CreateActionBtn("Reset / Clear Cell", []() { SetupCellAsPlaceholder(g_Editor.selectedCell); })
		->SetColor("face-color", RGB(255, 200, 200))
	);

	// -- Section: Container Styling --
	AddSectionHeader(g_Editor.propertiesPanel, "Container Styling");

	// Padding Input
	IWidget* padInput = WidgetFactory::Create("cw.EditBox.dll");
	padInput->SetProperty("placeholder", "Padding (e.g. 10)");
	padInput->addEventHandler("onInput", [](IWidget* s, const char*) {
		if (g_Editor.selectedCell) g_Editor.selectedCell->SetProperty("padding", s->GetProperty("value"));
	});
	g_Editor.propertiesPanel->AddWidget(padInput);

	// Background Color Input
	IWidget* bgInput = WidgetFactory::Create("cw.EditBox.dll");
	bgInput->SetProperty("placeholder", "Background (e.g. #ffffff)");
	bgInput->addEventHandler("onInput", [](IWidget* s, const char*) {
		if (g_Editor.selectedCell) g_Editor.selectedCell->SetProperty("background-color", s->GetProperty("value"));
	});
	g_Editor.propertiesPanel->AddWidget(bgInput);

	// -- Section: Widget Library --
	AddSectionHeader(g_Editor.propertiesPanel, "Insert Widget");

	// Scan for DLLs
	char buf[MAX_PATH]; GetModuleFileNameA(NULL, buf, MAX_PATH);
	std::string exeDir = fs::path(buf).parent_path().string();

	try {
		for (const auto& entry : fs::directory_iterator(exeDir)) {
			std::string filename = entry.path().filename().string();
			if (filename.find("cw.") == 0 && filename.find(".dll") != std::string::npos) {
				std::string name = filename.substr(0, filename.length() - 4);

				IWidget* wBtn = WidgetFactory::Create("cw.Button.dll");
				wBtn->SetProperty("title", name.c_str());
				wBtn->SetProperty("align", "left");
				wBtn->SetProperty("image_base64", ""); // Could add icons here
				wBtn->addEventHandler("onClick", [filename](auto...) {
					SetupCellWithWidget(g_Editor.selectedCell, filename);
				});
				g_Editor.propertiesPanel->AddWidget(wBtn);
			}
		}
	}
	catch (...) {}

	g_Editor.propertiesPanel->UpdateWidgets();
}


// --- Main Entry ---
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	StyleManager::LoadCSSFile("..\\assets\\bootstrap_lite.css");

	auto* win = CreateChronoContainer(GetDesktopWindow(), L"ChronoUI Layout Architect", 1400, 900, true);

	// Global Styles
	IContextNode* ctx = ChronoController::Instance();

	auto* root = win->CreateRootLayout(2, 1);
	root->SetRow(0, WidgetSize::Fixed(60)); // Toolbar
	root->SetRow(1, WidgetSize::Fill());    // Workspace

	// --- Toolbar ---
	ICell* toolbar = root->GetCell(0, 0);
	toolbar->SetStackMode(StackMode::CommandBar);

	toolbar->AddWidget(WidgetFactory::Create("cw.StaticText.dll")
		->SetProperty("title", "Layout Architect")
		->SetProperty("font-size", "20")
		->SetProperty("foreground-color", "#fff")
		->SetProperty("font-weight", "bold")
		->SetProperty("width", "250")
	);

	// Reset All Button
	toolbar->AddWidget(WidgetFactory::Create("cw.Button.dll")
		->SetProperty("title", "New Canvas")
		->SetProperty("image_base64", "")
		->addEventHandler("onClick", [](auto...) {
		SetupCellAsPlaceholder(g_Editor.canvasRoot);
		g_Editor.selectedCell = nullptr;
		RebuildPropertiesPanel();
	})
	);

	// Status Label
	g_Editor.statusLabel = WidgetFactory::Create("cw.StaticText.dll");
	g_Editor.statusLabel->SetProperty("title", "Ready");
	g_Editor.statusLabel->SetProperty("foreground-color", "#aaa");
	toolbar->AddWidget(g_Editor.statusLabel);


	// --- Workspace Split (Canvas | Properties) ---
	ICell* workspace = root->GetCell(1, 0);
	auto* wsLayout = workspace->CreateLayout(1, 2);
	wsLayout->SetCol(0, WidgetSize::Fill(), true);      // Canvas (Flexible)
	wsLayout->SetCol(1, WidgetSize::Fixed(350), true);  // Sidebar

	// Canvas Setup
	g_Editor.canvasRoot = wsLayout->GetCell(0, 0);
	g_Editor.canvasRoot->SetProperty("background-color", "#808080"); // App background
	g_Editor.canvasRoot->SetProperty("padding", "20"); // Outer margin for the canvas

	// Initial State: One big placeholder
	SetupCellAsPlaceholder(g_Editor.canvasRoot);

	// Sidebar Setup
	g_Editor.propertiesPanel = wsLayout->GetCell(0, 1);
	g_Editor.propertiesPanel->SetStackMode(StackMode::Vertical);
	g_Editor.propertiesPanel->EnableScroll(true);
	g_Editor.propertiesPanel->SetProperty("background-color", "#f9f9f9");
	g_Editor.propertiesPanel->SetProperty("border-left", "1px solid #ccc");
	g_Editor.propertiesPanel->SetProperty("padding", "10");

	RebuildPropertiesPanel();

	win->CenterToParent();
	win->Show();
	win->RunMessageLoop();

	delete win;
	return 0;
}
#pragma once

#include "virtual_drive.hpp"

/*
vdrive::VirtualDrive myVirtualDrive;
myVirtualDrive.Open(L"..\\assets\\resources.pak", nullptr);

// Create a Button
auto* btnAction = WidgetFactory::Create("cw.Button.dll");
btnAction->SetProperty("title", "Click Me!");
btnAction->SetProperty("width", "120");

// 5. EVENT HANDLING
// Handle the 'onClick' event using a lambda
btnAction->addEventHandler("onClick", [win, helloLabel](IWidget* sender, const char* jsonArgs) {
	// Change the text of the label
	static int counter = 1; // Example of capturing local state
	std::string newText = "You clicked the Hello World! button " + std::to_string(counter++) + " times!";
	helloLabel->SetProperty("title", newText.c_str());
	funMessageBox(win->GetHWND(), "Hello from ChronoUI!", "Welcome to item interaction", CMB_OK | CMB_ICON_INFO, &myVirtualDrive);
});
footerCell->AddWidget(btnAction);
*/

namespace ChronoUI {
	// --- ChronoMessageBox Flags ---
	// Simple bit-flags to control behavior
	enum ChronoMsgBoxFlags {
		// Buttons
		CMB_OK = 0x0001,
		CMB_OKCANCEL = 0x0002,
		CMB_YESNO = 0x0004,

		// Icons / Severity
		CMB_ICON_INFO = 0x0010,    // Blue, Info icon
		CMB_ICON_SUCCESS = 0x0020, // Green, Check icon
		CMB_ICON_WARNING = 0x0040, // Orange, Alert icon
		CMB_ICON_ERROR = 0x0080,   // Red, Stop icon
		CMB_ICON_QUESTION = 0x0100 // Purple/Blue, Question icon
	};

	// --- The Function ---
	// Returns: IDOK, IDCANCEL, IDYES, IDNO (Standard Win32 definitions)
	int funMessageBox(HWND parent, const std::string& title, const std::string& message, int flags, vdrive::VirtualDrive* myVirtualDrive=0)
	{
		// 1. Setup State
		auto result = std::make_shared<int>(IDCANCEL); // Default to cancel/close

		// Determine Styling based on Flags
		std::string headerTitle = title;
		std::string headerIcon;
		COLORREF headerBg = RGB(240, 240, 240); // Default Gray

		// Icon & Color Logic
		if (flags & CMB_ICON_ERROR) {
			headerIcon = R"(\mdifont48\alert-circle.png)";
			headerBg = RGB(255, 235, 235); // Light Red
		}
		else if (flags & CMB_ICON_SUCCESS) {
			headerIcon = R"(\mdifont48\check-circle.png)";
			headerBg = RGB(230, 255, 230); // Light Green
		}
		else if (flags & CMB_ICON_WARNING) {
			headerIcon = R"(\mdifont48\alert.png)";
			headerBg = RGB(255, 248, 225); // Light Orange
		}
		else if (flags & CMB_ICON_QUESTION) {
			headerIcon = R"(\mdifont48\help-circle.png)";
			headerBg = RGB(235, 240, 255); // Light Blue/Purple
		}
		else { // Info or Default
			headerIcon = R"(\mdifont48\information.png)";
			headerBg = RGB(230, 245, 255); // Light Blue
		}

		// 2. Create Window
		// Smaller, fixed size container (400x250 approx)
		auto* dlg = CreateChronoContainer(parent, std::wstring(title.begin(), title.end()).c_str(), 520, 380, true);

		// 3. Root Layout (3 Rows)
		auto* root = dlg->CreateRootLayout(3, 1);
		root->SetRow(0, WidgetSize::Fixed(90));  // Header (Compact)
		root->SetRow(1, WidgetSize::Fill());     // Message Body
		root->SetRow(2, WidgetSize::Fixed(55));  // Footer (Buttons)

		// --- ROW 0: Header ---
		root->GetCell(0, 0)->AddWidget(
			WidgetFactory::Create("cw.TitleDescCard.dll")
			->SetProperty("title", title.c_str())
			->SetProperty("top_text", "System Message")
			->SetProperty("height", "60")
			->SetProperty("description", "") // Hide description in header, use body for text
			->SetProperty("image_base64", (myVirtualDrive)?myVirtualDrive->GetBase64(NarrowToWide(headerIcon)).c_str() : "")
			->SetColor("foreground-color", RGB(32, 32, 32))
			->SetColor("background-color", headerBg)
			->SetColor("background-color:hover", headerBg)
		);

		// --- ROW 1: Body (The Message) ---
		auto* bodyCell = root->GetCell(1, 0);
		bodyCell->SetStackMode(ChronoUI::StackMode::Vertical);
		bodyCell->SetProperty("padding", "20");
		bodyCell->SetProperty("justify-content", "center"); // Vertically center the text

		bodyCell->AddWidget(
			WidgetFactory::Create("cw.StaticText.dll")
			->SetProperty("title", message.c_str())
			->SetProperty("font-size", "14") // Slightly larger text
			->SetProperty("font-size:hover", "14")
		);

		// --- ROW 2: Footer (Buttons) ---
		auto* footer = root->GetCell(2, 0);
		footer->SetStackMode(ChronoUI::StackMode::CommandBar);
		footer->SetProperty("justify-content", "right");
		footer->SetProperty("padding-right", "15");

		// Button Logic Helper
		auto AddBtn = [&](const char* title, const char* icon, COLORREF color, int retCode, bool isPrimary) {
			auto* btn = WidgetFactory::Create("cw.Button.dll")
				->SetProperty("title", title)
				->SetProperty("width", "90")
				->SetProperty("image_base64", (myVirtualDrive) ? myVirtualDrive->GetBase64(NarrowToWide(icon)).c_str() : "")
				->addEventHandler("onClick", [=](auto...) {
				*result = retCode;
				SendMessage(dlg->GetHWND(), WM_CLOSE, 0, 0);
			});
			if (isPrimary) {
				StyleManager::AddClass(btn, "btn btn-primary");
			}
			footer->AddWidget(btn);
		};

		// --- Add Buttons based on Flags ---

		if (flags & CMB_YESNO) {
			// NO Button (Secondary)
			AddBtn("No", R"(\mdifont48\close.png)", 0, IDNO, false);
			// YES Button (Primary)
			AddBtn("Yes", R"(\mdifont48\check.png)", RGB(0, 120, 215), IDYES, true);
		}
		else if (flags & CMB_OKCANCEL) {
			// CANCEL Button (Secondary)
			AddBtn("Cancel", R"(\mdifont48\window-close.png)", 0, IDCANCEL, false);
			// OK Button (Primary)
			AddBtn("OK", R"(\mdifont48\check.png)", RGB(0, 120, 215), IDOK, true);
		}
		else {
			// Default: OK Only
			AddBtn("OK", R"(\mdifont48\check.png)", RGB(0, 120, 215), IDOK, true);
		}

		// 4. Run
		dlg->DoModal();

		return *result;
	}
}
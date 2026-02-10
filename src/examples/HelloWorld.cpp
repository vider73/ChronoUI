#include <string>
#include <filesystem>
#include <windows.h>

// ChronoUI Headers
#include "ChronoUI.hpp"
#include "ChronoStyles.hpp"
#include "virtual_drive.hpp"
#include "funMessageBox.hpp"

// Namespace alias as seen in your reference code
namespace fs = std::filesystem;
using namespace ChronoUI;

// Global resource manager (required for loading internal assets)
vdrive::VirtualDrive myVirtualDrive;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	// 1. INITIALIZATION
	// Load default styles and resources (adjust paths as necessary)
	StyleManager::LoadCSSFile("..\\assets\\bootstrap_lite.css");

	myVirtualDrive.Open(L"..\\assets\\resources.pak", nullptr);

	// 2. CREATE WINDOW
	// Create the main application container (Window)
	// Args: Parent, Title, Width, Height, Custom Title Bar (true/false)
	auto* win = CreateChronoContainer(0, L"ChronoUI Hello World", 400, 280, true);

	// 3. LAYOUT SYSTEM
	// Create a Root Layout with 3 Rows and 1 Column
	// Row 0: Title Bar (Fixed size)
	// Row 1: Content Area (Fills remaining space)
	// Row 2: Footer/Button Area (Fixed size)
	auto* root = win->CreateRootLayout(3, 1);

	root->SetRow(0, WidgetSize::Fixed(35));
	root->SetRow(1, WidgetSize::Fill());
	root->SetRow(2, WidgetSize::Fixed(60));

	// 4. ADD WIDGETS

	// --- Row 0: Custom TitleBar ---
	// Access Cell (Row 0, Col 0) 
	auto* titleBar = root->GetCell(0, 0)->CreateLayout(1, 3);
	titleBar->SetCol(0, WidgetSize::Percent(25));
	titleBar->SetCol(1, WidgetSize::Fill());
	titleBar->SetCol(2, WidgetSize::Fixed(120));
	// Create a StaticText widget
	auto* titleWidget = WidgetFactory::Create("cw.StaticText.dll");
	titleWidget->SetProperty("title", "Welcome");
	titleWidget->SetProperty("text-align", "center");
	titleBar->GetCell(0, 0)->AddWidget(titleWidget);

	// Nice an simple close button in the title bar (using built-in Button widget)
	titleBar->GetCell(0, 2)->SetStackMode(StackMode::CommandBar);
	titleBar->GetCell(0, 2)->SetProperty("justify-content", "right");
	titleBar->GetCell(0, 2)->AddWidget(WidgetFactory::Create("cw.Button.dll"))
		->SetProperty("title", "X")
		->SetColor("color", RGB(200, 50, 50))
		->SetColor("color:hover", RGB(200, 100, 100))
		->SetProperty("width", "35")
		->SetProperty("align", "right")
 		->addEventHandler("onClick", [](auto...) { PostQuitMessage(0); });
		
	// --- Row 1: Main Content ---
	ICell* contentCell = root->GetCell(1, 0);
	contentCell->SetProperty("justify-content", "center"); // Vertically center content
	contentCell->SetProperty("align-items", "center");     // Horizontally center content

	// Create the main "Hello World" text
	auto* helloLabel = WidgetFactory::Create("cw.StaticText.dll");
	helloLabel->SetProperty("title", "Hello, World!");
	helloLabel->SetProperty("width", "100%");
	helloLabel->SetProperty("font-size", "14");
	helloLabel->SetProperty("font-style", "bold");
	helloLabel->SetProperty("border-width", "0"); // No border
	contentCell->AddWidget(helloLabel);

	
// 	--- Row 2: Footer / Interaction ---
	ICell* footerCell = root->GetCell(2, 0);
	footerCell->SetStackMode(ChronoUI::StackMode::CommandBar); // Auto-layout for buttons
	footerCell->SetProperty("justify-content", "center");
	
	// Create a Button
	auto* btnAction = WidgetFactory::Create("cw.Button.dll");
	btnAction->SetProperty("title", "Click Me!");
	btnAction->SetProperty("width", "120");

	// 5. EVENT HANDLING
	// Handle the 'onClick' event using a lambda
	btnAction->addEventHandler("onClick", [win, helloLabel](IWidget* sender, const char* jsonArgs) {
		// Change the text of the label
		static int counter = 1;
		std::string newText = "You clicked the Hello World! button " + std::to_string(counter++) + " times!";
		helloLabel->SetProperty("title", newText.c_str());
	});
	footerCell->AddWidget(btnAction);

	// 6. EXECUTION
	win->DoModal();

	// 7. CLEANUP
	delete win; // Destructor handles child widget cleanup

	return 0;
}
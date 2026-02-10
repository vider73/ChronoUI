#include <string>
#include <list>
#include <filesystem>
#include <shobjidl.h> 
#include <vector>
#include <chrono>
#include <algorithm>
#include <regex>
#include <cctype>

#include <pdh.h>
#pragma comment(lib, "pdh.lib")

// This alias fixes the "fs is not a class or namespace" error
namespace fs = std::filesystem;

#include "ChronoUI.hpp"
#include "ChronoStyles.hpp"
#include "virtual_drive.hpp"
#include "funMessageBox.hpp"

using namespace ChronoUI;
using namespace std::chrono;

vdrive::VirtualDrive myVirtualDrive;


class CpuMonitor {
private:
	PDH_HQUERY cpuQuery;
	PDH_HCOUNTER cpuTotal;

public:
	CpuMonitor() {
		PdhOpenQuery(NULL, NULL, &cpuQuery);
		// Add counter for total processor time
		PdhAddEnglishCounter(cpuQuery, L"\\Processor(_Total)\\% Processor Time", NULL, &cpuTotal);
		PdhCollectQueryData(cpuQuery);
	}

	~CpuMonitor() {
		PdhCloseQuery(cpuQuery);
	}

	double GetUsage() {
		PDH_FMT_COUNTERVALUE counterVal;
		PdhCollectQueryData(cpuQuery);
		PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);
		return counterVal.doubleValue;
	}
};

// Logic to safely delete a widget
void SafeDeleteWidget(ICell* parentCell, IWidget* widgetToDelete) {
	if (!parentCell || !widgetToDelete) return;

	// Step 1: Tell the cell to stop managing this widget
	parentCell->RemoveWidget(widgetToDelete);

	//widgetToDelete->Destroy();
}

// Helper for password complexity
bool IsPasswordComplex(const std::string& pass) {
	if (pass.length() < 8) return false;
	bool hasUpper = false;
	bool hasLower = false;
	bool hasDigit = false;

	for (char c : pass) {
		if (std::isupper(c)) hasUpper = true;
		else if (std::islower(c)) hasLower = true;
		else if (std::isdigit(c)) hasDigit = true;
	}
	return hasUpper && hasLower && hasDigit;
}

// Helper for email validation
bool IsValidEmail(const std::string& email) {
	const std::regex pattern(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
	return std::regex_match(email, pattern);
}

// --- Main Dialog Functions ---

BOOL ExampleRegisterDlg(HWND parent, std::string& outEmail, std::string& outPass)
{
	// 1. State Management
	auto emailObs = std::make_shared<ChronoObservable<std::string>>("");
	auto passObs = std::make_shared<ChronoObservable<std::string>>("");
	auto confirmObs = std::make_shared<ChronoObservable<std::string>>("");

	auto isWorking = std::make_shared<ChronoObservable<bool>>(false);
	auto registerSuccess = std::make_shared<bool>(false);

	// Start disabled until valid
	auto submitDisabled = std::make_shared<ChronoObservable<bool>>(true);

	// 2. Create Window
	auto* dlg = CreateChronoContainer(parent, L"Create Account", 450, 650, true);
	HWND hwnd = dlg->GetHWND();

	// 3. Validation Logic
	auto CheckValidation = [=](const std::string& e, const std::string& p, const std::string& c) {
		bool validEmail = IsValidEmail(e);
		bool validPass = IsPasswordComplex(p);
		bool validMatch = (!p.empty() && p == c);

		// Disable if working OR if any validation fails
		bool shouldDisable = isWorking->get() || !(validEmail && validPass && validMatch);

		if (submitDisabled->get() != shouldDisable) {
			*submitDisabled = shouldDisable;
		}
	};

	// 4. Layout
	auto* root = dlg->CreateRootLayout(3, 1);
	root->SetRow(0, WidgetSize::Fixed(110)); // Header
	root->SetRow(1, WidgetSize::Fill());     // Form
	root->SetRow(2, WidgetSize::Fixed(60));  // Footer

	// --- ROW 0: Header ---
	root->GetCell(0, 0)->AddWidget(
		WidgetFactory::Create("cw.TitleDescCard.dll")
		->SetProperty("title", "New User Registration")
		->SetProperty("top_text", "Secure Sign Up")
		->SetProperty("description", "Please fill in the details below.")
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\account-plus.png)").c_str())
		->SetColor("background-color", RGB(230, 255, 230))
	);

	// --- ROW 1: Form Inputs ---
	auto* form = root->GetCell(1, 0);
	form->SetStackMode(ChronoUI::StackMode::Vertical);
	form->SetProperty("padding", "20");

	// Email
	form->AddWidget(WidgetFactory::Create("cw.StaticText.dll")->SetProperty("title", "Email Address:"))
		->SetProperty("text-align", "left");
	form->AddWidget(
		WidgetFactory::Create("cw.EditBox.dll")
		->Bind("value", emailObs)
		->Bind("disabled", isWorking)
		->SetProperty("placeholder", "name@example.com")
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\email.png)").c_str())
		->SetProperty("height", "35")
		->SetProperty("margin-bottom", "10")
		->OnValidate([=](IWidget* sender, const char* payload) {
		std::string val = payload;
		CheckValidation(val, passObs->get(), confirmObs->get());
		if (val.empty()) return true;
		if (!IsValidEmail(val)) {
			sender->SetProperty("validation-error", "Invalid email format");
			return false;
		}
		return true;
	})
	);

	// Password
	form->AddWidget(WidgetFactory::Create("cw.StaticText.dll")->SetProperty("title", "Password:"))->SetProperty("text-align", "left");
	form->AddWidget(
		WidgetFactory::Create("cw.EditBox.dll")
		->Bind("value", passObs)
		->Bind("disabled", isWorking)
		->SetProperty("placeholder", "Min 8 chars, 1 Upper, 1 Lower, 1 Digit")
		->SetProperty("password", "true")
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\key.png)").c_str())
		->SetProperty("height", "35")
		->SetProperty("margin-bottom", "10")
		->OnValidate([=](IWidget* sender, const char* payload) {
		std::string val = payload;
		CheckValidation(emailObs->get(), val, confirmObs->get());
		if (val.empty()) return true;
		if (!IsPasswordComplex(val)) {
			sender->SetProperty("validation-error", "Complexity requirements not met");
			return false;
		}
		return true;
	})
	);

	// Confirm
	form->AddWidget(WidgetFactory::Create("cw.StaticText.dll")
		->SetProperty("title", "Confirm Password:"))
		->SetProperty("text-align", "left");
	form->AddWidget(
		WidgetFactory::Create("cw.EditBox.dll")
		->Bind("value", confirmObs)
		->Bind("disabled", isWorking)
		->SetProperty("placeholder", "Re-enter password")
		->SetProperty("password", "true")
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\check.png)").c_str())
		->SetProperty("height", "35")
		->SetProperty("margin-bottom", "10")
		->OnValidate([=](IWidget* sender, const char* payload) {
		std::string val = payload;
		std::string currentPass = passObs->get();
		CheckValidation(emailObs->get(), currentPass, val);
		if (val != currentPass) {
			sender->SetProperty("validation-error", "Passwords do not match");
			return false;
		}
		return true;
	})
	);

	// --- ROW 2: Footer ---
	auto* footer = root->GetCell(2, 0);
	footer->SetStackMode(ChronoUI::StackMode::CommandBar);
	footer->SetProperty("justify-content", "right");
	footer->SetProperty("background-color", "#f0f0f0");
	footer->SetProperty("padding-right", "15");

	footer->AddWidget(
		WidgetFactory::Create("cw.Button.dll")
		->SetProperty("title", "Cancel")
		->Bind("disabled", isWorking)
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\window-close.png)").c_str())
		->addEventHandler("onClick", [hwnd](auto...) { SendMessage(hwnd, WM_CLOSE, 0, 0); })
	);

	footer->AddWidget(
		WidgetFactory::Create("cw.Button.dll"))
		->SetProperty("title", "Create Account")
		->SetProperty("width", "140")
		->Bind("disabled", submitDisabled)
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\content-save.png)").c_str())
		->SetColor("face-color", RGB(0, 120, 215))
		->SetColor("foreground-color", RGB(255, 255, 255))
		->addEventHandler("onClick", [=](IWidget* sender, const char* json) {
		*isWorking = true;
		*submitDisabled = true;

		sender->AddTimer("Reg", 1500);
		sender->addEventHandler("Reg", [=](IWidget* s, const char* j) {
			*registerSuccess = true;
			*isWorking = false;
			SendMessage(hwnd, WM_CLOSE, 0, 0);
		});
	});

	dlg->DoModal();

	if (*registerSuccess) {
		outEmail = emailObs->get();
		outPass = passObs->get();
		return TRUE;
	}
	return FALSE;
}

// Returns TRUE if login was successful, FALSE if cancelled.
BOOL ExampleLoginDlg(HWND parent, std::string& resultUser, std::string& resultPassword)
{
	// 1. State Management
	auto userObs = std::make_shared<ChronoObservable<std::string>>(resultUser);
	auto passObs = std::make_shared<ChronoObservable<std::string>>(resultPassword);

	auto isWorking = std::make_shared<ChronoObservable<bool>>(false);
	auto loginSuccess = std::make_shared<bool>(false);

	// Start disabled if inputs are empty
	bool initialValid = !resultUser.empty() && !resultPassword.empty();
	auto submitDisabled = std::make_shared<ChronoObservable<bool>>(!initialValid);

	// 2. Create Window
	auto* dlg = CreateChronoContainer(parent, L"Secure Login", 450, 500, true);
	HWND hwnd = dlg->GetHWND();

	// 3. Validation Logic
	auto CheckValidation = [=](const std::string& u, const std::string& p) {
		bool valid = !u.empty() && !p.empty();

		// Disable if working OR if empty
		bool shouldDisable = isWorking->get() || !valid;

		if (submitDisabled->get() != shouldDisable) {
			*submitDisabled = shouldDisable;
		}
	};

	// 4. Layout (Matching Register Layout)
	auto* root = dlg->CreateRootLayout(3, 1);
	root->SetRow(0, WidgetSize::Fixed(110)); // Header
	root->SetRow(1, WidgetSize::Fill());     // Form
	root->SetRow(2, WidgetSize::Fixed(60));  // Footer

	// --- ROW 0: Header ---
	root->GetCell(0, 0)->AddWidget(
		WidgetFactory::Create("cw.TitleDescCard.dll")
		->SetProperty("title", "Welcome Back")
		->SetProperty("top_text", "Authentication Required")
		->SetProperty("description", "Please enter your credentials to access the system.")
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\lock.png)").c_str())
		->SetColor("background-color", RGB(245, 245, 250)) // Slightly blue tint for Login
	);

	// --- ROW 1: Form Inputs ---
	auto* form = root->GetCell(1, 0);
	form->SetStackMode(ChronoUI::StackMode::Vertical);
	form->SetProperty("padding", "20");

	// Username
	form->AddWidget(WidgetFactory::Create("cw.StaticText.dll")->SetProperty("title", "Username:"))->SetProperty("text-align", "left");
	form->AddWidget(
		WidgetFactory::Create("cw.EditBox.dll")
		->Bind("value", userObs)
		->Bind("disabled", isWorking)
		->SetProperty("placeholder", "Enter username")
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\account.png)").c_str())
		->SetProperty("height", "35")
		->SetProperty("margin-bottom", "15")
		->OnValidate([=](IWidget* sender, const char* payload) {
		CheckValidation(payload, passObs->get());
		return true;
	})
	);

	// Password
	form->AddWidget(WidgetFactory::Create("cw.StaticText.dll")->SetProperty("title", "Password:"))->SetProperty("text-align", "left");
	form->AddWidget(
		WidgetFactory::Create("cw.EditBox.dll"))
		->Bind("value", passObs)
		->Bind("disabled", isWorking)
		->SetProperty("placeholder", "Enter password")
		->SetProperty("text-align", "left")
		->SetProperty("password", "true")
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\key.png)").c_str())
		->SetProperty("height", "35")
		->SetProperty("margin-bottom", "10")
		->OnValidate([=](IWidget* sender, const char* payload) {
		CheckValidation(userObs->get(), payload);
		return true;
	}
		);

	// --- ROW 2: Footer ---
	auto* footer = root->GetCell(2, 0);
	footer->SetStackMode(ChronoUI::StackMode::CommandBar);
	footer->SetProperty("justify-content", "right");
	footer->SetProperty("background-color", "#f0f0f0");
	footer->SetProperty("padding-right", "15");

	footer->AddWidget(
		WidgetFactory::Create("cw.Button.dll")
		->SetProperty("title", "Cancel")
		->Bind("disabled", isWorking)
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\window-close.png)").c_str())
		->addEventHandler("onClick", [hwnd](auto...) { SendMessage(hwnd, WM_CLOSE, 0, 0); })
	);

	footer->AddWidget(
		WidgetFactory::Create("cw.Button.dll"))
		->SetProperty("title", "Login")
		->SetProperty("width", "120")
		->Bind("disabled", submitDisabled)
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\login.png)").c_str())
		->SetColor("face-color", RGB(80, 180, 80)) // Green for Login
		->SetColor("foreground-color", RGB(255, 255, 255))
		->addEventHandler("onClick", [=](IWidget* sender, const char* json) {

		// Simulate Work (Async)
		*isWorking = true;
		*submitDisabled = true;

		sender->AddTimer("LoginProcess", 1500);
		sender->addEventHandler("LoginProcess", [=](IWidget* s, const char* j) {
			*loginSuccess = true;
			*isWorking = false;
			SendMessage(hwnd, WM_CLOSE, 0, 0);
		});
	});

	dlg->DoModal();

	if (*loginSuccess) {
		resultUser = userObs->get();
		resultPassword = passObs->get();
		return TRUE;
	}

	return FALSE;
}

int exampleEqualizer(HWND parent)
{
	auto* dlg = CreateChronoContainer(parent, L"", 640, 480, true);

	auto* root = dlg->CreateRootLayout(3, 1);

	root->SetRow(0, WidgetSize::System(StandardMetric::TitleBar));			// WindowTitle
	root->SetRow(1, WidgetSize::Fill(), false, WidgetSize::Fixed(100));		// MessageBar
	root->SetRow(2, WidgetSize::Fixed(48));									// ButtonBar

	// Row 0, Cell 0 is your titlebar
	root->GetCell(0, 0)->SetProperty("title", "Example equalizer");
	root->GetCell(0, 0)->SetProperty("text-align", "left");

	// Row 1, Cell 0 is body
	{
		// Example of a child layout (IPanel)
		IPanel* subPanelVEqualizer = CreateChronoPanel(dlg);
		IWidget* equ = root->GetCell(1, 0)->AddWidget(subPanelVEqualizer);
		equ->SetProperty("height", "90%");

		int channels = 10;
		auto* panelLayout = subPanelVEqualizer->CreateLayout(1, channels);
		{
			for (int x = 0; x < channels; x++) {
				auto vu = WidgetFactory::Create("cw.EqualizerBar.dll");
				panelLayout->GetCell(0, x)->AddWidget(vu);

				vu->SetProperty("vertical", "true");

				// CHANGE 1: Invert direction (Top to Bottom)
				vu->SetProperty("inverted", "true");

				vu->SetProperty("segments", "20");

				// CHANGE 2: Increase update speed for smoothness (25ms = 40 FPS)
				// Previously 100ms
				vu->AddTimer("AudioSim", 100);

				vu->addEventHandler("AudioSim", [=](IWidget* sender, const char* json) {
					static float time = 0.0f;
					time += 0.05f; // Adjusted for smoothness

					// Complex sine wave to simulate music dynamics
					float val1 = (sin(time+x) * 0.5f + 0.5f) * (cos((time+x) * 3.0f) * 0.5f + 0.5f);
					float val2 = (cos((time+x) + 1.0f) * 0.5f + 0.5f);

					if (rand() % 40 == 0) val1 = 1.0f; // Adjusted rand probability for faster timer

					sender->SetProperty("value", std::to_string(val2).c_str());
				});
			}
		}

		// Horizontal VU Meter (Right side / Bottom)
		auto vuRight = WidgetFactory::Create("cw.EqualizerBar.dll");
		root->GetCell(1, 0)->AddWidget(vuRight);
		vuRight->SetProperty("vertical", "false");
		vuRight->SetProperty("segments", "40");
		vuRight->SetProperty("height", "10%");

		// Update the horizontal bar to be smooth as well
		vuRight->AddTimer("AudioSim", 100);
		vuRight->addEventHandler("AudioSim", [=](IWidget* sender, const char* json) {
			static float time = 0.0f;
			time += 0.05f; // Adjusted for smoothness

			// Complex sine wave to simulate music dynamics
			float val1 = (sin(time) * 0.5f + 0.5f) * (cos(time * 3.0f) * 0.5f + 0.5f);
			float val2 = (cos(time + 1.0f) * 0.5f + 0.5f);

			if (rand() % 40 == 0) val1 = 1.0f; // Adjusted rand probability for faster timer

			sender->SetProperty("value", std::to_string(val2).c_str());
		});
	}

	// Row 2, Cell 0 is a button bar
	root->GetCell(2, 0)->SetProperty("align-items", "center");
	root->GetCell(2, 0)->AddWidget(
		WidgetFactory::Create("cw.Button.dll")
		->SetProperty("title", "Close")
		->SetProperty("width", "25%")
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\cancel.png)").c_str())
		->addEventHandler("onClick", [dlg](IWidget* sender, const char* json)
	{
		SendMessage(dlg->GetHWND(), WM_CLOSE, 0, 0);
	}));

	dlg->DoModal();

	return 0;
}

int exampleGauges(HWND parent)
{
	auto* dlg = CreateChronoContainer(parent, L"", 640, 480, true);

	auto* root = dlg->CreateRootLayout(3, 1);

	root->SetRow(0, WidgetSize::System(StandardMetric::TitleBar));			// WindowTitle
	root->SetRow(1, WidgetSize::Fill(), false, WidgetSize::Fixed(50));		// MessageBar, fill area, no split, min size 100px
	root->SetRow(2, WidgetSize::Fixed(48));									// ButtonBar

	// Row 0, Cell 0 is your titlebar
	root->GetCell(0, 0)->SetProperty("title", "Example equalizer");
	root->GetCell(0, 0)->SetProperty("text-align", "left");

	// Row 1, Cell 0 is body
	{
		auto* gaugesLayout = root->GetCell(1, 0)->CreateLayout(1, 3);

		gaugesLayout->GetCell(0, 0)
			->AddWidget(WidgetFactory::Create("cw.GaugeBatteryLevelControl.dll"))
			->AddTimer("UptTemp", 1000)
			->addEventHandler("UptTemp", [&](IWidget* sender, const char* json) {
			sender->SetProperty("value", std::to_string(10 + rand() % 5).c_str());
		});

		gaugesLayout->GetCell(0, 1)
			->AddWidget(WidgetFactory::Create("cw.GaugeSpeedOmeter.dll"))
			->AddTimer("UptSpeed", 1000)
			->addEventHandler("UptSpeed", [&](IWidget* sender, const char* json) {
			sender->SetProperty("value", std::to_string(rand() % 220).c_str());
		});

		gaugesLayout->GetCell(0, 2)
			->AddWidget(WidgetFactory::Create("cw.GaugeEngineTemperatureControl.dll"))->AddTimer("UptTemp", 1000)
			->AddTimer("UptBattery", 1000)
			->addEventHandler("UptBattery", [&](IWidget* sender, const char* json) {
			sender->SetProperty("value", std::to_string(rand() % 100).c_str());
		});
	}

	root->GetCell(2, 0)->SetProperty("align-items", "center");
	{
		root->GetCell(2, 0)->AddWidget(
			WidgetFactory::Create("cw.Button.dll")
			->SetProperty("title", "Close")
			->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\cancel.png)").c_str())
			->addEventHandler("onClick", [dlg](IWidget* sender, const char* json)
		{
			SendMessage(dlg->GetHWND(), WM_CLOSE, 0, 0);
		}));
	}

	dlg->DoModal();

	return 0;
}

int imageViewDialog(HWND parent)
{
	EnableWindow(parent, FALSE);

	auto* dlg = CreateChronoContainer(parent, L"", 800, 600, true);

	HWND hwnd = dlg->GetHWND();

	// Main Layout: Title, Toolbar, Content Area
	auto* root = dlg->CreateRootLayout(3, 1);
	root->SetRow(0, WidgetSize::Fixed(getStandardMetric(StandardMetric::TitleBar)));
	root->SetRow(1, WidgetSize::Fixed(getStandardMetric(StandardMetric::Toolbar)));
	root->SetRow(2, WidgetSize::Fill());
	

	// --- ViewArea ---
	auto* viewArea = root->GetCell(2, 0)->CreateLayout(1, 2);

	// --- 1. Title Bar ---
	root->GetCell(0, 0)->SetProperty("title", "Image Compare - Horizontal View");
	root->GetCell(0, 0)->SetProperty("text-align", "center");

	// --- 2. Toolbar ---
	auto* toolbar = root->GetCell(1, 0)->CreateLayout(1, 4);
	{
		toolbar->SetCol(0, WidgetSize::Fixed(100));
		toolbar->SetCol(1, WidgetSize::Fixed(100));
		toolbar->SetCol(2, WidgetSize::Fill());
		toolbar->SetCol(3, WidgetSize::Fixed(50));

		toolbar->GetCell(0, 2)->SetProperty("title", "");

		toolbar->GetCell(0, 0)->AddWidget(
			WidgetFactory::Create("cw.Button.dll")->SetProperty("title", "Load Left")
			->addEventHandler("onClick", [hwnd, viewArea](IWidget* sender, const char* json)
			{
				viewArea->GetCell(0, 0)->GetWidget(0)->SetProperty("zoom-fit", "");
			}));
		toolbar->GetCell(0, 1)->AddWidget(
			WidgetFactory::Create("cw.Button.dll")->SetProperty("title", "Load Right")
			->addEventHandler("onClick", [hwnd, viewArea](IWidget* sender, const char* json)
			{
				viewArea->GetCell(0, 1)->GetWidget(0)->SetProperty("zoom-fit", "");
			}));
		
		toolbar->GetCell(0, 3)->AddWidget(
			WidgetFactory::Create("cw.Button.dll")->SetProperty("title", "Close")
			->addEventHandler("onClick", [hwnd, dlg](IWidget* sender, const char* json)
			{
				SendMessage(hwnd, WM_CLOSE, 0, 0);
			}));
	}

	// --- 3. Image Viewer Area (Horizontal Split) ---
	// Change: CreateLayout(1, 2) -> 1 Row, 2 Columns
	{
		// Define Column 0: Fills available space, Splitter = TRUE, Min Width = 100
		viewArea->SetCol(0, WidgetSize::Fill(), true, WidgetSize::Fixed(100));

		// Define Column 1: Fills remaining space
		viewArea->SetCol(1, WidgetSize::Fill());

		// --- Left Viewer (Column 0) ---
		viewArea->GetCell(0, 0)->AddWidget(
			WidgetFactory::Create("cw.ImageViewerWidget.dll")
			->SetProperty("image-path",WideToNarrow(LR"(..\assets\images\example1.jpg)").c_str())
			->SetProperty("zoom-fit", "")
		);

		// --- Right Viewer (Column 1) ---
		viewArea->GetCell(0, 1)->AddWidget(
			WidgetFactory::Create("cw.ImageViewerWidget.dll")
			// Using a placeholder path or the same one for demonstration
			->SetProperty("image-path",WideToNarrow(LR"(..\assets\images\example1.jpg)").c_str())
			->SetProperty("zoom-fit", "")
		);
	}

	dlg->CenterToParent();
	dlg->Show();
	dlg->RunMessageLoop();

	BringWindowToTop(parent);
	EnableWindow(parent, TRUE);

	return 0;
}

// Helper: Check if file extension is an image
bool IsImageFile(const fs::path& p)
{
	if (!p.has_extension()) return false;
	std::string s = p.extension().string();
	// Convert to lowercase for comparison
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return (char)::tolower(c); }
	);
	return (s == ".png" || s == ".jpg" || s == ".jpeg" || s == ".bmp" || s == ".gif");
}

// Helper: Open Windows Folder Picker
std::wstring OpenFolderDialog(HWND owner)
{
	std::wstring result = L"";
	IFileOpenDialog* pFileOpen;

	// Create the FileOpenDialog object.
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
		IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

	if (SUCCEEDED(hr))
	{
		DWORD dwOptions;
		if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions)))
		{
			// FOS_PICKFOLDERS ensures we select directories, not files
			pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
		}

		if (SUCCEEDED(pFileOpen->Show(owner)))
		{
			IShellItem* pItem;
			if (SUCCEEDED(pFileOpen->GetResult(&pItem)))
			{
				PWSTR pszFilePath;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)))
				{
					result = pszFilePath;
					CoTaskMemFree(pszFilePath);
				}
				pItem->Release();
			}
		}
		pFileOpen->Release();
	}
	return result;
}

int ImageViewerExample(HWND parent)
{
	EnableWindow(parent, FALSE);

	auto* dlg = CreateChronoContainer(parent, L"Image Browser", 1024, 768, true);
	
	HWND hwnd = dlg->GetHWND();

	// --- Layout ---
	// Row 0: Toolbar (50px)
	// Row 1: Content Area (Sidebar + Viewer)
	auto* root = dlg->CreateRootLayout(2, 1);

	root->SetRow(0, WidgetSize::Fixed(45));
	root->SetRow(1, WidgetSize::Fill());

	// --- Toolbar ---
	auto* toolbar = root->GetCell(0, 0)->CreateLayout(1, 3);

	toolbar->SetCol(0, WidgetSize::Fixed(150)); // Open Button
	toolbar->SetCol(1, WidgetSize::Fill());     // Path Label
	toolbar->SetCol(2, WidgetSize::Fixed(100)); // Close Button

	// --- Content Area (Splitter) ---
	auto* content = root->GetCell(1, 0)->CreateLayout(1, 2);
	// Col 0: File List (Sidebar), Col 1: Image Viewer
	content->SetCol(0, WidgetSize::Fixed(250), true, WidgetSize::Fixed(100));
	content->SetCol(1, WidgetSize::Fill());

	ICell* fileListCell = content->GetCell(0, 0);
	fileListCell->SetStackMode(ChronoUI::StackMode::Vertical);
	fileListCell->EnableScroll(true);
	// Style the sidebar slightly darker
	fileListCell->SetProperty("background-color", "#f0f0f0");

	ICell* imageCell = content->GetCell(0, 1);

	// Create the Image Widget once
	IWidget* imageWidget = WidgetFactory::Create("cw.ImageViewerWidget.dll");
	imageWidget->SetProperty("zoom-fit", "true");
	imageCell->AddWidget(imageWidget);

	// Label to show current path
	toolbar->GetCell(0, 1)->SetProperty("title", "No folder selected");

	// --- State Management ---
	// We use a shared_ptr to keep track of the widgets currently in the list
	// so we can remove them when loading a new folder.
	struct State {
		std::vector<IWidget*> listWidgets;
		std::wstring currentPath;
	};
	auto state = std::make_shared<State>();

	// --- Function to Load Files ---
	auto LoadFiles = [fileListCell, imageWidget, toolbar, state](std::wstring path)
	{
		// 1. Clear existing list widgets
		for (auto* w : state->listWidgets) {
			SafeDeleteWidget(fileListCell, w);
		}
		state->listWidgets.clear();
		state->currentPath = path;

		// Update Path Label
		toolbar->GetCell(0, 1)->SetProperty("title", WideToNarrow(path).c_str());

		// 2. Iterate Directory
		try {
			if (fs::exists(path) && fs::is_directory(path)) {
				for (const auto& entry : fs::directory_iterator(path)) {
					if (entry.is_regular_file() && IsImageFile(entry.path())) {
						std::wstring filename = entry.path().filename().wstring();
						std::wstring fullPath = entry.path().wstring();

						// Create a button for the file
						IWidget* btn = WidgetFactory::Create("cw.Button.dll");
						btn->SetProperty("title", WideToNarrow(filename).c_str());
						btn->SetProperty("align", "left");
						btn->SetProperty("margin-bottom", "1");
						btn->SetProperty("image_path", WideToNarrow(fullPath).c_str());

						// Event: Click to load image
						btn->addEventHandler("onClick", [imageWidget, fullPath](IWidget* sender, const char* json) {
							// HERE is the requested functionality
							imageWidget->SetProperty("image-path", WideToNarrow(fullPath).c_str());
						});

						fileListCell->AddWidget(btn);
						state->listWidgets.push_back(btn);
					}
				}
				fileListCell->UpdateWidgets();
			}
		}
		catch (...) {
			toolbar->GetCell(0, 1)->SetProperty("title", "Error accessing directory.");
		}
	};

	// --- Toolbar Buttons ---

	// [Open Folder]
	toolbar->GetCell(0, 0)->AddWidget(
		WidgetFactory::Create("cw.Button.dll")
		->SetProperty("title", "Open Folder...")
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\folder-open.png)").c_str())
		->addEventHandler("onClick", [hwnd, LoadFiles](IWidget* sender, const char* json) {
		std::wstring folder = OpenFolderDialog(hwnd);
		if (!folder.empty()) {
			LoadFiles(folder);
		}
	})
	);
	toolbar->GetCell(0, 1)->SetProperty("title", "No folder selected");
	// [Close]
	toolbar->GetCell(0, 2)->AddWidget(
		WidgetFactory::Create("cw.Button.dll")
		->SetProperty("title", "Close")
		->SetColor("face-color", RGB(222, 64, 64)) // Red button
		->addEventHandler("onClick", [hwnd](auto...) {
		SendMessage(hwnd, WM_CLOSE, 0, 0);
	})
	);

	// Initial load (Optional: load current directory)
	// LoadFiles(std::filesystem::current_path().wstring());

	dlg->CenterToParent();
	dlg->Show();
	dlg->RunMessageLoop();

	BringWindowToTop(parent);
	EnableWindow(parent, TRUE);

	return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) 
{
	std::shared_ptr<ChronoObservable<std::string>> editText = std::make_shared<ChronoObservable<std::string>>("");
	std::shared_ptr<ChronoObservable<std::string>> currentTime = std::make_shared<ChronoObservable<std::string>>("");
	std::shared_ptr<ChronoObservable<std::string>> title = std::make_shared<ChronoObservable<std::string>>("");
	std::shared_ptr<ChronoObservable<bool>> isWorking = std::make_shared<ChronoObservable<bool>>(false);
	std::shared_ptr<ChronoObservable<bool>> testing = std::make_shared<ChronoObservable<bool>>(false);
	std::shared_ptr<ChronoObservable<std::string>> snowFreq = std::make_shared<ChronoObservable<std::string>>("50");

	std::shared_ptr<ChronoObservable<std::string>> progTitle = std::make_shared<ChronoObservable<std::string>>("");

	StyleManager::LoadCSSFile("..\\assets\\bootstrap_lite.css");

	myVirtualDrive.Open(L"..\\assets\\resources.pak", nullptr);

	auto* win = CreateChronoContainer(GetDesktopWindow(), L"ChronoUI - Tabbed Workspace", 1280, 720, true);

	// Not ready yet, hard flickering !!
	//win->SetOverlay(WidgetFactory::Create("cw.WaitingOverlay.dll"))->Bind("waiting", isWorking);

	auto* root = win->CreateRootLayout(4, 1);

	HWND hwnd = win->GetHWND();

	root->SetRow(0, WidgetSize::Fixed(getStandardMetric(StandardMetric::TitleBarThin)));	// Toolbar
	root->SetRow(1, WidgetSize::Fixed(getStandardMetric(StandardMetric::Toolbar)));	// Title Bar
	root->SetRow(2, WidgetSize::Fill(), false, WidgetSize::Fill(0.25f)); // Main Panel
	root->SetRow(3, WidgetSize::Fixed(64), false, WidgetSize::Fixed(50.0)); // Footer

	// --- 1. TOOLBAR ---
	auto* toolbar = root->GetCell(0, 0)->CreateLayout(1, 3);
	{
		toolbar->SetProperty("border-width", "0");

		toolbar->SetCol(0, WidgetSize::Fill());
		toolbar->SetCol(1, WidgetSize::Fill());
		toolbar->SetCol(2, WidgetSize::Fixed(getStandardMetric(StandardMetric::WindowMenu)));

		auto leftBar = toolbar->GetCell(0, 0);
		auto titleBar = toolbar->GetCell(0, 1);
		auto rightBar = toolbar->GetCell(0, 2);

		leftBar->SetStackMode(ChronoUI::StackMode::CommandBar);
		
		rightBar->SetStackMode(ChronoUI::StackMode::CommandBar);
		rightBar->SetProperty("overflow", "false"); // Disable overflow handling
		rightBar->SetProperty("justify-content", "right");

		auto btnOpen = WidgetFactory::Create("cw.Button.dll");
		leftBar->AddWidget(btnOpen)
			->SetProperty("tt_title", "Open Image viewer")
			->SetProperty("arrow", "true")
			->SetProperty("border-width", "1px")
			->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\folder-open-outline.png)").c_str())
			->Bind("disabled", isWorking)
			->addEventHandler("onClick", [hwnd](auto...)
		{
			ImageViewerExample(hwnd);
		});
		StyleManager::AddClass(btnOpen, "btn-primary");

		/*
		leftBar->AddWidget(
			WidgetFactory::Create("cw.SwitchButton.dll")
			->SetProperty("title", "SetWorking")
			->Bind("checked", isWorking)
			->addEventHandler("onChange", [win, isWorking](IWidget* sender, const char* json)
		{
			std::string j = json;
			if (j == "true") {
				*isWorking = true;
			} else {
				*isWorking = false;
			}
		}));
		
		leftBar->AddWidget(
			WidgetFactory::Create("cw.Button.dll")
			->SetProperty("title", "Dark")
			->SetProperty("align", "left")
			->Bind("disabled", isWorking)
			->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\lightbulb.png)").c_str())
			->addEventHandler("onClick", [win, testing](auto...)
		{
			*testing = !*testing;
			setDarkStyle(win);
		}));
		*/

		leftBar->AddWidget(
			WidgetFactory::Create("cw.Button.dll")
			->SetProperty("title", "Dialog")
			->SetProperty("align", "left")
			->Bind("disabled", isWorking)
			->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\window-closed-variant.png)").c_str())
			->addEventHandler("onClick", [&](auto...)
		{ 
			funMessageBox(win->GetHWND(), "Success", "User deleted successfully.", CMB_OK | CMB_ICON_SUCCESS, &myVirtualDrive);
		}));

		leftBar->AddWidget(WidgetFactory::Create("cw.EyesControl.dll"))
			->AddOverlay(
				WidgetFactory::Create("cw.LightingStormOverlay.dll")
				->Bind("active", isWorking)
				->SetProperty("intensity", "200")
			);
		
		titleBar->AddWidget(
			WidgetFactory::Create("cw.StaticText.dll"))
			->Bind("title", title)
			->AddTimer("UptTitle", 1000)
			->addEventHandler("UptTitle", [&](IWidget* sender, const char* json) {
			auto now = system_clock::now();
			std::time_t t = system_clock::to_time_t(now);
			std::tm tm{};
			localtime_s(&tm, &t);   // MSVC-safe
			char buf[64];
			std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
			*currentTime = buf;
			*title = std::string("Window Title - Now: ") + buf;
			});

		
		/*
		rightBar->AddWidget(
			WidgetFactory::Create("cw.Button.dll")
			->SetProperty("width", std::to_string(getStandardMetric(StandardMetric::CaptionButton)).c_str())
			->SetProperty("tt_title", "Light")
			->Bind("disabled", isWorking)
			->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\lightbulb-on-10.png)").c_str())
			->addEventHandler("onClick", [win](auto...)
		{
			setLightStyle(win);
		}));
		rightBar->AddWidget(
			WidgetFactory::Create("cw.Button.dll")
			->SetProperty("width", std::to_string(getStandardMetric(StandardMetric::CaptionButton)).c_str())
			->SetProperty("tt_title", "Dark")
			->SetProperty("align", "left")
			->Bind("disabled", isWorking)
			->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\lightbulb.png)").c_str())
			->addEventHandler("onClick", [win](auto...)
		{
			setDarkStyle(win);
		}));
		*/

		// Minimize
		rightBar->AddWidget(
			WidgetFactory::Create("cw.Button.dll")
			->SetProperty("width", std::to_string(getStandardMetric(StandardMetric::CaptionButton)).c_str())
			->SetProperty("tt_title", "Minimize")
			->SetProperty("align", "left")
			->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\window-minimize.png)").c_str())
			->addEventHandler("onClick", [hwnd](auto...) 
			{ ShowWindow(hwnd, SW_MINIMIZE); }));

		rightBar->AddWidget(
			WidgetFactory::Create("cw.Button.dll")
			->SetProperty("width", std::to_string(getStandardMetric(StandardMetric::CaptionButton)).c_str())
			->SetProperty("tt_title", "Maximize/Restore")
			->SetProperty("image_base64", 
				myVirtualDrive.GetBase64(LR"(\mdifont48\window-restore.png)").c_str()
			)
			->addEventHandler("onClick", [hwnd, win](IWidget* sender, const char* json)
			{ 
				ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); 
				sender->SetProperty("image_base64", IsZoomed(win->GetHWND()) ? myVirtualDrive.GetBase64(LR"(\mdifont48\window-restore.png)").c_str() : myVirtualDrive.GetBase64(LR"(\mdifont48\window-maximize.png)").c_str());
			})
		);
		rightBar->AddWidget(
			WidgetFactory::Create("cw.Button.dll")
			->SetProperty("width", std::to_string(getStandardMetric(StandardMetric::CaptionButton)).c_str())
			->SetProperty("debugid", "Close1")
			->Bind("disabled", isWorking)
			->SetProperty("tt_title", "Close")
			->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\window-close.png)").c_str())
			->addEventHandler("onClick", [](auto...) { PostQuitMessage(0); })
			->SetColor("face-color", RGB(222, 64, 64))
			->SetColor("face-color:hover", RGB(255, 64, 64))
			->SetColor("foreground-color", RGB(0, 0, 0))
		);
	}

	// --- 2. TITLE BAR ---
	ICell* titleBar = root->GetCell(1, 0);

	titleBar->SetStackMode(ChronoUI::StackMode::CommandBar);

	titleBar->SetProperty("align-items", "normal");
	titleBar->SetProperty("justify-content", "right");
	titleBar->SetProperty("overflow", "true"); // Disable overflow handling

	titleBar->AddWidget(WidgetFactory::Create("cw.StaticText.dll"))
		->SetProperty("title", "Main Title Bar")
		->SetProperty("font-style", "bold")
		->SetProperty("width", "auto")
		->SetProperty("text-align", "center")
		->addEventHandler("onClick", [hwnd](IWidget* sender, const char* json)
		{  
			sender->SetProperty("title", "Main Title Bar clicked!!");
		}
		);

	titleBar->AddWidget(WidgetFactory::Create("cw.EditBox.dll"))
		->Bind("value", editText)
		->Bind("disabled", isWorking)
		->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\magnify.png)").c_str())
		->SetProperty("height", std::to_string(getStandardMetric(StandardMetric::EditBoxHeight)).c_str())
		->SetProperty("width", "220")
		->SetProperty("placeholder", "Write your search here");

	for (int x = 0; x < 20; x++) {
		std::string btnTitle = "Button " + std::to_string(x + 1);
		titleBar->AddWidget(
			WidgetFactory::Create("cw.Button.dll")
			->SetProperty("title", btnTitle.c_str())
			->SetProperty("width", "120px")
			->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\lightbulb.png)").c_str())
			->addEventHandler("onClick", [hwnd, btnTitle, titleBar](IWidget* sender, const char* json)
				{
					titleBar->GetWidget(0)->SetProperty("title", btnTitle.c_str());
				}
			));
	}

	// --- 3. WORKSPACE ---
	{
		auto* work = root->GetCell(2, 0)->CreateLayout(1, 3);

		// Sidebar (Col 0)
		work->SetCol(0, WidgetSize::Fixed(200), false, WidgetSize::Fixed(64));
		// Viewer
		work->SetCol(1, WidgetSize::Fill(0.8f), true, WidgetSize::Percent(30));
		// Properties/Inspector (Col 2)
		work->SetCol(2, WidgetSize::Fill(0.2f), false, WidgetSize::Percent(10));

		ICell* sidebar = work->GetCell(0, 0);
		{
			sidebar->SetStackMode(ChronoUI::StackMode::Vertical);
			sidebar->EnableScroll(true);

			sidebar->AddWidget(WidgetFactory::Create("cw.Button.dll"))
				->SetProperty("title", "Expand/Collapse")
				->SetProperty("align", "right")
				->SetProperty("is_pill", "true")
				->SetProperty("border-width", "0")
				->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\arrow-collapse.png)").c_str())
				->addEventHandler("onClick", [work](IWidget* sender, const char* json)
			{
				if (work->IsColumnCollapsed(2))
					work->RestoreColumn(2);
				else
					work->CollapseColumn(2);

				if (work->IsColumnCollapsed(0))
					work->RestoreColumn(0);
				else
					work->CollapseColumn(0);
			}
				)
				->SetColor("StaticText:background-color", RGB(255, 200, 200));

			sidebar->AddWidget(WidgetFactory::Create("cw.Button.dll"))
				->SetProperty("title", "ImageViewer Example")
				->SetProperty("is_pill", "true")
				->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\image-album.png)").c_str())
				->SetColor("StaticText:background-color", RGB(255, 200, 200))
				->addEventHandler("onClick", [hwnd](IWidget* sender, const char* json) {ImageViewerExample(hwnd);
			});

			sidebar->AddWidget(WidgetFactory::Create("cw.Button.dll"))
				->SetProperty("title", "5 chan Equalizer dialog")
				->SetProperty("is_pill", "true")
				->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\soundbar.png)").c_str())
				->addEventHandler("onClick", [hwnd](IWidget* sender, const char* json) {
					exampleEqualizer(hwnd);
			});
			sidebar->AddWidget(WidgetFactory::Create("cw.Button.dll"))
				->SetProperty("title", "Gauges dialog")
				->SetProperty("is_pill", "true")
				->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\speedometer.png)").c_str())
				->addEventHandler("onClick", [hwnd](IWidget* sender, const char* json) {
				exampleGauges(hwnd);
			});			
			sidebar->AddWidget(WidgetFactory::Create("cw.Button.dll"))
				->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\incognito.png)").c_str())
				->SetProperty("title", "Login dialog")
				->SetProperty("arrow", "true")
				->SetProperty("is_pill", "true")
				->SetColor("StaticText:background-color", RGB(100, 200, 110))
				->addEventHandler("onClick", [&](IWidget* sender, const char* json) {
				// Example usage inside wWinMain or an event handler:
				std::string user = "";
				std::string pass = "";

				if (ExampleLoginDlg(win->GetHWND(), user, pass)) {
					// Login successful
					std::string msg = "Login Successful!\nUser: " + user;
					funMessageBox(win->GetHWND(), msg.c_str(), "Success", CMB_OK, &myVirtualDrive);

					// Update main window title or state here
					*title = "Logged in as: " + user;
				}
				else {
					// User clicked Cancel or closed the window
					funMessageBox(win->GetHWND(), "Login Cancelled", "Info", CMB_OK, &myVirtualDrive);
				}
			});

			sidebar->AddWidget(WidgetFactory::Create("cw.Button.dll"))
				->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\account-box-plus-outline.png)").c_str())
				->SetProperty("title", "Create new account example")
				->SetProperty("arrow", "true")
				->SetProperty("is_pill", "true")
				->SetColor("StaticText:background-color", RGB(100, 200, 110))
				->addEventHandler("onClick", [&](IWidget* sender, const char* json) {
				std::string newEmail, newPass;
				if (ExampleRegisterDlg(win->GetHWND(), newEmail, newPass)) {
					std::string msg = "Registration Complete!\nUser: " + newEmail;
					funMessageBox(
						win->GetHWND(),
						msg.c_str(), "Welcome", CMB_OK | CMB_ICON_INFO, &myVirtualDrive);
				}
			});

			sidebar->AddWidget(WidgetFactory::Create("cw.Button.dll"))
				->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\message-alert-outline.png)").c_str())
				->SetProperty("title", "MessageBox Question")
				->SetProperty("is_pill", "true")
				->addEventHandler("onClick", [&](IWidget* sender, const char* json) {
				int choice = funMessageBox(
					win->GetHWND(),
					"Delete Account?",
					"Are you sure you want to permanently delete this user? This action cannot be undone.",
					CMB_YESNO | CMB_ICON_WARNING, &myVirtualDrive
				);

				if (choice == IDYES) {
					funMessageBox(win->GetHWND(), "Success", "User deleted successfully.", CMB_OK | CMB_ICON_SUCCESS);
				}
			});

			sidebar->AddWidget(WidgetFactory::Create("cw.Button.dll"))
				->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\message-alert-outline.png)").c_str())
				->SetProperty("title", "MessageBox Error")
				->SetProperty("is_pill", "true")
				->addEventHandler("onClick", [&](IWidget* sender, const char* json) {
				funMessageBox(
					win->GetHWND(),
					"Connection Failed",
					"Unable to reach the remote database.\nPlease check your internet connection and try again.",
					CMB_OK | CMB_ICON_ERROR
				);
			});

			sidebar->AddWidget(
				WidgetFactory::Create("cw.StaticText.dll")
				->SetProperty("title", "- Test edit box, write 'ok' -")
				->SetProperty("text-align", "center")
				->SetProperty("height", "20")
			);
			sidebar->AddWidget(WidgetFactory::Create("cw.EditBox.dll"))
				->Bind("value", editText)
				->Bind("disabled", isWorking)
				->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\magnify.png)").c_str())
				->SetProperty("height", std::to_string(getStandardMetric(StandardMetric::EditBoxHeight)).c_str())
				->SetProperty("placeholder", "Write ok to validate edit box")
				->OnValidate([&](IWidget* sender, const char* payload) {
				std::string val = payload;

				if (val != "ok") {
					// Set error state
					sender->SetProperty("validation-error", "Value must be 'ok'");
					return false;
				}
				// You can use the helper mentioned in the prompt
				return true;
			});
			


			sidebar->AddWidget(WidgetFactory::Create("cw.TextSlider.dll"))
				->Bind("text", currentTime);

			static CpuMonitor g_cpuMonitor;

			// 2. Add the Widget
			sidebar->AddWidget(WidgetFactory::Create("cw.DataPlotControl.dll"))
				// --- Visual Configuration ---
				->SetProperty("label_y", "CPU Load")
				->SetProperty("label_x", "History (60s)")
				->SetProperty("units", "%")
				->SetProperty("min", "0")
				->SetProperty("max", "100")
				->SetProperty("steps", "60")           // Keep last 60 data points
				->SetProperty("plot_color", "#00FFEA") // Cyan/Electric Blue line
				->SetProperty("grid_color", "#223333") // Dark teal grid

				// --- Logic Configuration ---
				->AddTimer("UpdateCpu", 1000)          // Tick every 1000ms (1 second)
				->addEventHandler("UpdateCpu", [&](IWidget* sender, const char* json) {

				// 1. Get real CPU data
				double usage = g_cpuMonitor.GetUsage();

				// 2. Format to string
				std::string valStr = std::to_string(usage);

				// 3. Push to plot
				// This triggers the "add_value" logic in OnPropertyChanged
				sender->SetProperty("add_value", valStr.c_str());
			});

			sidebar->AddWidget(WidgetFactory::Create("cw.SliderControl.dll"))
				->Bind("value", snowFreq);

			// 1. Create a container (assuming you have a 'row' or 'sidebar')
			auto clockWidget = WidgetFactory::Create("cw.AnalogClock.dll");
			// 4. Add to UI
			sidebar->AddWidget(clockWidget);
			clockWidget->AddOverlay(
				WidgetFactory::Create("cw.SnowingOverlay.dll")
				->SetProperty("active", "true")
				->Bind("freq", snowFreq)
				->SetProperty("size", "4")
			);
			clockWidget->SetProperty("width", "100%");        // Set CSS size
			clockWidget->SetProperty("height", "150");
			clockWidget->SetProperty("face_color", "#E0E0E0"); // Light gray ring
			clockWidget->SetProperty("hand_color", "#FFFFFF"); // White hands
			clockWidget->SetProperty("show_seconds", "true");  // Enable second hand

			// 3. Add Event Handler
			clockWidget->addEventHandler("onClick", [](IWidget* sender, const char* json) {
				MessageBoxA(NULL, "Time waits for no one!", "Clock Clicked", MB_OK | MB_ICONINFORMATION);
			});
		}


		// --- MAIN VIEW AREA (Col 1) ---
		{
			// Create a layout inside the main area: Row 0 for Selector, Row 1 for Content
			auto* mainLayout = work->GetCell(0, 1)->CreateLayout(2, 1);

			mainLayout->SetRow(0, WidgetSize::Fixed(25)); // Tab Selector Height
			mainLayout->SetRow(1, WidgetSize::Fill());    // Tab Content Area

			// Tab Selector (Row 0)
			ICell* tabSelector = mainLayout->GetCell(0, 0);
			tabSelector->SetStackMode(ChronoUI::StackMode::Horizontal);
			//tabSelector->EnableScroll(true);

			// 2. Create Buttons and Link them via Events
			const wchar_t* labels[] = { L" Main.cpp ", L" App.config ", L" Style.css " };

			// Tab Content (Row 1)
			ICell* tabContent = mainLayout->GetCell(1, 0);

			{
				tabContent->SetStackMode(StackMode::Tabbed); // Documents are stacked
				tabContent->AddWidget(WidgetFactory::Create("cw.ImageViewerWidget.dll"))
					->AddOverlay(
						WidgetFactory::Create("cw.WaitingOverlay.dll")
						->Bind("waiting", isWorking)
					)
					->SetProperty("image-path", 
						WideToNarrow(
							LR"(..\assets\images\example1.jpg)"
						).c_str());

				tabContent->AddWidget(
					WidgetFactory::Create("cw.StaticText.dll")
					->SetProperty("title", "// Main.cpp code editor view")
					->SetColor("StaticText:background-color", RGB(192, 30, 192))
				);
				tabContent->AddWidget(
					WidgetFactory::Create("cw.StaticText.dll")
					->SetProperty("title", "# CSS Stylesheet View #")
					->SetColor("StaticText:background-color", RGB(25, 192, 192))
				);

				// We store args in a static or long-lived container so pointers remain valid
				static std::map<int, IWidget*> tabButtons;
				for (int i = 0; i < 3; i++) {
					IWidget* btn = WidgetFactory::Create("cw.Button.dll")
						->SetProperty("Button:font-style", "bold")
						->SetProperty("title", WideToNarrow(labels[i]).c_str());
					btn->addEventHandler("onClick", [=](IWidget* sender, const char* json) mutable {
						for (int x = 0; x < 3; x++) {
							tabButtons[x]->SetProperty("checked", (sender== tabButtons[x])?"true":"false");
						}
						tabContent->SetActiveTab(i);
					});

					tabSelector->AddWidget(btn);

					tabButtons[i] = btn;
				}

				// Set default active document
				tabContent->SetActiveTab(0);
			}
		}

		// Properties Panel (Col 2)
		ICell* props = work->GetCell(0, 2);
		props->SetStackMode(ChronoUI::StackMode::Vertical);
		props->EnableScroll(true);

		props->AddWidget(WidgetFactory::Create("cw.AnimatedParticlesProgress.dll"))
			->SetProperty("title", "cw.AnimatedParticlesProgress.dll")
			->SetColor("background-color", RGB(70, 255, 80));

		props->AddWidget(WidgetFactory::Create("cw.VitalsMonitor.dll"));

		props->AddWidget(WidgetFactory::Create("cw.TitleDescCard.dll"))
			->SetProperty("height", "140")
			->AddOverlay(
				WidgetFactory::Create("cw.SnowingOverlay.dll")
				->SetProperty("freq", "80")
				->SetProperty("size", "2")
			)
			->SetProperty("title", "Snowing overlay")
			->SetProperty("top_text", "Top text")
			->SetProperty("description", "This panel displays contextual information related to the selected item.\r\nUse it to review details, status, and relevant metadata before taking action.\r\nChanges made here are applied immediately and may affect related elements.")
			->SetProperty("image_base64", myVirtualDrive.GetBase64(LR"(\mdifont48\spider-web.png)").c_str());

		props->AddWidget(WidgetFactory::Create("cw.TitleDescCard.dll"))
			->SetProperty("height", "140")
			->Bind("title", currentTime)
			->SetProperty("top_text", "No image")
			->SetProperty("description", "This panel displays contextual information related to the selected item.\r\nUse it to review details, status, and relevant metadata before taking action.\r\nChanges made here are applied immediately and may affect related elements.");

		{
			// 1. Create the List Control
			auto myList = WidgetFactory::Create("cw.ListCards.dll");

			// 2. Configure Layout Properties
			myList->SetProperty("height", "500");

			// 3. Register Images (Format: "imageID|base64String")
			// Assuming 'myVirtualDrive' is your resource helper from the previous snippet
			std::string iconUser = "img_user|" + myVirtualDrive.GetBase64(LR"(\mdifont48\soy-sauce.png)");
			std::string iconSettings = "img_settings|" + myVirtualDrive.GetBase64(LR"(\mdifont48\spa-outline.png)");
			std::string iconAlert = "img_alert|" + myVirtualDrive.GetBase64(LR"(\mdifont48\sticker-alert-outline.png)");

			myList->SetProperty("addImage", iconUser.c_str());
			myList->SetProperty("addImage", iconSettings.c_str());
			myList->SetProperty("addImage", iconAlert.c_str());

			// 4. Add Items (Format: "id|title|description|imageID")
			// Note: The pipe '|' is the delimiter used in the ListCards::OnPropertyChanged logic
			for (int x = 0; x < 10; x++) {
				myList->SetProperty("addItem", "usr_01|cw.ListCards.dll|A simple card list control|img_user");
				myList->SetProperty("addItem", "sys_conf|Configuration|Modify system preferences and defaults|img_settings");
				myList->SetProperty("addItem", "alert_99|System Update|Critical security patch available|img_alert");
				myList->SetProperty("addItem", "note_01|Simple Note|This item has no icon attached|");
			}
			

			// 5. Handle Events
			myList->addEventHandler("onItemClick", [](IWidget* widget, const char* jsonArgs) {
				// jsonArgs example: {"index":1,"id":"sys_conf"}
			});

			// 6. Add to layout
			props->AddWidget(myList);
		}

		for (int i = 1; i <= 20; i++) {
			std::wstring prop = L"Properties " + std::to_wstring(i);

			props->AddWidget(WidgetFactory::Create("cw.StaticText.dll"))
				->SetProperty("title", WideToNarrow(prop).c_str())
				->SetColor("StaticText:background-color", RGB(70, 70 + (i * 20), 80))
				->SetColor("StaticText:foreground-color", RGB(70, 70 - (i * 20), 80));
		}
	}

	// --- 4. BOTTOM BAR ---
	ICell* bottomBar = root->GetCell(3, 0);
	bottomBar->SetStackMode(ChronoUI::StackMode::Horizontal);
	bottomBar->EnableScroll(true);
	
	{
		IWidget* widget = WidgetFactory::Create("cw.ViewDateTimeWidget.dll");
		widget->SetProperty("text-align", "right");
		bottomBar->AddWidget(widget);
	}
	{
		IWidget* widget = WidgetFactory::Create("cw.AnimatedParticlesProgress.dll")
			->Bind("title", progTitle)
			->SetProperty("justify-content", "flex-start")
			->SetColor("background-color", RGB(190, 255, 190))
			->SetColor("foreground-color", RGB(0, 0, 0));

		bottomBar->AddWidget(widget);
	}
	{
		float progress = 0.0f;
		bottomBar->AddWidget(WidgetFactory::Create("cw.Progress.dll"))
			->SetProperty("title", "In progress...")
			->AddTimer("Step", 100)
			->addEventHandler("Step", [&](IWidget* sender, const char* json) {
				progress++;
				if (progress > 100.0f) progress = 0.0f;
				sender->SetProperty("value", std::to_string(progress).c_str());
				*progTitle = "Progress:\r\n" + std::to_string((int)progress) + "%";
		});
	}
	for (int i = 1; i <= 35; i++) {
		std::wstring status = L"Status log " + std::to_wstring(i + 1);
		IWidget* widget = WidgetFactory::Create("cw.StaticText.dll")
			->SetProperty("debugid", "22")
			->SetProperty("title", WideToNarrow(status).c_str())
			->SetColor("StaticText:background-color", RGB(0 + (i * 25), 80, 120))
			->SetColor("StaticText:foreground-color", RGB(40 - (i * 25), 70, 40 - (i * 25)));
		bottomBar->AddWidget(widget);
	}

	win->Maximize();
	win->RunMessageLoop();

	delete win;

	return 0;
}
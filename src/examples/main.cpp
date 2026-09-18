#include <string>
#include <list>
#include <filesystem>
#include <shobjidl.h> 
#include <vector>
#include <chrono>
#include <algorithm>
#include <regex>
#include <cctype>
#include <cmath>
#include <functional>
#include <memory>

#include <pdh.h>
#pragma comment(lib, "pdh.lib")

// This alias fixes the "fs is not a class or namespace" error
namespace fs = std::filesystem;

#include "ChronoUI.hpp"
#include "ChronoStyles.hpp"
#include "virtual_drive.hpp"
#include "funMessageBox.hpp"
#include "AppPaths.hpp"   // AssetPathA / AssetPathW — resolve assets next to the exe

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


// =============================================================================
// ChronoUIDemo — the 23 DLL widgets, one family per page.
//
// This is the older widget model: one HWND per widget, created by name from
// its DLL, styled with CSS classes (assets/bootstrap_lite.css) and driven
// through string properties that every widget describes in a JSON manifest.
// The window is a ChronoUI container with a root layout: a header row, a
// body (a sidebar of pill buttons and a tabbed cell of pages), and a footer.
// Each page is an IPanel with a grid; each card in the grid is a nested
// two-row layout, a caption above one widget with a few properties set.
// =============================================================================
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int)
{
	// The image viewer creates its WIC factory through COM; without an
	// apartment on this thread that CoCreateInstance fails silently and every
	// image stays blank. Nothing in the framework initialises COM for you.
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	StyleManager::LoadCSSFile(AssetPathA("bootstrap_lite.css").c_str());
	myVirtualDrive.Open(AssetPathW(L"resources.pak").c_str(), nullptr);
	auto icon = [](const wchar_t* name) {
		return myVirtualDrive.GetBase64((std::wstring(LR"(\mdifont48\)") + name + L".png").c_str());
	};

	// State shared with widgets through observables: set the variable, the UI follows.
	auto status      = std::make_shared<ChronoObservable<std::string>>("23 widgets, one ChronoUI.dll, styled by assets/bootstrap_lite.css.");
	auto sliderValue = std::make_shared<ChronoObservable<std::string>>("50");
	auto sliderText  = std::make_shared<ChronoObservable<std::string>>("Value: 50");
	auto switchOn    = std::make_shared<ChronoObservable<bool>>(true);
	auto waiting     = std::make_shared<ChronoObservable<bool>>(true);

	const COLORREF kInk = RGB(17, 24, 39), kMuted = RGB(107, 114, 128), kBorder = RGB(229, 231, 235);
	auto text = [&](const char* s, int size, bool bold, COLORREF col) {
		IWidget* t = WidgetFactory::Create("cw.StaticText.dll");
		t->SetProperty("title", s)->SetProperty("font-size", std::to_string(size).c_str())->SetProperty("text-align", "left");
		if (bold) t->SetProperty("font-style", "bold");
		t->SetColor("foreground-color", col);
		return t;
	};

	// Cell padding only insets a nested layout, so an inset area is a 1x1 layout
	// inside a padded cell; the returned cell is the one to stack widgets in.
	auto inset = [](ICell* cell, const char* pad) -> ICell* {
		cell->SetProperty("padding", pad);
		ILayout* l = cell->CreateLayout(1, 1);
		l->SetProperty("border-width", "0");
		return l->GetCell(0, 0);
	};

	auto* win = CreateChronoContainer(GetDesktopWindow(), L"ChronoUI \x2014 Widgets", 1280, 820, false);
	HWND hwnd = win->GetHWND();
	auto* root = win->CreateRootLayout(3, 1);
	root->SetProperty("border-width", "0");
	root->SetRow(0, WidgetSize::Fixed(88));
	root->SetRow(1, WidgetSize::Fill());
	root->SetRow(2, WidgetSize::Fixed(40));

	// --- header ------------------------------------------------------------
	{
		ICell* h = inset(root->GetCell(0, 0), "16");
		h->SetStackMode(StackMode::Vertical);
		h->SetProperty("gap", "2");
		h->AddWidget(text("ChronoUI widgets", 20, true, kInk))->SetProperty("height", "30");
		h->AddWidget(text("The 23 hot-pluggable DLL widgets, one family per page. Every card is one widget created by name with a few properties set.", 11, false, kMuted))->SetProperty("height", "18");
	}

	// --- body: sidebar + pages ------------------------------------------------
	auto* body = root->GetCell(1, 0)->CreateLayout(1, 2);
	body->SetProperty("border-width", "0");
	body->SetColor("background-color", RGB(243, 243, 243));   // the page; cards sit white on it
	body->SetCol(0, WidgetSize::Fixed(224));
	body->SetCol(1, WidgetSize::Fill());
	ICell* side = inset(body->GetCell(0, 0), "12");
	side->SetStackMode(StackMode::Vertical);
	side->SetProperty("gap", "6");
	ICell* pages = body->GetCell(0, 1);
	pages->SetStackMode(StackMode::Tabbed);

	static std::vector<IWidget*> nav;
	// A page = a pill button in the sidebar + an IPanel with a grid in the tabbed cell.
	auto addPage = [&](const char* title, const wchar_t* iconName, int rows, int cols) -> ILayout* {
		IPanel* panel = CreateChronoPanel(win);
		pages->AddWidget(panel);
		ILayout* grid = panel->CreateLayout(rows, cols);
		grid->SetProperty("border-width", "0");
		grid->SetProperty("row-gap", "14"); grid->SetProperty("col-gap", "14");
		int index = (int)nav.size();
		IWidget* b = WidgetFactory::Create("cw.Button.dll");
		b->SetProperty("title", title)->SetProperty("is_pill", "true")->SetProperty("align", "left")
		 ->SetProperty("image_base64", icon(iconName).c_str())->SetProperty("height", "40");
		b->addEventHandler("onClick", [index, pages](IWidget* sender, const char*) {
			for (auto* n : nav) n->SetProperty("checked", n == sender ? "true" : "false");
			pages->SetActiveTab(index);
		});
		side->AddWidget(b);
		nav.push_back(b);
		return grid;
	};
	// A card = a white block on the grey page: a 2x1 layout, caption row above
	// the content cell. Colours and properties set on a layout are inherited by
	// everything inside it (cells and widgets alike), which is why the card's
	// border-width is pinned to 0: otherwise every widget in it would draw one.
	auto card = [&](ILayout* grid, int r, int c, const char* caption) -> ICell* {
		ILayout* k = grid->GetCell(r, c)->CreateLayout(2, 1);
		k->SetProperty("border-width", "0");
		k->SetColor("background-color", RGB(255, 255, 255));
		k->SetRow(0, WidgetSize::Fixed(30));
		k->SetRow(1, WidgetSize::Fill());
		inset(k->GetCell(0, 0), "8")->AddWidget(text(caption, 10, true, kMuted))->SetProperty("width", "330");
		return inset(k->GetCell(1, 0), "10");
	};

	// --- 1. Gauges & time -------------------------------------------------------
	{
		ILayout* g = addPage("Gauges & time", L"speedometer", 2, 3);
		auto* speed = card(g, 0, 0, "cw.GaugeSpeedOmeter")->AddWidget(WidgetFactory::Create("cw.GaugeSpeedOmeter.dll"));
		speed->SetProperty("label", "Speed")->SetProperty("unit", "km/h")->SetProperty("min", "0")->SetProperty("max", "240")->SetProperty("value", "120");
		auto phase = std::make_shared<float>(0.0f);
		speed->AddTimer("tick", 50)->addEventHandler("tick", [phase](IWidget* s, const char*) {
			*phase += 0.04f;
			s->SetProperty("value", std::to_string((int)(120.0f + 95.0f * std::sin(*phase))).c_str());
		});
		card(g, 0, 1, "cw.GaugeEngineTemperatureControl")->AddWidget(WidgetFactory::Create("cw.GaugeEngineTemperatureControl.dll"))
			->SetProperty("label", "Engine")->SetProperty("unit", "C")->SetProperty("min", "40")->SetProperty("max", "130")
			->SetProperty("warning", "110")->SetProperty("value", "92");
		card(g, 0, 2, "cw.GaugeBatteryLevelControl")->AddWidget(WidgetFactory::Create("cw.GaugeBatteryLevelControl.dll"))
			->SetProperty("label", "Battery")->SetProperty("unit", "%")->SetProperty("value", "64")->SetProperty("lowWarning", "20");
		card(g, 1, 0, "cw.AnalogClock")->AddWidget(WidgetFactory::Create("cw.AnalogClock.dll"))->SetProperty("show_seconds", "true");
		card(g, 1, 1, "cw.ViewDateTimeWidget")->AddWidget(WidgetFactory::Create("cw.ViewDateTimeWidget.dll"));
		static CpuMonitor cpu;
		card(g, 1, 2, "cw.DataPlotControl, live CPU")->AddWidget(WidgetFactory::Create("cw.DataPlotControl.dll"))
			->SetProperty("label_y", "CPU")->SetProperty("label_x", "last 60 s")->SetProperty("units", "%")
			->SetProperty("min", "0")->SetProperty("max", "100")->SetProperty("steps", "60")
			->AddTimer("cpu", 1000)->addEventHandler("cpu", [](IWidget* s, const char*) {
				s->SetProperty("add_value", std::to_string(cpu.GetUsage()).c_str());
			});
	}

	// --- 2. Progress & motion ---------------------------------------------------
	{
		ILayout* g = addPage("Progress & motion", L"soundbar", 2, 3);
		auto pv = std::make_shared<int>(0);
		ICell* pc = card(g, 0, 0, "cw.Progress"); pc->SetStackMode(StackMode::Vertical);
		pc->AddWidget(WidgetFactory::Create("cw.Progress.dll"))
			->SetProperty("show_text", "true")->SetProperty("value", "0")->SetProperty("height", "30")
			->AddTimer("tick", 80)->addEventHandler("tick", [pv](IWidget* s, const char*) {
				*pv = (*pv + 1) % 101; s->SetProperty("value", std::to_string(*pv).c_str());
			});
		ICell* ac = card(g, 0, 1, "cw.AnimatedParticlesProgress"); ac->SetStackMode(StackMode::Vertical);
		ac->AddWidget(WidgetFactory::Create("cw.AnimatedParticlesProgress.dll"))
			->SetProperty("title", "Working...")->SetProperty("height", "60")
			->SetProperty("background-color", "#DBEAFE")->SetProperty("foreground-color", "#111827");
		auto ev = std::make_shared<float>(0.0f);
		card(g, 0, 2, "cw.EqualizerBar")->AddWidget(WidgetFactory::Create("cw.EqualizerBar.dll"))
			->SetProperty("vertical", "false")->SetProperty("segments", "24")->SetProperty("value", "50")
			->AddTimer("tick", 60)->addEventHandler("tick", [ev](IWidget* s, const char*) {
				*ev += 0.21f;
				s->SetProperty("value", std::to_string((int)(55.0f + 40.0f * std::sin(*ev) * std::sin(*ev * 0.37f))).c_str());
			});
		card(g, 1, 0, "cw.VitalsMonitor")->AddWidget(WidgetFactory::Create("cw.VitalsMonitor.dll"))
			->SetProperty("label", "ECG")->SetProperty("mode", "sim")->SetProperty("active", "true");
		ICell* tc = card(g, 1, 1, "cw.TextSlider"); tc->SetStackMode(StackMode::Vertical);
		// These two read their colours through the CSS style chain, so they are
		// set as string properties, not with SetColor().
		tc->AddWidget(WidgetFactory::Create("cw.TextSlider.dll"))->SetProperty("height", "44")
			->SetProperty("background-color", "#111827")->SetProperty("foreground-color", "#FFFFFF")->SetProperty("font-size", "14")
			->SetProperty("text", "cw.TextSlider scrolls a line of text across its width, like a ticker on a news channel.")
			->SetProperty("speed", "2");
		card(g, 1, 2, "cw.EyesControl, follows the mouse")->AddWidget(WidgetFactory::Create("cw.EyesControl.dll"));
	}

	// --- 3. Inputs --------------------------------------------------------------
	{
		ILayout* g = addPage("Inputs", L"lightbulb", 2, 3);

		ICell* bc = card(g, 0, 0, "cw.Button with CSS classes");
		bc->SetStackMode(StackMode::Vertical); bc->SetProperty("gap", "4");
		const char* kinds[3][2] = { { "Primary", "btn btn-primary" }, { "Success", "btn btn-success" }, { "Danger", "btn btn-danger" } };
		for (auto& k : kinds) {
			IWidget* b = WidgetFactory::Create("cw.Button.dll");
			b->SetProperty("title", k[0])->SetProperty("height", "30");
			StyleManager::AddClass(b, k[1]);
			std::string label = k[0];
			b->addEventHandler("onClick", [status, label](IWidget*, const char*) { *status = label + " button clicked."; });
			bc->AddWidget(b);
		}
		bc->AddWidget(WidgetFactory::Create("cw.Button.dll"))
			->SetProperty("title", "Pill with an icon and a badge")->SetProperty("is_pill", "true")->SetProperty("align", "left")
			->SetProperty("badge_text", "3")->SetProperty("image_base64", icon(L"folder-open-outline").c_str())->SetProperty("height", "34")
			->addEventHandler("onClick", [status](IWidget*, const char*) { *status = "Pill button clicked."; });

		ICell* sc = card(g, 0, 1, "cw.SwitchButton");
		sc->SetStackMode(StackMode::Vertical); sc->SetProperty("gap", "10");
		sc->AddWidget(WidgetFactory::Create("cw.SwitchButton.dll"))->SetProperty("title", "Notifications")->Bind("checked", switchOn)->SetProperty("height", "36")
			->addEventHandler("onChange", [status](IWidget*, const char* json) { *status = std::string("Notifications ") + (std::string(json) == "true" ? "on." : "off."); });
		sc->AddWidget(WidgetFactory::Create("cw.SwitchButton.dll"))->SetProperty("title", "Waiting overlay on the image (Content page)")->Bind("checked", waiting)->SetProperty("height", "36")
			->addEventHandler("onChange", [waiting](IWidget*, const char* json) { *waiting = std::string(json) == "true"; });

		ICell* sl = card(g, 0, 2, "cw.SliderControl, bound to a label");
		sl->SetStackMode(StackMode::Vertical); sl->SetProperty("gap", "10");
		sl->AddWidget(WidgetFactory::Create("cw.SliderControl.dll"))->SetProperty("min", "0")->SetProperty("max", "100")->Bind("value", sliderValue)->SetProperty("height", "36")
			->addEventHandler("onInput", [sliderText](IWidget* s, const char*) { *sliderText = std::string("Value: ") + s->GetProperty("value"); });
		sl->AddWidget(text("Value: 50", 13, false, kInk))->Bind("title", sliderText)->SetProperty("height", "24");

		ICell* ec = card(g, 1, 0, "cw.EditBox, validation, password");
		ec->SetStackMode(StackMode::Vertical); ec->SetProperty("gap", "10");
		ec->AddWidget(WidgetFactory::Create("cw.EditBox.dll"))->SetProperty("placeholder", "Type ok to pass validation")
			->SetProperty("image_base64", icon(L"magnify").c_str())->SetProperty("height", "36")
			->OnValidate([](IWidget* s, const char* v) {
				if (std::string(v) == "ok") return true;
				s->SetProperty("validation-error", "Value must be 'ok'"); return false;
			});
		ec->AddWidget(WidgetFactory::Create("cw.EditBox.dll"))->SetProperty("placeholder", "Password")->SetProperty("password", "true")->SetProperty("height", "36");

		IWidget* list = WidgetFactory::Create("cw.ListCards.dll");
		list->SetProperty("addImage", ("img_a|" + icon(L"spa-outline")).c_str());
		list->SetProperty("addImage", ("img_b|" + icon(L"sticker-alert-outline")).c_str());
		list->SetProperty("addItem", "a|Configuration|System preferences and defaults|img_a");
		list->SetProperty("addItem", "b|Security patch|Available, restart required|img_b");
		list->SetProperty("addItem", "c|Release notes|What changed in 2.1|img_a");
		list->SetProperty("addItem", "d|A plain item|This one has no icon|");
		list->SetProperty("addItem", "e|Backups|Last run tonight at 02:00|img_a");
		list->addEventHandler("onItemClick", [status](IWidget*, const char* json) { *status = std::string("List item: ") + json; });
		card(g, 1, 1, "cw.ListCards")->AddWidget(list);

		ICell* st = card(g, 1, 2, "cw.StaticText");
		st->SetStackMode(StackMode::Vertical); st->SetProperty("gap", "6");
		st->AddWidget(text("A label: a title, a size, a style,", 13, false, kInk))->SetProperty("height", "30");
		st->AddWidget(text("an alignment, CSS colours", 13, true, kInk))->SetProperty("height", "26");
		st->AddWidget(text("or from SetColor().", 13, false, RGB(37, 99, 235)))->SetProperty("height", "26");
	}

	// --- 4. Content & overlays ----------------------------------------------------
	{
		ILayout* g = addPage("Content & overlays", L"image-album", 2, 3);
		card(g, 0, 0, "cw.TitleDescCard")->AddWidget(WidgetFactory::Create("cw.TitleDescCard.dll"))
			->SetProperty("title", "A card")->SetProperty("top_text", "CARD")
			->SetProperty("description", "A heading, a small pill of text, a paragraph and an optional image.")
			->SetProperty("image_base64", icon(L"spider-web").c_str());
		card(g, 0, 1, "cw.ImageViewerWidget")->AddWidget(WidgetFactory::Create("cw.ImageViewerWidget.dll"))
			->SetProperty("image-path", AssetPathA("images\\example1.jpg").c_str());
		card(g, 0, 2, "cw.SnowingOverlay on a card")->AddWidget(WidgetFactory::Create("cw.TitleDescCard.dll"))
			->SetProperty("title", "Snowing")->SetProperty("top_text", "OVERLAY")
			->SetProperty("description", "Overlays are widgets stacked on another widget with AddOverlay().")
			->AddOverlay(WidgetFactory::Create("cw.SnowingOverlay.dll")->SetProperty("active", "true")->SetProperty("freq", "60")->SetProperty("size", "3"));
		card(g, 1, 0, "cw.LightingStormOverlay on a card")->AddWidget(WidgetFactory::Create("cw.TitleDescCard.dll"))
			->SetProperty("title", "Lightning storm")->SetProperty("top_text", "OVERLAY")
			->SetProperty("description", "Flashes and bolts over whatever sits underneath.")
			->AddOverlay(WidgetFactory::Create("cw.LightingStormOverlay.dll")->SetProperty("active", "true")->SetProperty("intensity", "150"));
		card(g, 1, 1, "cw.WaitingOverlay (switch on Inputs)")->AddWidget(WidgetFactory::Create("cw.ImageViewerWidget.dll"))
			->SetProperty("image-path", AssetPathA("images\\example1.jpg").c_str())
			->AddOverlay(WidgetFactory::Create("cw.WaitingOverlay.dll")->Bind("waiting", waiting));
		card(g, 1, 2, "Two overlays on a clock")->AddWidget(WidgetFactory::Create("cw.AnalogClock.dll"))
			->SetProperty("show_seconds", "true")
			->AddOverlay(WidgetFactory::Create("cw.SnowingOverlay.dll")->SetProperty("active", "true")->SetProperty("freq", "40")->SetProperty("size", "2"))
			->AddOverlay(WidgetFactory::Create("cw.LightingStormOverlay.dll")->SetProperty("active", "true")->SetProperty("intensity", "80"));
	}

	// --- 5. Dialogs -------------------------------------------------------------
	{
		ILayout* g = addPage("Dialogs", L"message-alert-outline", 1, 1);
		ICell* d = card(g, 0, 0, "Dialogs are containers too: the same widgets, a modal loop");
		d->SetStackMode(StackMode::Vertical); d->SetProperty("gap", "8");
		d->AddWidget(text("Each button opens a second ChronoUI container with its own root layout and runs it modally.", 12, false, kMuted))->SetProperty("height", "22");
		struct Dlg { const char* title; const wchar_t* icon; std::function<void()> run; };
		std::vector<Dlg> dlgs = {
			{ "Login dialog", L"incognito", [=] {
				std::string u, p;
				*status = ExampleLoginDlg(hwnd, u, p) ? "Logged in as " + u + "." : "Login cancelled.";
			} },
			{ "Create account", L"account-box-plus-outline", [=] {
				std::string e, p;
				*status = ExampleRegisterDlg(hwnd, e, p) ? "Account created for " + e + "." : "Registration cancelled.";
			} },
			{ "Message box: question", L"message-alert-outline", [=] {
				int r = funMessageBox(hwnd, "Delete account?", "This cannot be undone.", CMB_YESNO | CMB_ICON_WARNING, &myVirtualDrive);
				*status = r == IDYES ? "You chose Yes." : "You chose No.";
			} },
			{ "Message box: success", L"message-alert-outline", [=] {
				funMessageBox(hwnd, "Saved", "Everything is in place.", CMB_OK | CMB_ICON_SUCCESS, &myVirtualDrive);
			} },
			{ "Message box: error", L"message-alert-outline", [=] {
				funMessageBox(hwnd, "Connection failed", "Unable to reach the remote database.", CMB_OK | CMB_ICON_ERROR, &myVirtualDrive);
			} },
		};
		for (auto& x : dlgs) {
			auto run = x.run;
			d->AddWidget(WidgetFactory::Create("cw.Button.dll"))
				->SetProperty("title", x.title)->SetProperty("is_pill", "true")->SetProperty("align", "left")->SetProperty("arrow", "true")
				->SetProperty("image_base64", icon(x.icon).c_str())->SetProperty("height", "40")->SetProperty("width", "320")
				->addEventHandler("onClick", [run](IWidget*, const char*) { run(); });
		}
	}

	// --- footer: the status line ------------------------------------------------
	{
		ICell* f = inset(root->GetCell(2, 0), "10");
		f->SetStackMode(StackMode::Vertical);
		f->AddWidget(text("", 12, false, kMuted))->Bind("title", status)->SetProperty("height", "20");
	}

	// "ChronoUIDemo 3" opens on page 3: lets a script capture every page.
	int first = cmdLine && *cmdLine ? _wtoi(cmdLine) : 0;
	if (first < 0 || first >= (int)nav.size()) first = 0;
	nav[(size_t)first]->SetProperty("checked", "true");
	pages->SetActiveTab(first);
	win->Show();
	win->RunMessageLoop();
	delete win;
	CoUninitialize();
	return 0;
}

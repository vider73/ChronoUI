# ChronoUI 🤖

**A Modern, Hardware-Accelerated C++ UI Framework.**  
*Architected by vider73. Coded by Gemini 3.0 Pro.*

ChronoUI is a lightweight, high-performance user interface library built on **Direct2D** and **DirectWrite**. 

**What makes ChronoUI unique?** It was designed from the ground up to be **LLM-Native**. The architecture—self-contained DLLs, string-based property reflection, and JSON manifests—eliminates complex header dependencies. This makes it incredibly easy for Large Language Models (like ChatGPT, Claude, or Gemini) to generate fully functional, hardware-accelerated widgets in a single pass without hallucinating compilation errors.

![App screenshot](/assets/images/screenshot1.jpg)
---

## 🚀 Key Features

*   **🤖 AI-First Architecture:** The codebase is optimized for context-window efficiency, making it trivial to generate new controls via prompting.
*   **⚡ Hardware Accelerated:** Powered by Direct2D for 60FPS animations and smooth scaling.
*   **🧩 Hot-Pluggable Widgets:** Widgets are compiled as individual DLLs. You can add new controls without recompiling the core engine.
*   **🎨 CSS-Style Styling:** `background-color`, `border-radius`, `margin`, `:hover`, and more.
*   **🔗 Reactive Data Binding:** Bind C++ variables directly to UI properties.
*   **📦 Virtual File System:** Built-in `.pak` resource manager for portable assets.

---

## 🛠️ AI Workflow: How to Generate New Widgets

ChronoUI allows you to expand your component library using AI. Follow this 4-step workflow to create complex custom widgets in minutes.

### Step 1: Select a Template
Find an existing widget in `src/widgets/` that is geometrically similar to what you want.
*   *Need a circular control?* Use `cw.GaugeSpeedOmeter.cpp`.
*   *Need a graph?* Use `cw.DataPlotControl.cpp`.
*   *Need a standard input?* Use `cw.Button.cpp`.

### Step 2: Construct the Prompt
Copy the code of the template widget and use it as context.

**Example Scenario:** You want to create a **"WifiSignalWidget"** that shows signal strength bars.

**Copy/Paste this prompt into your LLM:**

> "I have a C++ UI framework called ChronoUI. Here is the source code for an existing widget (`cw.Progress.cpp`) to show you the API structure, the `WidgetImpl` base class, and how Direct2D drawing works:
>
> [PASTE CODE OF cw.Progress.cpp HERE]
>
> **Task:** Create a new widget called `WifiSignalWidget`. 
> 1. It should have a property `signal_strength` (0-100).
> 2. It should draw 4 vertical bars.
> 3. Bars should light up based on the signal strength.
> 4. Include the JSON manifest.
> 5. Output the single `.cpp` file ready to compile."

### Step 3: Implement
1.  Create a new file: `src/widgets/cw.WifiSignalWidget.cpp`.
2.  Paste the AI-generated code.

### Step 4: Add to CMakeLists
Open your `src/widgets/CMakeLists.txt` (or main build file) and register the new plugin:

```cmake
# Add the new widget to the build
add_library(cw.WifiSignalWidget SHARED "cw.WifiSignalWidget.cpp")
target_link_libraries(cw.WifiSignalWidget PRIVATE ChronoCore) # Link against core headers/libs
```

**Recompile, and your new widget is instantly available to use in your layout:**
```cpp
WidgetFactory::Create("cw.WifiSignalWidget.dll")->SetProperty("signal_strength", "75");
```

---

## 📚 Basic Usage

Here is a complete example to create a window, define a layout, and add a button.
HelloWorld.cpp example
```cpp
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
```

---

## 🧩 Widget Library Documentation

ChronoUI comes with a suite of built-in high-performance widgets.

| Widget Name | Description | Key Properties |
| :--- | :--- | :--- |
| `cw.Button.dll` | Standard button with icon support. | `title`, `image_base64`, `is_pill`, `badge_text` |
| `cw.EditBox.dll` | Text input with validation & masking. | `value`, `placeholder`, `password`, `allowed_chars` |
| `cw.SwitchButton.dll` | Animated toggle switch. | `checked`, `track-on-color`, `track-off-color` |
| `cw.SliderControl.dll` | Range slider. | `value`, `min`, `max`, `track-color` |
| `cw.DataPlotControl.dll` | Real-time scrolling line graph. | `add_value`, `min`, `max`, `line-color` |
| `cw.GaugeSpeedOmeter.dll`| Automotive-style speedometer. | `value`, `max`, `accent_color` |
| `cw.EqualizerBar.dll` | LED-style audio/data visualizer. | `value`, `vertical`, `segments` |
| `cw.ImageViewerWidget.dll`| Image viewer with zoom/pan. | `image-path`, `zoom-fit` |
| `cw.SnowingOverlay.dll` | Particle system overlay. | `active`, `intensity` |

---

## 📂 Project Structure

*   `include/` - Core headers (`ChronoUI.hpp`, `WidgetImpl.hpp`).
*   `src/core/` - The layout engine and window management logic.
*   `src/widgets/` - **The Plugin Folder.** All widgets live here as individual `.cpp` files.
*   `src/examples/` - Example applications.

## 🤝 Contribution & Credits

**Lead Architect:** Jose Luis Rey Mejías (github.com/vider73)
**Lead Developer:** Gemini 3.0 Pro (Google DeepMind)

This project is open source. We encourage you to use LLMs to generate new widgets and submit Pull Requests!

1.  Fork the repository.
2.  Prompt your AI to build a cool widget.
3.  Submit a PR with the new source file.

## 📄 License

MIT License.

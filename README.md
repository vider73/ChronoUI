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

Here is a Markdown description tailored for a GitHub README, focusing on the specific features implemented in your `main.cpp`.

***

# Core Features

ChronoUI simplifies modern C++ GUI development by bridging the gap between low-level Win32 performance and high-level reactive programming patterns. Below are the key mechanisms for interaction, timing, and state management.

## ⚡ Event Subscription system

ChronoUI utilizes a string-based event system that allows widgets to subscribe to specific actions using C++ lambdas. This approach eliminates the need for complex message maps or inheritance for basic interactions. Handlers receive the sending widget and a JSON payload containing event details.

**Key capabilities:**
*   **Lambda Support:** Capture local state or window handles directly within the callback.
*   **JSON Payloads:** Receive structured data (e.g., mouse coordinates, file paths, or input values) through a standardized `const char* json` argument.
*   **Chaining:** Add multiple handlers to the same widget effortlessly.

```cpp
// Example: Handling a button click to close a window
WidgetFactory::Create("cw.Button.dll")
    ->SetProperty("title", "Close Application")
    ->addEventHandler("onClick", [hwnd](IWidget* sender, const char* json) {
        // 'json' contains event details, 'sender' is the button instance
        SendMessage(hwnd, WM_CLOSE, 0, 0);
    });

// Example: Handling list item selection
myList->addEventHandler("onItemClick", [](IWidget* sender, const char* json) {
    // json example: {"index":1, "id":"sys_conf"}
    std::cout << "Selected item data: " << json << std::endl;
});
```

## ⏱️ Built-in Widget Timers

Forget creating separate threads or managing global Windows timers for UI updates. ChronoUI allows you to attach timers directly to any specific widget. When the timer ticks, it triggers a named event on that widget, which is handled on the UI thread.

**Key capabilities:**
*   **Widget-Scoped:** Timers are tied to the lifecycle of the widget.
*   **Event Integration:** A timer tick is treated just like a click or keypress, triggering a standard event handler.
*   **Animation & Polling:** Ideal for animating progress bars, updating clocks, or polling hardware sensors (like CPU usage).

```cpp
// Example: A self-updating progress bar
WidgetFactory::Create("cw.Progress.dll")
    ->SetProperty("title", "Processing...")
    // 1. Register a timer named "Step" that fires every 100ms
    ->AddTimer("Step", 100) 
    // 2. Handle the "Step" event to update the UI
    ->addEventHandler("Step", [&](IWidget* sender, const char* json) {
        static float progress = 0.0f;
        progress += 1.0f;
        if (progress > 100.0f) progress = 0.0f;
        
        // Update the visual property
        sender->SetProperty("value", std::to_string(progress).c_str());
    });
```

## 🔗 Reactive Observables (Data Binding)

ChronoUI supports reactive state management through `ChronoObservable<T>`. Instead of manually updating every widget when a variable changes, you can bind widget properties (like text, value, or enabled state) directly to a shared observable object.

**Key capabilities:**
*   **Two-Way Binding:** Changes in the UI (e.g., EditBox) update the variable; changes to the variable update the UI.
*   **One-to-Many:** A single boolean observable can control the "disabled" state of an entire form.
*   **Thread Safety:** Simplifies state synchronization across your application.

```cpp
// 1. Create shared state
auto isWorking = std::make_shared<ChronoObservable<bool>>(false);
auto userInput = std::make_shared<ChronoObservable<std::string>>("");

// 2. Bind UI elements to state
// This button automatically disables when 'isWorking' is true
WidgetFactory::Create("cw.Button.dll")
    ->SetProperty("title", "Submit")
    ->Bind("disabled", isWorking) 
    ->addEventHandler("onClick", [=](IWidget* sender, const char* json) {
        // Trigger the state change - UI updates automatically
        *isWorking = true; 
        
        // Simulate work...
        *userInput = "Processing...";
    });

// This text box is bound to 'userInput'. 
// It updates if the code changes the variable, and vice versa.
WidgetFactory::Create("cw.EditBox.dll")
    ->Bind("value", userInput);
```

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

## 🛠️ How to Build

ChronoUI is built for Windows and requires the **MSVC compiler** (included with Visual Studio).

### Prerequisites
*   **Visual Studio 2019 or 2022** (Desktop development with C++ workload)
*   **CMake** (3.15 or later)

### Build Instructions

You can build the project using the command line or by generating a Visual Studio solution.

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/YourUsername/ChronoUI.git
    cd ChronoUI
    ```

2.  **Generate the project files:**
    ```bash
    cmake -S . -B build
    ```

3.  **Compile:**
    
    **Option A: Command Line (Fastest)**
    ```bash
    cmake --build build --config Release
    ```

    **Option B: Visual Studio**
    *   Open `build\ChronoUI.sln` in Visual Studio.
    *   Select **Release** configuration.
    *   Build Solution (`Ctrl+Shift+B`).

### 📂 Output
Once compiled, you will find the **`ChronoUI.dll`** and **`ChronoUI.lib`** files in:
`build/Release/`

> **Note:** For AI-## 📂 Project Structure

*   `include/` - Core headers (`ChronoUI.hpp`, `WidgetImpl.hpp`).
*   `src/core/` - The layout engine and window management logic.
*   `src/widgets/` - **The Plugin Folder.** All widgets live here as individual `.cpp` files.
*   `src/examples/` - Example applications.
*   `assets/` - Images and css styles goes here.

## 🤝 Contribution & Credits

**Lead Architect:** Jose Luis Rey Mejías (github.com/vider73)
**Lead Developer:** Gemini 3.0 Pro (Google DeepMind)

This project is open source. We encourage you to use LLMs to generate new widgets and submit Pull Requests!

1.  Fork the repository.
2.  Prompt your AI to build a cool widget.
3.  Submit a PR with the new source file.

## 📄 License

MIT License.

// =============================================================================
// ChronoController.hpp — the three factories every widget draws with.
//
// Direct2D, DirectWrite and WIC, created once per module and shared by every
// window and widget in it. This is all the virtual widgets need from the
// process: VirtualWidget.hpp includes this and nothing else of the framework.
//
//     ChronoControllerImpl::Instance();                       // first line of wWinMain
//     ChronoControllerImpl::Instance().m_pDWriteFactory->...  // inside a widget
//
// Apps that want the factories released while COM is still alive call
// Shutdown() before CoUninitialize (ClaudeMM does); everyone else lets the
// process exit reclaim them, see the destructor.
// =============================================================================

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <mutex>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace ChronoUI {

	class ChronoControllerImpl {
		int m_refCount = 0;
		std::recursive_mutex m_mutex;

		ChronoControllerImpl() { Initialize(); }

		// The singleton dies from the CRT's atexit table, long after the app
		// left its message loop and possibly after COM tore its apartment down
		// (CoUninitialize unloads in-proc servers such as windowscodecs.dll even
		// while references are outstanding). Calling Release() there is a jump
		// into unmapped memory, so the destructor deliberately LEAKS whatever
		// is still held: the OS reclaims it at process exit anyway.
		~ChronoControllerImpl() {
			(void)m_pD2DFactory.Detach();
			(void)m_pDWriteFactory.Detach();
			(void)m_pWICFactory.Detach();
		}
	public:
		ComPtr<ID2D1Factory>       m_pD2DFactory;
		ComPtr<IDWriteFactory>     m_pDWriteFactory;
		ComPtr<IWICImagingFactory> m_pWICFactory;      // null when the app never initialised COM

		static ChronoControllerImpl& Instance() {
			static ChronoControllerImpl instance;
			return instance;
		}

		void Initialize() {
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			if (m_refCount == 0) {
				D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_pD2DFactory.GetAddressOf());
				DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
					reinterpret_cast<IUnknown**>(m_pDWriteFactory.GetAddressOf()));
				CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
					IID_PPV_ARGS(m_pWICFactory.GetAddressOf()));
			}
			m_refCount++;
		}

		void Shutdown() {
			std::lock_guard<std::recursive_mutex> lock(m_mutex);
			m_refCount--;
			if (m_refCount <= 0) {
				m_pD2DFactory.Reset();
				m_pDWriteFactory.Reset();
				m_pWICFactory.Reset();
				m_refCount = 0;
			}
		}
	};

} // namespace ChronoUI

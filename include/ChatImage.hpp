// =============================================================================
// ChatImage.hpp — imagen adjunta a un mensaje del chat.
//
// Carga el archivo de disco UNA vez, mantiene:
//   * rawBytes  — el contenido binario del archivo (PNG/JPG/...) para enviar
//                 a Ollama como base64.
//   * width/height — para layout.
//   * bitmap    — ID2D1Bitmap creado on-demand desde rawBytes via WIC. Si el
//                 render target se recrea (WM_SIZE → resize), el bitmap se
//                 invalida y se vuelve a crear la próxima vez que se pide.
//   * base64    — cache lazy del base64 (los modelos lo piden por mensaje,
//                 no por chunk, así que calcular una vez es suficiente).
//
// La factoría WIC es global (singleton via WICFactory()).
// =============================================================================

#pragma once

#include <windows.h>
#include <wincodec.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <memory>
#include <fstream>

#include "ChronoUI.hpp"   // ComPtr alias

#pragma comment(lib, "Windowscodecs.lib")
#pragma comment(lib, "Ole32.lib")

namespace ChronoUI {

	// Singleton WIC factory. Inicializa COM (apartment) la primera vez que se
	// llama desde el UI thread. CoInitializeEx es idempotent: si ya está
	// inicializado en otro modo, devuelve un código que ignoramos.
	inline IWICImagingFactory* WICFactory() {
		static ComPtr<IWICImagingFactory> s_factory;
		static bool s_tried = false;
		if (!s_tried) {
			s_tried = true;
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
			CoCreateInstance(CLSID_WICImagingFactory, nullptr,
				CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_factory));
		}
		return s_factory.Get();
	}

	// Decoder counterpart for Base64Encode. Permissive: skips whitespace
	// and stops at the first non-alphabet byte / '=', so it works for both
	// the strict encoder output above and the lazily-formatted JSON the
	// model sometimes emits. Returns the raw bytes.
	inline std::vector<BYTE> Base64Decode(const std::string& src) {
		static int8_t T[256];
		static bool init = false;
		if (!init) {
			for (int i = 0; i < 256; ++i) T[i] = -1;
			const char* kAlphabet =
				"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			for (int i = 0; i < 64; ++i) T[(unsigned char)kAlphabet[i]] = (int8_t)i;
			init = true;
		}
		std::vector<BYTE> out;
		out.reserve((src.size() / 4) * 3);
		uint32_t buf = 0;
		int bits = 0;
		for (char c : src) {
			if (c == '=') break;
			if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
			int8_t v = T[(unsigned char)c];
			if (v < 0) continue;        // tolerate stray chars
			buf = (buf << 6) | (uint32_t)v;
			bits += 6;
			if (bits >= 8) {
				bits -= 8;
				out.push_back((BYTE)((buf >> bits) & 0xFF));
			}
		}
		return out;
	}

	// Base64 encoder. Output ASCII (sin newlines). Suficiente para JSON.
	inline std::string Base64Encode(const std::vector<BYTE>& data) {
		static const char* kAlphabet =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		out.reserve(((data.size() + 2) / 3) * 4);
		size_t i = 0;
		while (i + 2 < data.size()) {
			uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
			out.push_back(kAlphabet[(v >> 18) & 0x3F]);
			out.push_back(kAlphabet[(v >> 12) & 0x3F]);
			out.push_back(kAlphabet[(v >>  6) & 0x3F]);
			out.push_back(kAlphabet[(v      ) & 0x3F]);
			i += 3;
		}
		if (i < data.size()) {
			uint32_t v = data[i] << 16;
			if (i + 1 < data.size()) v |= data[i + 1] << 8;
			out.push_back(kAlphabet[(v >> 18) & 0x3F]);
			out.push_back(kAlphabet[(v >> 12) & 0x3F]);
			out.push_back((i + 1 < data.size()) ? kAlphabet[(v >> 6) & 0x3F] : '=');
			out.push_back('=');
		}
		return out;
	}

	// Una imagen adjunta. Construir vía Load(); el objeto se devuelve por
	// shared_ptr para que puedan referenciarla a la vez la attachment bar
	// del input y la burbuja del usuario (que la conserva tras el envío).
	class ChatImage {
	public:
		std::wstring        path;        // ruta original (debug / tooltip)
		std::wstring        filename;    // sólo el nombre (para mostrar)
		std::vector<BYTE>   rawBytes;
		int                 width  = 0;
		int                 height = 0;

	private:
		mutable std::string                 m_base64Cache;
		mutable ComPtr<ID2D1Bitmap>         m_bitmap;
		mutable ID2D1RenderTarget*          m_bitmapRT = nullptr;  // RT con el que se creó

	public:
		// Carga un archivo de imagen del disco. Devuelve nullptr si falla
		// (archivo inexistente, formato no soportado por WIC, etc.). Las
		// extensiones típicas que WIC soporta out-of-box: png, jpg, jpeg,
		// bmp, gif, tiff, webp (Win10+), heic.
		static std::shared_ptr<ChatImage> Load(const std::wstring& fullPath) {
			auto img = std::make_shared<ChatImage>();
			img->path = fullPath;
			// Nombre limpio: parte tras el último '\' o '/'.
			size_t slash = fullPath.find_last_of(L"\\/");
			img->filename = (slash == std::wstring::npos)
				? fullPath : fullPath.substr(slash + 1);

			// Leer raw bytes del archivo.
			std::ifstream f(fullPath, std::ios::binary | std::ios::ate);
			if (!f) return nullptr;
			std::streamsize sz = f.tellg();
			if (sz <= 0 || sz > (50 * 1024 * 1024)) return nullptr;  // cap a 50 MB
			f.seekg(0, std::ios::beg);
			img->rawBytes.resize((size_t)sz);
			// Lectura defensiva: si read() falla parcialmente, vaciamos para
			// que un caller posterior no encuentre rawBytes "casi llenos" y
			// se confunda. La función devuelve nullptr de todas formas.
			if (!f.read((char*)img->rawBytes.data(), sz)
			    || f.gcount() != sz) {
				img->rawBytes.clear();
				return nullptr;
			}

			// Decodificar via WIC sólo para obtener width/height ahora; el
			// ID2D1Bitmap se crea on-demand en EnsureBitmap (necesita el RT).
			IWICImagingFactory* wic = WICFactory();
			if (!wic) return nullptr;
			ComPtr<IWICStream> stream;
			if (FAILED(wic->CreateStream(&stream))) return nullptr;
			if (FAILED(stream->InitializeFromMemory(img->rawBytes.data(),
				(DWORD)img->rawBytes.size()))) return nullptr;
			ComPtr<IWICBitmapDecoder> dec;
			if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr,
				WICDecodeMetadataCacheOnLoad, &dec))) return nullptr;
			ComPtr<IWICBitmapFrameDecode> frame;
			if (FAILED(dec->GetFrame(0, &frame))) return nullptr;
			UINT w = 0, h = 0;
			frame->GetSize(&w, &h);
			img->width  = (int)w;
			img->height = (int)h;
			return img;
		}

		// Construye un ChatImage a partir de bytes ya en memoria (por ejemplo,
		// el PNG que devolvió una tool). Decodifica con WIC para fijar
		// width/height — necesarios para layout en VChatBubble.
		static std::shared_ptr<ChatImage> FromBytes(std::vector<BYTE> bytes,
			const std::wstring& filename)
		{
			auto img = std::make_shared<ChatImage>();
			img->rawBytes = std::move(bytes);
			img->filename = filename;
			img->path     = filename;
			IWICImagingFactory* wic = WICFactory();
			if (!wic || img->rawBytes.empty()) return img;
			ComPtr<IWICStream> stream;
			if (FAILED(wic->CreateStream(&stream))) return img;
			if (FAILED(stream->InitializeFromMemory(img->rawBytes.data(),
				(DWORD)img->rawBytes.size()))) return img;
			ComPtr<IWICBitmapDecoder> dec;
			if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr,
				WICDecodeMetadataCacheOnLoad, &dec))) return img;
			ComPtr<IWICBitmapFrameDecode> frame;
			if (FAILED(dec->GetFrame(0, &frame))) return img;
			UINT w = 0, h = 0;
			frame->GetSize(&w, &h);
			img->width  = (int)w;
			img->height = (int)h;
			return img;
		}

		// Devuelve el bitmap para dibujar. Si el RT cambió (por ejemplo se
		// recreó tras un D2DERR_RECREATE_TARGET) o nunca se construyó, se
		// (re)crea aquí. NULL si la conversión WIC→D2D falla.
		ID2D1Bitmap* EnsureBitmap(ID2D1RenderTarget* pRT) const {
			if (!pRT) return nullptr;
			if (m_bitmap && m_bitmapRT == pRT) return m_bitmap.Get();
			m_bitmap.Reset();
			m_bitmapRT = nullptr;

			IWICImagingFactory* wic = WICFactory();
			if (!wic) return nullptr;
			ComPtr<IWICStream> stream;
			if (FAILED(wic->CreateStream(&stream))) return nullptr;
			if (FAILED(stream->InitializeFromMemory(
				const_cast<BYTE*>(rawBytes.data()), (DWORD)rawBytes.size())))
				return nullptr;
			ComPtr<IWICBitmapDecoder> dec;
			if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr,
				WICDecodeMetadataCacheOnLoad, &dec))) return nullptr;
			ComPtr<IWICBitmapFrameDecode> frame;
			if (FAILED(dec->GetFrame(0, &frame))) return nullptr;
			ComPtr<IWICFormatConverter> conv;
			if (FAILED(wic->CreateFormatConverter(&conv))) return nullptr;
			if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
				WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut)))
				return nullptr;
			if (FAILED(pRT->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &m_bitmap)))
				return nullptr;
			m_bitmapRT = pRT;
			return m_bitmap.Get();
		}

		// Base64 del archivo crudo, cacheado. Para enviar a Ollama.
		const std::string& Base64() const {
			if (m_base64Cache.empty() && !rawBytes.empty()) {
				m_base64Cache = Base64Encode(rawBytes);
			}
			return m_base64Cache;
		}
	};

	// Predicado: ¿la extensión del archivo es de una imagen que sabemos
	// cargar? No es la verdad absoluta (WIC podría no tener un codec en este
	// sistema) pero es suficiente para filtrar drag&drop.
	inline bool IsImageExtension(const std::wstring& path) {
		size_t dot = path.find_last_of(L'.');
		if (dot == std::wstring::npos) return false;
		std::wstring ext = path.substr(dot + 1);
		for (auto& c : ext) c = (wchar_t)towlower(c);
		return ext == L"png"  || ext == L"jpg"  || ext == L"jpeg"
		    || ext == L"bmp"  || ext == L"gif"  || ext == L"webp"
		    || ext == L"tiff" || ext == L"tif"  || ext == L"heic";
	}

} // namespace ChronoUI

// =============================================================================
// chrono_wic.hpp — Windows Imaging Component, the part ChronoUI uses, on
// stb_image.
//
// Decoding is all ChronoUI asks WIC for: a decoder from a file or a memory
// stream, frame 0, a converter to 32bpp premultiplied BGRA, CopyPixels. Here a
// frame decodes once to premultiplied BGRA (which is also Cairo's ARGB32 on a
// little-endian machine) and the converter just hands those pixels on.
// =============================================================================

#pragma once

#include "chrono_win32.hpp"
#include <fstream>
#include <iterator>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO_WARNINGS
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include <stb/stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define GENERIC_READ  0x80000000L
#define GENERIC_WRITE 0x40000000L

enum WICDecodeOptions       { WICDecodeMetadataCacheOnDemand = 0, WICDecodeMetadataCacheOnLoad = 1 };
enum WICBitmapDitherType    { WICBitmapDitherTypeNone = 0 };
enum WICBitmapPaletteType   { WICBitmapPaletteTypeCustom = 0, WICBitmapPaletteTypeMedianCut = 1 };
enum WICBitmapInterpolationMode { WICBitmapInterpolationModeNearestNeighbor = 0, WICBitmapInterpolationModeLinear = 1,
                                  WICBitmapInterpolationModeCubic = 2, WICBitmapInterpolationModeFant = 3 };
typedef GUID WICPixelFormatGUID;
typedef GUID REFWICPixelFormatGUID_t;
typedef const GUID& REFWICPixelFormatGUID;
struct WICRect { INT X, Y, Width, Height; };

static const GUID GUID_WICPixelFormat32bppPBGRA = { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10 } };
static const GUID GUID_WICPixelFormat32bppBGRA  = { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f } };
static const GUID GUID_ContainerFormatPng       = { 0x1b7cfaf4, 0x713f, 0x473c, { 0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf } };
static const CLSID CLSID_WICImagingFactory      = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const CLSID CLSID_WICImagingFactory2     = { 0x317d06e8, 0x5f24, 0x433d, { 0xbd, 0xf7, 0x79, 0xce, 0x68, 0xd8, 0xab, 0xc2 } };

struct IWICPalette : IUnknown {};

// Any source of pixels: a decoded frame, a converter, a scaler.
struct IWICBitmapSource : IUnknown {
	UINT width = 0, height = 0;
	std::vector<uint8_t> pbgra;   // premultiplied B, G, R, A; stride = width * 4

	virtual HRESULT GetSize(UINT* w, UINT* h) { if (w) *w = width; if (h) *h = height; return S_OK; }
	virtual HRESULT GetPixelFormat(WICPixelFormatGUID* f) { if (f) *f = GUID_WICPixelFormat32bppPBGRA; return S_OK; }
	virtual HRESULT GetResolution(double* x, double* y) { if (x) *x = 96.0; if (y) *y = 96.0; return S_OK; }
	virtual HRESULT CopyPixels(const WICRect* rc, UINT stride, UINT size, BYTE* out) {
		WICRect r = rc ? *rc : WICRect{ 0, 0, (INT)width, (INT)height };
		if (r.X < 0 || r.Y < 0 || r.X + r.Width > (INT)width || r.Y + r.Height > (INT)height) return E_INVALIDARG;
		if ((UINT64)stride * (UINT)r.Height > size || stride < (UINT)r.Width * 4) return E_INVALIDARG;
		for (INT y = 0; y < r.Height; ++y)
			memcpy(out + (size_t)y * stride, pbgra.data() + ((size_t)(r.Y + y) * width + (size_t)r.X) * 4, (size_t)r.Width * 4);
		return S_OK;
	}
	bool Decode(const uint8_t* data, size_t n) {
		int w = 0, h = 0, comp = 0;
		unsigned char* px = stbi_load_from_memory(data, (int)n, &w, &h, &comp, 4);
		if (!px) return false;
		width = (UINT)w; height = (UINT)h;
		pbgra.resize((size_t)w * (size_t)h * 4);
		for (size_t i = 0, k = (size_t)w * (size_t)h; i < k; ++i) {
			unsigned a = px[i * 4 + 3];
			pbgra[i * 4 + 0] = (uint8_t)((px[i * 4 + 2] * a + 127) / 255);
			pbgra[i * 4 + 1] = (uint8_t)((px[i * 4 + 1] * a + 127) / 255);
			pbgra[i * 4 + 2] = (uint8_t)((px[i * 4 + 0] * a + 127) / 255);
			pbgra[i * 4 + 3] = (uint8_t)a;
		}
		stbi_image_free(px);
		return true;
	}
};
struct IWICBitmap            : IWICBitmapSource {};
struct IWICBitmapFrameDecode : IWICBitmapSource {};
struct IWICBitmapScaler      : IWICBitmapSource {
	HRESULT Initialize(IWICBitmapSource* src, UINT w, UINT h, WICBitmapInterpolationMode) {
		if (!src || !w || !h) return E_INVALIDARG;
		width = w; height = h; pbgra.assign((size_t)w * h * 4, 0);
		for (UINT y = 0; y < h; ++y) for (UINT x = 0; x < w; ++x) {      // nearest: enough for thumbnails
			UINT sx = x * src->width / w, sy = y * src->height / h;
			memcpy(&pbgra[((size_t)y * w + x) * 4], &src->pbgra[((size_t)sy * src->width + sx) * 4], 4);
		}
		return S_OK;
	}
};
struct IWICFormatConverter : IWICBitmapSource {
	HRESULT Initialize(IWICBitmapSource* src, REFWICPixelFormatGUID, WICBitmapDitherType, IWICPalette*, double, WICBitmapPaletteType) {
		if (!src) return E_INVALIDARG;
		width = src->width; height = src->height; pbgra = src->pbgra;   // decoded frames are PBGRA already
		return S_OK;
	}
};

struct IWICStream : IUnknown {
	std::vector<uint8_t> bytes;
	HRESULT InitializeFromMemory(const BYTE* p, DWORD n) { if (!p) return E_INVALIDARG; bytes.assign(p, p + n); return S_OK; }
	HRESULT InitializeFromFilename(LPCWSTR path, DWORD) {
		std::ifstream f(chrono_linux::PosixPath(path), std::ios::binary);
		if (!f) return E_FAIL;
		bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		return S_OK;
	}
};

struct IWICBitmapDecoder : IUnknown {
	std::vector<uint8_t> bytes;
	HRESULT GetFrameCount(UINT* n) { if (n) *n = 1; return S_OK; }
	HRESULT GetFrame(UINT i, IWICBitmapFrameDecode** out) {
		if (!out) return E_POINTER;
		*out = nullptr;
		if (i != 0) return E_INVALIDARG;
		auto* f = new IWICBitmapFrameDecode();
		if (!f->Decode(bytes.data(), bytes.size())) { f->Release(); return E_FAIL; }
		*out = f;
		return S_OK;
	}
};

struct IWICImagingFactory : IUnknown {
	HRESULT CreateDecoderFromFilename(LPCWSTR path, const GUID*, DWORD, WICDecodeOptions, IWICBitmapDecoder** out) {
		if (!out) return E_POINTER;
		*out = nullptr;
		IWICStream s;
		if (FAILED(s.InitializeFromFilename(path, 0)) || s.bytes.empty()) return E_FAIL;
		int w, h, c;
		if (!stbi_info_from_memory(s.bytes.data(), (int)s.bytes.size(), &w, &h, &c)) return E_FAIL;   // not an image we can read
		auto* d = new IWICBitmapDecoder(); d->bytes = std::move(s.bytes); *out = d;
		return S_OK;
	}
	HRESULT CreateDecoderFromStream(IWICStream* s, const GUID*, WICDecodeOptions, IWICBitmapDecoder** out) {
		if (!out) return E_POINTER;
		*out = nullptr;
		if (!s || s->bytes.empty()) return E_INVALIDARG;
		int w, h, c;
		if (!stbi_info_from_memory(s->bytes.data(), (int)s->bytes.size(), &w, &h, &c)) return E_FAIL;
		auto* d = new IWICBitmapDecoder(); d->bytes = s->bytes; *out = d;
		return S_OK;
	}
	HRESULT CreateStream(IWICStream** out)                 { if (!out) return E_POINTER; *out = new IWICStream(); return S_OK; }
	HRESULT CreateFormatConverter(IWICFormatConverter** o) { if (!o) return E_POINTER; *o = new IWICFormatConverter(); return S_OK; }
	HRESULT CreateBitmapScaler(IWICBitmapScaler** o)       { if (!o) return E_POINTER; *o = new IWICBitmapScaler(); return S_OK; }
};

inline bool operator==(const GUID& a, const GUID& b) { return memcmp(&a, &b, sizeof(GUID)) == 0; }

inline HRESULT CoCreateInstance(REFCLSID clsid, IUnknown*, DWORD, REFIID, void** ppv) {
	if (!ppv) return E_POINTER;
	*ppv = nullptr;
	if (clsid == CLSID_WICImagingFactory || clsid == CLSID_WICImagingFactory2) { *ppv = new IWICImagingFactory(); return S_OK; }
	return E_NOINTERFACE;
}

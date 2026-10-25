// =============================================================================
// chrono_d2d.hpp — Direct2D, the part ChronoUI uses, on Cairo.
//
// Both are immediate-mode 2D APIs with a current transform, clips and
// antialiased paths, so most calls are one Cairo sequence each. The mapping:
//
//   ID2D1RenderTarget    a cairo_t on an ARGB32 image surface (premultiplied
//                        BGRA in memory, the same layout as D2D's default)
//   SetTransform         kept as a D2D matrix and loaded into Cairo before
//                        every draw: D2D's world transform is not part of the
//                        clip/layer stack, Cairo's matrix is part of save/restore
//   PushAxisAlignedClip  cairo_save + cairo_clip;  Pop restores
//   PushLayer            clip to bounds and mask, cairo_push_group; Pop paints
//                        the group back with the layer's opacity
//   brushes              set as the Cairo source (solid, linear, radial)
//   geometries           recorded figures replayed as Cairo paths; arcs are
//                        converted to Béziers (SVG endpoint parameterisation)
//   ID2D1HwndRenderTarget  the same, presented to the SDL window's surface on EndDraw
//
// Coordinates are DIPs and DIPs are pixels: the port runs at 96 DPI.
// =============================================================================

#pragma once

#include "chrono_win32.hpp"
#include "chrono_dwrite.hpp"
#include "chrono_wic.hpp"
#include <cfloat>
#include <cmath>
#include <vector>

#define D2DERR_RECREATE_TARGET ((HRESULT)0x8899000C)
#define D2DERR_WRONG_STATE     ((HRESULT)0x88990001)

// --- enums ---------------------------------------------------------------------
enum D2D1_FACTORY_TYPE        { D2D1_FACTORY_TYPE_SINGLE_THREADED = 0, D2D1_FACTORY_TYPE_MULTI_THREADED = 1 };
enum D2D1_ANTIALIAS_MODE      { D2D1_ANTIALIAS_MODE_PER_PRIMITIVE = 0, D2D1_ANTIALIAS_MODE_ALIASED = 1 };
enum D2D1_TEXT_ANTIALIAS_MODE { D2D1_TEXT_ANTIALIAS_MODE_DEFAULT = 0, D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE = 1,
                                D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE = 2, D2D1_TEXT_ANTIALIAS_MODE_ALIASED = 3 };
enum D2D1_FIGURE_BEGIN        { D2D1_FIGURE_BEGIN_FILLED = 0, D2D1_FIGURE_BEGIN_HOLLOW = 1 };
enum D2D1_FIGURE_END          { D2D1_FIGURE_END_OPEN = 0, D2D1_FIGURE_END_CLOSED = 1 };
enum D2D1_FILL_MODE           { D2D1_FILL_MODE_ALTERNATE = 0, D2D1_FILL_MODE_WINDING = 1 };
enum D2D1_PATH_SEGMENT        { D2D1_PATH_SEGMENT_NONE = 0, D2D1_PATH_SEGMENT_FORCE_UNSTROKED = 1, D2D1_PATH_SEGMENT_FORCE_ROUND_LINE_JOIN = 2 };
enum D2D1_SWEEP_DIRECTION     { D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE = 0, D2D1_SWEEP_DIRECTION_CLOCKWISE = 1 };
enum D2D1_ARC_SIZE            { D2D1_ARC_SIZE_SMALL = 0, D2D1_ARC_SIZE_LARGE = 1 };
enum D2D1_CAP_STYLE           { D2D1_CAP_STYLE_FLAT = 0, D2D1_CAP_STYLE_SQUARE = 1, D2D1_CAP_STYLE_ROUND = 2, D2D1_CAP_STYLE_TRIANGLE = 3 };
enum D2D1_LINE_JOIN           { D2D1_LINE_JOIN_MITER = 0, D2D1_LINE_JOIN_BEVEL = 1, D2D1_LINE_JOIN_ROUND = 2, D2D1_LINE_JOIN_MITER_OR_BEVEL = 3 };
enum D2D1_DASH_STYLE          { D2D1_DASH_STYLE_SOLID = 0, D2D1_DASH_STYLE_DASH = 1, D2D1_DASH_STYLE_DOT = 2,
                                D2D1_DASH_STYLE_DASH_DOT = 3, D2D1_DASH_STYLE_DASH_DOT_DOT = 4, D2D1_DASH_STYLE_CUSTOM = 5 };
enum D2D1_GAMMA               { D2D1_GAMMA_2_2 = 0, D2D1_GAMMA_1_0 = 1 };
enum D2D1_EXTEND_MODE         { D2D1_EXTEND_MODE_CLAMP = 0, D2D1_EXTEND_MODE_WRAP = 1, D2D1_EXTEND_MODE_MIRROR = 2 };
enum D2D1_BITMAP_INTERPOLATION_MODE { D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR = 0, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR = 1 };
enum D2D1_LAYER_OPTIONS       { D2D1_LAYER_OPTIONS_NONE = 0, D2D1_LAYER_OPTIONS_INITIALIZE_FOR_CLEARTYPE = 1 };
enum D2D1_DRAW_TEXT_OPTIONS   { D2D1_DRAW_TEXT_OPTIONS_NONE = 0, D2D1_DRAW_TEXT_OPTIONS_NO_SNAP = 1,
                                D2D1_DRAW_TEXT_OPTIONS_CLIP = 2, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT = 4 };
inline D2D1_DRAW_TEXT_OPTIONS operator|(D2D1_DRAW_TEXT_OPTIONS a, D2D1_DRAW_TEXT_OPTIONS b) { return (D2D1_DRAW_TEXT_OPTIONS)((int)a | (int)b); }
enum D2D1_RENDER_TARGET_TYPE  { D2D1_RENDER_TARGET_TYPE_DEFAULT = 0, D2D1_RENDER_TARGET_TYPE_SOFTWARE = 1, D2D1_RENDER_TARGET_TYPE_HARDWARE = 2 };
enum D2D1_RENDER_TARGET_USAGE { D2D1_RENDER_TARGET_USAGE_NONE = 0 };
enum D2D1_FEATURE_LEVEL       { D2D1_FEATURE_LEVEL_DEFAULT = 0 };
enum D2D1_PRESENT_OPTIONS     { D2D1_PRESENT_OPTIONS_NONE = 0, D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS = 1, D2D1_PRESENT_OPTIONS_IMMEDIATELY = 2 };
enum D2D1_ALPHA_MODE          { D2D1_ALPHA_MODE_UNKNOWN = 0, D2D1_ALPHA_MODE_PREMULTIPLIED = 1, D2D1_ALPHA_MODE_STRAIGHT = 2, D2D1_ALPHA_MODE_IGNORE = 3 };
enum DXGI_FORMAT              { DXGI_FORMAT_UNKNOWN = 0, DXGI_FORMAT_B8G8R8A8_UNORM = 87 };
enum D2D1_WINDOW_STATE        { D2D1_WINDOW_STATE_NONE = 0, D2D1_WINDOW_STATE_OCCLUDED = 1 };
enum D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS { D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE = 0 };

// --- structs ---------------------------------------------------------------------
struct D2D1_COLOR_F      { FLOAT r, g, b, a; };
struct D2D1_POINT_2F     { FLOAT x, y; };
struct D2D1_POINT_2U     { UINT32 x, y; };
struct D2D1_SIZE_F       { FLOAT width, height; };
struct D2D1_SIZE_U       { UINT32 width, height; };
struct D2D1_RECT_F       { FLOAT left, top, right, bottom; };
struct D2D1_RECT_U       { UINT32 left, top, right, bottom; };
struct D2D1_ROUNDED_RECT { D2D1_RECT_F rect; FLOAT radiusX, radiusY; };
struct D2D1_ELLIPSE      { D2D1_POINT_2F point; FLOAT radiusX, radiusY; };
struct D2D1_MATRIX_3X2_F { FLOAT _11, _12, _21, _22, _31, _32; };
struct D2D1_GRADIENT_STOP { FLOAT position; D2D1_COLOR_F color; };
struct D2D1_ARC_SEGMENT   { D2D1_POINT_2F point; D2D1_SIZE_F size; FLOAT rotationAngle; D2D1_SWEEP_DIRECTION sweepDirection; D2D1_ARC_SIZE arcSize; };
struct D2D1_BEZIER_SEGMENT { D2D1_POINT_2F point1, point2, point3; };
struct D2D1_QUADRATIC_BEZIER_SEGMENT { D2D1_POINT_2F point1, point2; };
struct D2D1_TRIANGLE       { D2D1_POINT_2F point1, point2, point3; };
struct D2D1_BRUSH_PROPERTIES { FLOAT opacity; D2D1_MATRIX_3X2_F transform; };
struct D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES { D2D1_POINT_2F startPoint, endPoint; };
struct D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES { D2D1_POINT_2F center, gradientOriginOffset; FLOAT radiusX, radiusY; };
struct D2D1_STROKE_STYLE_PROPERTIES {
	D2D1_CAP_STYLE startCap, endCap, dashCap; D2D1_LINE_JOIN lineJoin; FLOAT miterLimit; D2D1_DASH_STYLE dashStyle; FLOAT dashOffset;
};
struct D2D1_PIXEL_FORMAT { DXGI_FORMAT format; D2D1_ALPHA_MODE alphaMode; };
struct D2D1_BITMAP_PROPERTIES { D2D1_PIXEL_FORMAT pixelFormat; FLOAT dpiX, dpiY; };
struct D2D1_RENDER_TARGET_PROPERTIES {
	D2D1_RENDER_TARGET_TYPE type; D2D1_PIXEL_FORMAT pixelFormat; FLOAT dpiX, dpiY;
	D2D1_RENDER_TARGET_USAGE usage; D2D1_FEATURE_LEVEL minLevel;
};
struct D2D1_HWND_RENDER_TARGET_PROPERTIES { HWND hwnd; D2D1_SIZE_U pixelSize; D2D1_PRESENT_OPTIONS presentOptions; };
struct ID2D1Geometry;
struct ID2D1Brush;
struct D2D1_LAYER_PARAMETERS {
	D2D1_RECT_F contentBounds; ID2D1Geometry* geometricMask; D2D1_ANTIALIAS_MODE maskAntialiasMode;
	D2D1_MATRIX_3X2_F maskTransform; FLOAT opacity; ID2D1Brush* opacityBrush; D2D1_LAYER_OPTIONS layerOptions;
};
typedef UINT64 D2D1_TAG;

// --- D2D1:: helpers ------------------------------------------------------------------
namespace D2D1 {
	class ColorF : public D2D1_COLOR_F {
	public:
		enum Enum : UINT32 {
			Black = 0x000000, White = 0xFFFFFF, Red = 0xFF0000, Green = 0x008000, Lime = 0x00FF00, Blue = 0x0000FF,
			Yellow = 0xFFFF00, Orange = 0xFFA500, Gray = 0x808080, DarkGray = 0xA9A9A9, LightGray = 0xD3D3D3,
			Silver = 0xC0C0C0, DimGray = 0x696969, Gold = 0xFFD700, Purple = 0x800080, Cyan = 0x00FFFF,
			Magenta = 0xFF00FF, CornflowerBlue = 0x6495ED, SkyBlue = 0x87CEEB, SteelBlue = 0x4682B4,
			Navy = 0x000080, Teal = 0x008080, Pink = 0xFFC0CB, Crimson = 0xDC143C, Tomato = 0xFF6347,
			WhiteSmoke = 0xF5F5F5, Gainsboro = 0xDCDCDC, Transparent = 0x000000
		};
		ColorF(UINT32 rgb, FLOAT a = 1.0f) { Init(rgb, a); }
		ColorF(Enum known, FLOAT a = 1.0f) { Init((UINT32)known, a); }
		ColorF(FLOAT r_, FLOAT g_, FLOAT b_, FLOAT a_ = 1.0f) { r = r_; g = g_; b = b_; a = a_; }
	private:
		void Init(UINT32 rgb, FLOAT a_) {
			r = (FLOAT)((rgb >> 16) & 0xFF) / 255.0f; g = (FLOAT)((rgb >> 8) & 0xFF) / 255.0f;
			b = (FLOAT)(rgb & 0xFF) / 255.0f; a = a_;
		}
	};

	inline D2D1_POINT_2F Point2F(FLOAT x = 0.0f, FLOAT y = 0.0f) { return D2D1_POINT_2F{ x, y }; }
	inline D2D1_POINT_2U Point2U(UINT32 x = 0, UINT32 y = 0)     { return D2D1_POINT_2U{ x, y }; }
	inline D2D1_SIZE_F   SizeF(FLOAT w = 0.0f, FLOAT h = 0.0f)   { return D2D1_SIZE_F{ w, h }; }
	inline D2D1_SIZE_U   SizeU(UINT32 w = 0, UINT32 h = 0)       { return D2D1_SIZE_U{ w, h }; }
	inline D2D1_RECT_F   RectF(FLOAT l = 0.0f, FLOAT t = 0.0f, FLOAT r = 0.0f, FLOAT b = 0.0f) { return D2D1_RECT_F{ l, t, r, b }; }
	inline D2D1_RECT_U   RectU(UINT32 l = 0, UINT32 t = 0, UINT32 r = 0, UINT32 b = 0) { return D2D1_RECT_U{ l, t, r, b }; }
	inline D2D1_RECT_F   InfiniteRect() { return D2D1_RECT_F{ -FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX }; }
	inline D2D1_ROUNDED_RECT RoundedRect(const D2D1_RECT_F& r, FLOAT rx, FLOAT ry) { return D2D1_ROUNDED_RECT{ r, rx, ry }; }
	inline D2D1_ELLIPSE  Ellipse(const D2D1_POINT_2F& c, FLOAT rx, FLOAT ry) { return D2D1_ELLIPSE{ c, rx, ry }; }
	inline D2D1_ARC_SEGMENT ArcSegment(const D2D1_POINT_2F& p, const D2D1_SIZE_F& s, FLOAT rot,
	                                   D2D1_SWEEP_DIRECTION dir, D2D1_ARC_SIZE size) { return D2D1_ARC_SEGMENT{ p, s, rot, dir, size }; }
	inline D2D1_BEZIER_SEGMENT BezierSegment(const D2D1_POINT_2F& a, const D2D1_POINT_2F& b, const D2D1_POINT_2F& c) { return D2D1_BEZIER_SEGMENT{ a, b, c }; }
	inline D2D1_QUADRATIC_BEZIER_SEGMENT QuadraticBezierSegment(const D2D1_POINT_2F& a, const D2D1_POINT_2F& b) { return D2D1_QUADRATIC_BEZIER_SEGMENT{ a, b }; }
	inline D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES LinearGradientBrushProperties(const D2D1_POINT_2F& a, const D2D1_POINT_2F& b) { return { a, b }; }
	inline D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES RadialGradientBrushProperties(const D2D1_POINT_2F& c, const D2D1_POINT_2F& off, FLOAT rx, FLOAT ry) { return { c, off, rx, ry }; }
	inline D2D1_STROKE_STYLE_PROPERTIES StrokeStyleProperties(
		D2D1_CAP_STYLE startCap = D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE endCap = D2D1_CAP_STYLE_FLAT,
		D2D1_CAP_STYLE dashCap = D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN join = D2D1_LINE_JOIN_MITER,
		FLOAT miter = 10.0f, D2D1_DASH_STYLE dash = D2D1_DASH_STYLE_SOLID, FLOAT dashOffset = 0.0f) {
		return D2D1_STROKE_STYLE_PROPERTIES{ startCap, endCap, dashCap, join, miter, dash, dashOffset };
	}
	inline D2D1_PIXEL_FORMAT PixelFormat(DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE a = D2D1_ALPHA_MODE_UNKNOWN) { return { f, a }; }
	inline D2D1_BITMAP_PROPERTIES BitmapProperties(const D2D1_PIXEL_FORMAT& f = PixelFormat(), FLOAT dx = 96.0f, FLOAT dy = 96.0f) { return { f, dx, dy }; }
	inline D2D1_RENDER_TARGET_PROPERTIES RenderTargetProperties(D2D1_RENDER_TARGET_TYPE t = D2D1_RENDER_TARGET_TYPE_DEFAULT,
		const D2D1_PIXEL_FORMAT& f = PixelFormat(), FLOAT dx = 0.0f, FLOAT dy = 0.0f,
		D2D1_RENDER_TARGET_USAGE u = D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL l = D2D1_FEATURE_LEVEL_DEFAULT) {
		return { t, f, dx, dy, u, l };
	}
	inline D2D1_HWND_RENDER_TARGET_PROPERTIES HwndRenderTargetProperties(HWND h, D2D1_SIZE_U s = SizeU(),
		D2D1_PRESENT_OPTIONS o = D2D1_PRESENT_OPTIONS_NONE) { return { h, s, o }; }

	class Matrix3x2F : public D2D1_MATRIX_3X2_F {
	public:
		Matrix3x2F() { _11 = 1; _12 = 0; _21 = 0; _22 = 1; _31 = 0; _32 = 0; }
		Matrix3x2F(FLOAT a, FLOAT b, FLOAT c, FLOAT d, FLOAT e, FLOAT f) { _11 = a; _12 = b; _21 = c; _22 = d; _31 = e; _32 = f; }
		Matrix3x2F(const D2D1_MATRIX_3X2_F& m) : D2D1_MATRIX_3X2_F(m) {}
		static Matrix3x2F Identity() { return Matrix3x2F(); }
		static Matrix3x2F Translation(FLOAT x, FLOAT y) { return Matrix3x2F(1, 0, 0, 1, x, y); }
		static Matrix3x2F Translation(D2D1_SIZE_F s)    { return Translation(s.width, s.height); }
		static Matrix3x2F Scale(FLOAT sx, FLOAT sy, D2D1_POINT_2F c = Point2F()) { return Matrix3x2F(sx, 0, 0, sy, c.x - sx * c.x, c.y - sy * c.y); }
		static Matrix3x2F Scale(D2D1_SIZE_F s, D2D1_POINT_2F c = Point2F()) { return Scale(s.width, s.height, c); }
		static Matrix3x2F Rotation(FLOAT deg, D2D1_POINT_2F c = Point2F()) {
			float a = deg * 3.14159265358979f / 180.0f, cs = cosf(a), sn = sinf(a);
			return Matrix3x2F(cs, sn, -sn, cs, c.x - cs * c.x + sn * c.y, c.y - sn * c.x - cs * c.y);
		}
		static Matrix3x2F Skew(FLOAT ax, FLOAT ay, D2D1_POINT_2F c = Point2F()) {
			float tx = tanf(ax * 3.14159265358979f / 180.0f), ty = tanf(ay * 3.14159265358979f / 180.0f);
			return Matrix3x2F(1, ty, tx, 1, -c.y * tx, -c.x * ty);
		}
		static Matrix3x2F* ReinterpretBaseType(D2D1_MATRIX_3X2_F* m) { return static_cast<Matrix3x2F*>(m); }
		static const Matrix3x2F* ReinterpretBaseType(const D2D1_MATRIX_3X2_F* m) { return static_cast<const Matrix3x2F*>(m); }
		FLOAT Determinant() const { return _11 * _22 - _12 * _21; }
		bool  IsInvertible() const { return fabsf(Determinant()) > 1e-12f; }
		bool  IsIdentity() const { return _11 == 1 && _12 == 0 && _21 == 0 && _22 == 1 && _31 == 0 && _32 == 0; }
		bool  Invert() {
			float d = Determinant(); if (fabsf(d) <= 1e-12f) return false;
			Matrix3x2F m(_22 / d, -_12 / d, -_21 / d, _11 / d, (_21 * _32 - _22 * _31) / d, (_12 * _31 - _11 * _32) / d);
			*this = m; return true;
		}
		void SetProduct(const D2D1_MATRIX_3X2_F& a, const D2D1_MATRIX_3X2_F& b) {
			Matrix3x2F r(a._11 * b._11 + a._12 * b._21, a._11 * b._12 + a._12 * b._22,
			             a._21 * b._11 + a._22 * b._21, a._21 * b._12 + a._22 * b._22,
			             a._31 * b._11 + a._32 * b._21 + b._31, a._31 * b._12 + a._32 * b._22 + b._32);
			*this = r;
		}
		D2D1_POINT_2F TransformPoint(D2D1_POINT_2F p) const { return Point2F(p.x * _11 + p.y * _21 + _31, p.x * _12 + p.y * _22 + _32); }
	};
	inline D2D1_MATRIX_3X2_F IdentityMatrix() { return Matrix3x2F::Identity(); }
	inline D2D1_LAYER_PARAMETERS LayerParameters(const D2D1_RECT_F& bounds = InfiniteRect(), ID2D1Geometry* mask = nullptr,
		D2D1_ANTIALIAS_MODE aa = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1_MATRIX_3X2_F mt = IdentityMatrix(),
		FLOAT opacity = 1.0f, ID2D1Brush* ob = nullptr, D2D1_LAYER_OPTIONS opt = D2D1_LAYER_OPTIONS_NONE) {
		return D2D1_LAYER_PARAMETERS{ bounds, mask, aa, mt, opacity, ob, opt };
	}
	inline D2D1_BRUSH_PROPERTIES BrushProperties(FLOAT opacity = 1.0f, const D2D1_MATRIX_3X2_F& t = IdentityMatrix()) { return { opacity, t }; }
}
inline D2D1::Matrix3x2F operator*(const D2D1_MATRIX_3X2_F& a, const D2D1_MATRIX_3X2_F& b) { D2D1::Matrix3x2F r; r.SetProduct(a, b); return r; }

// --- paths ---------------------------------------------------------------------------
namespace chrono_linux {

	inline void ToCairo(const D2D1_MATRIX_3X2_F& m, cairo_matrix_t* c) { cairo_matrix_init(c, m._11, m._12, m._21, m._22, m._31, m._32); }
	inline void SetMatrix(cairo_t* cr, const D2D1_MATRIX_3X2_F& m) { cairo_matrix_t c; ToCairo(m, &c); cairo_set_matrix(cr, &c); }

	// A recorded path: figures of move / line / cubic / close, replayed into Cairo.
	struct PathData {
		struct Op { char t; float p[6]; };          // 'M', 'L', 'C', 'Z'
		struct Figure { size_t first, count; bool hollow; };
		std::vector<Op> ops;
		std::vector<Figure> figs;
		float cx = 0, cy = 0;                        // current point, for arcs and quadratics

		void Begin(float x, float y, bool hollow) { figs.push_back({ ops.size(), 0, hollow }); Push({ 'M', { x, y } }); cx = x; cy = y; }
		void Line(float x, float y) { Push({ 'L', { x, y } }); cx = x; cy = y; }
		void Curve(float a, float b, float c, float d, float e, float f) { Push({ 'C', { a, b, c, d, e, f } }); cx = e; cy = f; }
		void Close() { Push({ 'Z', {} }); }
		void Push(Op o) { if (figs.empty()) figs.push_back({ ops.size(), 0, false }); ops.push_back(o); figs.back().count++; }

		void Quad(float x1, float y1, float x2, float y2) {
			Curve(cx + 2.0f / 3.0f * (x1 - cx), cy + 2.0f / 3.0f * (y1 - cy),
			      x2 + 2.0f / 3.0f * (x1 - x2), y2 + 2.0f / 3.0f * (y1 - y2), x2, y2);
		}
		// SVG endpoint arc (F.6.5) -> centre form -> cubic Béziers of at most 90° each.
		void Arc(float x2, float y2, float rx, float ry, float rotDeg, bool large, bool clockwise) {
			double x1 = cx, y1 = cy;
			if ((x1 == x2 && y1 == y2)) return;
			if (rx == 0.0f || ry == 0.0f) { Line(x2, y2); return; }
			double phi = rotDeg * M_PI / 180.0, cp = cos(phi), sp = sin(phi);
			double dx = (x1 - x2) / 2.0, dy = (y1 - y2) / 2.0;
			double x1p = cp * dx + sp * dy, y1p = -sp * dx + cp * dy;
			double Rx = fabs(rx), Ry = fabs(ry);
			double lam = (x1p * x1p) / (Rx * Rx) + (y1p * y1p) / (Ry * Ry);
			if (lam > 1.0) { double s = sqrt(lam); Rx *= s; Ry *= s; }
			double num = Rx * Rx * Ry * Ry - Rx * Rx * y1p * y1p - Ry * Ry * x1p * x1p;
			double den = Rx * Rx * y1p * y1p + Ry * Ry * x1p * x1p;
			double coef = (large == clockwise ? -1.0 : 1.0) * sqrt((std::max)(0.0, den > 0 ? num / den : 0.0));
			double cxp = coef * (Rx * y1p / Ry), cyp = coef * (-Ry * x1p / Rx);
			double ccx = cp * cxp - sp * cyp + (x1 + x2) / 2.0, ccy = sp * cxp + cp * cyp + (y1 + y2) / 2.0;
			auto ang = [](double ux, double uy, double vx, double vy) {
				double a = atan2(ux * vy - uy * vx, ux * vx + uy * vy); return a;
			};
			double t1 = ang(1, 0, (x1p - cxp) / Rx, (y1p - cyp) / Ry);
			double dt = ang((x1p - cxp) / Rx, (y1p - cyp) / Ry, (-x1p - cxp) / Rx, (-y1p - cyp) / Ry);
			if (!clockwise && dt > 0) dt -= 2 * M_PI;
			if (clockwise && dt < 0)  dt += 2 * M_PI;
			int n = (int)ceil(fabs(dt) / (M_PI / 2.0) - 1e-9); if (n < 1) n = 1;
			double d = dt / n, k = 4.0 / 3.0 * tan(d / 4.0);
			auto map = [&](double u, double v, float& ox, float& oy) {
				ox = (float)(ccx + Rx * cp * u - Ry * sp * v);
				oy = (float)(ccy + Rx * sp * u + Ry * cp * v);
			};
			for (int i = 0; i < n; ++i) {
				double a = t1 + d * i, b = a + d;
				float c1x, c1y, c2x, c2y, ex, ey;
				map(cos(a) - k * sin(a), sin(a) + k * cos(a), c1x, c1y);
				map(cos(b) + k * sin(b), sin(b) - k * cos(b), c2x, c2y);
				if (i == n - 1) { ex = x2; ey = y2; } else map(cos(b), sin(b), ex, ey);
				Curve(c1x, c1y, c2x, c2y, ex, ey);
			}
		}
		void Emit(cairo_t* cr, bool forFill) const {
			for (auto& f : figs) {
				if (forFill && f.hollow) continue;
				for (size_t i = f.first; i < f.first + f.count; ++i) {
					const Op& o = ops[i];
					switch (o.t) {
						case 'M': cairo_move_to(cr, o.p[0], o.p[1]); break;
						case 'L': cairo_line_to(cr, o.p[0], o.p[1]); break;
						case 'C': cairo_curve_to(cr, o.p[0], o.p[1], o.p[2], o.p[3], o.p[4], o.p[5]); break;
						case 'Z': cairo_close_path(cr); break;
					}
				}
			}
		}
	};

	constexpr double kKappa = 0.5522847498307936;

	inline void RoundedRectPath(cairo_t* cr, const D2D1_ROUNDED_RECT& rr) {
		double l = rr.rect.left, t = rr.rect.top, r = rr.rect.right, b = rr.rect.bottom;
		if (r < l) std::swap(l, r);
		if (b < t) std::swap(t, b);
		double rx = (std::min)((double)fabsf(rr.radiusX), (r - l) / 2), ry = (std::min)((double)fabsf(rr.radiusY), (b - t) / 2);
		if (rx <= 0 || ry <= 0) { cairo_rectangle(cr, l, t, r - l, b - t); return; }
		double kx = rx * kKappa, ky = ry * kKappa;
		cairo_move_to(cr, l + rx, t);
		cairo_line_to(cr, r - rx, t);
		cairo_curve_to(cr, r - rx + kx, t, r, t + ry - ky, r, t + ry);
		cairo_line_to(cr, r, b - ry);
		cairo_curve_to(cr, r, b - ry + ky, r - rx + kx, b, r - rx, b);
		cairo_line_to(cr, l + rx, b);
		cairo_curve_to(cr, l + rx - kx, b, l, b - ry + ky, l, b - ry);
		cairo_line_to(cr, l, t + ry);
		cairo_curve_to(cr, l, t + ry - ky, l + rx - kx, t, l + rx, t);
		cairo_close_path(cr);
	}
	inline void EllipsePath(cairo_t* cr, const D2D1_ELLIPSE& e) {
		double x = e.point.x, y = e.point.y, rx = fabsf(e.radiusX), ry = fabsf(e.radiusY);
		if (rx <= 0 || ry <= 0) return;
		double kx = rx * kKappa, ky = ry * kKappa;
		cairo_move_to(cr, x + rx, y);
		cairo_curve_to(cr, x + rx, y + ky, x + kx, y + ry, x, y + ry);
		cairo_curve_to(cr, x - kx, y + ry, x - rx, y + ky, x - rx, y);
		cairo_curve_to(cr, x - rx, y - ky, x - kx, y - ry, x, y - ry);
		cairo_curve_to(cr, x + kx, y - ry, x + rx, y - ky, x + rx, y);
		cairo_close_path(cr);
	}

	// A 1x1 surface to ask Cairo geometric questions (bounds, containment).
	inline cairo_t* Scratch() {
		static cairo_t* cr = [] {
			cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
			cairo_t* c = cairo_create(s); cairo_surface_destroy(s); return c;
		}();
		cairo_new_path(cr);
		cairo_identity_matrix(cr);
		return cr;
	}
}

// --- resources ---------------------------------------------------------------------------
struct ID2D1Factory;
struct ID2D1Resource : IUnknown {
	void GetFactory(ID2D1Factory** f) const;
};

struct ID2D1StrokeStyle : ID2D1Resource {
	D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties();
	std::vector<FLOAT> dashes;
	D2D1_CAP_STYLE GetStartCap() const { return props.startCap; }
	D2D1_CAP_STYLE GetEndCap() const   { return props.endCap; }
	D2D1_LINE_JOIN GetLineJoin() const { return props.lineJoin; }
	D2D1_DASH_STYLE GetDashStyle() const { return props.dashStyle; }
	void Apply(cairo_t* cr, float width) const {
		cairo_set_line_cap(cr, props.startCap == D2D1_CAP_STYLE_ROUND ? CAIRO_LINE_CAP_ROUND
		                     : props.startCap == D2D1_CAP_STYLE_SQUARE ? CAIRO_LINE_CAP_SQUARE : CAIRO_LINE_CAP_BUTT);
		cairo_set_line_join(cr, props.lineJoin == D2D1_LINE_JOIN_ROUND ? CAIRO_LINE_JOIN_ROUND
		                      : props.lineJoin == D2D1_LINE_JOIN_BEVEL ? CAIRO_LINE_JOIN_BEVEL : CAIRO_LINE_JOIN_MITER);
		cairo_set_miter_limit(cr, props.miterLimit > 1.0f ? props.miterLimit : 10.0f);
		std::vector<double> d;
		switch (props.dashStyle) {
			case D2D1_DASH_STYLE_DASH:         d = { 2, 2 }; break;
			case D2D1_DASH_STYLE_DOT:          d = { 0, 2 }; break;
			case D2D1_DASH_STYLE_DASH_DOT:     d = { 2, 2, 0, 2 }; break;
			case D2D1_DASH_STYLE_DASH_DOT_DOT: d = { 2, 2, 0, 2, 0, 2 }; break;
			case D2D1_DASH_STYLE_CUSTOM:       for (float x : dashes) d.push_back(x); break;
			default: break;
		}
		if (d.empty()) { cairo_set_dash(cr, nullptr, 0, 0); return; }
		bool roundDots = props.dashCap != D2D1_CAP_STYLE_FLAT;
		for (double& x : d) x = x * width;                      // D2D dash lengths are in stroke widths
		if (!roundDots) for (double& x : d) if (x == 0) x = width * 0.01;
		if (roundDots) cairo_set_line_cap(cr, props.dashCap == D2D1_CAP_STYLE_ROUND ? CAIRO_LINE_CAP_ROUND : CAIRO_LINE_CAP_SQUARE);
		cairo_set_dash(cr, d.data(), (int)d.size(), props.dashOffset * width);
	}
};

struct ID2D1Brush : ID2D1Resource {
	FLOAT opacity = 1.0f;
	D2D1_MATRIX_3X2_F transform = D2D1::IdentityMatrix();
	void  SetOpacity(FLOAT o) { opacity = o; }
	FLOAT GetOpacity() const  { return opacity; }
	void  SetTransform(const D2D1_MATRIX_3X2_F& t) { transform = t; }
	void  SetTransform(const D2D1_MATRIX_3X2_F* t) { if (t) transform = *t; }
	void  GetTransform(D2D1_MATRIX_3X2_F* t) const { if (t) *t = transform; }
	virtual void Apply(cairo_t* cr) const = 0;
protected:
	void SetPatternMatrix(cairo_pattern_t* p) const {
		D2D1::Matrix3x2F inv(transform);
		if (inv.Invert()) { cairo_matrix_t m; chrono_linux::ToCairo(inv, &m); cairo_pattern_set_matrix(p, &m); }
	}
};

struct ID2D1SolidColorBrush : ID2D1Brush {
	D2D1_COLOR_F color{ 0, 0, 0, 1 };
	void         SetColor(const D2D1_COLOR_F& c) { color = c; }
	void         SetColor(const D2D1_COLOR_F* c) { if (c) color = *c; }
	D2D1_COLOR_F GetColor() const { return color; }
	void Apply(cairo_t* cr) const override { cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a * opacity); }
};

struct ID2D1GradientStopCollection : ID2D1Resource {
	std::vector<D2D1_GRADIENT_STOP> stops;
	D2D1_EXTEND_MODE extend = D2D1_EXTEND_MODE_CLAMP;
	UINT32 GetGradientStopCount() const { return (UINT32)stops.size(); }
	void   GetGradientStops(D2D1_GRADIENT_STOP* out, UINT32 n) const { for (UINT32 i = 0; i < n && i < stops.size(); ++i) out[i] = stops[i]; }
	D2D1_EXTEND_MODE GetExtendMode() const { return extend; }
	void Fill(cairo_pattern_t* p, float opacity) const {
		for (auto& s : stops) cairo_pattern_add_color_stop_rgba(p, s.position, s.color.r, s.color.g, s.color.b, s.color.a * opacity);
		cairo_pattern_set_extend(p, extend == D2D1_EXTEND_MODE_WRAP ? CAIRO_EXTEND_REPEAT
		                          : extend == D2D1_EXTEND_MODE_MIRROR ? CAIRO_EXTEND_REFLECT : CAIRO_EXTEND_PAD);
	}
};

struct ID2D1LinearGradientBrush : ID2D1Brush {
	D2D1_POINT_2F start{ 0, 0 }, end{ 0, 0 };
	ID2D1GradientStopCollection* coll = nullptr;
	~ID2D1LinearGradientBrush() override { if (coll) coll->Release(); }
	void SetStartPoint(D2D1_POINT_2F p) { start = p; }
	void SetEndPoint(D2D1_POINT_2F p)   { end = p; }
	D2D1_POINT_2F GetStartPoint() const { return start; }
	D2D1_POINT_2F GetEndPoint() const   { return end; }
	void GetGradientStopCollection(ID2D1GradientStopCollection** c) const { if (c) { *c = coll; if (coll) coll->AddRef(); } }
	void Apply(cairo_t* cr) const override {
		cairo_pattern_t* p = cairo_pattern_create_linear(start.x, start.y, end.x, end.y);
		if (coll) coll->Fill(p, opacity);
		SetPatternMatrix(p);
		cairo_set_source(cr, p);
		cairo_pattern_destroy(p);
	}
};

struct ID2D1RadialGradientBrush : ID2D1Brush {
	D2D1_POINT_2F center{ 0, 0 }, offset{ 0, 0 };
	FLOAT rx = 0, ry = 0;
	ID2D1GradientStopCollection* coll = nullptr;
	~ID2D1RadialGradientBrush() override { if (coll) coll->Release(); }
	void SetCenter(D2D1_POINT_2F c) { center = c; }
	void SetGradientOriginOffset(D2D1_POINT_2F o) { offset = o; }
	void SetRadiusX(FLOAT r) { rx = r; }
	void SetRadiusY(FLOAT r) { ry = r; }
	D2D1_POINT_2F GetCenter() const { return center; }
	FLOAT GetRadiusX() const { return rx; }
	FLOAT GetRadiusY() const { return ry; }
	void Apply(cairo_t* cr) const override {
		if (rx <= 0 || ry <= 0) { cairo_set_source_rgba(cr, 0, 0, 0, 0); return; }
		// Circle of radius rx in a space squashed vertically by ry/rx.
		cairo_pattern_t* p = cairo_pattern_create_radial(center.x + offset.x, (center.y + offset.y) * rx / ry, 0, center.x, center.y * rx / ry, rx);
		if (coll) coll->Fill(p, opacity);
		D2D1::Matrix3x2F squash = D2D1::Matrix3x2F::Scale(1.0f, ry / rx);
		D2D1::Matrix3x2F inv = squash * transform;
		if (inv.Invert()) { cairo_matrix_t m; chrono_linux::ToCairo(inv, &m); cairo_pattern_set_matrix(p, &m); }
		cairo_set_source(cr, p);
		cairo_pattern_destroy(p);
	}
};

struct ID2D1Bitmap : ID2D1Resource {
	cairo_surface_t* surf = nullptr;
	FLOAT dpiX = 96.0f, dpiY = 96.0f;
	~ID2D1Bitmap() override { if (surf) cairo_surface_destroy(surf); }
	D2D1_SIZE_U GetPixelSize() const { return surf ? D2D1::SizeU((UINT32)cairo_image_surface_get_width(surf), (UINT32)cairo_image_surface_get_height(surf)) : D2D1::SizeU(); }
	D2D1_SIZE_F GetSize() const { D2D1_SIZE_U s = GetPixelSize(); return D2D1::SizeF(s.width * 96.0f / dpiX, s.height * 96.0f / dpiY); }
	void GetDpi(FLOAT* x, FLOAT* y) const { if (x) *x = dpiX; if (y) *y = dpiY; }
	D2D1_PIXEL_FORMAT GetPixelFormat() const { return D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED); }
	HRESULT CopyFromMemory(const D2D1_RECT_U* r, const void* src, UINT32 pitch) {
		if (!surf || !src) return E_INVALIDARG;
		D2D1_SIZE_U s = GetPixelSize();
		D2D1_RECT_U rc = r ? *r : D2D1::RectU(0, 0, s.width, s.height);
		cairo_surface_flush(surf);
		unsigned char* dst = cairo_image_surface_get_data(surf);
		int stride = cairo_image_surface_get_stride(surf);
		for (UINT32 y = rc.top; y < rc.bottom && y < s.height; ++y)
			memcpy(dst + (size_t)y * stride + rc.left * 4, (const uint8_t*)src + (size_t)(y - rc.top) * pitch, (size_t)(rc.right - rc.left) * 4);
		cairo_surface_mark_dirty(surf);
		return S_OK;
	}
	static ID2D1Bitmap* FromPixels(UINT32 w, UINT32 h, const void* src, UINT32 pitch) {
		auto* b = new ID2D1Bitmap();
		b->surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)w, (int)h);
		if (src) b->CopyFromMemory(nullptr, src, pitch);
		return b;
	}
};

struct ID2D1Layer : ID2D1Resource {
	D2D1_SIZE_F size{ 0, 0 };
	D2D1_SIZE_F GetSize() const { return size; }
};

// --- geometries ----------------------------------------------------------------------------
struct ID2D1Geometry : ID2D1Resource {
	virtual void Emit(cairo_t* cr, bool forFill) const = 0;
	virtual bool EvenOdd() const { return false; }

	HRESULT GetBounds(const D2D1_MATRIX_3X2_F* world, D2D1_RECT_F* out) const {
		if (!out) return E_POINTER;
		cairo_t* cr = chrono_linux::Scratch();
		if (world) chrono_linux::SetMatrix(cr, *world);
		Emit(cr, false);
		cairo_identity_matrix(cr);
		double x0, y0, x1, y1; cairo_path_extents(cr, &x0, &y0, &x1, &y1);
		*out = D2D1::RectF((FLOAT)x0, (FLOAT)y0, (FLOAT)x1, (FLOAT)y1);
		return S_OK;
	}
	HRESULT GetBounds(const D2D1_MATRIX_3X2_F& world, D2D1_RECT_F* out) const { return GetBounds(&world, out); }
	HRESULT FillContainsPoint(D2D1_POINT_2F p, const D2D1_MATRIX_3X2_F* world, FLOAT, BOOL* out) const {
		if (!out) return E_POINTER;
		cairo_t* cr = chrono_linux::Scratch();
		if (world) chrono_linux::SetMatrix(cr, *world);
		Emit(cr, true);
		cairo_identity_matrix(cr);
		cairo_set_fill_rule(cr, EvenOdd() ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
		*out = cairo_in_fill(cr, p.x, p.y) ? TRUE : FALSE;
		return S_OK;
	}
	HRESULT FillContainsPoint(D2D1_POINT_2F p, const D2D1_MATRIX_3X2_F* world, BOOL* out) const { return FillContainsPoint(p, world, 0.25f, out); }
	HRESULT FillContainsPoint(D2D1_POINT_2F p, const D2D1_MATRIX_3X2_F& world, BOOL* out) const { return FillContainsPoint(p, &world, 0.25f, out); }
	HRESULT StrokeContainsPoint(D2D1_POINT_2F p, FLOAT width, ID2D1StrokeStyle* st, const D2D1_MATRIX_3X2_F* world, FLOAT, BOOL* out) const {
		if (!out) return E_POINTER;
		cairo_t* cr = chrono_linux::Scratch();
		if (world) chrono_linux::SetMatrix(cr, *world);
		Emit(cr, false);
		cairo_identity_matrix(cr);
		cairo_set_line_width(cr, width);
		if (st) st->Apply(cr, width);
		*out = cairo_in_stroke(cr, p.x, p.y) ? TRUE : FALSE;
		return S_OK;
	}
	HRESULT StrokeContainsPoint(D2D1_POINT_2F p, FLOAT w, ID2D1StrokeStyle* st, const D2D1_MATRIX_3X2_F* world, BOOL* out) const { return StrokeContainsPoint(p, w, st, world, 0.25f, out); }
	HRESULT StrokeContainsPoint(D2D1_POINT_2F p, FLOAT w, ID2D1StrokeStyle* st, const D2D1_MATRIX_3X2_F& world, BOOL* out) const { return StrokeContainsPoint(p, w, st, &world, 0.25f, out); }
};

struct ID2D1RectangleGeometry : ID2D1Geometry {
	D2D1_RECT_F rect{};
	void GetRect(D2D1_RECT_F* r) const { if (r) *r = rect; }
	void Emit(cairo_t* cr, bool) const override { cairo_rectangle(cr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top); }
};
struct ID2D1RoundedRectangleGeometry : ID2D1Geometry {
	D2D1_ROUNDED_RECT rr{};
	void GetRoundedRect(D2D1_ROUNDED_RECT* r) const { if (r) *r = rr; }
	void Emit(cairo_t* cr, bool) const override { chrono_linux::RoundedRectPath(cr, rr); }
};
struct ID2D1EllipseGeometry : ID2D1Geometry {
	D2D1_ELLIPSE e{};
	void GetEllipse(D2D1_ELLIPSE* o) const { if (o) *o = e; }
	void Emit(cairo_t* cr, bool) const override { chrono_linux::EllipsePath(cr, e); }
};
struct ID2D1TransformedGeometry : ID2D1Geometry {
	ID2D1Geometry* src = nullptr;
	D2D1_MATRIX_3X2_F m = D2D1::IdentityMatrix();
	~ID2D1TransformedGeometry() override { if (src) src->Release(); }
	bool EvenOdd() const override { return src && src->EvenOdd(); }
	void Emit(cairo_t* cr, bool forFill) const override {
		if (!src) return;
		cairo_matrix_t saved, mine; cairo_get_matrix(cr, &saved);
		chrono_linux::ToCairo(m, &mine);
		cairo_transform(cr, &mine);
		src->Emit(cr, forFill);
		cairo_set_matrix(cr, &saved);
	}
};

struct ID2D1GeometrySink : IUnknown {
	chrono_linux::PathData* path = nullptr;
	D2D1_FILL_MODE* fill = nullptr;
	void SetFillMode(D2D1_FILL_MODE m) { if (fill) *fill = m; }
	void SetSegmentFlags(D2D1_PATH_SEGMENT) {}
	void BeginFigure(D2D1_POINT_2F p, D2D1_FIGURE_BEGIN b) { path->Begin(p.x, p.y, b == D2D1_FIGURE_BEGIN_HOLLOW); }
	void AddLine(D2D1_POINT_2F p) { path->Line(p.x, p.y); }
	void AddLines(const D2D1_POINT_2F* p, UINT32 n) { for (UINT32 i = 0; i < n; ++i) path->Line(p[i].x, p[i].y); }
	void AddBezier(const D2D1_BEZIER_SEGMENT& b) { path->Curve(b.point1.x, b.point1.y, b.point2.x, b.point2.y, b.point3.x, b.point3.y); }
	void AddBezier(const D2D1_BEZIER_SEGMENT* b) { if (b) AddBezier(*b); }
	void AddBeziers(const D2D1_BEZIER_SEGMENT* b, UINT32 n) { for (UINT32 i = 0; i < n; ++i) AddBezier(b[i]); }
	void AddQuadraticBezier(const D2D1_QUADRATIC_BEZIER_SEGMENT& q) { path->Quad(q.point1.x, q.point1.y, q.point2.x, q.point2.y); }
	void AddQuadraticBezier(const D2D1_QUADRATIC_BEZIER_SEGMENT* q) { if (q) AddQuadraticBezier(*q); }
	void AddQuadraticBeziers(const D2D1_QUADRATIC_BEZIER_SEGMENT* q, UINT32 n) { for (UINT32 i = 0; i < n; ++i) AddQuadraticBezier(q[i]); }
	void AddArc(const D2D1_ARC_SEGMENT& a) {
		path->Arc(a.point.x, a.point.y, a.size.width, a.size.height, a.rotationAngle,
		          a.arcSize == D2D1_ARC_SIZE_LARGE, a.sweepDirection == D2D1_SWEEP_DIRECTION_CLOCKWISE);
	}
	void AddArc(const D2D1_ARC_SEGMENT* a) { if (a) AddArc(*a); }
	void EndFigure(D2D1_FIGURE_END e) { if (e == D2D1_FIGURE_END_CLOSED) path->Close(); }
	HRESULT Close() { return S_OK; }
};
typedef ID2D1GeometrySink ID2D1SimplifiedGeometrySink;

struct ID2D1PathGeometry : ID2D1Geometry {
	chrono_linux::PathData path;
	D2D1_FILL_MODE fillMode = D2D1_FILL_MODE_ALTERNATE;
	bool opened = false;
	bool EvenOdd() const override { return fillMode == D2D1_FILL_MODE_ALTERNATE; }
	HRESULT Open(ID2D1GeometrySink** sink) {
		if (!sink) return E_POINTER;
		if (opened) return D2DERR_WRONG_STATE;
		opened = true;
		auto* s = new ID2D1GeometrySink(); s->path = &path; s->fill = &fillMode;
		*sink = s;
		return S_OK;
	}
	HRESULT GetFigureCount(UINT32* n) const { if (n) *n = (UINT32)path.figs.size(); return S_OK; }
	HRESULT GetSegmentCount(UINT32* n) const { if (n) *n = (UINT32)(path.ops.size() - path.figs.size()); return S_OK; }
	void Emit(cairo_t* cr, bool forFill) const override { path.Emit(cr, forFill); }
};
typedef ID2D1PathGeometry ID2D1PathGeometry1;

// --- render target ---------------------------------------------------------------------------
struct ID2D1RenderTarget : ID2D1Resource {
	cairo_surface_t*   surf = nullptr;
	cairo_t*           cr = nullptr;
	FLOAT              dpiX = 96.0f, dpiY = 96.0f;
	D2D1_MATRIX_3X2_F  world = D2D1::IdentityMatrix();
	D2D1_ANTIALIAS_MODE aa = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
	struct Pushed { bool layer; FLOAT opacity; };
	std::vector<Pushed> stack;

	~ID2D1RenderTarget() override { Free(); }
	void Free() {
		if (cr) { cairo_destroy(cr); cr = nullptr; }
		if (surf) { cairo_surface_destroy(surf); surf = nullptr; }
	}
	void Allocate(UINT32 w, UINT32 h) {
		Free();
		surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)(w ? w : 1), (int)(h ? h : 1));
		cr = cairo_create(surf);
	}
	virtual HRESULT Present() { return S_OK; }

	// The world transform, scaled from DIPs to pixels, loaded into Cairo.
	void World() {
		D2D1::Matrix3x2F m = D2D1::Matrix3x2F(world) * D2D1::Matrix3x2F::Scale(dpiX / 96.0f, dpiY / 96.0f);
		chrono_linux::SetMatrix(cr, m);
		cairo_set_antialias(cr, aa == D2D1_ANTIALIAS_MODE_ALIASED ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_DEFAULT);
		cairo_new_path(cr);
	}
	void FillWith(ID2D1Brush* b, bool evenOdd = false) {
		if (!b) { cairo_new_path(cr); return; }
		b->Apply(cr);
		cairo_set_fill_rule(cr, evenOdd ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
		cairo_fill(cr);
	}
	void StrokeWith(ID2D1Brush* b, FLOAT width, ID2D1StrokeStyle* st) {
		if (!b) { cairo_new_path(cr); return; }
		b->Apply(cr);
		cairo_set_line_width(cr, width);
		if (st) st->Apply(cr, width);
		else { cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT); cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER); cairo_set_miter_limit(cr, 10); cairo_set_dash(cr, nullptr, 0, 0); }
		cairo_stroke(cr);
	}

	// --- frame ---
	void BeginDraw() {
		if (!cr) return;
		while (!stack.empty()) { cairo_restore(cr); stack.pop_back(); }
		cairo_reset_clip(cr);
		world = D2D1::IdentityMatrix();
	}
	HRESULT EndDraw(D2D1_TAG* t1 = nullptr, D2D1_TAG* t2 = nullptr) {
		if (t1) *t1 = 0;
		if (t2) *t2 = 0;
		if (!cr) return D2DERR_RECREATE_TARGET;
		while (!stack.empty()) { cairo_restore(cr); stack.pop_back(); }
		cairo_surface_flush(surf);
		return Present();
	}
	HRESULT Flush(D2D1_TAG* t1 = nullptr, D2D1_TAG* t2 = nullptr) { if (t1) *t1 = 0; if (t2) *t2 = 0; return S_OK; }
	void Clear(const D2D1_COLOR_F* c = nullptr) {
		cairo_save(cr);
		cairo_identity_matrix(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		if (c) cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a); else cairo_set_source_rgba(cr, 0, 0, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
	}
	void Clear(const D2D1_COLOR_F& c) { Clear(&c); }

	// --- state ---
	void SetTransform(const D2D1_MATRIX_3X2_F& m) { world = m; }
	void SetTransform(const D2D1_MATRIX_3X2_F* m) { if (m) world = *m; }
	void GetTransform(D2D1_MATRIX_3X2_F* m) const { if (m) *m = world; }
	void SetAntialiasMode(D2D1_ANTIALIAS_MODE m) { aa = m; }
	D2D1_ANTIALIAS_MODE GetAntialiasMode() const { return aa; }
	void SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE) {}
	D2D1_TEXT_ANTIALIAS_MODE GetTextAntialiasMode() const { return D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE; }
	void SetDpi(FLOAT x, FLOAT y) { dpiX = x > 0 ? x : 96.0f; dpiY = y > 0 ? y : 96.0f; }
	void GetDpi(FLOAT* x, FLOAT* y) const { if (x) *x = dpiX; if (y) *y = dpiY; }
	D2D1_SIZE_U GetPixelSize() const { return surf ? D2D1::SizeU((UINT32)cairo_image_surface_get_width(surf), (UINT32)cairo_image_surface_get_height(surf)) : D2D1::SizeU(); }
	D2D1_SIZE_F GetSize() const { D2D1_SIZE_U s = GetPixelSize(); return D2D1::SizeF(s.width * 96.0f / dpiX, s.height * 96.0f / dpiY); }
	D2D1_PIXEL_FORMAT GetPixelFormat() const { return D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED); }
	UINT32 GetMaximumBitmapSize() const { return 16384; }

	// --- clips and layers ---
	void PushAxisAlignedClip(const D2D1_RECT_F& r, D2D1_ANTIALIAS_MODE mode) {
		cairo_save(cr);
		World();
		cairo_set_antialias(cr, mode == D2D1_ANTIALIAS_MODE_ALIASED ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_DEFAULT);
		cairo_rectangle(cr, r.left, r.top, r.right - r.left, r.bottom - r.top);
		cairo_clip(cr);
		stack.push_back({ false, 1.0f });
	}
	void PushAxisAlignedClip(const D2D1_RECT_F* r, D2D1_ANTIALIAS_MODE mode) { if (r) PushAxisAlignedClip(*r, mode); }
	void PopAxisAlignedClip() {
		if (stack.empty() || stack.back().layer) return;
		stack.pop_back();
		cairo_restore(cr);
	}
	void PushLayer(const D2D1_LAYER_PARAMETERS& p, ID2D1Layer*) {
		cairo_save(cr);
		World();
		if (p.contentBounds.left > -FLT_MAX / 2) {
			cairo_rectangle(cr, p.contentBounds.left, p.contentBounds.top,
			                p.contentBounds.right - p.contentBounds.left, p.contentBounds.bottom - p.contentBounds.top);
			cairo_clip(cr);
		}
		if (p.geometricMask) {
			cairo_matrix_t mt; chrono_linux::ToCairo(p.maskTransform, &mt);
			cairo_transform(cr, &mt);
			cairo_set_antialias(cr, p.maskAntialiasMode == D2D1_ANTIALIAS_MODE_ALIASED ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_DEFAULT);
			p.geometricMask->Emit(cr, true);
			cairo_set_fill_rule(cr, p.geometricMask->EvenOdd() ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
			cairo_clip(cr);
		}
		cairo_identity_matrix(cr);
		cairo_push_group(cr);
		stack.push_back({ true, p.opacity });
	}
	void PushLayer(const D2D1_LAYER_PARAMETERS* p, ID2D1Layer* l) { if (p) PushLayer(*p, l); }
	void PopLayer() {
		if (stack.empty() || !stack.back().layer) return;
		FLOAT op = stack.back().opacity;
		stack.pop_back();
		cairo_identity_matrix(cr);
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, op);
		cairo_restore(cr);
	}

	// --- shapes ---
	void DrawLine(D2D1_POINT_2F a, D2D1_POINT_2F b, ID2D1Brush* br, FLOAT w = 1.0f, ID2D1StrokeStyle* st = nullptr) {
		World(); cairo_move_to(cr, a.x, a.y); cairo_line_to(cr, b.x, b.y); StrokeWith(br, w, st);
	}
	void DrawRectangle(const D2D1_RECT_F& r, ID2D1Brush* br, FLOAT w = 1.0f, ID2D1StrokeStyle* st = nullptr) {
		World(); cairo_rectangle(cr, r.left, r.top, r.right - r.left, r.bottom - r.top); StrokeWith(br, w, st);
	}
	void DrawRectangle(const D2D1_RECT_F* r, ID2D1Brush* br, FLOAT w = 1.0f, ID2D1StrokeStyle* st = nullptr) { if (r) DrawRectangle(*r, br, w, st); }
	void FillRectangle(const D2D1_RECT_F& r, ID2D1Brush* br) {
		World(); cairo_rectangle(cr, r.left, r.top, r.right - r.left, r.bottom - r.top); FillWith(br);
	}
	void FillRectangle(const D2D1_RECT_F* r, ID2D1Brush* br) { if (r) FillRectangle(*r, br); }
	void DrawRoundedRectangle(const D2D1_ROUNDED_RECT& r, ID2D1Brush* br, FLOAT w = 1.0f, ID2D1StrokeStyle* st = nullptr) {
		World(); chrono_linux::RoundedRectPath(cr, r); StrokeWith(br, w, st);
	}
	void DrawRoundedRectangle(const D2D1_ROUNDED_RECT* r, ID2D1Brush* br, FLOAT w = 1.0f, ID2D1StrokeStyle* st = nullptr) { if (r) DrawRoundedRectangle(*r, br, w, st); }
	void FillRoundedRectangle(const D2D1_ROUNDED_RECT& r, ID2D1Brush* br) { World(); chrono_linux::RoundedRectPath(cr, r); FillWith(br); }
	void FillRoundedRectangle(const D2D1_ROUNDED_RECT* r, ID2D1Brush* br) { if (r) FillRoundedRectangle(*r, br); }
	void DrawEllipse(const D2D1_ELLIPSE& e, ID2D1Brush* br, FLOAT w = 1.0f, ID2D1StrokeStyle* st = nullptr) { World(); chrono_linux::EllipsePath(cr, e); StrokeWith(br, w, st); }
	void DrawEllipse(const D2D1_ELLIPSE* e, ID2D1Brush* br, FLOAT w = 1.0f, ID2D1StrokeStyle* st = nullptr) { if (e) DrawEllipse(*e, br, w, st); }
	void FillEllipse(const D2D1_ELLIPSE& e, ID2D1Brush* br) { World(); chrono_linux::EllipsePath(cr, e); FillWith(br); }
	void FillEllipse(const D2D1_ELLIPSE* e, ID2D1Brush* br) { if (e) FillEllipse(*e, br); }
	void DrawGeometry(ID2D1Geometry* g, ID2D1Brush* br, FLOAT w = 1.0f, ID2D1StrokeStyle* st = nullptr) {
		if (!g) return;
		World(); g->Emit(cr, false); StrokeWith(br, w, st);
	}
	void FillGeometry(ID2D1Geometry* g, ID2D1Brush* br, ID2D1Brush* = nullptr) {
		if (!g) return;
		World(); g->Emit(cr, true); FillWith(br, g->EvenOdd());
	}

	// --- text ---
	void DrawTextLayout(D2D1_POINT_2F o, IDWriteTextLayout* l, ID2D1Brush* br, D2D1_DRAW_TEXT_OPTIONS opt = D2D1_DRAW_TEXT_OPTIONS_NONE) {
		if (!l || !br) return;
		PangoLayout* pl = l->L();
		cairo_save(cr);
		World();
		if ((opt & D2D1_DRAW_TEXT_OPTIONS_CLIP) && l->maxW < 1e6f && l->maxH < 1e6f) {
			cairo_rectangle(cr, o.x, o.y, l->maxW, l->maxH);
			cairo_clip(cr);
		}
		br->Apply(cr);
		// Per-range brushes (SetDrawingEffect) become Pango foreground attributes for this draw only.
		PangoAttrList* saved = nullptr;
		bool effects = false;
		for (auto& r : l->ranges) if (r.kind == IDWriteTextLayout::Range::Effect && r.effect) effects = true;
		if (effects) {
			saved = pango_layout_get_attributes(pl);
			if (saved) pango_attr_list_ref(saved);
			PangoAttrList* a = saved ? pango_attr_list_copy(saved) : pango_attr_list_new();
			for (auto& r : l->ranges) {
				if (r.kind != IDWriteTextLayout::Range::Effect || !r.effect) continue;
				auto* sb = dynamic_cast<ID2D1SolidColorBrush*>(r.effect);
				if (!sb) continue;
				guint s = (guint)l->ByteOf(r.start), e = (guint)l->ByteOf((UINT64)r.start + r.len > l->text.size() ? (UINT32)l->text.size() : r.start + r.len);
				auto clamp16 = [](float v) { return (guint16)(v <= 0 ? 0 : v >= 1 ? 65535 : v * 65535.0f); };
				PangoAttribute* fg = pango_attr_foreground_new(clamp16(sb->color.r), clamp16(sb->color.g), clamp16(sb->color.b));
				PangoAttribute* fa = pango_attr_foreground_alpha_new(clamp16(sb->color.a * sb->opacity));
				fg->start_index = fa->start_index = s; fg->end_index = fa->end_index = e;
				pango_attr_list_change(a, fg); pango_attr_list_change(a, fa);
			}
			pango_layout_set_attributes(pl, a);
			pango_attr_list_unref(a);
		}
		cairo_move_to(cr, o.x + l->xoff, o.y + l->yoff);
		pango_cairo_show_layout(cr, pl);
		if (effects) {
			pango_layout_set_attributes(pl, saved);
			if (saved) pango_attr_list_unref(saved);
		}
		cairo_restore(cr);
	}
	void DrawText(const WCHAR* s, UINT32 n, IDWriteTextFormat* f, const D2D1_RECT_F& r, ID2D1Brush* br,
	              D2D1_DRAW_TEXT_OPTIONS opt = D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE = DWRITE_MEASURING_MODE_NATURAL) {
		if (!s || !f || !br) return;
		IDWriteTextLayout l;
		l.CopySettings(*f);
		l.maxW = (std::max)(0.0f, r.right - r.left); l.maxH = (std::max)(0.0f, r.bottom - r.top);
		l.SetText(s, n);
		DrawTextLayout(D2D1::Point2F(r.left, r.top), &l, br, opt);
	}
	void DrawText(const WCHAR* s, UINT32 n, IDWriteTextFormat* f, const D2D1_RECT_F* r, ID2D1Brush* br,
	              D2D1_DRAW_TEXT_OPTIONS opt = D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE m = DWRITE_MEASURING_MODE_NATURAL) {
		if (r) DrawText(s, n, f, *r, br, opt, m);
	}

	// --- bitmaps ---
	void DrawBitmap(ID2D1Bitmap* b, const D2D1_RECT_F* dest = nullptr, FLOAT opacity = 1.0f,
	                D2D1_BITMAP_INTERPOLATION_MODE mode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, const D2D1_RECT_F* src = nullptr) {
		if (!b || !b->surf) return;
		D2D1_SIZE_F bs = b->GetSize();
		D2D1_RECT_F s = src ? *src : D2D1::RectF(0, 0, bs.width, bs.height);
		D2D1_RECT_F d = dest ? *dest : D2D1::RectF(0, 0, s.right - s.left, s.bottom - s.top);
		float dw = d.right - d.left, dh = d.bottom - d.top, sw = s.right - s.left, sh = s.bottom - s.top;
		if (dw == 0 || dh == 0 || sw <= 0 || sh <= 0) return;
		cairo_save(cr);
		World();
		cairo_rectangle(cr, d.left, d.top, dw, dh);
		cairo_clip(cr);
		cairo_pattern_t* p = cairo_pattern_create_for_surface(b->surf);
		cairo_matrix_t m;
		cairo_matrix_init_identity(&m);
		cairo_matrix_scale(&m, b->dpiX / 96.0, b->dpiY / 96.0);           // DIPs -> bitmap pixels
		cairo_matrix_translate(&m, s.left, s.top);
		cairo_matrix_scale(&m, sw / dw, sh / dh);
		cairo_matrix_translate(&m, -d.left, -d.top);
		cairo_pattern_set_matrix(p, &m);
		cairo_pattern_set_filter(p, mode == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_GOOD);
		cairo_pattern_set_extend(p, CAIRO_EXTEND_PAD);
		cairo_set_source(cr, p);
		cairo_paint_with_alpha(cr, opacity);
		cairo_pattern_destroy(p);
		cairo_restore(cr);
	}
	void DrawBitmap(ID2D1Bitmap* b, const D2D1_RECT_F& dest, FLOAT opacity = 1.0f,
	                D2D1_BITMAP_INTERPOLATION_MODE mode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, const D2D1_RECT_F* src = nullptr) {
		DrawBitmap(b, &dest, opacity, mode, src);
	}

	// --- resource creation ---
	HRESULT CreateSolidColorBrush(const D2D1_COLOR_F& c, const D2D1_BRUSH_PROPERTIES* props, ID2D1SolidColorBrush** out) {
		if (!out) return E_POINTER;
		auto* b = new ID2D1SolidColorBrush(); b->color = c;
		if (props) { b->opacity = props->opacity; b->transform = props->transform; }
		*out = b; return S_OK;
	}
	HRESULT CreateSolidColorBrush(const D2D1_COLOR_F& c, ID2D1SolidColorBrush** out) { return CreateSolidColorBrush(c, nullptr, out); }
	HRESULT CreateGradientStopCollection(const D2D1_GRADIENT_STOP* stops, UINT32 n, D2D1_GAMMA, D2D1_EXTEND_MODE ext, ID2D1GradientStopCollection** out) {
		if (!out) return E_POINTER;
		auto* c = new ID2D1GradientStopCollection();
		if (stops) c->stops.assign(stops, stops + n);
		std::sort(c->stops.begin(), c->stops.end(), [](const D2D1_GRADIENT_STOP& a, const D2D1_GRADIENT_STOP& b) { return a.position < b.position; });
		c->extend = ext;
		*out = c; return S_OK;
	}
	HRESULT CreateGradientStopCollection(const D2D1_GRADIENT_STOP* stops, UINT32 n, ID2D1GradientStopCollection** out) {
		return CreateGradientStopCollection(stops, n, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, out);
	}
	HRESULT CreateLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& lp, const D2D1_BRUSH_PROPERTIES* bp,
	                                  ID2D1GradientStopCollection* coll, ID2D1LinearGradientBrush** out) {
		if (!out) return E_POINTER;
		auto* b = new ID2D1LinearGradientBrush(); b->start = lp.startPoint; b->end = lp.endPoint;
		b->coll = coll; if (coll) coll->AddRef();
		if (bp) { b->opacity = bp->opacity; b->transform = bp->transform; }
		*out = b; return S_OK;
	}
	HRESULT CreateLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& lp, ID2D1GradientStopCollection* coll, ID2D1LinearGradientBrush** out) {
		return CreateLinearGradientBrush(lp, nullptr, coll, out);
	}
	HRESULT CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& rp, const D2D1_BRUSH_PROPERTIES* bp,
	                                  ID2D1GradientStopCollection* coll, ID2D1RadialGradientBrush** out) {
		if (!out) return E_POINTER;
		auto* b = new ID2D1RadialGradientBrush(); b->center = rp.center; b->offset = rp.gradientOriginOffset;
		b->rx = rp.radiusX; b->ry = rp.radiusY; b->coll = coll; if (coll) coll->AddRef();
		if (bp) { b->opacity = bp->opacity; b->transform = bp->transform; }
		*out = b; return S_OK;
	}
	HRESULT CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& rp, ID2D1GradientStopCollection* coll, ID2D1RadialGradientBrush** out) {
		return CreateRadialGradientBrush(rp, nullptr, coll, out);
	}
	HRESULT CreateLayer(const D2D1_SIZE_F* size, ID2D1Layer** out) {
		if (!out) return E_POINTER;
		auto* l = new ID2D1Layer(); if (size) l->size = *size;
		*out = l; return S_OK;
	}
	HRESULT CreateLayer(ID2D1Layer** out) { return CreateLayer(nullptr, out); }
	HRESULT CreateBitmap(D2D1_SIZE_U size, const void* src, UINT32 pitch, const D2D1_BITMAP_PROPERTIES* props, ID2D1Bitmap** out) {
		if (!out) return E_POINTER;
		auto* b = ID2D1Bitmap::FromPixels(size.width, size.height, src, pitch);
		if (props && props->dpiX > 0) { b->dpiX = props->dpiX; b->dpiY = props->dpiY; }
		*out = b; return S_OK;
	}
	HRESULT CreateBitmap(D2D1_SIZE_U size, const void* src, UINT32 pitch, const D2D1_BITMAP_PROPERTIES& props, ID2D1Bitmap** out) { return CreateBitmap(size, src, pitch, &props, out); }
	HRESULT CreateBitmap(D2D1_SIZE_U size, const D2D1_BITMAP_PROPERTIES& props, ID2D1Bitmap** out) { return CreateBitmap(size, nullptr, 0, &props, out); }
	HRESULT CreateBitmapFromWicBitmap(IWICBitmapSource* src, const D2D1_BITMAP_PROPERTIES* props, ID2D1Bitmap** out) {
		if (!out) return E_POINTER;
		*out = nullptr;
		if (!src || src->pbgra.empty()) return E_INVALIDARG;
		auto* b = ID2D1Bitmap::FromPixels(src->width, src->height, src->pbgra.data(), src->width * 4);
		if (props && props->dpiX > 0) { b->dpiX = props->dpiX; b->dpiY = props->dpiY; }
		*out = b; return S_OK;
	}
	HRESULT CreateBitmapFromWicBitmap(IWICBitmapSource* src, ID2D1Bitmap** out) { return CreateBitmapFromWicBitmap(src, nullptr, out); }
};

// The window's target: draws into its own image, copied to the SDL window on EndDraw.
struct ID2D1HwndRenderTarget : ID2D1RenderTarget {
	HWND hwnd = nullptr;
	HWND GetHwnd() const { return hwnd; }
	D2D1_WINDOW_STATE CheckWindowState() const { return D2D1_WINDOW_STATE_NONE; }
	HRESULT Resize(const D2D1_SIZE_U& s) {
		D2D1_SIZE_U cur = GetPixelSize();
		if (cur.width != s.width || cur.height != s.height) Allocate(s.width, s.height);
		return S_OK;
	}
	HRESULT Resize(const D2D1_SIZE_U* s) { return s ? Resize(*s) : E_INVALIDARG; }
	HRESULT Present() override {
		if (!IsWindow(hwnd) || !hwnd->sdl) return D2DERR_RECREATE_TARGET;
		SDL_Surface* ws = SDL_GetWindowSurface(hwnd->sdl);
		if (!ws) return S_OK;
		int w = (std::min)(ws->w, cairo_image_surface_get_width(surf));
		int h = (std::min)(ws->h, cairo_image_surface_get_height(surf));
		if (ws->w != cairo_image_surface_get_width(surf) || ws->h != cairo_image_surface_get_height(surf)) {
			// The window changed size before WM_SIZE reached us: resize, and paint again soon.
			hwnd->dirty = true;
		}
		SDL_LockSurface(ws);
		SDL_ConvertPixels(w, h, SDL_PIXELFORMAT_ARGB8888, cairo_image_surface_get_data(surf), cairo_image_surface_get_stride(surf),
		                  ws->format->format, ws->pixels, ws->pitch);
		SDL_UnlockSurface(ws);
		SDL_UpdateWindowSurface(hwnd->sdl);
		if (hwnd->frame != surf) {
			if (hwnd->frame) cairo_surface_destroy(hwnd->frame);
			hwnd->frame = cairo_surface_reference(surf);
		}
		Snapshot();
		return S_OK;
	}
	// CHRONOUI_SHOT=out.png: the first frame presented after the message loop
	// armed the shot (CHRONOUI_SHOT_MS, default 1000 ms) goes to a PNG and the
	// app quits. Screenshots without touching the screen, the Linux counterpart
	// of tools/shoot.ps1's PrintWindow.
	void Snapshot() {
		auto& s = chrono_linux::S();
		if (!s.shotArmed) return;
		s.shotArmed = false;
		cairo_surface_write_to_png(surf, getenv("CHRONOUI_SHOT"));
		PostQuitMessage(0);
	}
};

// --- factory -----------------------------------------------------------------------------------
struct ID2D1Factory : IUnknown {
	static ID2D1Factory* Instance() { static ID2D1Factory* f = new ID2D1Factory(); return f; }

	HRESULT CreatePathGeometry(ID2D1PathGeometry** out) { if (!out) return E_POINTER; *out = new ID2D1PathGeometry(); return S_OK; }
	HRESULT CreateRectangleGeometry(const D2D1_RECT_F& r, ID2D1RectangleGeometry** out) {
		if (!out) return E_POINTER; auto* g = new ID2D1RectangleGeometry(); g->rect = r; *out = g; return S_OK;
	}
	HRESULT CreateRectangleGeometry(const D2D1_RECT_F* r, ID2D1RectangleGeometry** out) { return r ? CreateRectangleGeometry(*r, out) : E_INVALIDARG; }
	HRESULT CreateRoundedRectangleGeometry(const D2D1_ROUNDED_RECT& r, ID2D1RoundedRectangleGeometry** out) {
		if (!out) return E_POINTER; auto* g = new ID2D1RoundedRectangleGeometry(); g->rr = r; *out = g; return S_OK;
	}
	HRESULT CreateRoundedRectangleGeometry(const D2D1_ROUNDED_RECT* r, ID2D1RoundedRectangleGeometry** out) { return r ? CreateRoundedRectangleGeometry(*r, out) : E_INVALIDARG; }
	HRESULT CreateEllipseGeometry(const D2D1_ELLIPSE& e, ID2D1EllipseGeometry** out) {
		if (!out) return E_POINTER; auto* g = new ID2D1EllipseGeometry(); g->e = e; *out = g; return S_OK;
	}
	HRESULT CreateEllipseGeometry(const D2D1_ELLIPSE* e, ID2D1EllipseGeometry** out) { return e ? CreateEllipseGeometry(*e, out) : E_INVALIDARG; }
	HRESULT CreateTransformedGeometry(ID2D1Geometry* src, const D2D1_MATRIX_3X2_F& m, ID2D1TransformedGeometry** out) {
		if (!out) return E_POINTER;
		auto* g = new ID2D1TransformedGeometry(); g->src = src; if (src) src->AddRef(); g->m = m;
		*out = g; return S_OK;
	}
	HRESULT CreateTransformedGeometry(ID2D1Geometry* src, const D2D1_MATRIX_3X2_F* m, ID2D1TransformedGeometry** out) {
		return CreateTransformedGeometry(src, m ? *m : D2D1::IdentityMatrix(), out);
	}
	HRESULT CreateStrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES& p, const FLOAT* dashes, UINT32 n, ID2D1StrokeStyle** out) {
		if (!out) return E_POINTER;
		auto* s = new ID2D1StrokeStyle(); s->props = p;
		if (dashes) s->dashes.assign(dashes, dashes + n);
		*out = s; return S_OK;
	}
	HRESULT CreateStrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES* p, const FLOAT* dashes, UINT32 n, ID2D1StrokeStyle** out) {
		return CreateStrokeStyle(p ? *p : D2D1::StrokeStyleProperties(), dashes, n, out);
	}
	HRESULT CreateHwndRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES& rp, const D2D1_HWND_RENDER_TARGET_PROPERTIES& hp, ID2D1HwndRenderTarget** out) {
		if (!out) return E_POINTER;
		*out = nullptr;
		if (!IsWindow(hp.hwnd)) return E_INVALIDARG;
		auto* rt = new ID2D1HwndRenderTarget();
		rt->hwnd = hp.hwnd;
		D2D1_SIZE_U s = hp.pixelSize;
		if (!s.width || !s.height) { RECT rc; GetClientRect(hp.hwnd, &rc); s = D2D1::SizeU((UINT32)rc.right, (UINT32)rc.bottom); }
		rt->Allocate(s.width, s.height);
		if (rp.dpiX > 0) rt->SetDpi(rp.dpiX, rp.dpiY);
		*out = rt; return S_OK;
	}
	HRESULT CreateHwndRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES* rp, const D2D1_HWND_RENDER_TARGET_PROPERTIES* hp, ID2D1HwndRenderTarget** out) {
		return (rp && hp) ? CreateHwndRenderTarget(*rp, *hp, out) : E_INVALIDARG;
	}
	void GetDesktopDpi(FLOAT* x, FLOAT* y) { if (x) *x = 96.0f; if (y) *y = 96.0f; }
	HRESULT ReloadSystemMetrics() { return S_OK; }
};

inline void ID2D1Resource::GetFactory(ID2D1Factory** f) const {
	if (!f) return;
	*f = ID2D1Factory::Instance();
	(*f)->AddRef();
}

template <class T> inline HRESULT D2D1CreateFactory(D2D1_FACTORY_TYPE, T** out) {
	if (!out) return E_POINTER;
	ID2D1Factory* f = ID2D1Factory::Instance();
	f->AddRef();
	*out = f;
	return S_OK;
}

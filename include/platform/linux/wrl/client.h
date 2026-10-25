// Microsoft::WRL::ComPtr for the Linux port: a reference-counting smart
// pointer over IUnknown::AddRef / Release, with the WRL spellings ChronoUI uses.
#pragma once

#include "../chrono_win32.hpp"
#include <utility>

namespace Microsoft { namespace WRL {

	template <class T> class ComPtr {
		T* p = nullptr;
		template <class U> friend class ComPtr;
	public:
		typedef T InterfaceType;
		ComPtr() = default;
		ComPtr(std::nullptr_t) {}
		ComPtr(T* q) : p(q) { if (p) p->AddRef(); }
		ComPtr(const ComPtr& o) : p(o.p) { if (p) p->AddRef(); }
		ComPtr(ComPtr&& o) noexcept : p(o.p) { o.p = nullptr; }
		template <class U> ComPtr(const ComPtr<U>& o) : p(o.p) { if (p) p->AddRef(); }
		~ComPtr() { if (p) p->Release(); }

		ComPtr& operator=(const ComPtr& o) { ComPtr(o).Swap(*this); return *this; }
		ComPtr& operator=(ComPtr&& o) noexcept { ComPtr(std::move(o)).Swap(*this); return *this; }
		ComPtr& operator=(T* q) { ComPtr(q).Swap(*this); return *this; }
		ComPtr& operator=(std::nullptr_t) { Reset(); return *this; }
		void Swap(ComPtr& o) { std::swap(p, o.p); }

		T*  Get() const { return p; }
		T*  operator->() const { return p; }
		T&  operator*() const { return *p; }
		explicit operator bool() const { return p != nullptr; }
		// Like WRL: taking the address releases what was held, ready for an out-parameter.
		T** operator&() { Reset(); return &p; }
		T** GetAddressOf() { return &p; }
		T* const* GetAddressOf() const { return &p; }
		T** ReleaseAndGetAddressOf() { Reset(); return &p; }
		unsigned long Reset() { T* q = p; p = nullptr; return q ? q->Release() : 0; }
		T* Detach() { T* q = p; p = nullptr; return q; }
		void Attach(T* q) { if (p) p->Release(); p = q; }
		HRESULT CopyTo(T** out) const { if (!out) return E_POINTER; *out = p; if (p) p->AddRef(); return S_OK; }
		template <class U> HRESULT As(ComPtr<U>* out) const {
			if (!out) return E_POINTER;
			U* q = dynamic_cast<U*>(p);
			*out = q;   // AddRefs
			return q ? S_OK : E_NOINTERFACE;
		}
		template <class U> HRESULT As(U** out) const {
			U* q = dynamic_cast<U*>(p);
			if (q) q->AddRef();
			*out = q;
			return q ? S_OK : E_NOINTERFACE;
		}
		bool operator==(std::nullptr_t) const { return p == nullptr; }
		bool operator!=(std::nullptr_t) const { return p != nullptr; }
		bool operator==(const ComPtr& o) const { return p == o.p; }
		bool operator!=(const ComPtr& o) const { return p != o.p; }
	};

}} // namespace Microsoft::WRL

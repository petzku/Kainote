/////////////////////////////////////////////////////////////////////////////
// Name:        src/msw/graphicsd2d.cpp
// Purpose:     Implementation of Direct2D Render Context
// Author:      Pana Alexandru <astronothing@gmail.com>
// Created:     2014-05-20
// Copyright:   (c) 2014 wxWidgets development team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifndef wxFALLTHROUGH
#define wxFALLTHROUGH ((void)0)
#endif

#define wxUSE_GRAPHICS_DIRECT2D 1
#if wxUSE_GRAPHICS_DIRECT2D

// Minimum supported client: Windows 8 and Platform Update for Windows 7
#define wxD2D_DEVICE_CONTEXT_SUPPORTED 0

// We load these functions at runtime from the d2d1.dll.
// However, since they are also used inside the d2d1.h header we must provide
// implementations matching the exact declarations. These defines ensures we
// are not violating the ODR rule.
#define D2D1CreateFactory wxD2D1CreateFactory
#define D2D1MakeRotateMatrix wxD2D1MakeRotateMatrix
#define D2D1MakeSkewMatrix wxD2D1MakeSkewMatrix
#define D2D1IsMatrixInvertible wxD2D1IsMatrixInvertible
#define D2D1InvertMatrix wxD2D1InvertMatrix
#if wxD2D_DEVICE_CONTEXT_SUPPORTED
#define D3D11CreateDevice wxD3D11CreateDevice
#endif

// There are clashes between the names of the member fields and parameters
// in the standard d2d1helper.h header resulting in C4458 with VC14,
// so disable this warning for this file as there is no other way to
// avoid it.
#ifdef __VISUALC__
	#pragma warning(push)
	#pragma warning(disable:4458) // declaration of 'xxx' hides class member
#endif


#include "D2DInit.h"

#ifdef __MINGW64_TOOLCHAIN__
#ifndef DWRITE_E_NOFONT
#define DWRITE_E_NOFONT _HRESULT_TYPEDEF_(0x88985002L)
#endif
#endif

#if wxD2D_DEVICE_CONTEXT_SUPPORTED
#include <d3d11.h>
#include <d2d1_1.h>
#include <dxgi1_2.h>
#endif

#ifdef __VISUALC__
	#pragma warning(pop)
#endif

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#include <float.h> // for FLT_MAX, FLT_MIN

#ifndef WX_PRECOMP
	#include "wx/dc.h"
	#include "wx/dcclient.h"
	#include "wx/dcmemory.h"
	#include "wx/image.h"
	#include "wx/module.h"
	#include "wx/window.h"
	#include "wx/msw/private.h"
#endif // !WX_PRECOMP

#include "GraphicsD2D.h"
//#include "wx/graphics.h"
#include "wx/dynlib.h"
#include "wx/msw/private/comptr.h"
#include "wx/private/graphics.h"
#include "wx/stack.h"
#include "wx/sharedptr.h"

// This must be the last header included to only affect the DEFINE_GUID()
// occurrences below but not any GUIDs declared in the standard files included
// above.
#include <initguid.h>

// Generic error message for a failed direct2d operation
#define wxFAILED_HRESULT_MSG(result) \
	wxString::Format("Direct2D failed with HRESULT %x", (result))

// Checks a HRESULT value for success, otherwise displays an error message and
// returns from the enclosing function.
#define wxCHECK_HRESULT_RET(result) \
	wxCHECK_RET(SUCCEEDED(result), wxFAILED_HRESULT_MSG(result))

#define wxCHECK2_HRESULT_RET(result, returnValue)                             \
	wxCHECK2_MSG(SUCCEEDED(result), return returnValue,                       \
		wxFAILED_HRESULT_MSG(result))

// Variation of wxCHECK_HRESULT_RET for functions which must return a pointer
#define wxCHECK_HRESULT_RET_PTR(result) wxCHECK2_HRESULT_RET(result, NULL)

// Checks the precondition of wxManagedResourceHolder::AcquireResource, namely
// that it is bound to a manager.
#define wxCHECK_RESOURCE_HOLDER_PRE()                                         \
	{                                                                         \
	if (IsResourceAcquired()) return;                                         \
	wxCHECK_RET(IsBound(),                                                    \
		"Cannot acquire a native resource without being bound to a manager"); \
	}

// Checks the postcondition of wxManagedResourceHolder::AcquireResource, namely
// that it was successful in acquiring the native resource.
#define wxCHECK_RESOURCE_HOLDER_POST() \
	wxCHECK_RET(m_nativeResource != NULL, "Could not acquire native resource");

inline double wxRadToDeg(double rad) { return (rad * 180.0) / M_PI; }


// Helper class used to check for direct2d availability at runtime and to
// dynamically load the required symbols from d2d1.dll and dwrite.dll
class wxDirect2D
{
public:

	enum wxD2DVersion
	{
		wxD2D_VERSION_1_0,
		wxD2D_VERSION_1_1,
		wxD2D_VERSION_NONE
	};

	static bool Initialize()
	{
		if (!m_initialized)
		{
			m_hasDirect2DSupport = LoadLibraries();
			m_initialized = true;
		}

		return m_hasDirect2DSupport;
	}

	static bool HasDirect2DSupport()
	{
		Initialize();

		return m_hasDirect2DSupport;
	}

	static wxD2DVersion GetDirect2DVersion()
	{
		return m_D2DRuntimeVersion;
	}

private:
	static bool LoadLibraries()
	{
		if ( !m_dllDirect2d.Load(wxT("d2d1.dll"), wxDL_VERBATIM | wxDL_QUIET) )
			return false;

		if ( !m_dllDirectWrite.Load(wxT("dwrite.dll"), wxDL_VERBATIM | wxDL_QUIET) )
			return false;

#if wxD2D_DEVICE_CONTEXT_SUPPORTED
		if (!m_dllDirect3d.Load(wxT("d3d11.dll"), wxDL_VERBATIM | wxDL_QUIET))
			return false;
#endif

		#define wxLOAD_FUNC(dll, name)                    \
		name = (name##_t)dll.RawGetSymbol(#name);         \
			if ( !name )                                  \
			return false;

		wxLOAD_FUNC(m_dllDirect2d, D2D1CreateFactory);
		wxLOAD_FUNC(m_dllDirect2d, D2D1MakeRotateMatrix);
		wxLOAD_FUNC(m_dllDirect2d, D2D1MakeSkewMatrix);
		wxLOAD_FUNC(m_dllDirect2d, D2D1IsMatrixInvertible);
		wxLOAD_FUNC(m_dllDirect2d, D2D1InvertMatrix);
		wxLOAD_FUNC(m_dllDirectWrite, DWriteCreateFactory);

#if wxD2D_DEVICE_CONTEXT_SUPPORTED
		wxLOAD_FUNC(m_dllDirect3d, D3D11CreateDevice);
#endif

		m_D2DRuntimeVersion = wxD2D_VERSION_1_0;

		return true;
	}

public:
	typedef HRESULT (WINAPI *D2D1CreateFactory_t)(D2D1_FACTORY_TYPE, REFIID, CONST D2D1_FACTORY_OPTIONS*, void**);
	static D2D1CreateFactory_t D2D1CreateFactory;

	typedef void (WINAPI *D2D1MakeRotateMatrix_t)(FLOAT, D2D1_POINT_2F, D2D1_MATRIX_3X2_F*);
	static D2D1MakeRotateMatrix_t D2D1MakeRotateMatrix;

	typedef void (WINAPI *D2D1MakeSkewMatrix_t)(FLOAT, FLOAT, D2D1_POINT_2F, D2D1_MATRIX_3X2_F*);
	static D2D1MakeSkewMatrix_t D2D1MakeSkewMatrix;

	typedef BOOL (WINAPI *D2D1IsMatrixInvertible_t)(const D2D1_MATRIX_3X2_F*);
	static D2D1IsMatrixInvertible_t D2D1IsMatrixInvertible;

	typedef BOOL (WINAPI *D2D1InvertMatrix_t)(D2D1_MATRIX_3X2_F*);
	static D2D1InvertMatrix_t D2D1InvertMatrix;

	typedef HRESULT (WINAPI *DWriteCreateFactory_t)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
	static DWriteCreateFactory_t DWriteCreateFactory;

#if wxD2D_DEVICE_CONTEXT_SUPPORTED
	typedef HRESULT (WINAPI *D3D11CreateDevice_t)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, CONST D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
	static D3D11CreateDevice_t D3D11CreateDevice;
#endif

private:
	static bool m_initialized;
	static bool m_hasDirect2DSupport;
	static wxD2DVersion m_D2DRuntimeVersion;

	static wxDynamicLibrary m_dllDirect2d;
	static wxDynamicLibrary m_dllDirectWrite;
#if wxD2D_DEVICE_CONTEXT_SUPPORTED
	static wxDynamicLibrary m_dllDirect3d;
#endif
};

// define the members
bool wxDirect2D::m_initialized = false;
bool wxDirect2D::m_hasDirect2DSupport = false;
wxDirect2D::wxD2DVersion wxDirect2D::m_D2DRuntimeVersion = wxD2D_VERSION_NONE;

wxDynamicLibrary wxDirect2D::m_dllDirect2d;
wxDynamicLibrary wxDirect2D::m_dllDirectWrite;
#if wxD2D_DEVICE_CONTEXT_SUPPORTED
wxDynamicLibrary wxDirect2D::m_dllDirect3d;
#endif

// define the (not yet imported) functions
wxDirect2D::D2D1CreateFactory_t wxDirect2D::D2D1CreateFactory = NULL;
wxDirect2D::D2D1MakeRotateMatrix_t wxDirect2D::D2D1MakeRotateMatrix = NULL;
wxDirect2D::D2D1MakeSkewMatrix_t wxDirect2D::D2D1MakeSkewMatrix = NULL;
wxDirect2D::D2D1IsMatrixInvertible_t wxDirect2D::D2D1IsMatrixInvertible = NULL;
wxDirect2D::D2D1InvertMatrix_t wxDirect2D::D2D1InvertMatrix = NULL;
wxDirect2D::DWriteCreateFactory_t wxDirect2D::DWriteCreateFactory = NULL;

#if wxD2D_DEVICE_CONTEXT_SUPPORTED
wxDirect2D::D3D11CreateDevice_t wxDirect2D::D3D11CreateDevice = NULL;
#endif

// define the interface GUIDs
DEFINE_GUID(wxIID_IWICImagingFactory,
			0xec5ec8a9, 0xc395, 0x4314, 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70);

DEFINE_GUID(wxIID_ID2D1Factory,
			0x06152247, 0x6f50, 0x465a, 0x92, 0x45, 0x11, 0x8b, 0xfd, 0x3b, 0x60, 0x07);

DEFINE_GUID(wxIID_IDWriteFactory,
			0xb859ee5a, 0xd838, 0x4b5b, 0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48);

DEFINE_GUID(wxIID_IWICBitmapSource,
			0x00000120, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);

DEFINE_GUID(GUID_WICPixelFormat32bppPBGRA,
			0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10);

DEFINE_GUID(GUID_WICPixelFormat32bppBGR,
			0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0e);

#if wxD2D_DEVICE_CONTEXT_SUPPORTED
DEFINE_GUID(IID_IDXGIDevice,
			0x54ec77fa, 0x1377, 0x44e6, 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c);
#endif

#ifndef CLSID_WICImagingFactory
DEFINE_GUID(CLSID_WICImagingFactory,
			0xcacaf262, 0x9370, 0x4615, 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0xa);
#endif

// Implementation of the Direct2D functions
HRESULT WINAPI wxD2D1CreateFactory(
	D2D1_FACTORY_TYPE factoryType,
	REFIID riid,
	CONST D2D1_FACTORY_OPTIONS *pFactoryOptions,
	void **ppIFactory)
{
	if (!wxDirect2D::Initialize())
		return S_FALSE;

	return wxDirect2D::D2D1CreateFactory(
		factoryType,
		riid,
		pFactoryOptions,
		ppIFactory);
}

void WINAPI wxD2D1MakeRotateMatrix(
	FLOAT angle,
	D2D1_POINT_2F center,
	D2D1_MATRIX_3X2_F *matrix)
{
	if (!wxDirect2D::Initialize())
		return;

	wxDirect2D::D2D1MakeRotateMatrix(angle, center, matrix);
}

void WINAPI wxD2D1MakeSkewMatrix(
	FLOAT angleX,
	FLOAT angleY,
	D2D1_POINT_2F center,
	D2D1_MATRIX_3X2_F *matrix)
{
	if (!wxDirect2D::Initialize())
		return;

	wxDirect2D::D2D1MakeSkewMatrix(angleX, angleY, center, matrix);
}

BOOL WINAPI wxD2D1IsMatrixInvertible(
	const D2D1_MATRIX_3X2_F *matrix)
{
	if (!wxDirect2D::Initialize())
		return FALSE;

	return wxDirect2D::D2D1IsMatrixInvertible(matrix);
}

BOOL WINAPI wxD2D1InvertMatrix(
	D2D1_MATRIX_3X2_F *matrix)
{
	if (!wxDirect2D::Initialize())
		return FALSE;

	return wxDirect2D::D2D1InvertMatrix(matrix);
}

#if wxD2D_DEVICE_CONTEXT_SUPPORTED
HRESULT WINAPI wxD3D11CreateDevice(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	CONST D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	if (!wxDirect2D::Initialize())
		return S_FALSE;

	return wxDirect2D::D3D11CreateDevice(
		pAdapter,
		DriverType,
		Software,
		Flags,
		pFeatureLevels,
		FeatureLevels,
		SDKVersion,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);
}
#endif

static IWICImagingFactory* gs_WICImagingFactory = NULL;

IWICImagingFactory* wxWICImagingFactory()
{
	if (gs_WICImagingFactory == NULL) {
		HRESULT hr = CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			wxIID_IWICImagingFactory,
			(LPVOID*)&gs_WICImagingFactory);
		wxCHECK_HRESULT_RET_PTR(hr);
	}
	return gs_WICImagingFactory;
}

static ID2D1Factory* gs_ID2D1Factory = NULL;

ID2D1Factory* wxD2D1Factory()
{
	if (!wxDirect2D::Initialize())
		return NULL;

	if (gs_ID2D1Factory == NULL)
	{
		D2D1_FACTORY_OPTIONS factoryOptions = {D2D1_DEBUG_LEVEL_NONE};

		// According to
		// https://msdn.microsoft.com/en-us/library/windows/desktop/ee794287(v=vs.85).aspx
		// the Direct2D Debug Layer is only available starting with Windows 8
		// and Visual Studio 2012.
#if defined(__WXDEBUG__) && defined(__VISUALC__) && wxCHECK_VISUALC_VERSION(11)
		if (wxGetWinVersion() >= 0x602)
		{
			factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_WARNING;
		}
#endif  //__WXDEBUG__

		HRESULT hr = wxDirect2D::D2D1CreateFactory(
			D2D1_FACTORY_TYPE_SINGLE_THREADED,
			wxIID_ID2D1Factory,
			&factoryOptions,
			reinterpret_cast<void**>(&gs_ID2D1Factory)
			);
		wxCHECK_HRESULT_RET_PTR(hr);
	}
	return gs_ID2D1Factory;
}

static IDWriteFactory* gs_IDWriteFactory = NULL;

IDWriteFactory* wxDWriteFactory()
{
	if (!wxDirect2D::Initialize())
		return NULL;

	if (gs_IDWriteFactory == NULL)
	{
		wxDirect2D::DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			wxIID_IDWriteFactory,
			reinterpret_cast<IUnknown**>(&gs_IDWriteFactory)
			);
	}
	return gs_IDWriteFactory;
}

//extern WXDLLIMPEXP_DATA_CORE(wxGraphicsPen) wxNullGraphicsPen;
//extern WXDLLIMPEXP_DATA_CORE(wxGraphicsBrush) wxNullGraphicsBrush;

// We use the notion of a context supplier because the context
// needed to create Direct2D resources (namely the RenderTarget)
// is itself device-dependent and might change during the lifetime
// of the resources which were created from it.
template <typename C>
class wxContextSupplier
{
public:
	typedef C ContextType;

	virtual C GetContext() = 0;
};

typedef wxContextSupplier<ID2D1RenderTarget*> wxD2DContextSupplier;

// A resource holder manages a generic resource by acquiring
// and releasing it on demand.
class wxResourceHolder
{
public:
	// Acquires the managed resource if necessary (not already acquired)
	virtual void AcquireResource() = 0;

	// Releases the managed resource
	virtual void ReleaseResource() = 0;

	// Checks if the resources was previously acquired
	virtual bool IsResourceAcquired() = 0;

	// Returns the managed resource or NULL if the resources
	// was not previously acquired
	virtual void* GetResource() = 0;

	virtual ~wxResourceHolder() {}
};

class wxD2DResourceManager;

class wxD2DManagedObject
{
public:
	virtual void Bind(wxD2DResourceManager* manager) = 0;
	virtual void UnBind() = 0;
	virtual bool IsBound() = 0;
	virtual wxD2DResourceManager* GetManager() = 0;

	virtual ~wxD2DManagedObject() {}
};

class wxManagedResourceHolder : public wxResourceHolder, public wxD2DManagedObject
{
public:
	virtual ~wxManagedResourceHolder() {}
};

// A Direct2D resource manager handles the device-dependent
// resource holders attached to it by requesting them to
// release their resources when the API invalidates.
// NOTE: We're using a list because we expect to have multiple
// insertions but very rarely a traversal (if ever).
WX_DECLARE_LIST(wxManagedResourceHolder, wxManagedResourceListType);
#include <wx/listimpl.cpp>
WX_DEFINE_LIST(wxManagedResourceListType);

class wxD2DResourceManager: public wxD2DContextSupplier
{
public:
	void RegisterResourceHolder(wxManagedResourceHolder* resourceHolder)
	{
		m_resources.push_back(resourceHolder);
	}

	void UnregisterResourceHolder(wxManagedResourceHolder* resourceHolder)
	{
		m_resources.remove(resourceHolder);
	}

	void ReleaseResources()
	{
		wxManagedResourceListType::iterator it;
		for (it = m_resources.begin(); it != m_resources.end(); ++it)
		{
			(*it)->ReleaseResource();
		}

		// Check that all resources were released
		for (it = m_resources.begin(); it != m_resources.end(); ++it)
		{
			wxCHECK_RET(!(*it)->IsResourceAcquired(), "One or more device-dependent resources failed to release");
		}
	}

	virtual ~wxD2DResourceManager()
	{
		while (!m_resources.empty())
		{
			m_resources.front()->ReleaseResource();
			m_resources.front()->UnBind();
		}
	}

private:
	wxManagedResourceListType m_resources;
};

// A Direct2D resource holder manages device dependent resources
// by storing any information necessary for acquiring the resource
// and releasing the resource when the API invalidates it.
template<typename T>
class wxD2DResourceHolder: public wxManagedResourceHolder
{
public:
	wxD2DResourceHolder() : m_resourceManager(NULL)
	{
	}

	virtual ~wxD2DResourceHolder()
	{
		UnBind();
		ReleaseResource();
	}

	bool IsResourceAcquired() override
	{
		return m_nativeResource != NULL;
	}

	void* GetResource() override
	{
		return GetD2DResource();
	}

	wxCOMPtr<T>& GetD2DResource()
	{
		if (!IsResourceAcquired())
		{
			AcquireResource();
		}

		return m_nativeResource;
	}

	void AcquireResource() override
	{
		wxCHECK_RESOURCE_HOLDER_PRE();

		DoAcquireResource();

		wxCHECK_RESOURCE_HOLDER_POST();
	}

	void ReleaseResource() override
	{
		m_nativeResource.reset();
	}

	wxD2DContextSupplier::ContextType GetContext()
	{
		return m_resourceManager->GetContext();
	}

	void Bind(wxD2DResourceManager* manager) override
	{
		if (IsBound())
			return;

		m_resourceManager = manager;
		m_resourceManager->RegisterResourceHolder(this);
	}

	void UnBind() override
	{
		if (!IsBound())
			return;

		m_resourceManager->UnregisterResourceHolder(this);
		m_resourceManager = NULL;
	}

	bool IsBound() override
	{
		return m_resourceManager != NULL;
	}

	wxD2DResourceManager* GetManager() override
	{
		return m_resourceManager;
	}

protected:
	virtual void DoAcquireResource() = 0;

private:
	wxD2DResourceManager* m_resourceManager;

protected:
	wxCOMPtr<T> m_nativeResource;
};

// Used as super class for graphics data objects
// to forward the bindings to their internal resource holder.
class wxD2DManagedGraphicsData : public wxD2DManagedObject
{
public:
	void Bind(wxD2DResourceManager* manager) override
	{
		GetManagedObject()->Bind(manager);
	}

	void UnBind() override
	{
		GetManagedObject()->UnBind();
	}

	bool IsBound() override
	{
		return GetManagedObject()->IsBound();
	}

	wxD2DResourceManager* GetManager() override
	{
		return GetManagedObject()->GetManager();
	}

	virtual wxD2DManagedObject* GetManagedObject() = 0;

	~wxD2DManagedGraphicsData() {}
};

D2D1_CAP_STYLE wxD2DConvertPenCap(wxPenCap cap)
{
	switch (cap)
	{
	case wxCAP_ROUND:
		return D2D1_CAP_STYLE_ROUND;
	case wxCAP_PROJECTING:
		return D2D1_CAP_STYLE_SQUARE;
	case wxCAP_BUTT:
		return D2D1_CAP_STYLE_FLAT;
	case wxCAP_INVALID:
		return D2D1_CAP_STYLE_FLAT;
	}

	wxFAIL_MSG("unknown pen cap");
	return D2D1_CAP_STYLE_FLAT;
}

D2D1_LINE_JOIN wxD2DConvertPenJoin(wxPenJoin join)
{
	switch (join)
	{
	case wxJOIN_BEVEL:
		return D2D1_LINE_JOIN_BEVEL;
	case wxJOIN_MITER:
		return D2D1_LINE_JOIN_MITER;
	case wxJOIN_ROUND:
		return D2D1_LINE_JOIN_ROUND;
	case wxJOIN_INVALID:
		return D2D1_LINE_JOIN_MITER;
	}

	wxFAIL_MSG("unknown pen join");
	return D2D1_LINE_JOIN_MITER;
}

D2D1_DASH_STYLE wxD2DConvertPenStyle(wxPenStyle dashStyle)
{
	switch (dashStyle)
	{
	case wxPENSTYLE_SOLID:
		return D2D1_DASH_STYLE_SOLID;
	case wxPENSTYLE_DOT:
		return D2D1_DASH_STYLE_DOT;
	case wxPENSTYLE_LONG_DASH:
		return D2D1_DASH_STYLE_DASH;
	case wxPENSTYLE_SHORT_DASH:
		return D2D1_DASH_STYLE_DASH;
	case wxPENSTYLE_DOT_DASH:
		return D2D1_DASH_STYLE_DASH_DOT;
	case wxPENSTYLE_USER_DASH:
		return D2D1_DASH_STYLE_CUSTOM;

	// NB: These styles cannot be converted to a D2D1_DASH_STYLE
	// and must be handled separately.
	case wxPENSTYLE_TRANSPARENT:
		wxFALLTHROUGH;
	case wxPENSTYLE_INVALID:
		wxFALLTHROUGH;
	case wxPENSTYLE_STIPPLE_MASK_OPAQUE:
		wxFALLTHROUGH;
	case wxPENSTYLE_STIPPLE_MASK:
		wxFALLTHROUGH;
	case wxPENSTYLE_STIPPLE:
		wxFALLTHROUGH;
	case wxPENSTYLE_BDIAGONAL_HATCH:
		wxFALLTHROUGH;
	case wxPENSTYLE_CROSSDIAG_HATCH:
		wxFALLTHROUGH;
	case wxPENSTYLE_FDIAGONAL_HATCH:
		wxFALLTHROUGH;
	case wxPENSTYLE_CROSS_HATCH:
		wxFALLTHROUGH;
	case wxPENSTYLE_HORIZONTAL_HATCH:
		wxFALLTHROUGH;
	case wxPENSTYLE_VERTICAL_HATCH:
		return D2D1_DASH_STYLE_SOLID;
	}

	wxFAIL_MSG("unknown pen style");
	return D2D1_DASH_STYLE_SOLID;
}

D2D1_COLOR_F wxD2DConvertColour(wxColour colour)
{
	return D2D1::ColorF(
		colour.Red() / 255.0f,
		colour.Green() / 255.0f,
		colour.Blue() / 255.0f,
		colour.Alpha() / 255.0f);
}

D2D1_ANTIALIAS_MODE wxD2DConvertAntialiasMode(wxAntialiasMode antialiasMode)
{
	switch (antialiasMode)
	{
	case wxANTIALIAS_NONE:
		return D2D1_ANTIALIAS_MODE_ALIASED;
	case wxANTIALIAS_DEFAULT:
		return D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
	}

	wxFAIL_MSG("unknown antialias mode");
	return D2D1_ANTIALIAS_MODE_ALIASED;
}

#if wxD2D_DEVICE_CONTEXT_SUPPORTED
bool wxD2DCompositionModeSupported(wxCompositionMode compositionMode)
{
	if (compositionMode == wxCOMPOSITION_CLEAR || compositionMode == wxCOMPOSITION_INVALID)
	{
		return false;
	}

	return true;
}

D2D1_COMPOSITE_MODE wxD2DConvertCompositionMode(wxCompositionMode compositionMode)
{
	switch (compositionMode)
	{
	case wxCOMPOSITION_SOURCE:
		return D2D1_COMPOSITE_MODE_SOURCE_COPY;
	case wxCOMPOSITION_OVER:
		return D2D1_COMPOSITE_MODE_SOURCE_OVER;
	case wxCOMPOSITION_IN:
		return D2D1_COMPOSITE_MODE_SOURCE_IN;
	case wxCOMPOSITION_OUT:
		return D2D1_COMPOSITE_MODE_SOURCE_OUT;
	case wxCOMPOSITION_ATOP:
		return D2D1_COMPOSITE_MODE_SOURCE_ATOP;
	case wxCOMPOSITION_DEST_OVER:
		return D2D1_COMPOSITE_MODE_DESTINATION_OVER;
	case wxCOMPOSITION_DEST_IN:
		return D2D1_COMPOSITE_MODE_DESTINATION_IN;
	case wxCOMPOSITION_DEST_OUT:
		return D2D1_COMPOSITE_MODE_DESTINATION_OUT;
	case wxCOMPOSITION_DEST_ATOP:
		return D2D1_COMPOSITE_MODE_DESTINATION_ATOP;
	case wxCOMPOSITION_XOR:
		return D2D1_COMPOSITE_MODE_XOR;
	case wxCOMPOSITION_ADD:
		return D2D1_COMPOSITE_MODE_PLUS;

	// unsupported composition modes
	case wxCOMPOSITION_DEST:
		wxFALLTHROUGH;
	case wxCOMPOSITION_CLEAR:
		wxFALLTHROUGH;
	case wxCOMPOSITION_INVALID:
		return D2D1_COMPOSITE_MODE_SOURCE_COPY;
	}

	wxFAIL_MSG("unknown composition mode");
	return D2D1_COMPOSITE_MODE_SOURCE_COPY;
}
#endif // wxD2D_DEVICE_CONTEXT_SUPPORTED

// Direct2D 1.1 introduces a new enum for specifying the interpolation quality
// which is only used with the ID2D1DeviceContext::DrawImage method.
#if wxD2D_DEVICE_CONTEXT_SUPPORTED
D2D1_INTERPOLATION_MODE wxD2DConvertInterpolationMode(wxInterpolationQuality interpolationQuality)
{
	switch (interpolationQuality)
	{
	case wxINTERPOLATION_DEFAULT:
		wxFALLTHROUGH;
	case wxINTERPOLATION_NONE:
		wxFALLTHROUGH;
	case wxINTERPOLATION_FAST:
		return D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
	case wxINTERPOLATION_GOOD:
		return D2D1_INTERPOLATION_MODE_LINEAR;
	case wxINTERPOLATION_BEST:
		return D2D1_INTERPOLATION_MODE_CUBIC;
	}

	wxFAIL_MSG("unknown interpolation quality");
	return D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
}
#endif // wxD2D_DEVICE_CONTEXT_SUPPORTED

D2D1_BITMAP_INTERPOLATION_MODE wxD2DConvertBitmapInterpolationMode(wxInterpolationQuality interpolationQuality)
{
	switch (interpolationQuality)
	{
	case wxINTERPOLATION_DEFAULT:
		wxFALLTHROUGH;
	case wxINTERPOLATION_NONE:
		wxFALLTHROUGH;
	case wxINTERPOLATION_FAST:
		wxFALLTHROUGH;
	case wxINTERPOLATION_GOOD:
		return D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
	case wxINTERPOLATION_BEST:
		return D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
	}

	wxFAIL_MSG("unknown interpolation quality");
	return D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
}

D2D1_RECT_F wxD2DConvertRect(const wxRect& rect)
{
	return D2D1::RectF(rect.GetLeft(), rect.GetTop(), rect.GetRight(), rect.GetBottom());
}

wxCOMPtr<ID2D1Geometry> wxD2DConvertRegionToGeometry(ID2D1Factory* direct2dFactory, const wxRegion& region)
{
	// Build the array of geometries
	HRESULT hr;
	int i;
	ID2D1Geometry** geometries;
	int rectCount;
	if ( region.IsEmpty() )
	{
		// Empty region is skipped by iterator
		// so we have to create it in a special way.
		rectCount = 1;
		geometries = new ID2D1Geometry*[rectCount];

		geometries[0] = NULL;
		hr = direct2dFactory->CreateRectangleGeometry(
						D2D1::RectF(0.0F, 0.0F, 0.0F, 0.0F),
						(ID2D1RectangleGeometry**)(&geometries[0]));
		wxFAILED_HRESULT_MSG(hr);
	}
	else
	{
		// Count the number of rectangles which compose the region
		wxRegionIterator regionIterator(region);
		rectCount = 0;
		while(regionIterator++)
			rectCount++;

		geometries = new ID2D1Geometry*[rectCount];
		regionIterator.Reset(region);

		i = 0;
		while(regionIterator)
		{
			geometries[i] = NULL;

			wxRect rect = regionIterator.GetRect();
			rect.SetWidth(rect.GetWidth() + 1);
			rect.SetHeight(rect.GetHeight() + 1);

			hr = direct2dFactory->CreateRectangleGeometry(
				wxD2DConvertRect(rect),
				(ID2D1RectangleGeometry**)(&geometries[i]));
			wxFAILED_HRESULT_MSG(hr);

			i++;
			++regionIterator;
		}
	}

	// Create a geometry group to hold all the rectangles
	wxCOMPtr<ID2D1GeometryGroup> resultGeometry;
	hr = direct2dFactory->CreateGeometryGroup(
		D2D1_FILL_MODE_WINDING,
		geometries,
		rectCount,
		&resultGeometry);
	wxFAILED_HRESULT_MSG(hr);

	// Cleanup temporaries
	for (i = 0; i < rectCount; ++i)
	{
		geometries[i]->Release();
	}

	delete[] geometries;

	return wxCOMPtr<ID2D1Geometry>(resultGeometry);
}


bool operator==(const D2D1::Matrix3x2F& lhs, const D2D1::Matrix3x2F& rhs)
{
	return
		lhs._11 == rhs._11 && lhs._12 == rhs._12 &&
		lhs._21 == rhs._21 && lhs._22 == rhs._22 &&
		lhs._31 == rhs._31 && lhs._32 == rhs._32;
}

//-----------------------------------------------------------------------------
// wxD2DMatrixData declaration
//-----------------------------------------------------------------------------

class wxD2DMatrixData : public GraphicsMatrixData
{
public:
	wxD2DMatrixData(wxD2DRenderer* renderer);
	wxD2DMatrixData(wxD2DRenderer* renderer, const D2D1::Matrix3x2F& matrix);

	//virtual GraphicsObjectRefData* Clone() const override;

	void Concat(const GraphicsMatrixData* t) override;

	void Set(wxDouble a = 1.0, wxDouble b = 0.0, wxDouble c = 0.0, wxDouble d = 1.0,
		wxDouble tx = 0.0, wxDouble ty = 0.0) override;

	void Get(wxDouble* a = NULL, wxDouble* b = NULL,  wxDouble* c = NULL,
		wxDouble* d = NULL, wxDouble* tx = NULL, wxDouble* ty = NULL) const override;

	void Invert() override;

	bool IsEqual(const GraphicsMatrixData* t) const override;

	bool IsIdentity() const override;

	void Translate(wxDouble dx, wxDouble dy) override;

	void Scale(wxDouble xScale, wxDouble yScale) override;

	void Rotate(wxDouble angle) override;

	void TransformPoint(wxDouble* x, wxDouble* y) const override;

	void TransformDistance(wxDouble* dx, wxDouble* dy) const override;

	void* GetNativeMatrix() const override;

	D2D1::Matrix3x2F GetMatrix3x2F() const;

private:
	D2D1::Matrix3x2F m_matrix;
};

//-----------------------------------------------------------------------------
// wxD2DMatrixData implementation
//-----------------------------------------------------------------------------

wxD2DMatrixData::wxD2DMatrixData(wxD2DRenderer* renderer) : GraphicsMatrixData(renderer)
{
	m_matrix = D2D1::Matrix3x2F::Identity();
}

wxD2DMatrixData::wxD2DMatrixData(wxD2DRenderer* renderer, const D2D1::Matrix3x2F& matrix) :
	GraphicsMatrixData(renderer), m_matrix(matrix)
{
}

//GraphicsObjectRefData* wxD2DMatrixData::Clone() const
//{
//    return new wxD2DMatrixData(GetRenderer(), m_matrix);
//}

void wxD2DMatrixData::Concat(const GraphicsMatrixData* t)
{
	// Elements of resulting matrix are modified in-place in SetProduct()
	// so multiplied matrices cannot be the instances of the resulting matrix.
	// Note that parameter matrix (t) is the multiplicand.
	const D2D1::Matrix3x2F m1(static_cast<const wxD2DMatrixData*>(t)->m_matrix);
	const D2D1::Matrix3x2F m2(m_matrix);
	m_matrix.SetProduct(m1, m2);
}

void wxD2DMatrixData::Set(wxDouble a, wxDouble b, wxDouble c, wxDouble d, wxDouble tx, wxDouble ty)
{
	m_matrix._11 = a;
	m_matrix._12 = b;
	m_matrix._21 = c;
	m_matrix._22 = d;
	m_matrix._31 = tx;
	m_matrix._32 = ty;
}

void wxD2DMatrixData::Get(wxDouble* a, wxDouble* b,  wxDouble* c, wxDouble* d, wxDouble* tx, wxDouble* ty) const
{
	*a = m_matrix._11;
	*b = m_matrix._12;
	*c = m_matrix._21;
	*d = m_matrix._22;
	*tx = m_matrix._31;
	*ty = m_matrix._32;
}

void wxD2DMatrixData::Invert()
{
	m_matrix.Invert();
}

bool wxD2DMatrixData::IsEqual(const GraphicsMatrixData* t) const
{
	return m_matrix == static_cast<const wxD2DMatrixData*>(t)->m_matrix;
}

bool wxD2DMatrixData::IsIdentity() const
{
	return m_matrix.IsIdentity();
}

void wxD2DMatrixData::Translate(wxDouble dx, wxDouble dy)
{
	m_matrix = D2D1::Matrix3x2F::Translation(dx, dy) * m_matrix;
}

void wxD2DMatrixData::Scale(wxDouble xScale, wxDouble yScale)
{
	m_matrix = D2D1::Matrix3x2F::Scale(xScale, yScale) * m_matrix;
}

void wxD2DMatrixData::Rotate(wxDouble angle)
{
	m_matrix = D2D1::Matrix3x2F::Rotation(wxRadToDeg(angle)) * m_matrix;
}

void wxD2DMatrixData::TransformPoint(wxDouble* x, wxDouble* y) const
{
	D2D1_POINT_2F result = m_matrix.TransformPoint(D2D1::Point2F(*x, *y));
	*x = result.x;
	*y = result.y;
}

void wxD2DMatrixData::TransformDistance(wxDouble* dx, wxDouble* dy) const
{
	D2D1::Matrix3x2F noTranslationMatrix = m_matrix;
	noTranslationMatrix._31 = 0;
	noTranslationMatrix._32 = 0;
	D2D1_POINT_2F result = noTranslationMatrix.TransformPoint(D2D1::Point2F(*dx, *dy));
	*dx = result.x;
	*dy = result.y;
}

void* wxD2DMatrixData::GetNativeMatrix() const
{
	return (void*)&m_matrix;
}

D2D1::Matrix3x2F wxD2DMatrixData::GetMatrix3x2F() const
{
	return m_matrix;
}

//const wxD2DMatrixData* wxGetD2DMatrixData(const wxGraphicsMatrix& matrix)
//{
//    return static_cast<const wxD2DMatrixData*>(matrix.GetMatrixData());
//}

//-----------------------------------------------------------------------------
// wxD2DPathData declaration
//-----------------------------------------------------------------------------

bool operator==(const D2D1_POINT_2F& lhs, const D2D1_POINT_2F& rhs)
{
	return lhs.x == rhs.x && lhs.y == rhs.y;
}

class wxD2DPathData : public GraphicsPathData
{
public :

	// ID2D1PathGeometry objects are device-independent resources created
	// from a ID2D1Factory. This means we can safely create the resource outside
	// (the wxD2DRenderer handles this) and store it here since it never gets
	// thrown away by the GPU.
	wxD2DPathData(wxD2DRenderer* renderer, ID2D1Factory* d2dFactory);

	~wxD2DPathData();

	void SetFillMode(D2D1_FILL_MODE fillMode)
	{
		m_fillMode = fillMode;
	}

	D2D1_FILL_MODE GetFillMode() const
	{
		return m_fillMode;
	}

	ID2D1PathGeometry* GetPathGeometry();

	// This closes the geometry sink, ensuring all the figures are stored inside
	// the ID2D1PathGeometry. Calling this method is required before any draw operation
	// involving a path.
	void Flush();

	//GraphicsObjectRefData* Clone() const override;

	// begins a new subpath at (x,y)
	void MoveToPoint(wxDouble x, wxDouble y) override;

	// adds a straight line from the current point to (x,y)
	void AddLineToPoint(wxDouble x, wxDouble y) override;

	// adds a cubic Bezier curve from the current point, using two control points and an end point
	void AddCurveToPoint(wxDouble cx1, wxDouble cy1, wxDouble cx2, wxDouble cy2, wxDouble x, wxDouble y) override;

	// adds an arc of a circle centering at (x,y) with radius (r) from startAngle to endAngle
	void AddArc(wxDouble x, wxDouble y, wxDouble r, wxDouble startAngle, wxDouble endAngle, bool clockwise) override;

	// gets the last point of the current path, (0,0) if not yet set
	void GetCurrentPoint(wxDouble* x, wxDouble* y) const override;

	// adds another path
	void AddPath(const GraphicsPathData* path) override;

	// closes the current sub-path
	void CloseSubpath() override;

	// returns the native path
	void* GetNativePath() const override;

	// give the native path returned by GetNativePath() back (there might be some deallocations necessary)
	void UnGetNativePath(void* WXUNUSED(p)) const override {}

	// transforms each point of this path by the matrix
	void Transform(const GraphicsMatrixData* matrix) override;

	// gets the bounding box enclosing all points (possibly including control points)
	void GetBox(wxDouble* x, wxDouble* y, wxDouble* w, wxDouble *h) const override;

	bool Contains(wxDouble x, wxDouble y, wxPolygonFillMode fillStyle = wxODDEVEN_RULE) const override;

	// appends an ellipsis as a new closed subpath fitting the passed rectangle
	void AddCircle(wxDouble x, wxDouble y, wxDouble r) override;

	// appends an ellipse
	void AddEllipse(wxDouble x, wxDouble y, wxDouble w, wxDouble h) override;

private:
	void EnsureGeometryOpen();

	void EnsureSinkOpen();

	void EnsureFigureOpen(const D2D1_POINT_2F& pos);

	void EndFigure(D2D1_FIGURE_END figureEnd);

	ID2D1Geometry* GetFullGeometry(D2D1_FILL_MODE fillMode) const;

	bool IsEmpty() const;
	bool IsStateSafeForFlush() const;

	struct GeometryStateData
	{
		bool m_isCurrentPointSet;
		D2D1_POINT_2F m_currentPoint;
		bool m_isFigureOpen;
		D2D1_POINT_2F m_figureStart;
		bool m_isFigureLogStartSet;
		D2D1_POINT_2F m_figureLogStart;
	};
	void SaveGeometryState(GeometryStateData& data) const;
	void RestoreGeometryState(const GeometryStateData& data);

private :
	wxCOMPtr<ID2D1PathGeometry> m_pathGeometry;

	wxCOMPtr<ID2D1GeometrySink> m_geometrySink;

	wxCOMPtr<ID2D1Factory> m_direct2dfactory;

	mutable wxCOMPtr<ID2D1GeometryGroup> m_combinedGeometry;
	wxVector<ID2D1Geometry*> m_pTransformedGeometries;

	bool m_currentPointSet;
	D2D1_POINT_2F m_currentPoint;

	bool m_figureOpened;
	D2D1_POINT_2F m_figureStart;
	bool m_figureLogStartSet;
	D2D1_POINT_2F m_figureLogStart;

	bool m_geometryWritable;

	D2D1_FILL_MODE m_fillMode;
};

//-----------------------------------------------------------------------------
// wxD2DPathData implementation
//-----------------------------------------------------------------------------

wxD2DPathData::wxD2DPathData(wxD2DRenderer* renderer, ID2D1Factory* d2dFactory) :
	GraphicsPathData(renderer),
	m_direct2dfactory(d2dFactory),
	m_currentPointSet(false),
	m_currentPoint(D2D1::Point2F(0.0f, 0.0f)),
	m_figureOpened(false),
	m_figureStart(D2D1::Point2F(0.0f, 0.0f)),
	m_figureLogStartSet(false),
	m_figureLogStart(D2D1::Point2F(0.0f, 0.0f)),
	m_geometryWritable(true),
	m_fillMode(D2D1_FILL_MODE_ALTERNATE)
{
	m_direct2dfactory->CreatePathGeometry(&m_pathGeometry);
	// To properly initialize path geometry there is also
	// necessary to open at least once its geometry sink.
	m_pathGeometry->Open(&m_geometrySink);
}

wxD2DPathData::~wxD2DPathData()
{
	Flush();
	for( size_t i = 0; i < m_pTransformedGeometries.size(); i++ )
	{
		m_pTransformedGeometries[i]->Release();
	}
}

ID2D1PathGeometry* wxD2DPathData::GetPathGeometry()
{
	return m_pathGeometry;
}

//wxD2DPathData::GraphicsObjectRefData* wxD2DPathData::Clone() const
//{
//    wxD2DPathData* newPathData = new wxD2DPathData(GetRenderer(), m_direct2dfactory);
//
//    newPathData->EnsureGeometryOpen();
//
//    // Only geometry with closed sink can be
//    // transferred to another geometry object with
//    // ID2D1PathGeometry::Stream() so we have to check
//    // if actual transfer succeeded.
//
//    // Transfer geometry to the new geometry sink.
//    HRESULT hr = m_pathGeometry->Stream(newPathData->m_geometrySink);
//    wxASSERT_MSG( SUCCEEDED(hr), wxS("Current geometry is in invalid state") );
//    if ( FAILED(hr) )
//    {
//        delete newPathData;
//        return NULL;
//    }
//
//    // Copy the collection of transformed geometries.
//    ID2D1TransformedGeometry* pTransformedGeometry;
//    for ( size_t i = 0; i < m_pTransformedGeometries.size(); i++ )
//    {
//        pTransformedGeometry = NULL;
//        hr = m_direct2dfactory->CreateTransformedGeometry(
//                    m_pTransformedGeometries[i],
//                    D2D1::Matrix3x2F::Identity(), &pTransformedGeometry);
//        wxASSERT_MSG( SUCCEEDED(hr), wxFAILED_HRESULT_MSG(hr) );
//        newPathData->m_pTransformedGeometries.push_back(pTransformedGeometry);
//    }
//
//    // Copy positional data.
//    GeometryStateData curState;
//    SaveGeometryState(curState);
//    newPathData->RestoreGeometryState(curState);
//
//    return newPathData;
//}

void wxD2DPathData::Flush()
{
	if (m_geometrySink != NULL)
	{
		if ( m_figureOpened )
		{
			m_geometrySink->EndFigure(D2D1_FIGURE_END_OPEN);
			m_figureOpened = false;
		}

		if( m_geometryWritable )
		{
			HRESULT hr = m_geometrySink->Close();
			wxCHECK_HRESULT_RET(hr);
			m_geometryWritable = false;
		}
	}
}

void wxD2DPathData::EnsureGeometryOpen()
{
	if (!m_geometryWritable)
	{
		wxCOMPtr<ID2D1PathGeometry> newPathGeometry;
		HRESULT hr;
		hr = m_direct2dfactory->CreatePathGeometry(&newPathGeometry);
		wxCHECK_HRESULT_RET(hr);

		m_geometrySink.reset();
		hr = newPathGeometry->Open(&m_geometrySink);
		wxCHECK_HRESULT_RET(hr);

		if (m_pathGeometry != NULL)
		{
			hr = m_pathGeometry->Stream(m_geometrySink);
			wxCHECK_HRESULT_RET(hr);
		}

		m_pathGeometry = newPathGeometry;
		m_geometryWritable = true;
	}
}

void wxD2DPathData::EnsureSinkOpen()
{
	EnsureGeometryOpen();

	if (m_geometrySink == NULL)
	{
		HRESULT hr = m_pathGeometry->Open(&m_geometrySink);
		wxCHECK_HRESULT_RET(hr);
	}
}

void wxD2DPathData::EnsureFigureOpen(const D2D1_POINT_2F& pos)
{
	EnsureSinkOpen();

	if (!m_figureOpened)
	{
		m_figureStart = pos;
		m_geometrySink->BeginFigure(m_figureStart, D2D1_FIGURE_BEGIN_FILLED);
		m_figureOpened = true;
		m_currentPoint = m_figureStart;
	}
}

void wxD2DPathData::EndFigure(D2D1_FIGURE_END figureEnd)
{
	if (m_figureOpened)
	{
		// Ensure that sub-path being closed contains at least one point.
		if( figureEnd == D2D1_FIGURE_END_CLOSED )
			m_geometrySink->AddLine(m_currentPoint);

		if( figureEnd == D2D1_FIGURE_END_OPEN ||
			!m_figureLogStartSet ||
			m_figureLogStart == m_figureStart )
		{
			// If figure will remain open or if its logical startpoint
			// is not used or if it is the same as the actual
			// startpoint then we can end the figure in a standard way.
			m_geometrySink->EndFigure(figureEnd);
		}
		else
		{
			// If we want to end and close the figure for which
			// logical startpoint is not the same as actual startpoint
			// we have to fill the gap between the actual and logical
			// endpoints on our own.
			m_geometrySink->AddLine(m_figureLogStart);
			m_geometrySink->EndFigure(D2D1_FIGURE_END_OPEN);
			m_figureStart = m_figureLogStart;
		}
		m_figureOpened = false;
		m_figureLogStartSet = false;

		// If the figure is closed then current point
		// should be moved to the beginning of the figure.
		if( figureEnd == D2D1_FIGURE_END_CLOSED )
			m_currentPoint = m_figureStart;
	}
}

ID2D1Geometry* wxD2DPathData::GetFullGeometry(D2D1_FILL_MODE fillMode) const
{
	// Our final path geometry is represented by geometry group
	// which contains all transformed geometries plus current geometry.

	// We have to store pointers to all transformed geometries
	// as well as pointer to the current geometry in the auxiliary array.
	const size_t numGeometries = m_pTransformedGeometries.size();
	ID2D1Geometry** pGeometries = new ID2D1Geometry*[numGeometries+1];
	for( size_t i = 0; i < numGeometries; i++ )
		pGeometries[i] = m_pTransformedGeometries[i];

	pGeometries[numGeometries] = m_pathGeometry;

	// And use this array as a source to create geometry group.
	m_combinedGeometry.reset();
	HRESULT hr = m_direct2dfactory->CreateGeometryGroup(fillMode,
								  pGeometries, numGeometries+1, &m_combinedGeometry);
	wxFAILED_HRESULT_MSG(hr);
	delete []pGeometries;

	return m_combinedGeometry;
}

bool wxD2DPathData::IsEmpty() const
{
	return !m_currentPointSet && !m_figureOpened &&
			m_pTransformedGeometries.size() == 0;
}

bool wxD2DPathData::IsStateSafeForFlush() const
{
	// Only geometry with not yet started figure
	// or with started but empty figure can be fully
	// restored to its initial state after invoking Flush().
	if( !m_figureOpened )
		return true;

	D2D1_POINT_2F actFigureStart = m_figureLogStartSet ?
						m_figureLogStart : m_figureStart;
	return m_currentPoint == actFigureStart;
}

void wxD2DPathData::SaveGeometryState(GeometryStateData& data) const
{
	data.m_isFigureOpen = m_figureOpened;
	data.m_isFigureLogStartSet = m_figureLogStartSet;
	data.m_isCurrentPointSet = m_currentPointSet;
	data.m_currentPoint = m_currentPoint;
	data.m_figureStart = m_figureStart;
	data.m_figureLogStart = m_figureLogStart;
}

void wxD2DPathData::RestoreGeometryState(const GeometryStateData& data)
{
	if( data.m_isFigureOpen )
	{
		// If the figure has to be re-started at the startpoint
		// which is not the current point then we have to start it
		// physically at the current point but with storing also its
		// logical startpoint to use it later on to close the figure,
		// if necessary.
		// Ending and closing the figure using this proxy startpoint
		// is only a simulation of regular closure and figure can behave
		// in a slightly different way than figure closed with physical
		// startpoint so this action should be avoided if only possible.
		D2D1_POINT_2F actFigureStart = data.m_isFigureLogStartSet ?
						 data.m_figureLogStart : data.m_figureStart;
		if ( !(data.m_currentPoint == actFigureStart) )
		{
			m_figureLogStart = actFigureStart;
			m_figureLogStartSet = true;
			EnsureFigureOpen(data.m_currentPoint);
		}
		else
		{
			EnsureFigureOpen(actFigureStart);
		}
	}
	else
	{
		m_figureOpened = false;
	}

	m_currentPointSet = data.m_isCurrentPointSet;
	m_currentPoint = data.m_isCurrentPointSet ?
				data.m_currentPoint : D2D1::Point2F(0.0F, 0.0F);
}

void wxD2DPathData::MoveToPoint(wxDouble x, wxDouble y)
{
	// Close current sub-path (leaving the figure as is).
	EndFigure(D2D1_FIGURE_END_OPEN);
	// Store new current point
	m_currentPoint = D2D1::Point2F(x, y);
	m_currentPointSet = true;
}

// adds a straight line from the current point to (x,y)
void wxD2DPathData::AddLineToPoint(wxDouble x, wxDouble y)
{
	// If current point is not yet set then
	// this function should behave as MoveToPoint.
	if( !m_currentPointSet )
	{
		MoveToPoint(x, y);
		return;
	}

	EnsureFigureOpen(m_currentPoint);
	m_geometrySink->AddLine(D2D1::Point2F(x, y));

	m_currentPoint = D2D1::Point2F(x, y);
}

// adds a cubic Bezier curve from the current point, using two control points and an end point
void wxD2DPathData::AddCurveToPoint(wxDouble cx1, wxDouble cy1, wxDouble cx2, wxDouble cy2, wxDouble x, wxDouble y)
{
	// If no current point is set then this function should behave
	// as if preceded by a call to MoveToPoint(cx1, cy1).
	if( !m_currentPointSet )
		MoveToPoint(cx1, cy1);

	EnsureFigureOpen(m_currentPoint);

	D2D1_BEZIER_SEGMENT bezierSegment = {
		{ (FLOAT)cx1, (FLOAT)cy1 },
		{ (FLOAT)cx2, (FLOAT)cy2 },
		{ (FLOAT)x, (FLOAT)y } };
	m_geometrySink->AddBezier(bezierSegment);

	m_currentPoint = D2D1::Point2F(x, y);
}

// adds an arc of a circle centering at (x,y) with radius (r) from startAngle to endAngle
void wxD2DPathData::AddArc(wxDouble x, wxDouble y, wxDouble r, wxDouble startAngle, wxDouble endAngle, bool clockwise)
{
	double angle;

	// For the sake of compatibility normalize angles the same way
	// as it is done in Cairo.
	if ( clockwise )
	{
		// If endAngle < startAngle it needs to be progressively
		// increased by 2*M_PI until endAngle > startAngle.
		if ( endAngle < startAngle )
		{
			while ( endAngle <= startAngle )
			{
				endAngle += 2.0*M_PI;
			}
		}

		angle = endAngle - startAngle;
	}
	else
	{
		// If endAngle > startAngle it needs to be progressively
		// decreased by 2*M_PI until endAngle < startAngle.
		if ( endAngle > startAngle )
		{
			while ( endAngle >= startAngle )
			{
				endAngle -= 2.0*M_PI;
			}
		}

		angle = startAngle - endAngle;
	}

	wxPoint2DDouble start = wxPoint2DDouble(cos(startAngle) * r, sin(startAngle) * r);
	wxPoint2DDouble end = wxPoint2DDouble(cos(endAngle) * r, sin(endAngle) * r);

	// To ensure compatibility with Cairo an initial
	// line segment to the beginning of the arc needs
	// to be added to the path.
	if ( m_figureOpened )
	{
		AddLineToPoint(start.m_x + x, start.m_y + y);
	}
	else if ( m_currentPointSet )
	{
		EnsureFigureOpen(m_currentPoint);
		AddLineToPoint(start.m_x + x, start.m_y + y);
	}
	else
	{
		MoveToPoint(start.m_x + x, start.m_y + y);
		EnsureFigureOpen(m_currentPoint);
	}

	D2D1_SWEEP_DIRECTION sweepDirection = clockwise ?
	   D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
	D2D1_SIZE_F size = D2D1::SizeF((FLOAT)r, (FLOAT)r);

	if ( angle >= 2.0*M_PI )
	{
		// In addition to arc we need to draw full circle(s).
		// Remarks:
		// 1. Parity of the number of the circles has to be
		// preserved because this matters when path would be
		// filled with wxODDEVEN_RULE flag set (using
		// D2D1_FILL_MODE_ALTERNATE mode) when number of the
		// edges is counted.
		// 2. ID2D1GeometrySink::AddArc() doesn't work
		// with 360-degree arcs so we need to construct
		// the circle from two halves.
		D2D1_ARC_SEGMENT circleSegment1 =
		{
			D2D1::Point2((FLOAT)(x - start.m_x), (FLOAT)(y - start.m_y)),  // end point
			size,                     // size
			0.0f,                     // rotation
			sweepDirection,           // sweep direction
			D2D1_ARC_SIZE_SMALL       // arc size
		};
		D2D1_ARC_SEGMENT circleSegment2 =
		{
			D2D1::Point2((FLOAT)(x + start.m_x), (FLOAT)(y + start.m_y)),  // end point
			size,                     // size
			0.0f,                     // rotation
			sweepDirection,           // sweep direction
			D2D1_ARC_SIZE_SMALL       // arc size
		};

		int numCircles = (int)(angle / (2.0*M_PI));
		numCircles = (numCircles - 1) % 2 + 1;
		for( int i = 0; i < numCircles; i++ )
		{
			m_geometrySink->AddArc(circleSegment1);
			m_geometrySink->AddArc(circleSegment2);
		}

		// Reduce the angle to [0..2*M_PI) range.
		angle = fmod(angle, 2.0*M_PI);
	}

	D2D1_ARC_SIZE arcSize = angle > M_PI ?
	   D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
	D2D1_POINT_2F endPoint =
	   D2D1::Point2((FLOAT)(end.m_x + x), (FLOAT)(end.m_y + y));

	D2D1_ARC_SEGMENT arcSegment =
	{
		endPoint,                     // end point
		size,                         // size
		0.0f,                         // rotation
		sweepDirection,               // sweep direction
		arcSize                       // arc size
	};

	m_geometrySink->AddArc(arcSegment);

	m_currentPoint = endPoint;
}

// appends an ellipsis as a new closed subpath fitting the passed rectangle
void wxD2DPathData::AddCircle(wxDouble x, wxDouble y, wxDouble r)
{
	AddEllipse(x - r, y - r, r * 2, r * 2);
}

// appends an ellipse
void wxD2DPathData::AddEllipse(wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
	if ( w <= 0.0 || h <= 0.0 )
	  return;

	// Calculate radii
	const wxDouble rx = w / 2.0;
	const wxDouble ry = h / 2.0;

	MoveToPoint(x + w, y + ry);
	// Open new subpath
	EnsureFigureOpen(m_currentPoint);

	D2D1_ARC_SEGMENT arcSegmentLower =
	{
		D2D1::Point2((FLOAT)(x), (FLOAT)(y + ry)),     // end point
		D2D1::SizeF((FLOAT)(rx), (FLOAT)(ry)),         // size
		0.0f,
		D2D1_SWEEP_DIRECTION_CLOCKWISE,
		D2D1_ARC_SIZE_SMALL
	};
	m_geometrySink->AddArc(arcSegmentLower);

	D2D1_ARC_SEGMENT arcSegmentUpper =
	{
		D2D1::Point2((FLOAT)(x + w), (FLOAT)(y + ry)), // end point
		D2D1::SizeF((FLOAT)(rx), (FLOAT)(ry)),         // size
		0.0f,
		D2D1_SWEEP_DIRECTION_CLOCKWISE,
		D2D1_ARC_SIZE_SMALL
	};
	m_geometrySink->AddArc(arcSegmentUpper);

	CloseSubpath();
}

// gets the last point of the current path, (0,0) if not yet set
void wxD2DPathData::GetCurrentPoint(wxDouble* x, wxDouble* y) const
{
	if (x != NULL) *x = m_currentPoint.x;
	if (y != NULL) *y = m_currentPoint.y;
}

// adds another path
void wxD2DPathData::AddPath(const GraphicsPathData* path)
{
	wxD2DPathData* pathSrc =
		 const_cast<wxD2DPathData*>(static_cast<const wxD2DPathData*>(path));

	// Nothing to do if geometry of appended path is not initialized.
	if ( pathSrc->m_pathGeometry == NULL || pathSrc->m_geometrySink == NULL )
		return;

	// Because only closed geometry (with closed sink)
	// can be transferred to another geometry object with
	// ID2D1PathGeometry::Stream() so we have to close it
	// before any operation and to re-open afterwards.
	// Unfortunately, to close the sink it is also necessary
	// to end the figure it contains, if it was open.
	// After re-opening the geometry we should also re-start
	// the figure (if it was open) and restore its state but
	// it seems there is no straightforward way to do so
	// if the figure is not empty.
	//
	// So, only if appended path has a sub-path closed or
	// has an empty sub-path open there is possible to restore
	// its state after appending it to the current path and only
	// in this case the operation doesn't introduce side effects.

	// Nothing to do if appended path is empty.
	if ( pathSrc->IsEmpty() )
		return;

	// Save positional and auxiliary data
	// of the appended path and its geometry.
	GeometryStateData curStateSrc;
	pathSrc->SaveGeometryState(curStateSrc);

	// Close appended geometry.
	pathSrc->Flush();

	// Close current geometry (leaving the figure as is).
	Flush();

	HRESULT hr;
	ID2D1TransformedGeometry* pTransformedGeometry = NULL;
	// Add current geometry to the collection transformed geometries.
	hr = m_direct2dfactory->CreateTransformedGeometry(m_pathGeometry,
						D2D1::Matrix3x2F::Identity(), &pTransformedGeometry);
	wxCHECK_HRESULT_RET(hr);
	m_pTransformedGeometries.push_back(pTransformedGeometry);

	// Add to the collection transformed geometries from the appended path.
	for ( size_t i = 0; i < pathSrc->m_pTransformedGeometries.size(); i++ )
	{
		pTransformedGeometry = NULL;
		hr = m_direct2dfactory->CreateTransformedGeometry(
					pathSrc->m_pTransformedGeometries[i],
					D2D1::Matrix3x2F::Identity(), &pTransformedGeometry);
		wxCHECK_HRESULT_RET(hr);
		m_pTransformedGeometries.push_back(pTransformedGeometry);
	}

	// Clear and reopen current geometry.
	m_pathGeometry.reset();
	EnsureGeometryOpen();

	// Transfer appended geometry to the current geometry sink.
	hr = pathSrc->m_pathGeometry->Stream(m_geometrySink);
	wxCHECK_HRESULT_RET(hr);

	// Apply to the current path positional data from the appended path.
	// This operation fully sets geometry to the required state
	// only if it represents geometry without started figure
	// or with started but empty figure.
	RestoreGeometryState(curStateSrc);

	// Reopen appended geometry.
	pathSrc->EnsureGeometryOpen();
	// Restore its positional data.
	// This operation fully restores geometry to the required state
	// only if it represents geometry without started figure
	// or with started but empty figure.
	pathSrc->RestoreGeometryState(curStateSrc);
}

// closes the current sub-path
void wxD2DPathData::CloseSubpath()
{
	// If we have a sub-path open by call to MoveToPoint(),
	// which doesn't open a new figure by itself,
	// we have to open a new figure now to get a required 1-point path.
	if ( !m_figureOpened && m_currentPointSet )
	{
		EnsureFigureOpen(m_currentPoint);
	}
	// Close sub-path and close the figure.
	if ( m_figureOpened )
	{
		EndFigure(D2D1_FIGURE_END_CLOSED);
		MoveToPoint(m_figureStart.x, m_figureStart.y);
	}
}

void* wxD2DPathData::GetNativePath() const
{
	return GetFullGeometry(GetFillMode());
}

void wxD2DPathData::Transform(const GraphicsMatrixData* matrix)
{
	// Unfortunately, it looks there is no straightforward way to apply
	// transformation to the current underlying path geometry
	// (ID2D1PathGeometry object) "in-place" (ie. transform it and use
	// for further graphics operations, including next transformations too).
	// Some simple methods offered by D2D are not useful for these purposes:
	// 1. ID2D1Factory::CreateTransformedGeometry() converts ID2D1PathGeometry
	// object to ID2D1TransformedGeometry object but ID2D1TransformedGeometry
	// inherits from ID2D1Geometry (not from ID2D1PathGeometry)
	// and hence cannot be used for further path operations.
	// 2. ID2D1Geometry::CombineWithGeometry() which could be used to get final
	// path geometry by combining empty geometry with transformed geometry
	// doesn't offer any combine mode which would produce a "sum" of geometries
	// (D2D1_COMBINE_MODE_UNION produces kind of outline). Moreover the result
	// is stored in ID2D1SimplifiedGeometrySink not in ID2DGeometrySink.

	// So, it seems that ability to transform the wxGraphicsPath
	// (several times) and still use it after this operation(s)
	// can be achieved (only?) by using a geometry group object
	// (ID2D1GeometryGroup) this way:
	// 1. After applying transformation to the current path geometry with
	// ID2D1Factory::CreateTransformedGeometry() the result is stored
	// in the collection of transformed geometries (an auxiliary array)
	// and after that a new (empty) geometry is open (in the same state
	// as just closed one) and this geometry is used as a current one
	// for further graphics operations.
	// 2. Since above steps are done at every transformation so our effective
	// geometry will be a superposition of all previously transformed
	// geometries stored in the collection (array) and the current
	// operational geometry.
	// 3. If there is necessary to use this combined effective geometry
	// in any operation then ID2D1GeometryGroup created with
	// ID2D1Factory::CreateGeometryGroup() from the collection
	// of stored geometries will act as a proxy geometry.

	const D2D1::Matrix3x2F* m = static_cast<D2D1::Matrix3x2F*>(matrix->GetNativeMatrix());

	// Save current positional data.
	GeometryStateData curState;
	SaveGeometryState(curState);
	// We need to close the geometry what requires also to end a figure
	// (if started). This ended figure should be re-started in its initial
	// state when all path processing is done but due to the Direct2D
	// constraints this can be fully done only if open figure was empty.
	// So, Transform() can be safely called if path doesn't contain the open
	// sub-path or if open sub-path is empty.
	
	// Close current geometry.
	Flush();

	HRESULT hr;
	ID2D1TransformedGeometry* pTransformedGeometry;
	// Apply given transformation to all previously stored geometries too.
	for( size_t i = 0; i < m_pTransformedGeometries.size(); i++ )
	{
		pTransformedGeometry = NULL;
		hr = m_direct2dfactory->CreateTransformedGeometry(m_pTransformedGeometries[i], m, &pTransformedGeometry);
		wxCHECK_HRESULT_RET(hr);

		m_pTransformedGeometries[i]->Release();
		m_pTransformedGeometries[i] = pTransformedGeometry;
	}

	// Transform current geometry and add the result
	// to the collection of transformed geometries.
	pTransformedGeometry = NULL;
	hr = m_direct2dfactory->CreateTransformedGeometry(m_pathGeometry, m, &pTransformedGeometry);
	wxCHECK_HRESULT_RET(hr);
	m_pTransformedGeometries.push_back(pTransformedGeometry);

	// Clear and reopen current geometry.
	m_pathGeometry.reset();
	EnsureGeometryOpen();
	// Restore the figure with transformed positional data.
	// This operation fully restores geometry to the required state
	// only if IsStateSafeForFlush() returns true.
	curState.m_currentPoint = m->TransformPoint(curState.m_currentPoint);
	curState.m_figureLogStart = m->TransformPoint(curState.m_figureLogStart);
	curState.m_figureStart = m->TransformPoint(curState.m_figureStart);
	RestoreGeometryState(curState);
}

void wxD2DPathData::GetBox(wxDouble* x, wxDouble* y, wxDouble* w, wxDouble *h) const
{
	D2D1_RECT_F bounds;
	ID2D1Geometry *curGeometry = GetFullGeometry(GetFillMode());
	HRESULT hr = curGeometry->GetBounds(D2D1::Matrix3x2F::Identity(), &bounds);
	wxCHECK_HRESULT_RET(hr);
	// Check if bounds are empty
	if ( bounds.left > bounds.right )
	{
		bounds.left = bounds.top = bounds.right = bounds.bottom = 0.0F;
	}
	if (x) *x = bounds.left;
	if (y) *y = bounds.top;
	if (w) *w = bounds.right - bounds.left;
	if (h) *h = bounds.bottom - bounds.top;
}

bool wxD2DPathData::Contains(wxDouble x, wxDouble y, wxPolygonFillMode fillStyle) const
{
	BOOL result;
	D2D1_FILL_MODE fillMode = (fillStyle == wxODDEVEN_RULE) ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING;
	ID2D1Geometry *curGeometry = GetFullGeometry(fillMode);
	curGeometry->FillContainsPoint(D2D1::Point2F(x, y), D2D1::Matrix3x2F::Identity(), &result);
	return result != FALSE;
}

//wxD2DPathData* wxGetD2DPathData(const GraphicsPathData& path)
//{
//    return static_cast<wxD2DPathData*>(path.GetGraphicsData());
//}

// This utility class is used to read a color value with the format
// PBGRA from a byte stream and to write a color back to the stream.
// It's used in conjunction with the IWICBitmapSource or IWICBitmap
// pixel data to easily read and write color values.
struct wxPBGRAColor
{
	wxPBGRAColor(BYTE* stream) :
		b(*stream), g(*(stream + 1)), r(*(stream + 2)), a(*(stream + 3))
	{}

	wxPBGRAColor(const wxColor& color) :
		b(color.Blue()), g(color.Green()), r(color.Red()), a(color.Alpha())
	{}

	bool IsBlack() const { return r == 0 && g == 0 && b == 0; }

	void Write(BYTE* stream) const
	{
		*(stream + 0) = b;
		*(stream + 1) = g;
		*(stream + 2) = r;
		*(stream + 3) = a;
	}

	BYTE b, g, r, a;
};

wxCOMPtr<IWICBitmapSource> wxCreateWICBitmap(const WXHBITMAP sourceBitmap, bool hasAlpha = false)
{
	HRESULT hr;

	wxCOMPtr<IWICBitmap> wicBitmap;
	hr = wxWICImagingFactory()->CreateBitmapFromHBITMAP(sourceBitmap, NULL, WICBitmapUseAlpha, &wicBitmap);
	wxCHECK2_HRESULT_RET(hr, wxCOMPtr<IWICBitmapSource>(NULL));

	wxCOMPtr<IWICFormatConverter> converter;
	hr = wxWICImagingFactory()->CreateFormatConverter(&converter);
	wxCHECK2_HRESULT_RET(hr, wxCOMPtr<IWICBitmapSource>(NULL));

	WICPixelFormatGUID pixelFormat = hasAlpha ? GUID_WICPixelFormat32bppPBGRA : GUID_WICPixelFormat32bppBGR;

	hr = converter->Initialize(
		wicBitmap,
		pixelFormat,
		WICBitmapDitherTypeNone, NULL, 0.f,
		WICBitmapPaletteTypeMedianCut);
	wxCHECK2_HRESULT_RET(hr, wxCOMPtr<IWICBitmapSource>(NULL));

	return wxCOMPtr<IWICBitmapSource>(converter);
}

wxCOMPtr<IWICBitmapSource> wxCreateWICBitmap(const wxBitmap& sourceBitmap, bool hasAlpha = false)
{
	return wxCreateWICBitmap(sourceBitmap.GetHBITMAP(), hasAlpha);
}

// WIC Bitmap Source for creating hatch patterned bitmaps
class wxHatchBitmapSource : public IWICBitmapSource
{
public:
	wxHatchBitmapSource(wxBrushStyle brushStyle, const wxColor& color) :
		m_brushStyle(brushStyle), m_color(color), m_refCount(0l)
	{
	}

	virtual ~wxHatchBitmapSource() {}

	HRESULT STDMETHODCALLTYPE GetSize(__RPC__out UINT *width, __RPC__out UINT *height) override
	{
		if (width != NULL) *width = 8;
		if (height != NULL) *height = 8;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetPixelFormat(__RPC__out WICPixelFormatGUID *pixelFormat) override
	{
		if (pixelFormat != NULL) *pixelFormat = GUID_WICPixelFormat32bppPBGRA;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetResolution(__RPC__out double *dpiX, __RPC__out double *dpiY) override
	{
		if (dpiX != NULL) *dpiX = 96.0;
		if (dpiY != NULL) *dpiY = 96.0;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE CopyPalette(__RPC__in_opt IWICPalette*  WXUNUSED(palette)) override
	{
		return WINCODEC_ERR_PALETTEUNAVAILABLE;
	}

	HRESULT STDMETHODCALLTYPE CopyPixels(
		const WICRect* WXUNUSED(prc),
		UINT WXUNUSED(stride),
		UINT WXUNUSED(bufferSize),
		BYTE *buffer) override
	{
		// patterns are encoded in a bit map of size 8 x 8
		static const unsigned char BDIAGONAL_PATTERN[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
		static const unsigned char FDIAGONAL_PATTERN[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
		static const unsigned char CROSSDIAG_PATTERN[8] = { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 };
		static const unsigned char CROSS_PATTERN[8] = { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0xFF };
		static const unsigned char HORIZONTAL_PATTERN[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF };
		static const unsigned char VERTICAL_PATTERN[8] = { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 };

		switch (m_brushStyle)
		{
			case wxBRUSHSTYLE_BDIAGONAL_HATCH:
				CopyPattern(buffer, BDIAGONAL_PATTERN);
				break;
			case wxBRUSHSTYLE_CROSSDIAG_HATCH:
				CopyPattern(buffer, CROSSDIAG_PATTERN);
				break;
			case wxBRUSHSTYLE_FDIAGONAL_HATCH:
				CopyPattern(buffer, FDIAGONAL_PATTERN);
				break;
			case wxBRUSHSTYLE_CROSS_HATCH:
				CopyPattern(buffer, CROSS_PATTERN);
				break;
			case wxBRUSHSTYLE_HORIZONTAL_HATCH:
				CopyPattern(buffer, HORIZONTAL_PATTERN);
				break;
			case wxBRUSHSTYLE_VERTICAL_HATCH:
				CopyPattern(buffer, VERTICAL_PATTERN);
				break;
			default:
				break;
		}

		return S_OK;
	}

	// Implementations adapted from: "Implementing IUnknown in C++"
	// http://msdn.microsoft.com/en-us/library/office/cc839627%28v=office.15%29.aspx

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID referenceId, void** object) override
	{
		if (!object)
		{
			return E_INVALIDARG;
		}

		*object = NULL;

		if (referenceId == IID_IUnknown || referenceId == wxIID_IWICBitmapSource)
		{
			*object = (LPVOID)this;
			AddRef();
			return NOERROR;
		}

		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef(void) override
	{
		InterlockedIncrement(&m_refCount);
		return m_refCount;
	}

	ULONG STDMETHODCALLTYPE Release(void) override
	{
		wxCHECK_MSG(m_refCount > 0, 0, "Unbalanced number of calls to Release");

		ULONG refCount = InterlockedDecrement(&m_refCount);
		if (m_refCount == 0)
		{
			delete this;
		}
		return refCount;
	}

private:

	// Copies an 8x8 bit pattern to a PBGRA byte buffer
	void CopyPattern(BYTE* buffer, const unsigned char* pattern) const
	{
		static const wxPBGRAColor transparent(wxTransparentColour);

		int k = 0;

		for (int i = 0; i < 8; ++i)
		{
			for (int j = 7; j >= 0; --j)
			{
				bool isColorBit = (pattern[i] & (1 << j)) > 0;
				(isColorBit ? m_color : transparent).Write(buffer + k);
				k += 4;
			}
		}
	}

private:
	// The hatch style produced by this bitmap source
	const wxBrushStyle m_brushStyle;

	// The colour of the hatch
	const wxPBGRAColor m_color;

	// Internally used to implement IUnknown's reference counting
	ULONG m_refCount;
};

// RAII class hosting a WIC bitmap lock used for writing
// pixel data to a WICBitmap
class wxBitmapPixelWriteLock
{
public:
	wxBitmapPixelWriteLock(IWICBitmap* bitmap)
	{
		// Retrieve the size of the bitmap
		UINT w, h;
		bitmap->GetSize(&w, &h);
		WICRect lockSize = {0, 0, (INT)w, (INT)h};

		// Obtain a bitmap lock for exclusive write
		bitmap->Lock(&lockSize, WICBitmapLockWrite, &m_pixelLock);
	}

	IWICBitmapLock* GetLock() { return m_pixelLock; }

private:
	wxCOMPtr<IWICBitmapLock> m_pixelLock;
};

class wxD2DBitmapResourceHolder : public wxD2DResourceHolder<ID2D1Bitmap>
{
public:
	wxD2DBitmapResourceHolder(const wxBitmap& sourceBitmap) :
		m_sourceBitmap(sourceBitmap)
	{
	}

	const wxBitmap& GetSourceBitmap() const { return m_sourceBitmap; }

protected:
	void DoAcquireResource() override
	{
		ID2D1RenderTarget* renderTarget = GetContext();

		HRESULT hr;

		if(m_sourceBitmap.GetMask())
		{
			int w = m_sourceBitmap.GetWidth();
			int h = m_sourceBitmap.GetHeight();

			wxCOMPtr<IWICBitmapSource> colorBitmap = wxCreateWICBitmap(m_sourceBitmap);
			wxCOMPtr<IWICBitmapSource> maskBitmap = wxCreateWICBitmap(m_sourceBitmap.GetMask()->GetMaskBitmap());
			wxCOMPtr<IWICBitmap> resultBitmap;

			wxWICImagingFactory()->CreateBitmap(
				w, h,
				GUID_WICPixelFormat32bppPBGRA,
				WICBitmapCacheOnLoad,
				&resultBitmap);

			BYTE* colorBuffer = new BYTE[4 * w * h];
			BYTE* maskBuffer = new BYTE[4 * w * h];
			BYTE* resultBuffer;

			hr = colorBitmap->CopyPixels(NULL, w * 4, 4 * w * h, colorBuffer);
			hr = maskBitmap->CopyPixels(NULL, w * 4, 4 * w * h, maskBuffer);

			{
				wxBitmapPixelWriteLock lock(resultBitmap);

				UINT bufferSize = 0;
				hr = lock.GetLock()->GetDataPointer(&bufferSize, &resultBuffer);

				static const wxPBGRAColor transparentColor(wxTransparentColour);

				// Create the result bitmap
				for (int i = 0; i < w * h * 4; i += 4)
				{
					wxPBGRAColor color(colorBuffer + i);
					wxPBGRAColor mask(maskBuffer + i);

					if (mask.IsBlack())
					{
						transparentColor.Write(resultBuffer + i);
					}
					else
					{
						color.a = 255;
						color.Write(resultBuffer + i);
					}
				}
			}

			hr = renderTarget->CreateBitmapFromWicBitmap(resultBitmap, 0, &m_nativeResource);
			wxCHECK_HRESULT_RET(hr);

			delete[] colorBuffer;
			delete[] maskBuffer;
		}
		else
		{
			wxCOMPtr<IWICBitmapSource> bitmapSource = wxCreateWICBitmap(m_sourceBitmap, m_sourceBitmap.HasAlpha());
			hr = renderTarget->CreateBitmapFromWicBitmap(bitmapSource, 0, &m_nativeResource);
		}
	}

private:
	const wxBitmap m_sourceBitmap;
};

//-----------------------------------------------------------------------------
// wxD2DBitmapData declaration
//-----------------------------------------------------------------------------

class wxD2DBitmapData : public GraphicsBitmapData, public wxD2DManagedGraphicsData
{
public:
	typedef wxD2DBitmapResourceHolder NativeType;

	wxD2DBitmapData(wxD2DRenderer* renderer, const wxBitmap& bitmap) :
		GraphicsBitmapData(renderer), m_bitmapHolder(bitmap) {}

	wxD2DBitmapData(wxD2DRenderer* renderer, const void* pseudoNativeBitmap) :
		GraphicsBitmapData(renderer), m_bitmapHolder(*static_cast<const NativeType*>(pseudoNativeBitmap)) {}

	// returns the native representation
	void* GetNativeBitmap() const override;

	wxCOMPtr<ID2D1Bitmap> GetD2DBitmap();

	wxD2DManagedObject* GetManagedObject() override
	{
		return &m_bitmapHolder;
	}

private:
	NativeType m_bitmapHolder;
};

//-----------------------------------------------------------------------------
// wxD2DBitmapData implementation
//-----------------------------------------------------------------------------

void* wxD2DBitmapData::GetNativeBitmap() const
{
	return (void*)&m_bitmapHolder;
}

wxCOMPtr<ID2D1Bitmap> wxD2DBitmapData::GetD2DBitmap()
{
	return m_bitmapHolder.GetD2DResource();
}

//wxD2DBitmapData* wxGetD2DBitmapData(const wxGraphicsBitmap& bitmap)
//{
//    return static_cast<wxD2DBitmapData*>(bitmap.GetRefData());
//}

// Helper class used to create and safely release a ID2D1GradientStopCollection from wxGraphicsGradientStops
class wxD2DGradientStopsHelper
{
public:
	wxD2DGradientStopsHelper(const wxGraphicsGradientStops& gradientStops, ID2D1RenderTarget* renderTarget)
	{
		int stopCount = gradientStops.GetCount();

		D2D1_GRADIENT_STOP* gradientStopArray = new D2D1_GRADIENT_STOP[stopCount];

		for (int i = 0; i < stopCount; ++i)
		{
			gradientStopArray[i].color = wxD2DConvertColour(gradientStops.Item(i).GetColour());
			gradientStopArray[i].position = gradientStops.Item(i).GetPosition();
		}

		renderTarget->CreateGradientStopCollection(gradientStopArray, stopCount, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &m_gradientStopCollection);

		delete[] gradientStopArray;
	}

	ID2D1GradientStopCollection* GetGradientStopCollection()
	{
		return m_gradientStopCollection;
	}

private:
	wxCOMPtr<ID2D1GradientStopCollection> m_gradientStopCollection;
};

template <typename B>
class wxD2DBrushResourceHolder : public wxD2DResourceHolder<B>
{
public:
	wxD2DBrushResourceHolder(const wxBrush& brush) : m_sourceBrush(brush) {}
	virtual ~wxD2DBrushResourceHolder() {}
protected:
	const wxBrush m_sourceBrush;
};

class wxD2DSolidBrushResourceHolder : public wxD2DBrushResourceHolder<ID2D1SolidColorBrush>
{
public:
	wxD2DSolidBrushResourceHolder(const wxBrush& brush) : wxD2DBrushResourceHolder(brush) {}

protected:
	void DoAcquireResource() override
	{
		wxColour colour = m_sourceBrush.GetColour();
		HRESULT hr = GetContext()->CreateSolidColorBrush(wxD2DConvertColour(colour), &m_nativeResource);
		wxCHECK_HRESULT_RET(hr);
	}
};

class wxD2DBitmapBrushResourceHolder : public wxD2DBrushResourceHolder<ID2D1BitmapBrush>
{
public:
	wxD2DBitmapBrushResourceHolder(const wxBrush& brush) : wxD2DBrushResourceHolder(brush) {}

protected:
	void DoAcquireResource() override
	{
		// TODO: cache this bitmap
		wxD2DBitmapResourceHolder bitmap(*(m_sourceBrush.GetStipple()));
		bitmap.Bind(GetManager());

		HRESULT result = GetContext()->CreateBitmapBrush(
			bitmap.GetD2DResource(),
			D2D1::BitmapBrushProperties(
			D2D1_EXTEND_MODE_WRAP,
			D2D1_EXTEND_MODE_WRAP,
			D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),
			&m_nativeResource);

		wxCHECK_HRESULT_RET(result);
	}
};

class wxD2DHatchBrushResourceHolder : public wxD2DBrushResourceHolder<ID2D1BitmapBrush>
{
public:
	wxD2DHatchBrushResourceHolder(const wxBrush& brush) : wxD2DBrushResourceHolder(brush) {}

protected:
	void DoAcquireResource() override
	{
		wxCOMPtr<wxHatchBitmapSource> hatchBitmapSource(new wxHatchBitmapSource(m_sourceBrush.GetStyle(), m_sourceBrush.GetColour()));

		wxCOMPtr<ID2D1Bitmap> bitmap;

		HRESULT hr = GetContext()->CreateBitmapFromWicBitmap(hatchBitmapSource, &bitmap);
		wxCHECK_HRESULT_RET(hr);

		hr = GetContext()->CreateBitmapBrush(
			bitmap,
			D2D1::BitmapBrushProperties(
			D2D1_EXTEND_MODE_WRAP,
			D2D1_EXTEND_MODE_WRAP,
			D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),
			&m_nativeResource);
		wxCHECK_HRESULT_RET(hr);
	}
};

class wxD2DLinearGradientBrushResourceHolder : public wxD2DResourceHolder<ID2D1LinearGradientBrush>
{
public:
	struct LinearGradientInfo {
		const wxDouble x1;
		const wxDouble y1;
		const wxDouble x2;
		const wxDouble y2;
		const wxGraphicsGradientStops stops;
		wxD2DMatrixData *matrix;
		LinearGradientInfo(wxDouble& x1_, wxDouble& y1_, 
						   wxDouble& x2_, wxDouble& y2_, 
						   const wxGraphicsGradientStops& stops_,
						   wxD2DMatrixData * matrix_)
			: x1(x1_), y1(y1_), x2(x2_), y2(y2_), stops(stops_), matrix(matrix_) {}
	};

	wxD2DLinearGradientBrushResourceHolder(wxDouble& x1, wxDouble& y1, 
										   wxDouble& x2, wxDouble& y2, 
										   const wxGraphicsGradientStops& stops,
										   wxD2DMatrixData* matrix)
		: m_linearGradientInfo(x1, y1, x2, y2, stops, matrix) {}

protected:
	void DoAcquireResource() override
	{
		wxD2DGradientStopsHelper helper(m_linearGradientInfo.stops, GetContext());
		ID2D1LinearGradientBrush  *linearGradientBrush;

		HRESULT hr = GetContext()->CreateLinearGradientBrush(
			D2D1::LinearGradientBrushProperties(
				D2D1::Point2F(m_linearGradientInfo.x1, m_linearGradientInfo.y1),
				D2D1::Point2F(m_linearGradientInfo.x2, m_linearGradientInfo.y2)),
			helper.GetGradientStopCollection(),
			&linearGradientBrush);
		wxCHECK_HRESULT_RET(hr);

		if (! m_linearGradientInfo.matrix)
		{
			D2D1::Matrix3x2F matrix = m_linearGradientInfo.matrix->GetMatrix3x2F();
			matrix.Invert();
			linearGradientBrush->SetTransform(matrix);
		}
		m_nativeResource = linearGradientBrush;
	}
private:
	const LinearGradientInfo m_linearGradientInfo;
};

class wxD2DRadialGradientBrushResourceHolder : public wxD2DResourceHolder<ID2D1RadialGradientBrush>
{
public:
	struct RadialGradientInfo {
		const wxDouble x1;
		const wxDouble y1;
		const wxDouble x2;
		const wxDouble y2;
		const wxDouble radius;
		const wxGraphicsGradientStops stops;
		wxD2DMatrixData *matrix;

		RadialGradientInfo(wxDouble x1_, wxDouble y1_, 
						   wxDouble x2_, wxDouble y2_, 
						   wxDouble r, 
						   const wxGraphicsGradientStops& stops_,
						   wxD2DMatrixData* matrix_)
			: x1(x1_), y1(y1_), x2(x2_), y2(y2_), radius(r), stops(stops_), matrix(matrix_) {}
	};

	wxD2DRadialGradientBrushResourceHolder(wxDouble& x1, wxDouble& y1, 
										   wxDouble& x2, wxDouble& y2, 
										   wxDouble& r, 
										   const wxGraphicsGradientStops& stops,
										   wxD2DMatrixData* matrix)
		: m_radialGradientInfo(x1, y1, x2, y2, r, stops, matrix) {}

protected:
	void DoAcquireResource() override
	{
		wxD2DGradientStopsHelper helper(m_radialGradientInfo.stops, GetContext());
		ID2D1RadialGradientBrush *radialGradientBrush;

		wxDouble xo = m_radialGradientInfo.x1 - m_radialGradientInfo.x2;
		wxDouble yo = m_radialGradientInfo.y1 - m_radialGradientInfo.y2;

		HRESULT hr = GetContext()->CreateRadialGradientBrush(
			D2D1::RadialGradientBrushProperties(
				D2D1::Point2F(m_radialGradientInfo.x1, m_radialGradientInfo.y1),
				D2D1::Point2F(xo, yo),
				m_radialGradientInfo.radius, m_radialGradientInfo.radius),
			helper.GetGradientStopCollection(),
			&radialGradientBrush);
		wxCHECK_HRESULT_RET(hr);

		if (! m_radialGradientInfo.matrix)
		{
			D2D1::Matrix3x2F matrix = m_radialGradientInfo.matrix->GetMatrix3x2F();
			matrix.Invert();
			radialGradientBrush->SetTransform(matrix);
		}
		m_nativeResource = radialGradientBrush;
	}

private:
	const RadialGradientInfo m_radialGradientInfo;
};

//-----------------------------------------------------------------------------
// wxD2DBrushData declaration
//-----------------------------------------------------------------------------

class wxD2DBrushData : /*public GraphicsObjectRefData, */public wxD2DManagedGraphicsData
{
public:
	wxD2DBrushData(wxD2DRenderer* renderer, const wxBrush brush);

	wxD2DBrushData(wxD2DRenderer* renderer);

	void CreateLinearGradientBrush(wxDouble x1, wxDouble y1, 
								   wxDouble x2, wxDouble y2, 
								   const wxGraphicsGradientStops& stops,
								   wxD2DMatrixData* matrix = NULL);

	void CreateRadialGradientBrush(wxDouble startX, wxDouble startY, 
								   wxDouble endX, wxDouble endY, 
								   wxDouble radius, 
								   const wxGraphicsGradientStops& stops,
								   wxD2DMatrixData* matrix = NULL);

	ID2D1Brush* GetBrush() const
	{
		return (ID2D1Brush*)(m_brushResourceHolder->GetResource());
	}

	wxD2DManagedObject* GetManagedObject() override
	{
		return m_brushResourceHolder.get();
	}

private:
	wxSharedPtr<wxManagedResourceHolder> m_brushResourceHolder;
};

//-----------------------------------------------------------------------------
// wxD2DBrushData implementation
//-----------------------------------------------------------------------------

wxD2DBrushData::wxD2DBrushData(wxD2DRenderer* renderer, const wxBrush brush)
	: /*GraphicsObjectRefData(renderer), */m_brushResourceHolder(NULL)
{
	if (brush.GetStyle() == wxBRUSHSTYLE_SOLID)
	{
		m_brushResourceHolder = new wxD2DSolidBrushResourceHolder(brush);
	}
	else if (brush.IsHatch())
	{
		m_brushResourceHolder = new wxD2DHatchBrushResourceHolder(brush);
	}
	else
	{
		m_brushResourceHolder = new wxD2DBitmapBrushResourceHolder(brush);
	}
}

wxD2DBrushData::wxD2DBrushData(wxD2DRenderer* renderer) 
	: /*GraphicsObjectRefData(renderer), */m_brushResourceHolder(NULL)
{
}

void wxD2DBrushData::CreateLinearGradientBrush(
	wxDouble x1, wxDouble y1,
	wxDouble x2, wxDouble y2,
	const wxGraphicsGradientStops& stops,
	wxD2DMatrixData * matrix)
{
	m_brushResourceHolder = new wxD2DLinearGradientBrushResourceHolder(
		x1, y1, x2, y2, stops, matrix);
}

void wxD2DBrushData::CreateRadialGradientBrush(
	wxDouble startX, wxDouble startY,
	wxDouble endX, wxDouble endY,
	wxDouble radius,
	const wxGraphicsGradientStops& stops,
	wxD2DMatrixData * matrix)
{
	m_brushResourceHolder = new wxD2DRadialGradientBrushResourceHolder(
		startX, startY, endX, endY, radius, stops, matrix);
}

//wxD2DBrushData* wxGetD2DBrushData(const wxGraphicsBrush& brush)
//{
//    return static_cast<wxD2DBrushData*>(brush.GetGraphicsData());
//}

bool wxIsHatchPenStyle(wxPenStyle penStyle)
{
	return penStyle >= wxPENSTYLE_FIRST_HATCH && penStyle <= wxPENSTYLE_LAST_HATCH;
}

wxBrushStyle wxConvertPenStyleToBrushStyle(wxPenStyle penStyle)
{
	switch(penStyle)
	{
	case wxPENSTYLE_BDIAGONAL_HATCH:
		return wxBRUSHSTYLE_BDIAGONAL_HATCH;
	case wxPENSTYLE_CROSSDIAG_HATCH:
		return wxBRUSHSTYLE_CROSSDIAG_HATCH;
	case wxPENSTYLE_FDIAGONAL_HATCH:
		return wxBRUSHSTYLE_FDIAGONAL_HATCH;
	case wxPENSTYLE_CROSS_HATCH:
		return wxBRUSHSTYLE_CROSS_HATCH;
	case wxPENSTYLE_HORIZONTAL_HATCH:
		return wxBRUSHSTYLE_HORIZONTAL_HATCH;
	case wxPENSTYLE_VERTICAL_HATCH:
		return wxBRUSHSTYLE_VERTICAL_HATCH;
	default:
		break;
	}

	return wxBRUSHSTYLE_SOLID;
}

// ----------------------------------------------------------------------------
// wxGraphicsPenInfo describes a wxGraphicsPen
// ----------------------------------------------------------------------------

class wxGraphicsPenInfo : public wxPenInfoBase<wxGraphicsPenInfo>
{
public:
	explicit wxGraphicsPenInfo(const wxColour& colour = wxColour(),
		wxDouble width = 1.0,
		wxPenStyle style = wxPENSTYLE_SOLID)
		: wxPenInfoBase<wxGraphicsPenInfo>(colour, style)
	{
		m_width = width;
		m_gradientType = wxGRADIENT_NONE;
	}

	// Setters

	wxGraphicsPenInfo& Width(wxDouble width)
	{
		m_width = width; return *this;
	}

	wxGraphicsPenInfo&
		LinearGradient(wxDouble x1, wxDouble y1, wxDouble x2, wxDouble y2,
		const wxColour& c1, const wxColour& c2,
		wxD2DMatrixData *  matrix = NULL)
	{
		m_gradientType = wxGRADIENT_LINEAR;
		m_x1 = x1;
		m_y1 = y1;
		m_x2 = x2;
		m_y2 = y2;
		m_stops.SetStartColour(c1);
		m_stops.SetEndColour(c2);
		m_matrix = matrix;
		return *this;
	}

	wxGraphicsPenInfo&
		LinearGradient(wxDouble x1, wxDouble y1, wxDouble x2, wxDouble y2,
		const wxGraphicsGradientStops& stops,
		wxD2DMatrixData *  matrix = NULL)
	{
		m_gradientType = wxGRADIENT_LINEAR;
		m_x1 = x1;
		m_y1 = y1;
		m_x2 = x2;
		m_y2 = y2;
		m_stops = stops;
		m_matrix = matrix;
		return *this;
	}

	wxGraphicsPenInfo&
		RadialGradient(wxDouble startX, wxDouble startY,
		wxDouble endX, wxDouble endY, wxDouble radius,
		const wxColour& oColor, const wxColour& cColor,
		wxD2DMatrixData * matrix = NULL)
	{
		m_gradientType = wxGRADIENT_RADIAL;
		m_x1 = startX;
		m_y1 = startY;
		m_x2 = endX;
		m_y2 = endY;
		m_radius = radius;
		m_stops.SetStartColour(oColor);
		m_stops.SetEndColour(cColor);
		m_matrix = matrix;
		return *this;
	}

	wxGraphicsPenInfo&
		RadialGradient(wxDouble startX, wxDouble startY,
		wxDouble endX, wxDouble endY,
		wxDouble radius, const wxGraphicsGradientStops& stops,
		wxD2DMatrixData * matrix = NULL)
	{
		m_gradientType = wxGRADIENT_RADIAL;
		m_x1 = startX;
		m_y1 = startY;
		m_x2 = endX;
		m_y2 = endY;
		m_radius = radius;
		m_stops = stops;
		m_matrix = matrix;
		return *this;
	}

	// Accessors

	wxDouble GetWidth() const { return m_width; }
	wxGradientType GetGradientType() const { return m_gradientType; }
	wxDouble GetX1() const { return m_x1; }
	wxDouble GetY1() const { return m_y1; }
	wxDouble GetX2() const { return m_x2; }
	wxDouble GetY2() const { return m_y2; }
	wxDouble GetStartX() const { return m_x1; }
	wxDouble GetStartY() const { return m_y1; }
	wxDouble GetEndX() const { return m_x2; }
	wxDouble GetEndY() const { return m_y2; }
	wxDouble GetRadius() const { return m_radius; }
	const wxGraphicsGradientStops& GetStops() const { return m_stops; }
	wxD2DMatrixData * GetMatrix() const { return m_matrix; }

private:
	wxDouble m_width;
	wxGradientType m_gradientType;
	wxDouble m_x1, m_y1, m_x2, m_y2; // also used for m_xo, m_yo, m_xc, m_yc
	wxDouble m_radius;
	wxGraphicsGradientStops m_stops;
	wxD2DMatrixData * m_matrix = NULL;
};

//-----------------------------------------------------------------------------
// wxD2DPenData declaration
//-----------------------------------------------------------------------------

class wxD2DPenData : /*public GraphicsObjectRefData, */public wxD2DManagedGraphicsData
{
public:
	wxD2DPenData(wxD2DRenderer* renderer,
				 ID2D1Factory* direct2dFactory,
				 const wxGraphicsPenInfo& info);


	void CreateStrokeStyle(ID2D1Factory* const direct2dfactory);

	ID2D1Brush* GetBrush();

	FLOAT GetWidth();

	ID2D1StrokeStyle* GetStrokeStyle();

	wxD2DManagedObject* GetManagedObject() override
	{
		return m_stippleBrush->GetManagedObject();
	}

private:
	// We store the original pen description for later when we need to recreate
	// the device-dependent resources.
	const wxGraphicsPenInfo m_penInfo;

	// A stroke style is a device-independent resource.
	// Describes the caps, miter limit, line join, and dash information.
	wxCOMPtr<ID2D1StrokeStyle> m_strokeStyle;

	// Drawing outlines with Direct2D requires a brush for the color or stipple.
	wxSharedPtr<wxD2DBrushData> m_stippleBrush;

	// The width of the stroke
	FLOAT m_width;
};



//-----------------------------------------------------------------------------
// wxD2DPenData implementation
//-----------------------------------------------------------------------------

wxD2DPenData::wxD2DPenData(
	wxD2DRenderer* renderer,
	ID2D1Factory* direct2dFactory,
	const wxGraphicsPenInfo& info)
	: /*GraphicsObjectRefData(renderer),*/
	  m_penInfo(info),
	  m_width(info.GetWidth())
{
	CreateStrokeStyle(direct2dFactory);

	wxBrush strokeBrush;

	if (m_penInfo.GetStyle() == wxPENSTYLE_STIPPLE)
	{
		strokeBrush.SetStipple(m_penInfo.GetStipple());
		strokeBrush.SetStyle(wxBRUSHSTYLE_STIPPLE);
	}
	else if(wxIsHatchPenStyle(m_penInfo.GetStyle()))
	{
		strokeBrush.SetStyle(wxConvertPenStyleToBrushStyle(m_penInfo.GetStyle()));
		strokeBrush.SetColour(m_penInfo.GetColour());
	}
	else
	{
		strokeBrush.SetColour(m_penInfo.GetColour());
		strokeBrush.SetStyle(wxBRUSHSTYLE_SOLID);
	}

	switch ( m_penInfo.GetGradientType() )
	{
	case wxGRADIENT_NONE:
		m_stippleBrush = new wxD2DBrushData(renderer, strokeBrush);
		break;

	case wxGRADIENT_LINEAR:
		m_stippleBrush = new wxD2DBrushData(renderer);
		m_stippleBrush->CreateLinearGradientBrush(
								m_penInfo.GetX1(), m_penInfo.GetY1(),
								m_penInfo.GetX2(), m_penInfo.GetY2(),
								m_penInfo.GetStops(),
								m_penInfo.GetMatrix());
		break;

	case wxGRADIENT_RADIAL:
		m_stippleBrush = new wxD2DBrushData(renderer);
		m_stippleBrush->CreateRadialGradientBrush(
								m_penInfo.GetStartX(), m_penInfo.GetStartY(),
								m_penInfo.GetEndX(), m_penInfo.GetEndY(),
								m_penInfo.GetRadius(),
								m_penInfo.GetStops(),
								m_penInfo.GetMatrix());
		break;
	}
}


void wxD2DPenData::CreateStrokeStyle(ID2D1Factory* const direct2dfactory)
{
	D2D1_CAP_STYLE capStyle = wxD2DConvertPenCap(m_penInfo.GetCap());
	D2D1_LINE_JOIN lineJoin = wxD2DConvertPenJoin(m_penInfo.GetJoin());
	D2D1_DASH_STYLE dashStyle = wxD2DConvertPenStyle(m_penInfo.GetStyle());

	int dashCount = 0;
	FLOAT* dashes = NULL;

	if (dashStyle == D2D1_DASH_STYLE_CUSTOM)
	{
		dashCount = m_penInfo.GetDashCount();
		dashes = new FLOAT[dashCount];

		for (int i = 0; i < dashCount; ++i)
		{
			dashes[i] = m_penInfo.GetDash()[i];
		}

	}

	direct2dfactory->CreateStrokeStyle(
		D2D1::StrokeStyleProperties(capStyle, capStyle, capStyle, lineJoin, 0, dashStyle, 0.0f),
		dashes, dashCount,
		&m_strokeStyle);

	delete[] dashes;
}

ID2D1Brush* wxD2DPenData::GetBrush()
{
	return m_stippleBrush->GetBrush();
}

FLOAT wxD2DPenData::GetWidth()
{
	return m_width;
}

ID2D1StrokeStyle* wxD2DPenData::GetStrokeStyle()
{
	return m_strokeStyle;
}

//wxD2DPenData* wxGetD2DPenData(const wxGraphicsPen& pen)
//{
//    return static_cast<wxD2DPenData*>(pen.GetGraphicsData());
//}

class wxD2DFontData/* : public GraphicsObjectRefData*/// : public wxD2DManagedGraphicsData
{
public:
	wxD2DFontData(wxD2DRenderer* renderer, const wxFont& font, const wxRealPoint& dpi, const wxColor& color);

	wxCOMPtr<IDWriteTextLayout> CreateTextLayout(const wxString& text) const;

	wxD2DBrushData& GetBrushData() { return m_brushData; }

	wxCOMPtr<IDWriteTextFormat> GetTextFormat() const { return m_textFormat; }

	wxCOMPtr<IDWriteFont> GetFont() { return m_font; }

private:
	// The native, device-independent font object
	wxCOMPtr<IDWriteFont> m_font;

	// The native, device-independent font object
	wxCOMPtr<IDWriteTextFormat> m_textFormat;

	// We use a color brush to render the font
	wxD2DBrushData m_brushData;

	bool m_underlined;

	bool m_strikethrough;
};

wxD2DFontData::wxD2DFontData(wxD2DRenderer* renderer, const wxFont& font, const wxRealPoint& dpi, const wxColor& color) :
	/*GraphicsObjectRefData(renderer), */m_brushData(renderer, wxBrush(color)),
	m_underlined(font.GetUnderlined()), m_strikethrough(font.GetStrikethrough())
{
	HRESULT hr;

	wxCOMPtr<IDWriteGdiInterop> gdiInterop;
	hr = wxDWriteFactory()->GetGdiInterop(&gdiInterop);
	wxCHECK_HRESULT_RET(hr);

	LOGFONTW logfont;
	int n = GetObjectW(font.GetHFONT(), sizeof(logfont), &logfont);
	wxCHECK_RET( n > 0, wxS("Failed to obtain font info") );

	// Ensure the LOGFONT object contains the correct font face name
	if (logfont.lfFaceName[0] == L'\0')
	{
		// The length of the font name must not exceed LF_FACESIZE TCHARs,
		// including the terminating NULL.
		wxString name = font.GetFaceName().Mid(0, WXSIZEOF(logfont.lfFaceName)-1);
		for (unsigned int i = 0; i < name.Length(); ++i)
		{
			logfont.lfFaceName[i] = name.GetChar(i);
		}
		logfont.lfFaceName[name.Length()] = L'\0';
	}

	hr = gdiInterop->CreateFontFromLOGFONT(&logfont, &m_font);
	if ( hr == DWRITE_E_NOFONT )
	{
		// It was attempted to create DirectWrite font from non-TrueType GDI font.
		return;
	}

	wxCHECK_RET( SUCCEEDED(hr),
				 wxString::Format("Failed to create font '%s' (HRESULT = %x)", logfont.lfFaceName, hr) );

	wxCOMPtr<IDWriteFontFamily> fontFamily;
	hr = m_font->GetFontFamily(&fontFamily);
	wxCHECK_HRESULT_RET(hr);

	wxCOMPtr<IDWriteLocalizedStrings> familyNames;
	hr = fontFamily->GetFamilyNames(&familyNames);
	wxCHECK_HRESULT_RET(hr);

	UINT32 length;
	hr = familyNames->GetStringLength(0, &length);
	wxCHECK_HRESULT_RET(hr);

	wchar_t* name = new wchar_t[length+1];
	hr = familyNames->GetString(0, name, length+1);
	wxCHECK_HRESULT_RET(hr);

	FLOAT fontSize = (FLOAT)(!dpi.y
		? font.GetPixelSize().GetHeight()
		: (font.GetPointSize()/*.GetFractionalPointSize()*/ * dpi.y / 72.0f));

	hr = wxDWriteFactory()->CreateTextFormat(
		name,
		NULL,
		m_font->GetWeight(),
		m_font->GetStyle(),
		m_font->GetStretch(),
		fontSize,
		L"en-us",
		&m_textFormat);

	delete[] name;

	wxCHECK_HRESULT_RET(hr);
}

wxCOMPtr<IDWriteTextLayout> wxD2DFontData::CreateTextLayout(const wxString& text) const
{
	static const FLOAT MAX_WIDTH = FLT_MAX;
	static const FLOAT MAX_HEIGHT = FLT_MAX;

	HRESULT hr;

	wxCOMPtr<IDWriteTextLayout> textLayout;

	hr = wxDWriteFactory()->CreateTextLayout(
		text.c_str(),
		text.length(),
		m_textFormat,
		MAX_WIDTH,
		MAX_HEIGHT,
		&textLayout);
	wxCHECK2_HRESULT_RET(hr, wxCOMPtr<IDWriteTextLayout>(NULL));

	DWRITE_TEXT_RANGE textRange = { 0, (UINT32) text.length() };

	if (m_underlined)
	{
		textLayout->SetUnderline(true, textRange);
	}

	if (m_strikethrough)
	{
		textLayout->SetStrikethrough(true, textRange);
	}

	return textLayout;
}

//wxD2DFontData* wxGetD2DFontData(const wxGraphicsFont& font)
//{
//    return static_cast<wxD2DFontData*>(font.GetGraphicsData());
//}

// A render target resource holder exposes methods relevant
// for native render targets such as resize
class wxD2DRenderTargetResourceHolder : public wxD2DResourceHolder<ID2D1RenderTarget>
{
public:
	// This method is called when an external event signals the underlying DC
	// is resized (e.g. the resizing of a window). Some implementations can leave
	// this method empty, while others must adjust the render target size to match
	// the underlying DC.
	virtual void Resize()
	{
	}

	// We use this method instead of the one provided by the native render target
	// because Direct2D 1.0 render targets do not accept a composition mode
	// parameter, while the device context in Direct2D 1.1 does. This way, we make
	// best use of the capabilities of each render target.
	//
	// The default implementation works for all render targets, but the D2D 1.0
	// render target holders shouldn't need to override it, since none of the
	// 1.0 render targets offer a better version of this method.
	virtual void DrawBitmap(ID2D1Bitmap* bitmap, D2D1_POINT_2F offset,
		D2D1_RECT_F imageRectangle, wxInterpolationQuality interpolationQuality,
		wxCompositionMode WXUNUSED(compositionMode))
	{
		D2D1_RECT_F destinationRectangle = D2D1::RectF(offset.x, offset.y, offset.x + imageRectangle.right, offset.y + imageRectangle.bottom);
		m_nativeResource->DrawBitmap(
			bitmap,
			destinationRectangle,
			1.0f,
			wxD2DConvertBitmapInterpolationMode(interpolationQuality),
			imageRectangle);
	}

	// We use this method instead of the one provided by the native render target
	// because some contexts might require writing to a buffer (e.g. an image
	// context), and some render targets might require additional operations to
	// be executed (e.g. the device context must present the swap chain)
	virtual HRESULT Flush()
	{
		return m_nativeResource->Flush();
	}

	// Composition is not supported at in D2D 1.0, and we only allow for:
	// wxCOMPOSITION_DEST - which is essentially a no-op and is handled
	//                      externally by preventing any draw calls.
	// wxCOMPOSITION_OVER - which copies the source over the destination using
	//                      alpha blending. This is the default way D2D 1.0
	//                      draws images.
	virtual bool SetCompositionMode(wxCompositionMode compositionMode)
	{
		if (compositionMode == wxCOMPOSITION_DEST ||
			compositionMode == wxCOMPOSITION_OVER)
		{
			// There's nothing we can do but notify the caller the composition
			// mode is supported
			return true;
		}

		return false;
	}
};

#if wxUSE_IMAGE
class wxD2DImageRenderTargetResourceHolder : public wxD2DRenderTargetResourceHolder
{
public:
	wxD2DImageRenderTargetResourceHolder(wxImage* image, ID2D1Factory* factory) :
		m_resultImage(image), m_factory(factory)
	{
	}

	HRESULT Flush() override
	{
		HRESULT hr = m_nativeResource->Flush();
		FlushRenderTargetToImage();
		return hr;
	}

	~wxD2DImageRenderTargetResourceHolder()
	{
		FlushRenderTargetToImage();
	}

protected:
	void DoAcquireResource() override
	{
		HRESULT hr;

		// Create a compatible WIC Bitmap
		hr = wxWICImagingFactory()->CreateBitmap(
			m_resultImage->GetWidth(),
			m_resultImage->GetHeight(),
			GUID_WICPixelFormat32bppPBGRA,
			WICBitmapCacheOnDemand,
			&m_wicBitmap);
		wxCHECK_HRESULT_RET(hr);

		// Copy contents of source image to the WIC bitmap.
		const int width = m_resultImage->GetWidth();
		const int height = m_resultImage->GetHeight();
		WICRect rcLock = { 0, 0, width, height };
		IWICBitmapLock *pLock = NULL;
		hr = m_wicBitmap->Lock(&rcLock, WICBitmapLockWrite, &pLock);
		wxCHECK_HRESULT_RET(hr);

		UINT rowStride = 0;
		hr = pLock->GetStride(&rowStride);
		if ( FAILED(hr) )
		{
			pLock->Release();
			wxFAILED_HRESULT_MSG(hr);
			return;
		}

		UINT bufferSize = 0;
		BYTE *pBmpBuffer = NULL;
		hr = pLock->GetDataPointer(&bufferSize, &pBmpBuffer);
		if ( FAILED(hr) )
		{
			pLock->Release();
			wxFAILED_HRESULT_MSG(hr);
			return;
		}

		const unsigned char *imgRGB = m_resultImage->GetData();    // source RGB buffer
		const unsigned char *imgAlpha = m_resultImage->GetAlpha(); // source alpha buffer
		for( int y = 0; y < height; y++ )
		{
			BYTE *pPixByte = pBmpBuffer;
			for ( int x = 0; x < width; x++ )
			{
				unsigned char r = *imgRGB++;
				unsigned char g = *imgRGB++;
				unsigned char b = *imgRGB++;
				unsigned char a = imgAlpha ? *imgAlpha++ : 255;
				// Premultiply RGB values
				*pPixByte++ = (b * a + 127) / 255;
				*pPixByte++ = (g * a + 127) / 255;
				*pPixByte++ = (r * a + 127) / 255;
				*pPixByte++ = a;
			}

			pBmpBuffer += rowStride;
		}

		pLock->Release();

		// Create the render target
		hr = m_factory->CreateWicBitmapRenderTarget(
			m_wicBitmap,
			D2D1::RenderTargetProperties(
				D2D1_RENDER_TARGET_TYPE_SOFTWARE,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
			&m_nativeResource);
		wxCHECK_HRESULT_RET(hr);
	}

private:
	void FlushRenderTargetToImage()
	{
		const int width = m_resultImage->GetWidth();
		const int height = m_resultImage->GetHeight();

		WICRect rcLock = { 0, 0, width, height };
		IWICBitmapLock *pLock = NULL;
		HRESULT hr = m_wicBitmap->Lock(&rcLock, WICBitmapLockRead, &pLock);
		wxCHECK_HRESULT_RET(hr);

		UINT rowStride = 0;
		hr = pLock->GetStride(&rowStride);
		if ( FAILED(hr) )
		{
			pLock->Release();
			wxFAILED_HRESULT_MSG(hr);
			return;
		}

		UINT bufferSize = 0;
		BYTE *pBmpBuffer = NULL;
		hr = pLock->GetDataPointer(&bufferSize, &pBmpBuffer);
		if ( FAILED(hr) )
		{
			pLock->Release();
			wxFAILED_HRESULT_MSG(hr);
			return;
		}

		WICPixelFormatGUID pixelFormat;
		hr = pLock->GetPixelFormat(&pixelFormat);
		if ( FAILED(hr) )
		{
			pLock->Release();
			wxFAILED_HRESULT_MSG(hr);
			return;
		}
		wxASSERT_MSG( pixelFormat == GUID_WICPixelFormat32bppPBGRA ||
				  pixelFormat == GUID_WICPixelFormat32bppBGR,
				  wxS("Unsupported pixel format") );

		// Only premultiplied ARGB bitmaps are supported.
		const bool hasAlpha = pixelFormat == GUID_WICPixelFormat32bppPBGRA;

		unsigned char* destRGB = m_resultImage->GetData();
		unsigned char* destAlpha = m_resultImage->GetAlpha();
		for( int y = 0; y < height; y++ )
		{
			BYTE *pPixByte = pBmpBuffer;
			for ( int x = 0; x < width; x++ )
			{
				wxPBGRAColor color = wxPBGRAColor(pPixByte);
				unsigned char a =  hasAlpha ? color.a : 255;
				// Undo premultiplication for ARGB bitmap
				*destRGB++ = (a > 0 && a < 255) ? ( color.r * 255 ) / a : color.r;
				*destRGB++ = (a > 0 && a < 255) ? ( color.g * 255 ) / a : color.g;
				*destRGB++ = (a > 0 && a < 255) ? ( color.b * 255 ) / a : color.b;
				if ( destAlpha )
					*destAlpha++ = a;

				pPixByte += 4;
			}

			pBmpBuffer += rowStride;
		}

		pLock->Release();
   }

private:
	wxImage* m_resultImage;
	wxCOMPtr<IWICBitmap> m_wicBitmap;

	ID2D1Factory* m_factory;
};
#endif // wxUSE_IMAGE

class wxD2DHwndRenderTargetResourceHolder : public wxD2DRenderTargetResourceHolder
{
public:
	typedef ID2D1HwndRenderTarget* ImplementationType;

	wxD2DHwndRenderTargetResourceHolder(HWND hwnd, ID2D1Factory* factory) :
		m_hwnd(hwnd), m_factory(factory)
	{
	}

	void Resize() override
	{
		RECT clientRect;
		GetClientRect(m_hwnd, &clientRect);

		D2D1_SIZE_U hwndSize = D2D1::SizeU(
			clientRect.right - clientRect.left,
			clientRect.bottom - clientRect.top);

		D2D1_SIZE_U renderTargetSize = GetRenderTarget()->GetPixelSize();

		if (hwndSize.width != renderTargetSize.width || hwndSize.height != renderTargetSize.height)
		{
			GetRenderTarget()->Resize(hwndSize);
		}
	}

protected:
	void DoAcquireResource() override
	{
		wxCOMPtr<ID2D1HwndRenderTarget> renderTarget;

		HRESULT result;

		RECT clientRect;
		GetClientRect(m_hwnd, &clientRect);

		D2D1_SIZE_U size = D2D1::SizeU(
			clientRect.right - clientRect.left,
			clientRect.bottom - clientRect.top);

		result = m_factory->CreateHwndRenderTarget(
			D2D1::RenderTargetProperties(),
			D2D1::HwndRenderTargetProperties(m_hwnd, size),
			&renderTarget);

		if (FAILED(result))
		{
			wxFAIL_MSG("Could not create Direct2D render target");
		}

		renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

		m_nativeResource = renderTarget;
	}

private:
	// Converts the underlying resource pointer of type
	// ID2D1RenderTarget* to the actual implementation type
	ImplementationType GetRenderTarget()
	{
		return static_cast<ImplementationType>(GetD2DResource().get());
	}

private:
	HWND m_hwnd;
	ID2D1Factory* m_factory;
};

#if wxD2D_DEVICE_CONTEXT_SUPPORTED
class wxD2DDeviceContextResourceHolder : public wxD2DRenderTargetResourceHolder
{
public:
	wxD2DDeviceContextResourceHolder(ID2D1Factory* factory, HWND hwnd) :
		m_factory(NULL), m_hwnd(hwnd)
	{
		HRESULT hr = factory->QueryInterface(IID_ID2D1Factory1, (void**)&m_factory);
		wxCHECK_HRESULT_RET(hr);
	}

	void DrawBitmap(ID2D1Bitmap* image, D2D1_POINT_2F offset,
		D2D1_RECT_F imageRectangle, wxInterpolationQuality interpolationQuality,
		wxCompositionMode compositionMode) override
	{
		m_context->DrawImage(image,
			offset,
			imageRectangle,
			wxD2DConvertInterpolationMode(interpolationQuality),
			wxD2DConvertCompositionMode(compositionMode));
	}

	HRESULT Flush() override
	{
		HRESULT hr = m_nativeResource->Flush();
		DXGI_PRESENT_PARAMETERS params = { 0 };
		m_swapChain->Present1(1, 0, &params);
		return hr;
	}

protected:

	// Adapted from http://msdn.microsoft.com/en-us/library/windows/desktop/hh780339%28v=vs.85%29.aspx
	void DoAcquireResource() override
	{
		HRESULT hr;

		// This flag adds support for surfaces with a different color channel ordering than the API default.
		// You need it for compatibility with Direct2D.
		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

		// This array defines the set of DirectX hardware feature levels this app  supports.
		// The ordering is important and you should  preserve it.
		// Don't forget to declare your app's minimum required feature level in its
		// description.  All apps are assumed to support 9.1 unless otherwise stated.
		D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
			D3D_FEATURE_LEVEL_9_3,
			D3D_FEATURE_LEVEL_9_2,
			D3D_FEATURE_LEVEL_9_1
		};

		// Create the DX11 API device object, and get a corresponding context.
		wxCOMPtr<ID3D11Device> device;
		wxCOMPtr<ID3D11DeviceContext> context;

		hr = D3D11CreateDevice(
			NULL,                    // specify null to use the default adapter
			D3D_DRIVER_TYPE_HARDWARE,
			0,
			creationFlags,              // optionally set debug and Direct2D compatibility flags
			featureLevels,              // list of feature levels this app can support
			ARRAYSIZE(featureLevels),   // number of possible feature levels
			D3D11_SDK_VERSION,
			&device,                    // returns the Direct3D device created
			&m_featureLevel,            // returns feature level of device created
			&context);                  // returns the device immediate context
		wxCHECK_HRESULT_RET(hr);

		// Obtain the underlying DXGI device of the Direct3D11 device.
		hr = device->QueryInterface(IID_IDXGIDevice, (void**)&m_dxgiDevice);
		wxCHECK_HRESULT_RET(hr);

		// Obtain the Direct2D device for 2-D rendering.
		hr = m_factory->CreateDevice(m_dxgiDevice, &m_device);
		wxCHECK_HRESULT_RET(hr);

		// Get Direct2D device's corresponding device context object.
		hr = m_device->CreateDeviceContext(
			D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
			&m_context);
		wxCHECK_HRESULT_RET(hr);

		m_nativeResource = m_context;

		AttachSurface();
	}

private:
	void AttachSurface()
	{
		HRESULT hr;

		// Allocate a descriptor.
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {0};
		swapChainDesc.Width = 0;
		swapChainDesc.Height = 0;
		swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		swapChainDesc.Stereo = false;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapChainDesc.Flags = 0;

		// Identify the physical adapter (GPU or card) this device is runs on.
		wxCOMPtr<IDXGIAdapter> dxgiAdapter;
		hr = m_dxgiDevice->GetAdapter(&dxgiAdapter);
		wxCHECK_HRESULT_RET(hr);

		// Get the factory object that created the DXGI device.
		wxCOMPtr<IDXGIFactory2> dxgiFactory;
		hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
		wxCHECK_HRESULT_RET(hr);

		// Get the final swap chain for this window from the DXGI factory.
		hr = dxgiFactory->CreateSwapChainForHwnd(
			m_dxgiDevice,
			m_hwnd,
			&swapChainDesc,
			NULL,    // allow on all displays
			NULL,
			&m_swapChain);
		wxCHECK_HRESULT_RET(hr);

		// Ensure that DXGI doesn't queue more than one frame at a time.
		hr = m_dxgiDevice->SetMaximumFrameLatency(1);
		wxCHECK_HRESULT_RET(hr);

		// Get the backbuffer for this window which is be the final 3D render target.
		wxCOMPtr<ID3D11Texture2D> backBuffer;
		hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		wxCHECK_HRESULT_RET(hr);

		FLOAT dpiX, dpiY;
		m_factory->GetDesktopDpi(&dpiX, &dpiY);

		// Now we set up the Direct2D render target bitmap linked to the swapchain.
		// Whenever we render to this bitmap, it is directly rendered to the
		// swap chain associated with the window.
		D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
			D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
			dpiX, dpiY);

		// Direct2D needs the dxgi version of the backbuffer surface pointer.
		wxCOMPtr<IDXGISurface> dxgiBackBuffer;
		hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer));
		wxCHECK_HRESULT_RET(hr);

		// Get a D2D surface from the DXGI back buffer to use as the D2D render target.
		hr = m_context->CreateBitmapFromDxgiSurface(
			dxgiBackBuffer.get(),
			&bitmapProperties,
			&m_targetBitmap);
		wxCHECK_HRESULT_RET(hr);

		// Now we can set the Direct2D render target.
		m_context->SetTarget(m_targetBitmap);
	}

	~wxD2DDeviceContextResourceHolder()
	{
		DXGI_PRESENT_PARAMETERS params = { 0 };
		m_swapChain->Present1(1, 0, &params);
	}

private:
	ID2D1Factory1* m_factory;

	HWND m_hwnd;

	D3D_FEATURE_LEVEL m_featureLevel;
	wxCOMPtr<IDXGIDevice1> m_dxgiDevice;
	wxCOMPtr<ID2D1Device> m_device;
	wxCOMPtr<ID2D1DeviceContext> m_context;
	wxCOMPtr<ID2D1Bitmap1> m_targetBitmap;
	wxCOMPtr<IDXGISwapChain1> m_swapChain;
};
#endif

class wxD2DDCRenderTargetResourceHolder : public wxD2DRenderTargetResourceHolder
{
public:
	wxD2DDCRenderTargetResourceHolder(ID2D1Factory* factory, HDC hdc, D2D1_ALPHA_MODE alphaMode) :
		m_factory(factory), m_hdc(hdc), m_alphaMode(alphaMode)
	{
	}

protected:
	void DoAcquireResource() override
	{
		wxCOMPtr<ID2D1DCRenderTarget> renderTarget;
		D2D1_RENDER_TARGET_PROPERTIES renderTargetProperties = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, m_alphaMode));

		HRESULT hr = m_factory->CreateDCRenderTarget(
			&renderTargetProperties,
			&renderTarget);
		wxCHECK_HRESULT_RET(hr);

		// We want draw on the entire device area.
		// GetClipBox() retrieves logical size of DC
		// what is what we need to pass to BindDC.
		RECT r;
		int status = ::GetClipBox(m_hdc, &r);
		wxCHECK_RET( status != ERROR, wxS("Error retrieving DC dimensions") );

		hr = renderTarget->BindDC(m_hdc, &r);
		wxCHECK_HRESULT_RET(hr);
		renderTarget->SetTransform(
					   D2D1::Matrix3x2F::Translation(-r.left, -r.top));

		m_nativeResource = renderTarget;
	}

private:
	ID2D1Factory* m_factory;
	HDC m_hdc;
	D2D1_ALPHA_MODE m_alphaMode;
};

// The null context has no state of its own and does nothing.
// It is only used as a base class for the lightweight
// measuring context. The measuring context cannot inherit from
// the default implementation wxD2DContext, because some methods
// from wxD2DContext require the presence of a "context"
// (render target) in order to acquire various device-dependent
// resources. Without a proper context, those methods would fail.
// The methods implemented in the null context are fundamentally no-ops.
class wxNullContext : public GraphicsContext
{
public:
	wxNullContext(wxD2DRenderer* _renderer) : GraphicsContext(){ renderer = _renderer; }
	~wxNullContext(){ wxDELETE(m_font); }
	void GetTextExtent(const wxString&, wxDouble*, wxDouble*, wxDouble*, wxDouble*) const {}
	void GetPartialTextExtents(const wxString&, wxArrayDouble&) const {}
	void Clip(const wxRegion&) {}
	void Clip(wxDouble, wxDouble, wxDouble, wxDouble) {}
	void ResetClip() {}
	void GetClipBox(wxDouble*, wxDouble*, wxDouble*, wxDouble*) {}
	void* GetNativeContext() { return NULL; }
	bool SetAntialiasMode(wxAntialiasMode) { return false; }
	bool SetInterpolationQuality(wxInterpolationQuality) { return false; }
	bool SetCompositionMode(wxCompositionMode) { return false; }
	void BeginLayer(wxDouble) {}
	void EndLayer() {}
	void Translate(wxDouble, wxDouble) {}
	void Scale(wxDouble, wxDouble) {}
	void Rotate(wxDouble) {}
	void ConcatTransform(const wxGraphicsMatrix&) {}
	void SetTransform(const wxGraphicsMatrix&) {}
	wxD2DMatrixData *GetTransform() const { return NULL; }
	void StrokePath(const wxGraphicsPath&) {}
	void FillPath(const wxGraphicsPath&, wxPolygonFillMode) {}
	void DrawBitmap(const wxGraphicsBitmap&, wxDouble, wxDouble, wxDouble, wxDouble) {}
	void DrawBitmap(const wxBitmap&, wxDouble, wxDouble, wxDouble, wxDouble) {}
	void DrawIcon(const wxIcon&, wxDouble, wxDouble, wxDouble, wxDouble) {}
	void PushState() {}
	void PopState() {}
	void Flush() {}
	void SetFont(const wxFont& font, const wxColour& col);

protected:
	void DoDrawText(const wxString&, wxDouble, wxDouble) {}
	wxD2DFontData *m_font = NULL;
	wxD2DRenderer *renderer;
};



class wxD2DMeasuringContext : public wxNullContext
{
public:
	wxD2DMeasuringContext(wxD2DRenderer* renderer) : wxNullContext(renderer) {}

	void GetTextExtent(const wxString& str, wxDouble* width, wxDouble* height, wxDouble* descent, wxDouble* externalLeading) const
	{
		GetTextExtent(m_font, str, width, height, descent, externalLeading);
	}

	void GetPartialTextExtents(const wxString& text, wxArrayDouble& widths) const
	{
		GetPartialTextExtents(m_font, text, widths);
	}

	static void GetPartialTextExtents(wxD2DFontData* fontData, const wxString& text, wxArrayDouble& widths)
	{
		for (unsigned int i = 0; i < text.Length(); ++i)
		{
			wxDouble width;
			GetTextExtent(fontData, text.SubString(0, i), &width, NULL, NULL, NULL);
			widths.push_back(width);
		}
	}

	static void GetTextExtent(wxD2DFontData* fontData, const wxString& str, wxDouble* width, wxDouble* height, wxDouble* descent, wxDouble* externalLeading)
	{
		if (!fontData)
			return;

		wxCOMPtr<IDWriteTextLayout> textLayout = fontData->CreateTextLayout(str);
		wxCOMPtr<IDWriteFont> font = fontData->GetFont();

		DWRITE_TEXT_METRICS textMetrics;
		textLayout->GetMetrics(&textMetrics);

		DWRITE_FONT_METRICS fontMetrics;
		font->GetMetrics(&fontMetrics);

		FLOAT ratio = fontData->GetTextFormat()->GetFontSize() / (FLOAT)fontMetrics.designUnitsPerEm;

		if (width != NULL) *width = textMetrics.widthIncludingTrailingWhitespace;
		if (height != NULL) *height = textMetrics.height;

		if (descent != NULL) *descent = fontMetrics.descent * ratio;
		if (externalLeading != NULL) *externalLeading = wxMax(0.0f, (fontMetrics.ascent + fontMetrics.descent) * ratio - textMetrics.height);
	}
};



//-----------------------------------------------------------------------------
// wxD2DContext declaration
//-----------------------------------------------------------------------------

class wxD2DContext : public GraphicsContext, wxD2DResourceManager
{
private:
	wxD2DRenderer* m_renderer;
public:
	// Create the context for the given HWND, which may be associated (if it's
	// non-null) with the given wxWindow.
	wxD2DContext(wxD2DRenderer* renderer,
				 ID2D1Factory* direct2dFactory,
				 HWND hwnd,
				 wxWindow* window = NULL);

	// Create the context for the given HDC which may be associated (if it's
	// non-null) with the given wxDC.
	wxD2DContext(wxD2DRenderer* renderer,
				 ID2D1Factory* direct2dFactory,
				 HDC hdc,
				 const wxDC* dc = NULL,
				 D2D1_ALPHA_MODE alphaMode = D2D1_ALPHA_MODE_IGNORE);

#if wxUSE_IMAGE
	wxD2DContext(wxD2DRenderer* renderer, ID2D1Factory* direct2dFactory, wxImage& image);
#endif // wxUSE_IMAGE

	wxD2DContext(wxD2DRenderer* renderer, ID2D1Factory* direct2dFactory, void* nativeContext);

	~wxD2DContext();

	void Clip(const wxRegion& region);

	void Clip(wxDouble x, wxDouble y, wxDouble w, wxDouble h);

	void ResetClip();

	void GetClipBox(wxDouble* x, wxDouble* y, wxDouble* w, wxDouble* h);

	// The native context used by wxD2DContext is a Direct2D render target.
	void* GetNativeContext();

	bool SetAntialiasMode(wxAntialiasMode antialias);

	bool SetInterpolationQuality(wxInterpolationQuality interpolation);

	bool SetCompositionMode(wxCompositionMode op);

	void BeginLayer(wxDouble opacity);

	void EndLayer();

	void Translate(wxDouble dx, wxDouble dy);

	void Scale(wxDouble xScale, wxDouble yScale);

	void Rotate(wxDouble angle);

	void ConcatTransform(GraphicsMatrixData* matrix);

	void SetTransform(GraphicsMatrixData* matrix);

	GraphicsMatrixData *GetTransform() const;

	void StrokePath(GraphicsPathData * p);

	void FillPath(GraphicsPathData * p, wxPolygonFillMode fillStyle = wxODDEVEN_RULE);

	void DrawRectangle(wxDouble x, wxDouble y, wxDouble w, wxDouble h);

	void DrawRoundedRectangle(wxDouble x, wxDouble y, wxDouble w, wxDouble h, wxDouble radius);

	void DrawEllipse(wxDouble x, wxDouble y, wxDouble w, wxDouble h);

	void DrawBitmap(GraphicsBitmapData * bmp, wxDouble x, wxDouble y, wxDouble w, wxDouble h);

	void DrawBitmap(const wxBitmap& bmp, wxDouble x, wxDouble y, wxDouble w, wxDouble h);

	void DrawIcon(const wxIcon& icon, wxDouble x, wxDouble y, wxDouble w, wxDouble h);

	GraphicsPathData * CreatePath();

	void SetPen(const wxPen& pen, double width = 1.0);

	void SetBrush(const wxBrush& brush);

	void SetFont(const wxFont& font, const wxColour& col);

	void PushState();

	void PopState();

	void GetTextExtent(
		const wxString& str,
		wxDouble* width,
		wxDouble* height,
		wxDouble* descent,
		wxDouble* externalLeading) const;

	void GetPartialTextExtents(const wxString& text, wxArrayDouble& widths) const;

	bool ShouldOffset() const;

	void SetPen(const wxD2DPenData& pen);

	void Flush();

	void GetDPI(wxDouble* dpiX, wxDouble* dpiY) const;

	wxD2DContextSupplier::ContextType GetContext()
	{
		return GetRenderTarget();
	}

private:
	void Init();

	void DoDrawText(const wxString& str, wxDouble x, wxDouble y);

	void EnsureInitialized();

	HRESULT CreateRenderTarget();

	void AdjustRenderTargetSize();

	void ReleaseDeviceDependentResources();

	ID2D1RenderTarget* GetRenderTarget() const;

	void SetClipLayer(ID2D1Geometry* clipGeometry);

	wxD2DRenderer *GetRenderer(){ return m_renderer; };

	GraphicsMatrixData *CreateMatrix(wxDouble a = 1.0, wxDouble b = 0.0, wxDouble c = 0.0, wxDouble d = 1.0,
		wxDouble tx = 0.0, wxDouble ty = 0.0) const;

	wxWindow* GetWindow() const { return m_window; }

	wxInterpolationQuality GetInterpolationQuality() const { return m_interpolation; }

	wxCompositionMode GetCompositionMode() const { return m_composition; }

	GraphicsBitmapData *CreateBitmap(const wxBitmap& bmp) const;

private:
	enum LayerType
	{
		CLIP_LAYER,
		OTHER_LAYER
	};

	struct LayerData
	{
		LayerType type;
		D2D1_LAYER_PARAMETERS params;
		wxCOMPtr<ID2D1Layer> layer;
		wxCOMPtr<ID2D1Geometry> geometry;
		D2D1_MATRIX_3X2_F transformMatrix;
	};

	struct StateData
	{
		// A ID2D1DrawingStateBlock represents the drawing state of a render target:
		// the anti aliasing mode, transform, tags, and text-rendering options.
		// The context owns these pointers and is responsible for releasing them.
		wxCOMPtr<ID2D1DrawingStateBlock> drawingState;
		// We need to store also current layers.
		wxStack<LayerData> layers;
	};

private:
	wxDouble m_width,
		m_height;

	wxD2DPenData * m_pen = NULL;
	wxD2DBrushData * m_brush = NULL;
	wxD2DFontData * m_font = NULL;
	wxAntialiasMode m_antialias;
	wxCompositionMode m_composition;
	wxInterpolationQuality m_interpolation;
	bool m_enableOffset;
	ID2D1Factory* m_direct2dFactory;
	wxSharedPtr<wxD2DRenderTargetResourceHolder> m_renderTargetHolder;
	wxStack<StateData> m_stateStack;
	wxStack<LayerData> m_layers;
	ID2D1RenderTarget* m_cachedRenderTarget;
	D2D1::Matrix3x2F m_initTransform;
	// Clipping box
	bool m_isClipBoxValid;
	double m_clipX1, m_clipY1, m_clipX2, m_clipY2;
	wxWindow* m_window;
private:
	//wxDECLARE_NO_COPY_CLASS(wxD2DContext);
};

class wxD2DOffsetHelper
{
public:
	wxD2DOffsetHelper(wxD2DContext* g);

	~wxD2DOffsetHelper();

private:
	wxD2DContext* m_context;
};

wxD2DOffsetHelper::wxD2DOffsetHelper(wxD2DContext* g) : m_context(g)
{
	if (m_context->ShouldOffset())
	{
		m_context->Translate(0.5, 0.5);
	}
}

wxD2DOffsetHelper::~wxD2DOffsetHelper()
{
	if (m_context->ShouldOffset())
	{
		m_context->Translate(-0.5, -0.5);
	}
}
//-----------------------------------------------------------------------------
// wxD2DContext implementation
//-----------------------------------------------------------------------------

wxD2DContext::wxD2DContext(wxD2DRenderer* renderer,
						   ID2D1Factory* direct2dFactory,
						   HWND hwnd,
						   wxWindow* window) :
	m_direct2dFactory(direct2dFactory),
#if wxD2D_DEVICE_CONTEXT_SUPPORTED
	m_renderTargetHolder(new wxD2DDeviceContextResourceHolder(direct2dFactory, hwnd)),
#else
	m_renderTargetHolder(new wxD2DHwndRenderTargetResourceHolder(hwnd, direct2dFactory)),
#endif
	m_renderer(renderer)
{
	RECT r = wxGetWindowRect(hwnd);
	m_width = r.right - r.left;
	m_height = r.bottom - r.top;
	Init();
}

wxD2DContext::wxD2DContext(wxD2DRenderer* renderer,
						   ID2D1Factory* direct2dFactory,
						   HDC hdc,
						   const wxDC* dc,
						   D2D1_ALPHA_MODE alphaMode) :
	m_direct2dFactory(direct2dFactory),
	m_renderTargetHolder(new wxD2DDCRenderTargetResourceHolder(direct2dFactory, hdc, alphaMode)),
	m_renderer(renderer)
{
	if ( dc )
	{
		const wxSize dcSize = dc->GetSize();
		m_width = dcSize.GetWidth();
		m_height = dcSize.GetHeight();
	}

	Init();
}

#if wxUSE_IMAGE
wxD2DContext::wxD2DContext(wxD2DRenderer* renderer, ID2D1Factory* direct2dFactory, wxImage& image) :
	m_direct2dFactory(direct2dFactory),
	m_renderTargetHolder(new wxD2DImageRenderTargetResourceHolder(&image, direct2dFactory)),
	m_renderer(renderer)
{
	m_width = image.GetWidth();
	m_height = image.GetHeight();
	Init();
}
#endif // wxUSE_IMAGE

wxD2DContext::wxD2DContext(wxD2DRenderer* renderer, ID2D1Factory* direct2dFactory, void* nativeContext) :
	m_direct2dFactory(direct2dFactory),
	m_renderer(renderer)
{
	m_renderTargetHolder = *((wxSharedPtr<wxD2DRenderTargetResourceHolder>*)nativeContext);
	m_width = 0;
	m_height = 0;
	Init();
}

void wxD2DContext::Init()
{
	m_cachedRenderTarget = NULL;
	m_composition = wxCOMPOSITION_OVER;
	m_renderTargetHolder->Bind(this);
	m_enableOffset = true;
	m_isClipBoxValid = false;
	m_clipX1 = m_clipY1 = m_clipX2 = m_clipY2 = 0.0;
	m_interpolation = wxINTERPOLATION_BEST;
	m_antialias = wxANTIALIAS_NONE;
	EnsureInitialized();
}

wxD2DContext::~wxD2DContext()
{
	// Remove all layers from the stack of layers.
	while ( !m_layers.empty() )
	{
		LayerData ld = m_layers.top();
		m_layers.pop();

		GetRenderTarget()->PopLayer();
		ld.layer.reset();
		ld.geometry.reset();
	}

	HRESULT result = GetRenderTarget()->EndDraw();
	wxCHECK_HRESULT_RET(result);

	ReleaseResources();

	if (m_pen)
		delete m_pen;

	if (m_brush)
		delete m_brush;

	if (m_font)
		delete m_font;
}

ID2D1RenderTarget* wxD2DContext::GetRenderTarget() const
{
	return m_cachedRenderTarget;
}

void wxD2DContext::Clip(const wxRegion& region)
{
	wxCOMPtr<ID2D1Geometry> clipGeometry = wxD2DConvertRegionToGeometry(m_direct2dFactory, region);

	SetClipLayer(clipGeometry);
}

void wxD2DContext::Clip(wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
	wxCOMPtr<ID2D1RectangleGeometry> clipGeometry;
	HRESULT hr = m_direct2dFactory->CreateRectangleGeometry(
						D2D1::RectF(x, y, x + w, y + h), &clipGeometry);
	wxCHECK_HRESULT_RET(hr);

	SetClipLayer(clipGeometry);
}

void wxD2DContext::SetClipLayer(ID2D1Geometry* clipGeometry)
{
	EnsureInitialized();

	wxCOMPtr<ID2D1Layer> clipLayer;
	HRESULT hr = GetRenderTarget()->CreateLayer(&clipLayer);
	wxCHECK_HRESULT_RET(hr);

	LayerData ld;
	ld.type = CLIP_LAYER;
	ld.params = D2D1::LayerParameters(D2D1::InfiniteRect(), clipGeometry,
									  wxD2DConvertAntialiasMode(m_antialias));
	ld.layer = clipLayer;
	ld.geometry = clipGeometry;
	// We need to store CTM to be able to re-apply
	// the layer at the original position later on.
	GetRenderTarget()->GetTransform(&ld.transformMatrix);

	GetRenderTarget()->PushLayer(ld.params, clipLayer);
	// Store layer parameters.
	m_layers.push(ld);

	m_isClipBoxValid = false;
}

void wxD2DContext::ResetClip()
{
	wxStack<LayerData> layersToRestore;
	// Remove all clipping layers from the stack of layers.
	while ( !m_layers.empty() )
	{
		LayerData ld = m_layers.top();
		m_layers.pop();

		if ( ld.type == CLIP_LAYER )
		{
			GetRenderTarget()->PopLayer();
			ld.layer.reset();
			ld.geometry.reset();
			continue;
		}

		GetRenderTarget()->PopLayer();
		// Save non-clipping layer
		layersToRestore.push(ld);
	}

	HRESULT hr = GetRenderTarget()->Flush();
	wxCHECK_HRESULT_RET(hr);

	// Re-apply all remaining non-clipping layers.
	// First, save current transformation matrix.
	D2D1_MATRIX_3X2_F currTransform;
	GetRenderTarget()->GetTransform(&currTransform);
	while ( !layersToRestore.empty() )
	{
		LayerData ld = layersToRestore.top();
		layersToRestore.pop();

		// Restore layer at original position.
		GetRenderTarget()->SetTransform(&ld.transformMatrix);
		GetRenderTarget()->PushLayer(ld.params, ld.layer);
		// Store layer parameters.
		m_layers.push(ld);
	}
	// Restore current transformation matrix.
	GetRenderTarget()->SetTransform(&currTransform);

	m_isClipBoxValid = false;
}

void wxD2DContext::GetClipBox(wxDouble* x, wxDouble* y, wxDouble* w, wxDouble* h)
{
	if ( !m_isClipBoxValid )
	{
		// To obtain actual clipping box we have to start with rectangle
		// covering the entire render target and interesect with this rectangle
		// all clipping layers. Bounding box of the final geometry
		// (being intersection of all clipping layers) is a clipping box.

		HRESULT hr;
		wxCOMPtr<ID2D1RectangleGeometry> rectGeometry;
		hr = m_direct2dFactory->CreateRectangleGeometry(
					D2D1::RectF(0.0F, 0.0F, (FLOAT)m_width, (FLOAT)m_height),
					&rectGeometry);
		wxCHECK_HRESULT_RET(hr);

		wxCOMPtr<ID2D1Geometry> clipGeometry(rectGeometry);

		wxStack<LayerData> layers(m_layers);
		while( !layers.empty() )
		{
			LayerData ld = layers.top();
			layers.pop();

			if ( ld.type == CLIP_LAYER )
			{
				// If current geometry is empty (null region)
				// or there is no intersection between geometries
				// then final result is "null" rectangle geometry.
				FLOAT area;
				hr = ld.geometry->ComputeArea(ld.transformMatrix, &area);
				wxCHECK_HRESULT_RET(hr);
				D2D1_GEOMETRY_RELATION geomRel;
				hr = clipGeometry->CompareWithGeometry(ld.geometry, ld.transformMatrix, &geomRel);
				wxCHECK_HRESULT_RET(hr);
				if ( area <= FLT_MIN || geomRel == D2D1_GEOMETRY_RELATION_DISJOINT )
				{
					wxCOMPtr<ID2D1RectangleGeometry> nullGeometry;
					hr = m_direct2dFactory->CreateRectangleGeometry(
								D2D1::RectF(0.0F, 0.0F, 0.0F, 0.0F), &nullGeometry);
					wxCHECK_HRESULT_RET(hr);

					clipGeometry.reset();
					clipGeometry = nullGeometry;
					break;
				}

				wxCOMPtr<ID2D1PathGeometry> pathGeometryClip;
				hr = m_direct2dFactory->CreatePathGeometry(&pathGeometryClip);
				wxCHECK_HRESULT_RET(hr);
				wxCOMPtr<ID2D1GeometrySink> pGeometrySink;
				hr = pathGeometryClip->Open(&pGeometrySink);
				wxCHECK_HRESULT_RET(hr);

				hr = clipGeometry->CombineWithGeometry(ld.geometry, D2D1_COMBINE_MODE_INTERSECT,
													   ld.transformMatrix, pGeometrySink);
				wxCHECK_HRESULT_RET(hr);
				hr = pGeometrySink->Close();
				wxCHECK_HRESULT_RET(hr);
				pGeometrySink.reset();

				clipGeometry = pathGeometryClip;
				pathGeometryClip.reset();
			}
		}

		// Final clipping geometry is given in device coordinates
		// so we need to transform its bounds to logical coordinates.
		D2D1::Matrix3x2F currTransform;
		GetRenderTarget()->GetTransform(&currTransform);
		currTransform.Invert();

		D2D1_RECT_F bounds;
		// First check if clip region is empty.
		FLOAT clipArea;
		hr = clipGeometry->ComputeArea(currTransform, &clipArea);
		wxCHECK_HRESULT_RET(hr);
		if ( clipArea <= FLT_MIN )
		{
			bounds.left = bounds.top = bounds.right = bounds.bottom = 0.0F;
		}
		else
		{
			// If it is not empty then get it bounds.
			hr = clipGeometry->GetBounds(currTransform, &bounds);
			wxCHECK_HRESULT_RET(hr);
		}

		m_clipX1 = bounds.left;
		m_clipY1 = bounds.top;
		m_clipX2 = bounds.right;
		m_clipY2 = bounds.bottom;
		m_isClipBoxValid = true;
	}

	if ( x )
		*x = m_clipX1;
	if ( y )
		*y = m_clipY1;
	if ( w )
		*w = m_clipX2 - m_clipX1;
	if ( h )
		*h = m_clipY2 - m_clipY1;
}

void* wxD2DContext::GetNativeContext()
{
	return &m_renderTargetHolder;
}

void wxD2DContext::StrokePath(GraphicsPathData * p)
{
	if (m_composition == wxCOMPOSITION_DEST)
		return;

	wxD2DOffsetHelper helper(this);

	EnsureInitialized();
	AdjustRenderTargetSize();

	wxD2DPathData *pathData = (wxD2DPathData*)p;
	pathData->Flush();

	if (m_pen)
	{
		//wxD2DPenData* penData = wxGetD2DPenData(m_pen);
		m_pen->Bind(this);

		ID2D1Brush* nativeBrush = m_pen->GetBrush();
		GetRenderTarget()->DrawGeometry((ID2D1Geometry*)pathData->GetNativePath(), nativeBrush, m_pen->GetWidth(), m_pen->GetStrokeStyle());
	}
	delete pathData;
}

void wxD2DContext::FillPath(GraphicsPathData * p, wxPolygonFillMode fillStyle)
{
	if (m_composition == wxCOMPOSITION_DEST)
		return;

	EnsureInitialized();
	AdjustRenderTargetSize();

	wxD2DPathData *pathData = (wxD2DPathData*)p;
	pathData->SetFillMode(fillStyle == wxODDEVEN_RULE ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
	pathData->Flush();

	if (m_brush)
	{
		//wxD2DBrushData* brushData = wxGetD2DBrushData(m_brush);
		m_brush->Bind(this);
		GetRenderTarget()->FillGeometry((ID2D1Geometry*)pathData->GetNativePath(), m_brush->GetBrush());
	}
}

bool wxD2DContext::SetAntialiasMode(wxAntialiasMode antialias)
{
	if (m_antialias == antialias)
	{
		return true;
	}

	GetRenderTarget()->SetAntialiasMode(wxD2DConvertAntialiasMode(antialias));

	m_antialias = antialias;
	return true;
}

bool wxD2DContext::SetInterpolationQuality(wxInterpolationQuality interpolation)
{
	// Since different versions of Direct2D have different enumerations for
	// interpolation quality, we deffer the conversion to the method which
	// does the actual drawing.

	m_interpolation = interpolation;
	return true;
}

bool wxD2DContext::SetCompositionMode(wxCompositionMode compositionMode)
{
	if (m_composition == compositionMode)
		return true;

	if (m_renderTargetHolder->SetCompositionMode(compositionMode))
	{
		// the composition mode is supported by the render target
		m_composition = compositionMode;
		return true;
	}

	return false;
}

void wxD2DContext::BeginLayer(wxDouble opacity)
{
	EnsureInitialized();

	wxCOMPtr<ID2D1Layer> layer;
	HRESULT hr = GetRenderTarget()->CreateLayer(&layer);
	wxCHECK_HRESULT_RET(hr);

	LayerData ld;
	ld.type = OTHER_LAYER;
	ld.params = D2D1::LayerParameters(D2D1::InfiniteRect(),
						NULL,
						D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
						D2D1::IdentityMatrix(), opacity);
	ld.layer = layer;
	// We need to store CTM to be able to re-apply
	// the layer at the original position later on.
	GetRenderTarget()->GetTransform(&ld.transformMatrix);

	GetRenderTarget()->PushLayer(ld.params, layer);

	// Store layer parameters.
	m_layers.push(ld);
}

void wxD2DContext::EndLayer()
{
	wxStack<LayerData> layersToRestore;
	// Temporarily remove all clipping layers
	// above the first standard layer
	// and next permanently remove this layer.
	while ( !m_layers.empty() )
	{
		LayerData ld = m_layers.top();
		m_layers.pop();

		if ( ld.type == CLIP_LAYER )
		{
			GetRenderTarget()->PopLayer();
			layersToRestore.push(ld);
			continue;
		}

		// We found a non-clipping layer to remove.
		GetRenderTarget()->PopLayer();
		ld.layer.reset();
		break;
	}

	if ( m_layers.empty() )
	{
		HRESULT hr = GetRenderTarget()->Flush();
		wxCHECK_HRESULT_RET(hr);
	}

	// Re-apply all stored clipping layers.
	// First, save current transformation matrix.
	D2D1_MATRIX_3X2_F currTransform;
	GetRenderTarget()->GetTransform(&currTransform);
	while ( !layersToRestore.empty() )
	{
		LayerData ld = layersToRestore.top();
		layersToRestore.pop();

		if ( ld.type == CLIP_LAYER )
		{
			// Restore layer at original position.
			GetRenderTarget()->SetTransform(&ld.transformMatrix);
			GetRenderTarget()->PushLayer(ld.params, ld.layer);
		}
		else
		{
			wxFAIL_MSG( wxS("Invalid layer type") );
		}
		// Store layer parameters.
		m_layers.push(ld);
	}
	// Restore current transformation matrix.
	GetRenderTarget()->SetTransform(&currTransform);
}

void wxD2DContext::Translate(wxDouble dx, wxDouble dy)
{
	GraphicsMatrixData * translationMatrix = CreateMatrix();
	translationMatrix->Translate(dx, dy);
	ConcatTransform(translationMatrix);
	delete translationMatrix;
}

void wxD2DContext::Scale(wxDouble xScale, wxDouble yScale)
{
	GraphicsMatrixData * scaleMatrix = CreateMatrix();
	scaleMatrix->Scale(xScale, yScale);
	ConcatTransform(scaleMatrix);
	delete scaleMatrix;
}

void wxD2DContext::Rotate(wxDouble angle)
{
	GraphicsMatrixData *rotationMatrix = CreateMatrix();
	rotationMatrix->Rotate(angle);
	ConcatTransform(rotationMatrix);
	delete rotationMatrix;
}

void wxD2DContext::ConcatTransform(GraphicsMatrixData* matrix)
{
	GraphicsMatrixData *getTransform = GetTransform();
	D2D1::Matrix3x2F localMatrix = ((wxD2DMatrixData*)getTransform)->GetMatrix3x2F();
	D2D1::Matrix3x2F concatMatrix = ((wxD2DMatrixData*)matrix)->GetMatrix3x2F();

	D2D1::Matrix3x2F resultMatrix;
	resultMatrix.SetProduct(concatMatrix, localMatrix);

	wxD2DMatrixData *resultTransform = new wxD2DMatrixData(m_renderer, resultMatrix);

	delete getTransform;

	SetTransform(resultTransform);
	delete resultTransform;
}

void wxD2DContext::SetTransform(GraphicsMatrixData* matrix)
{
	EnsureInitialized();

	D2D1::Matrix3x2F m;
	m.SetProduct(((wxD2DMatrixData*)matrix)->GetMatrix3x2F(), m_initTransform);
	GetRenderTarget()->SetTransform(&m);

	m_isClipBoxValid = false;
}

GraphicsMatrixData *wxD2DContext::GetTransform() const
{
	D2D1::Matrix3x2F transformMatrix;

	if (GetRenderTarget() != NULL)
	{
		GetRenderTarget()->GetTransform(&transformMatrix);

		if ( m_initTransform.IsInvertible() )
		{
			D2D1::Matrix3x2F invMatrix = m_initTransform;
			invMatrix.Invert();

			D2D1::Matrix3x2F m;
			m.SetProduct(transformMatrix, invMatrix);
			transformMatrix = m;
		}
	}
	else
	{
		transformMatrix = D2D1::Matrix3x2F::Identity();
	}

	wxD2DMatrixData* matrixData = new wxD2DMatrixData(m_renderer, transformMatrix);


	return matrixData;
}

void wxD2DContext::DrawBitmap(GraphicsBitmapData* bmp, wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
	if (m_composition == wxCOMPOSITION_DEST)
		return;

	wxD2DBitmapData* bitmapData = (wxD2DBitmapData*)bmp;
	bitmapData->Bind(this);

	m_renderTargetHolder->DrawBitmap(
		bitmapData->GetD2DBitmap(),
		D2D1::Point2F(x, y),
		D2D1::RectF(0, 0, w, h),
		GetInterpolationQuality(),
		GetCompositionMode());
}

void wxD2DContext::DrawBitmap(const wxBitmap& bmp, wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
	GraphicsBitmapData * graphicsBitmap = CreateBitmap(bmp);
	DrawBitmap(graphicsBitmap, x, y, w, h);
	delete graphicsBitmap;
}

void wxD2DContext::DrawIcon(const wxIcon& icon, wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
	DrawBitmap(wxBitmap(icon), x, y, w, h);
}

void wxD2DContext::PushState()
{
	EnsureInitialized();

	StateData state;
	m_direct2dFactory->CreateDrawingStateBlock(&state.drawingState);
	GetRenderTarget()->SaveDrawingState(state.drawingState);
	state.layers = m_layers;

	m_stateStack.push(state);
}

void wxD2DContext::PopState()
{
	wxCHECK_RET(!m_stateStack.empty(), wxS("No state to pop"));

	// Remove all layers from the stack of layers.
	while ( !m_layers.empty() )
	{
		LayerData ld = m_layers.top();
		m_layers.pop();

		GetRenderTarget()->PopLayer();
		ld.layer.reset();
		ld.geometry.reset();
	}

	// Retrieve state data.
	StateData state;
	state = m_stateStack.top();
	m_stateStack.pop();

	// Restore all saved layers.
	wxStack<LayerData> layersToRestore;
	// We have to restore layers on the stack from "bottom" to "top",
	// so we have to create a "reverted" stack.
	while ( !state.layers.empty() )
	{
		LayerData ld = state.layers.top();
		state.layers.pop();

		layersToRestore.push(ld);
	}
	// And next set layers from the top of "reverted" stack.
	while ( !layersToRestore.empty() )
	{
		LayerData ld = layersToRestore.top();
		layersToRestore.pop();

		// Restore layer at original position.
		GetRenderTarget()->SetTransform(&ld.transformMatrix);
		GetRenderTarget()->PushLayer(ld.params, ld.layer);

		// Store layer parameters.
		m_layers.push(ld);
	}

	// Restore drawing state.
	GetRenderTarget()->RestoreDrawingState(state.drawingState);

	m_isClipBoxValid = false;
}

void wxD2DContext::GetTextExtent(
	const wxString& str,
	wxDouble* width,
	wxDouble* height,
	wxDouble* descent,
	wxDouble* externalLeading) const
{
	//wxCHECK_RET(m_font,
		//wxS("wxD2DContext::GetTextExtent - no valid font set"));
	if (!m_font)
		return;

	wxD2DMeasuringContext::GetTextExtent(
		m_font, str, width, height, descent, externalLeading);
}

void wxD2DContext::GetPartialTextExtents(const wxString& text, wxArrayDouble& widths) const
{
	wxCHECK_RET(m_font,
		wxS("wxD2DContext::GetPartialTextExtents - no valid font set"));

	wxD2DMeasuringContext::GetPartialTextExtents(
		m_font, text, widths);
}

bool wxD2DContext::ShouldOffset() const
{
	if (!m_enableOffset)
	{
		return false;
	}

	int penWidth = 0;
	if (m_pen)
	{
		penWidth = m_pen->GetWidth();
		penWidth = wxMax(penWidth, 1);
	}

	return (penWidth % 2) == 1;
}

void wxD2DContext::DoDrawText(const wxString& str, wxDouble x, wxDouble y)
{
	//wxCHECK_RET(m_font,
		//wxS("wxD2DContext::DrawText - no valid font set"));
	if (!m_font)
		return;

	if (m_composition == wxCOMPOSITION_DEST)
		return;

	//wxD2DFontData* fontData = m_font;
	//fontData->GetBrushData().Bind(this);
	m_font->GetBrushData().Bind(this);
	wxCOMPtr<IDWriteTextLayout> textLayout = m_font->CreateTextLayout(str);

	// Render the text
	GetRenderTarget()->DrawTextLayout(
		D2D1::Point2F(x, y),
		textLayout,
		m_font->GetBrushData().GetBrush());
}

void wxD2DContext::EnsureInitialized()
{
	if (!m_renderTargetHolder->IsResourceAcquired())
	{
		//loop it avoid to crash when target is null cause of context problem
		while (!m_cachedRenderTarget) {
			m_cachedRenderTarget = m_renderTargetHolder->GetD2DResource();
			Sleep(10);
		}
		GetRenderTarget()->GetTransform(&m_initTransform);
		GetRenderTarget()->BeginDraw();
	}
	else
	{
		m_cachedRenderTarget = m_renderTargetHolder->GetD2DResource();
	}
}

void wxD2DContext::SetPen(const wxD2DPenData& pen)
{
	m_pen = new wxD2DPenData(pen);

	if (m_pen)
	{
		EnsureInitialized();

		//wxD2DPenData* penData = wxGetD2DPenData(pen);
		m_pen->Bind(this);
	}
}

void wxD2DContext::AdjustRenderTargetSize()
{
	m_renderTargetHolder->Resize();

	// Currently GetSize() can only be called when using MSVC because gcc
	// doesn't handle returning aggregates by value as done by D2D libraries,
	// see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=64384. Not updating the
	// size is not great, but it's better than crashing.
#ifdef __VISUALC__
	D2D1_SIZE_F renderTargetSize = m_renderTargetHolder->GetD2DResource()->GetSize();
	m_width = renderTargetSize.width;
	m_height =  renderTargetSize.height;
#endif // __VISUALC__
}

void wxD2DContext::ReleaseDeviceDependentResources()
{
	ReleaseResources();
}

void wxD2DContext::DrawRectangle(wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
	if (m_composition == wxCOMPOSITION_DEST)
		return;

	wxD2DOffsetHelper helper(this);

	EnsureInitialized();
	AdjustRenderTargetSize();

	D2D1_RECT_F rect = { (FLOAT)x, (FLOAT)y, (FLOAT)(x + w), (FLOAT)(y + h) };


	if (m_brush)
	{
		//wxD2DBrushData* brushData = m_brush;
		m_brush->Bind(this);
		GetRenderTarget()->FillRectangle(rect, m_brush->GetBrush());
	}

	if (m_pen)
	{
		//wxD2DPenData* penData = m_pen;
		m_pen->Bind(this);
		GetRenderTarget()->DrawRectangle(rect, m_pen->GetBrush(), m_pen->GetWidth(), m_pen->GetStrokeStyle());
	}
}

void wxD2DContext::DrawRoundedRectangle(wxDouble x, wxDouble y, wxDouble w, wxDouble h, wxDouble radius)
{
	if (m_composition == wxCOMPOSITION_DEST)
		return;

	wxD2DOffsetHelper helper(this);

	EnsureInitialized();
	AdjustRenderTargetSize();

	D2D1_RECT_F rect = { (FLOAT)x, (FLOAT)y, (FLOAT)(x + w), (FLOAT)(y + h) };

	D2D1_ROUNDED_RECT roundedRect = { rect, (FLOAT)radius, (FLOAT)radius };

	if (m_brush)
	{
		//wxD2DBrushData* brushData = m_brush;
		m_brush->Bind(this);
		GetRenderTarget()->FillRoundedRectangle(roundedRect, m_brush->GetBrush());
	}

	if (m_pen)
	{
		//wxD2DPenData* penData = m_pen;
		m_pen->Bind(this);
		GetRenderTarget()->DrawRoundedRectangle(roundedRect, m_pen->GetBrush(), m_pen->GetWidth(), m_pen->GetStrokeStyle());
	}
}

void wxD2DContext::DrawEllipse(wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
	if (m_composition == wxCOMPOSITION_DEST)
		return;

	wxD2DOffsetHelper helper(this);

	EnsureInitialized();
	AdjustRenderTargetSize();

	D2D1_ELLIPSE ellipse = {
		{ (FLOAT)(x + w / 2), (FLOAT)(y + h / 2) }, // center point
		(FLOAT)(w / 2),                      // radius x
		(FLOAT)(h / 2)                       // radius y
	};

	if (m_brush)
	{
		//wxD2DBrushData* brushData = m_brush;
		m_brush->Bind(this);
		GetRenderTarget()->FillEllipse(ellipse, m_brush->GetBrush());
	}

	if (m_pen)
	{
		//wxD2DPenData* penData = m_pen;
		m_pen->Bind(this);
		GetRenderTarget()->DrawEllipse(ellipse, m_pen->GetBrush(), m_pen->GetWidth(), m_pen->GetStrokeStyle());
	}
}

void wxD2DContext::Flush()
{
	wxStack<LayerData> layersToRestore;
	// Temporarily remove all layers from the stack of layers.
	while ( !m_layers.empty() )
	{
		LayerData ld = m_layers.top();
		m_layers.pop();

		GetRenderTarget()->PopLayer();

		// Save layer data.
		layersToRestore.push(ld);
	}

	HRESULT hr = m_renderTargetHolder->Flush();

	if ( hr == (HRESULT)D2DERR_RECREATE_TARGET )
	{
		ReleaseDeviceDependentResources();
	}
	else
	{
		wxCHECK_HRESULT_RET(hr);
	}

	// Re-apply all layers.
	// First, save current transformation matrix.
	D2D1_MATRIX_3X2_F currTransform;
	GetRenderTarget()->GetTransform(&currTransform);
	while ( !layersToRestore.empty() )
	{
		LayerData ld = layersToRestore.top();
		layersToRestore.pop();

		// Restore layer at original position.
		GetRenderTarget()->SetTransform(&ld.transformMatrix);
		GetRenderTarget()->PushLayer(ld.params, ld.layer);

		// Store layer parameters.
		m_layers.push(ld);
	}
	// Restore current transformation matrix.
	GetRenderTarget()->SetTransform(&currTransform);
}

void wxD2DContext::GetDPI(wxDouble* dpiX, wxDouble* dpiY) const
{
  //  if ( GetWindow() )
  //  {
		//
  //      const wxSize dpi = GetWindow()->GetDPI();

  //      if ( dpiX )
  //          *dpiX = dpi.x;
  //      if ( dpiY )
  //          *dpiY = dpi.y;
  //  }
  //  else
  //  {
		FLOAT x, y;
		GetRenderTarget()->GetDpi(&x, &y);

		if ( dpiX )
			*dpiX = x;
		if ( dpiY )
			*dpiY = y;
   // }
}



//-----------------------------------------------------------------------------
// wxD2DRenderer implementation
//-----------------------------------------------------------------------------

class wxD2DRenderer : public GraphicsRenderer
{
public:
	wxD2DRenderer();

	virtual ~wxD2DRenderer();

	static wxD2DRenderer* GetDirect2DRenderer();

	GraphicsContext * CreateContext(const wxWindowDC& dc);

	GraphicsContext * CreateContext(const wxMemoryDC& dc);

	GraphicsContext * CreateContextFromNativeContext(void* context);

	GraphicsContext * CreateContextFromNativeWindow(void* window);

	GraphicsContext * CreateContextFromNativeHDC(WXHDC dc);

	GraphicsContext * CreateContext(wxWindow* window);

#if wxUSE_IMAGE
	GraphicsContext * CreateContextFromImage(wxImage& image);
#endif // wxUSE_IMAGE

	GraphicsContext * CreateMeasuringContext();

	wxD2DPathData * CreatePath();

	GraphicsMatrixData *CreateMatrix(
		wxDouble a = 1.0, wxDouble b = 0.0, wxDouble c = 0.0, wxDouble d = 1.0,
		wxDouble tx = 0.0, wxDouble ty = 0.0);

	wxD2DPenData * CreatePen(const wxGraphicsPenInfo& info);

	wxD2DBrushData * CreateBrush(const wxBrush& brush);

	wxD2DBrushData * CreateLinearGradientBrush(
		wxDouble x1, wxDouble y1,
		wxDouble x2, wxDouble y2,
		const wxGraphicsGradientStops& stops,
		wxD2DMatrixData * matrix = NULL);

	wxD2DBrushData * CreateRadialGradientBrush(
		wxDouble startX, wxDouble startY,
		wxDouble endX, wxDouble endY,
		wxDouble radius,
		const wxGraphicsGradientStops& stops,
		wxD2DMatrixData * matrix = NULL);

	// create a native bitmap representation
	GraphicsBitmapData * CreateBitmap(const wxBitmap& bitmap);

	//#if wxUSE_IMAGE
	//	GraphicsBitmapData * CreateBitmapFromImage(const wxImage& image);
	//	wxImage CreateImageFromBitmap(const wxGraphicsBitmap& bmp);
	//#endif

	wxD2DFontData * CreateFont(const wxFont& font, const wxColour& col);

	/*wxD2DFontData * CreateFont(
	double sizeInPixels, const wxString& facename,
	int flags = wxFONTFLAG_DEFAULT,
	const wxColour& col = *wxBLACK);*/

	wxD2DFontData * CreateFontAtDPI(const wxFont& font,
		const wxRealPoint& dpi,
		const wxColour& col);

	// create a graphics bitmap from a native bitmap
	GraphicsBitmapData * CreateBitmapFromNativeBitmap(void* bitmap);

	// create a sub-image from a native image representation
	GraphicsBitmapData * CreateSubBitmap(const GraphicsBitmapData& bitmap, wxDouble x, wxDouble y, wxDouble w, wxDouble h);

	wxString GetName() const;
	void GetVersion(int* major, int* minor, int* micro) const;

	ID2D1Factory* GetD2DFactory();

private:
	wxCOMPtr<ID2D1Factory> m_direct2dFactory;

private:
	//wxDECLARE_DYNAMIC_CLASS_NO_COPY(wxD2DRenderer);
};

//wxIMPLEMENT_DYNAMIC_CLASS(wxD2DRenderer,wxGraphicsRenderer);

void wxNullContext::SetFont(const wxFont& font, const wxColour& col){
	wxDELETE(m_font);
	m_font = renderer->CreateFont(font, col);
}

GraphicsPathData * wxD2DContext::CreatePath(){ return m_renderer->CreatePath(); };

void wxD2DContext::SetPen(const wxPen& pen, double width){
	wxGraphicsPenInfo info(pen.GetColour(), width, pen.GetStyle());
	wxDELETE(m_pen);
	m_pen = m_renderer->CreatePen(info);
}

void wxD2DContext::SetBrush(const wxBrush& brush){
	wxDELETE(m_brush);
	m_brush = m_renderer->CreateBrush(brush);
}

void wxD2DContext::SetFont(const wxFont& font, const wxColour& col){
	wxDELETE(m_font);
	m_font = m_renderer->CreateFont(font, col);
}

GraphicsBitmapData *wxD2DContext::CreateBitmap(const wxBitmap& bmp) const
{
	return m_renderer->CreateBitmap(bmp);
}

GraphicsMatrixData *wxD2DContext::CreateMatrix(wxDouble a, wxDouble b, wxDouble c, wxDouble d,
	wxDouble tx, wxDouble ty) const {
	return m_renderer->CreateMatrix(a, b, c, d, tx, ty);
}

static wxD2DRenderer *gs_D2DRenderer = NULL;

wxD2DRenderer* wxD2DRenderer::GetDirect2DRenderer()
{
	if (!wxDirect2D::Initialize())
		return NULL;

	if (!gs_D2DRenderer)
	{
		gs_D2DRenderer = new wxD2DRenderer();
	}

	return gs_D2DRenderer;
}

wxD2DRenderer::wxD2DRenderer()
	: m_direct2dFactory(wxD2D1Factory())
{
	if ( m_direct2dFactory.get() == NULL )
	{
		wxFAIL_MSG("Could not create Direct2D Factory.");
	}
}

wxD2DRenderer::~wxD2DRenderer()
{
	m_direct2dFactory.reset();
}

GraphicsContext* wxD2DRenderer::CreateContext(const wxWindowDC& dc)
{
	return new wxD2DContext(this, m_direct2dFactory, dc.GetHDC(), &dc);
}

GraphicsContext* wxD2DRenderer::CreateContext(const wxMemoryDC& dc)
{
	wxBitmap bmp = dc.GetSelectedBitmap();
	wxASSERT_MSG( bmp.IsOk(), wxS("Should select a bitmap before creating wxGraphicsContext") );

	return new wxD2DContext(this, m_direct2dFactory, dc.GetHDC(), &dc,
							bmp.HasAlpha() ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE);
}

GraphicsContext* wxD2DRenderer::CreateContextFromNativeContext(void* nativeContext)
{
	return new wxD2DContext(this, m_direct2dFactory, nativeContext);
}

GraphicsContext* wxD2DRenderer::CreateContextFromNativeWindow(void* window)
{
	return new wxD2DContext(this, m_direct2dFactory, (HWND)window);
}

GraphicsContext* wxD2DRenderer::CreateContextFromNativeHDC(WXHDC dc)
{
	return new wxD2DContext(this, m_direct2dFactory, (HDC)dc);
}

GraphicsContext* wxD2DRenderer::CreateContext(wxWindow* window)
{
	return new wxD2DContext(this, m_direct2dFactory, (HWND)window->GetHWND(), window);
}

#if wxUSE_IMAGE
GraphicsContext* wxD2DRenderer::CreateContextFromImage(wxImage& image)
{
	return new wxD2DContext(this, m_direct2dFactory, image);
}
#endif // wxUSE_IMAGE

GraphicsContext* wxD2DRenderer::CreateMeasuringContext()
{
	return new wxD2DMeasuringContext(this);
}

wxD2DPathData * wxD2DRenderer::CreatePath()
{
	return new wxD2DPathData(this, m_direct2dFactory);
}

GraphicsMatrixData *wxD2DRenderer::CreateMatrix(
	wxDouble a, wxDouble b, wxDouble c, wxDouble d,
	wxDouble tx, wxDouble ty)
{
	wxD2DMatrixData* matrixData = new wxD2DMatrixData(this);
	matrixData->Set(a, b, c, d, tx, ty);

  /*  wxGraphicsMatrix matrix;
	matrix.SetRefData(matrixData);*/

	return matrixData;
}

wxD2DPenData *wxD2DRenderer::CreatePen(const wxGraphicsPenInfo& info)
{
	if ( info.GetStyle() == wxPENSTYLE_TRANSPARENT )
	{
		return NULL;
	}
	else
	{
		
		wxD2DPenData* penData = new wxD2DPenData(this, m_direct2dFactory, info);
		return penData;
	}
}

wxD2DBrushData *wxD2DRenderer::CreateBrush(const wxBrush& brush)
{
	if ( !brush.IsOk() || brush.GetStyle() == wxBRUSHSTYLE_TRANSPARENT )
	{
		return NULL;
	}
	else
	{
		
		return new wxD2DBrushData(this, brush);
	}
}

wxD2DBrushData * wxD2DRenderer::CreateLinearGradientBrush(
	wxDouble x1, wxDouble y1,
	wxDouble x2, wxDouble y2,
	const wxGraphicsGradientStops& stops,
	wxD2DMatrixData * matrix)
{
	wxD2DBrushData* brushData = new wxD2DBrushData(this);
	brushData->CreateLinearGradientBrush(x1, y1, x2, y2, stops, matrix);


	return brushData;
}

wxD2DBrushData * wxD2DRenderer::CreateRadialGradientBrush(
	wxDouble startX, wxDouble startY,
	wxDouble endX, wxDouble endY,
	wxDouble radius,
	const wxGraphicsGradientStops& stops,
	wxD2DMatrixData * matrix)
{
	wxD2DBrushData* brushData = new wxD2DBrushData(this);
	brushData->CreateRadialGradientBrush(startX, startY, endX, endY, radius, stops, matrix);

	return brushData;
}

// create a native bitmap representation
GraphicsBitmapData * wxD2DRenderer::CreateBitmap(const wxBitmap& bitmap)
{
	wxD2DBitmapData* bitmapData = new wxD2DBitmapData(this, bitmap);

	return bitmapData;
}

// create a graphics bitmap from a native bitmap
GraphicsBitmapData * wxD2DRenderer::CreateBitmapFromNativeBitmap(void* bitmap)
{
	wxD2DBitmapData* bitmapData = new wxD2DBitmapData(this, bitmap);

	return bitmapData;
}

//#if wxUSE_IMAGE
//GraphicsBitmapData * wxD2DRenderer::CreateBitmapFromImage(const wxImage& image)
//{
//    return CreateBitmap(wxBitmap(image));
//}
//
//wxImage wxD2DRenderer::CreateImageFromBitmap(const wxGraphicsBitmap& bmp)
//{
//    return static_cast<wxD2DBitmapData::NativeType*>(bmp.GetNativeBitmap())
//        ->GetSourceBitmap().ConvertToImage();
//}
//#endif

wxD2DFontData * wxD2DRenderer::CreateFont(const wxFont& font, const wxColour& col)
{
	return CreateFontAtDPI(font, wxRealPoint(), col);
}

//wxD2DFontData * wxD2DRenderer::CreateFont(
//    double sizeInPixels, const wxString& facename,
//    int flags,
//    const wxColour& col)
//{
//    // Use the same DPI as wxFont will use in SetPixelSize, so these cancel
//    // each other out and we are left with the actual pixel size.
//    ScreenHDC hdc;
//    wxRealPoint dpi(::GetDeviceCaps(hdc, LOGPIXELSX),
//                    ::GetDeviceCaps(hdc, LOGPIXELSY));
//
//    return CreateFontAtDPI(
//        wxFont(wxSize(sizeInPixels, sizeInPixels)).AllFlags(flags).FaceName(facename),
//        dpi, col);
//}

wxD2DFontData * wxD2DRenderer::CreateFontAtDPI(const wxFont& font,
											  const wxRealPoint& dpi,
											  const wxColour& col)
{
	wxD2DFontData* fontData = new wxD2DFontData(this, font, dpi, col);
	if ( !fontData->GetFont() )
	{
		// Apparently a non-TrueType font is given and hence
		// corresponding DirectWrite font couldn't be created.
		delete fontData;
		return NULL;
	}

	return fontData;
}

// create a sub-image from a native image representation
GraphicsBitmapData* wxD2DRenderer::CreateSubBitmap(const GraphicsBitmapData& bitmap, wxDouble x, wxDouble y, wxDouble w, wxDouble h)
{
	typedef wxD2DBitmapData::NativeType* NativeBitmap;
	wxBitmap sourceBitmap = static_cast<NativeBitmap>(bitmap.GetNativeBitmap())->GetSourceBitmap();
	return CreateBitmap(sourceBitmap.GetSubBitmap(wxRect(x, y, w, h)));
}

wxString wxD2DRenderer::GetName() const
{
	return "direct2d";
}

void wxD2DRenderer::GetVersion(int* major, int* minor, int* micro) const
{
	if (wxDirect2D::HasDirect2DSupport())
	{
		if (major)
			*major = 1;

		if (minor)
		{
			switch(wxDirect2D::GetDirect2DVersion())
			{
			case wxDirect2D::wxD2D_VERSION_1_0:
				*minor = 0;
				break;
			case wxDirect2D::wxD2D_VERSION_1_1:
				*minor = 1;
				break;
			case wxDirect2D::wxD2D_VERSION_NONE:
				// This is not supposed to happen, but we handle this value in
				// the switch to ensure that we'll get warnings if any new
				// values, not handled here, are added to the enum later.
				*minor = -1;
				break;
			}
		}

		if (micro)
			*micro = 0;
	}
}

ID2D1Factory* wxD2DRenderer::GetD2DFactory()
{
	return m_direct2dFactory;
}

ID2D1Factory* wxGetD2DFactory(wxD2DRenderer* renderer)
{
	return renderer->GetD2DFactory();
}

// ----------------------------------------------------------------------------
// Module ensuring all global/singleton objects are destroyed on shutdown.
// ----------------------------------------------------------------------------

class wxDirect2DModule : public wxModule
{
public:
	wxDirect2DModule()
	{
	}

	virtual bool OnInit() override
	{
		HRESULT hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		// RPC_E_CHANGED_MODE is not considered as an error
		// - see remarks for wxOleInitialize().
		return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
	}

	virtual void OnExit() override
	{
		if ( gs_WICImagingFactory )
		{
			gs_WICImagingFactory->Release();
			gs_WICImagingFactory = NULL;
		}

		if ( gs_IDWriteFactory )
		{
			gs_IDWriteFactory->Release();
			gs_IDWriteFactory = NULL;
		}

		if ( gs_D2DRenderer )
		{
			delete gs_D2DRenderer;
			gs_D2DRenderer = NULL;
		}

		if ( gs_ID2D1Factory )
		{
			gs_ID2D1Factory->Release();
			gs_ID2D1Factory = NULL;
		}

		::CoUninitialize();
	}

private:
	wxDECLARE_DYNAMIC_CLASS(wxDirect2DModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxDirect2DModule, wxModule);

GraphicsContext * Create(const wxWindowDC& dc)
{
	wxD2DRenderer * renderer = wxD2DRenderer::GetDirect2DRenderer();
	return renderer->CreateContext(dc);
}

GraphicsContext * Create(const wxMemoryDC& dc)
{
	wxD2DRenderer * renderer = wxD2DRenderer::GetDirect2DRenderer();
	return renderer->CreateContext(dc);
}

GraphicsContext * Create(wxWindow* window)
{
	wxD2DRenderer * renderer = wxD2DRenderer::GetDirect2DRenderer();
	return renderer->CreateContext(window);
}

void GraphicsContext::StrokeLine(wxDouble x1, wxDouble y1, wxDouble x2, wxDouble y2)
{
	GraphicsPathData * path = CreatePath();
	path->MoveToPoint(x1, y1);
	path->AddLineToPoint(x2, y2);
	StrokePath(path);
}

void GraphicsContext::DrawTextU(const wxString& str, wxDouble x, wxDouble y)
{
	DoDrawText(str, x, y);
}

GraphicsRenderer* GraphicsRenderer::GetDirect2DRenderer()
{
	return wxD2DRenderer::GetDirect2DRenderer();
}
#endif // wxUSE_GRAPHICS_DIRECT2D

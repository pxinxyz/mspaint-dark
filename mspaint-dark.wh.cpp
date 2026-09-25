// ==WindhawkMod==
// @id              mspaint-dark
// @name            Dark Paint
// @description     Complete dark mode for Microsoft Paint (Windows 10 & 11) with dark canvas spawn, dark dialogs, and flicker-free resizing
// @version         1.2.0
// @author          pxin
// @github          https://github.com/pxinxyz
// @include         mspaint.exe
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -luxtheme -lgdi32 -lcomctl32 -ldwmapi
// @architecture    x86-64
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Dark Paint (Windows 10 & 11)
A comprehensive, production-grade dark mode for Microsoft Paint on both Windows 10 (classic Win32 / Ribbon)
and Windows 11 (modern WinUI / XAML), featuring dark canvas spawning, themed dialogs, and smooth flicker-free resizing.

## Features
- **Windows 10 Complete Dark Mode**:
  - Dark title bar and non-client frame via undocumented DWM composition attributes
  - Fully themed Windows Ribbon framework (tabs, command bar, QAT, app menu)
  - Dark workspace area with custom gradient fill interception
  - Dark status bar with high-contrast zoom icons, dark slider, and custom dark size grip
  - Smooth, flicker-free window resizing and movement (eliminating white flash)
- **Dark Task Dialogs**:
  - Themed close / save-confirmation prompt ("Do you want to save changes to Untitled?")
  - Custom dark styling with keyboard shortcuts (Enter, Escape, Tab navigation)
- **Dark Canvas Spawning**:
  - Automatically initializes new Paint documents with a dark canvas instead of blinding white
  - Synchronizes primary (pencil = white) and secondary (eraser = dark canvas) palette colors
- **Windows 11 Compatibility**:
  - Automatically requests Dark theme for the modern WinUI / XAML Paint application
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- darkCanvas: true
  $name: Spawn dark canvas
  $description: Automatically spawn new canvases with a dark background.
- canvasColor: "#202020"
  $name: Canvas color
  $description: Hex color for the canvas background (e.g. #202020 or #1e1e1e or #000000).
- workspaceColor: "#282828"
  $name: Workspace color
  $description: Hex color for the workspace surrounding the canvas.
- ribbonBrightness: 38
  $name: Ribbon brightness
  $description: Brightness level of the ribbon on Windows 10 (0 to 100, recommended 32 to 45).
*/
// ==/WindhawkModSettings==

#undef GetCurrentTime

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <vssym32.h>
#include <propidl.h>
#include <roapi.h>
#include <winstring.h>
#include <winrt/Windows.UI.Xaml.h>
#include <stdio.h>
#include <stdint.h>

#include <windhawk_api.h>
#include <windhawk_utils.h>

#define UI_MAKE_HSB(h, s, b) ((DWORD)(((BYTE)(h)) | (((WORD)((BYTE)(s))) << 8) | (((DWORD)((BYTE)(b))) << 16)))

// Ribbon Framework Property Keys
// DEFINE_UIPROPERTYKEY(name, type, index) -> fmtid = { index, 0x7363, 0x696e, ... }, pid = type
const PROPERTYKEY UI_PKEY_GlobalBackgroundColor = { { 2000, 0x7363, 0x696e, { 0x84, 0x41, 0x79, 0x8a, 0xcf, 0x5a, 0xeb, 0xb7 } }, VT_UI4 };
const PROPERTYKEY UI_PKEY_GlobalHighlightColor  = { { 2001, 0x7363, 0x696e, { 0x84, 0x41, 0x79, 0x8a, 0xcf, 0x5a, 0xeb, 0xb7 } }, VT_UI4 };
const PROPERTYKEY UI_PKEY_GlobalTextColor       = { { 2002, 0x7363, 0x696e, { 0x84, 0x41, 0x79, 0x8a, 0xcf, 0x5a, 0xeb, 0xb7 } }, VT_UI4 };
const PROPERTYKEY UI_PKEY_DarkModeRibbon        = { { 2004, 0x7363, 0x696e, { 0x84, 0x41, 0x79, 0x8a, 0xcf, 0x5a, 0xeb, 0xb7 } }, 11 };

#ifndef VARIANT_TRUE
#define VARIANT_TRUE ((VARIANT_BOOL)-1)
#endif

const CLSID CLSID_UIRibbonFramework_Val = { 0x926749fa, 0x2615, 0x4987, { 0x88, 0x45, 0xc3, 0x3e, 0x65, 0xf2, 0xb9, 0x57 } };
const IID IID_IUIFramework_Val          = { 0xf4f0385d, 0x6872, 0x43a8, { 0xad, 0x09, 0x4c, 0x33, 0x9c, 0xb3, 0xf5, 0xc5 } };
const IID IID_IPropertyStore_Val        = { 0x886d8eeb, 0x8cf2, 0x4446, { 0x8d, 0x02, 0xcd, 0xba, 0x1d, 0xbd, 0xcf, 0x99 } };

// IPropertyStore definition
struct IPropertyStore : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetCount(DWORD *cProps) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAt(DWORD iProp, PROPERTYKEY *pkey) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetValue(const PROPERTYKEY &key, PROPVARIANT *pv) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetValue(const PROPERTYKEY &key, const PROPVARIANT &propvar) = 0;
    virtual HRESULT STDMETHODCALLTYPE Commit(void) = 0;
};

// Settings
struct {
    bool darkCanvas;
    COLORREF canvasColor;
    COLORREF workspaceColor;
    BYTE ribbonBrightness;
} g_settings = {
    true,
    RGB(32, 32, 32),
    RGB(40, 40, 40),
    38
};

// State
DWORD g_buildNumber = 0;
bool g_win32DarkModeSupported = false;
bool g_darkModeEnabled = true;
bool g_inSizeMove = false;
int g_topLineTimerTicks = 0;
HWND g_hMainWnd = NULL;
HWND g_hStatusBarWnd = NULL;
HTHEME g_menuTheme = NULL;
HBRUSH g_hWorkspaceBrush = NULL;
HBRUSH g_hCanvasBrush = NULL;
HMODULE g_hPaintExe = NULL;
uintptr_t g_paintExeBase = 0;
uintptr_t g_paintExeSize = 0;
void* g_pRibbonFramework = NULL;

void ModLog(const wchar_t* format, ...) {
    wchar_t buf[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buf, sizeof(buf)/sizeof(buf[0]), _TRUNCATE, format, args);
    va_end(args);

    Wh_Log(L"%s", buf);
    OutputDebugStringW(buf);
}

COLORREF ParseColorHex(PCWSTR str, COLORREF defaultColor) {
    if (!str || !*str) return defaultColor;
    if (*str == L'#') str++;
    wchar_t* end = nullptr;
    unsigned long val = wcstoul(str, &end, 16);
    if (end == str) return defaultColor;
    BYTE r = (BYTE)((val >> 16) & 0xFF);
    BYTE g = (BYTE)((val >> 8) & 0xFF);
    BYTE b = (BYTE)(val & 0xFF);
    return RGB(r, g, b);
}

void LoadSettings() {
    PCWSTR darkCanvasStr = Wh_GetStringSetting(L"darkCanvas");
    if (darkCanvasStr) {
        g_settings.darkCanvas = (wcscmp(darkCanvasStr, L"0") != 0 && _wcsicmp(darkCanvasStr, L"false") != 0);
        Wh_FreeStringSetting(darkCanvasStr);
    } else {
        g_settings.darkCanvas = true; // Default to true!
    }

    PCWSTR canvasColorStr = Wh_GetStringSetting(L"canvasColor");
    g_settings.canvasColor = ParseColorHex(canvasColorStr, RGB(32, 32, 32));
    Wh_FreeStringSetting(canvasColorStr);

    PCWSTR workspaceColorStr = Wh_GetStringSetting(L"workspaceColor");
    g_settings.workspaceColor = ParseColorHex(workspaceColorStr, RGB(40, 40, 40));
    Wh_FreeStringSetting(workspaceColorStr);

    int ribbonBri = Wh_GetIntSetting(L"ribbonBrightness");
    if (ribbonBri <= 0 || ribbonBri > 100) ribbonBri = 38;
    g_settings.ribbonBrightness = (BYTE)(ribbonBri * 255 / 100);

    if (g_hWorkspaceBrush) DeleteObject(g_hWorkspaceBrush);
    g_hWorkspaceBrush = CreateSolidBrush(g_settings.workspaceColor);

    if (g_hCanvasBrush) DeleteObject(g_hCanvasBrush);
    g_hCanvasBrush = CreateSolidBrush(g_settings.canvasColor);

    ModLog(L"Settings loaded: darkCanvas=%d, canvasColor=0x%06X, workspaceColor=0x%06X, ribbonBrightness=%d",
           g_settings.darkCanvas ? 1 : 0, g_settings.canvasColor, g_settings.workspaceColor, g_settings.ribbonBrightness);
}

#pragma region Win10 Uxtheme Undocumented

enum class PreferredAppMode {
    Default,
    AllowDark,
    ForceDark,
    ForceLight,
    Max
};

enum WINDOWCOMPOSITIONATTRIB {
    WCA_UNDEFINED = 0,
    WCA_USEDARKMODECOLORS = 26,
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

using fnSetPreferredAppMode = PreferredAppMode (WINAPI *)(PreferredAppMode appMode);
using fnAllowDarkModeForApp = bool (WINAPI *)(bool allow);
using fnAllowDarkModeForWindow = bool (WINAPI *)(HWND hWnd, bool allow);
using fnFlushMenuThemes = void (WINAPI *)();
using fnRefreshImmersiveColorPolicyState = void (WINAPI *)();
using fnSetWindowCompositionAttribute = BOOL (WINAPI *)(HWND hWnd, WINDOWCOMPOSITIONATTRIBDATA*);

fnSetPreferredAppMode _SetPreferredAppMode = nullptr;
fnAllowDarkModeForApp _AllowDarkModeForApp = nullptr;
fnAllowDarkModeForWindow _AllowDarkModeForWindow = nullptr;
fnFlushMenuThemes _FlushMenuThemes = nullptr;
fnRefreshImmersiveColorPolicyState _RefreshImmersiveColorPolicyState = nullptr;
fnSetWindowCompositionAttribute _SetWindowCompositionAttribute = nullptr;

void InitWin32DarkMode() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        using fnRtlGetNtVersionNumbers = void (WINAPI *)(LPDWORD, LPDWORD, LPDWORD);
        auto RtlGetNtVersionNumbers = (fnRtlGetNtVersionNumbers)GetProcAddress(hNtdll, "RtlGetNtVersionNumbers");
        if (RtlGetNtVersionNumbers) {
            DWORD major = 0, minor = 0;
            RtlGetNtVersionNumbers(&major, &minor, &g_buildNumber);
            g_buildNumber &= ~0xF0000000;
        }
    }

    if (g_buildNumber >= 17763) {
        HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (hUxtheme) {
            _RefreshImmersiveColorPolicyState = (fnRefreshImmersiveColorPolicyState)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(104));
            _AllowDarkModeForWindow = (fnAllowDarkModeForWindow)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133));

            auto ord135 = GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
            if (g_buildNumber < 18362) {
                _AllowDarkModeForApp = (fnAllowDarkModeForApp)ord135;
            } else {
                _SetPreferredAppMode = (fnSetPreferredAppMode)ord135;
            }

            _FlushMenuThemes = (fnFlushMenuThemes)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136));

            HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
            if (hUser32) {
                _SetWindowCompositionAttribute = (fnSetWindowCompositionAttribute)GetProcAddress(hUser32, "SetWindowCompositionAttribute");
            }

            g_win32DarkModeSupported = true;
        }
    }

    if (g_win32DarkModeSupported) {
        if (_SetPreferredAppMode) {
            _SetPreferredAppMode(PreferredAppMode::ForceDark);
        } else if (_AllowDarkModeForApp) {
            _AllowDarkModeForApp(true);
        }
        if (_FlushMenuThemes) {
            _FlushMenuThemes();
        }
    }
}

void SetTitleBarDarkMode(HWND hWnd, BOOL dark) {
    if (!hWnd || !IsWindow(hWnd)) return;

    BOOL dwmDark = dark;
    DwmSetWindowAttribute(hWnd, 20, &dwmDark, sizeof(dwmDark));
    DwmSetWindowAttribute(hWnd, 19, &dwmDark, sizeof(dwmDark));

    COLORREF darkBorder = dark ? RGB(0, 0, 0) : 0xFFFFFFFF;
    DwmSetWindowAttribute(hWnd, 34, &darkBorder, sizeof(darkBorder)); // DWMWA_BORDER_COLOR
    COLORREF darkCaption = dark ? RGB(0, 0, 0) : 0xFFFFFFFF;
    DwmSetWindowAttribute(hWnd, 35, &darkCaption, sizeof(darkCaption)); // DWMWA_CAPTION_COLOR

    if (g_buildNumber < 18362) {
        SetPropW(hWnd, L"UseImmersiveDarkModeColors", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(dark)));
    } else if (_SetWindowCompositionAttribute) {
        WINDOWCOMPOSITIONATTRIBDATA data = { WCA_USEDARKMODECOLORS, &dark, sizeof(dark) };
        _SetWindowCompositionAttribute(hWnd, &data);
    }
}

#pragma endregion Win10 Uxtheme Undocumented

#pragma region UAH Dark Menu Bar

#define WM_UAHDRAWMENU         0x0091
#define WM_UAHDRAWMENUITEM     0x0092

typedef union tagUAHMENUITEMMETRICS {
    struct { DWORD cx; DWORD cy; } rgsizeBar[2];
    struct { DWORD cx; DWORD cy; } rgsizePopup[4];
} UAHMENUITEMMETRICS;

typedef struct tagUAHMENUPOPUPMETRICS {
    DWORD rgcx[4];
    DWORD fUpdateMaxWidths : 2;
} UAHMENUPOPUPMETRICS;

typedef struct tagUAHMENU {
    HMENU hmenu;
    HDC hdc;
    DWORD dwFlags;
} UAHMENU;

typedef struct tagUAHMENUITEM {
    int iPosition;
    UAHMENUITEMMETRICS umim;
    UAHMENUPOPUPMETRICS umpm;
} UAHMENUITEM;

typedef struct UAHDRAWMENUITEM {
    DRAWITEMSTRUCT dis;
    UAHMENU um;
    UAHMENUITEM umi;
} UAHDRAWMENUITEM;

bool UAHDarkModeWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* lr) {
    switch (message) {
    case WM_UAHDRAWMENU: {
        UAHMENU* pUDM = (UAHMENU*)lParam;
        RECT rc = { 0 };
        MENUBARINFO mbi = { sizeof(mbi) };
        GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi);
        RECT rcWindow;
        GetWindowRect(hWnd, &rcWindow);
        rc = mbi.rcBar;
        OffsetRect(&rc, -rcWindow.left, -rcWindow.top);
        rc.top -= 1;
        if (!g_menuTheme) {
            g_menuTheme = OpenThemeData(hWnd, L"Menu");
        }
        if (g_menuTheme) {
            DrawThemeBackground(g_menuTheme, pUDM->hdc, MENU_POPUPITEM, MPI_NORMAL, &rc, nullptr);
        } else {
            FillRect(pUDM->hdc, &rc, g_hWorkspaceBrush);
        }
        *lr = 0;
        return true;
    }
    case WM_UAHDRAWMENUITEM: {
        UAHDRAWMENUITEM* pUDMI = (UAHDRAWMENUITEM*)lParam;
        wchar_t menuString[256] = { 0 };
        MENUITEMINFO mii = { sizeof(mii), MIIM_STRING };
        mii.dwTypeData = menuString;
        mii.cch = (sizeof(menuString) / 2) - 1;
        GetMenuItemInfo(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii);

        DWORD dwFlags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
        int iTextStateID = MPI_NORMAL;
        int iBackgroundStateID = MPI_NORMAL;
        if ((pUDMI->dis.itemState & ODS_INACTIVE) | (pUDMI->dis.itemState & ODS_DEFAULT)) {
            iTextStateID = MPI_NORMAL;
            iBackgroundStateID = MPI_NORMAL;
        }
        if (pUDMI->dis.itemState & ODS_HOTLIGHT) {
            iTextStateID = MPI_HOT;
            iBackgroundStateID = MPI_HOT;
        }
        if (pUDMI->dis.itemState & ODS_SELECTED) {
            iTextStateID = MPI_HOT;
            iBackgroundStateID = MPI_HOT;
        }
        if ((pUDMI->dis.itemState & ODS_GRAYED) || (pUDMI->dis.itemState & ODS_DISABLED)) {
            iTextStateID = MPI_DISABLED;
            iBackgroundStateID = MPI_DISABLED;
        }
        if (pUDMI->dis.itemState & ODS_NOACCEL) {
            dwFlags |= DT_HIDEPREFIX;
        }

        if (!g_menuTheme) {
            g_menuTheme = OpenThemeData(hWnd, L"Menu");
        }
        if (g_menuTheme) {
            DrawThemeBackground(g_menuTheme, pUDMI->um.hdc, MENU_POPUPITEM, iBackgroundStateID, &pUDMI->dis.rcItem, nullptr);
            DrawThemeText(g_menuTheme, pUDMI->um.hdc, MENU_POPUPITEM, iTextStateID, menuString, mii.cch, dwFlags, 0, &pUDMI->dis.rcItem);
        }
        *lr = 0;
        return true;
    }
    case WM_THEMECHANGED: {
        if (g_menuTheme) {
            CloseThemeData(g_menuTheme);
            g_menuTheme = nullptr;
        }
        return false;
    }
    default:
        return false;
    }
}

#pragma endregion UAH Dark Menu Bar

#pragma region Zoom Button Hooks

// Forward declaration
LRESULT CALLBACK StatusBarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, DWORD_PTR dwRefData);

// CAppletPopoutButton::CreateGDIObjects
using CreateGDIObjects_t = void (__fastcall *)(void* pThis, COLORREF color);
CreateGDIObjects_t pOriginalCreateGDIObjects = nullptr;

void __fastcall CreateGDIObjectsHook(void* pThis, COLORREF color) {
    if (g_darkModeEnabled) {
        color = g_settings.workspaceColor;
    }
    pOriginalCreateGDIObjects(pThis, color);
}

// CAppletPopoutButton::UpdateIcons
using UpdateIcons_t = void (__fastcall *)(void* pThis, int iconBase);
UpdateIcons_t pOriginalUpdateIcons = nullptr;

// CStatBar::UpdateZoomControlIcons
using UpdateZoomControlIcons_t = void (__fastcall *)(void* pThis);
UpdateZoomControlIcons_t pOriginalUpdateZoomControlIcons = nullptr;

void __fastcall UpdateZoomControlIconsHook(void* pThis) {
    if (g_darkModeEnabled) {
        int iconBase = 6471;
        void* pZoomOut = (char*)pThis + 1120;
        void* pZoomIn = (char*)pThis + 896;
        if (*((void**)pThis + 49) && pOriginalUpdateIcons) {
            pOriginalUpdateIcons(pZoomOut, iconBase);
            pOriginalUpdateIcons(pZoomIn, iconBase + 5);
            return;
        }
    }
    if (pOriginalUpdateZoomControlIcons) {
        pOriginalUpdateZoomControlIcons(pThis);
    }
}

// CAppletPopoutButton::DrawItem
using PopoutButton_DrawItem_t = void (__fastcall *)(void* pThis, DRAWITEMSTRUCT* pDIS);
PopoutButton_DrawItem_t pOriginalPopoutButtonDrawItem = nullptr;

void __fastcall PopoutButtonDrawItemHook(void* pThis, DRAWITEMSTRUCT* pDIS) {
    if (g_darkModeEnabled && pDIS) {
        FillRect(pDIS->hDC, &pDIS->rcItem, g_hWorkspaceBrush);
    }
    if (pOriginalPopoutButtonDrawItem) {
        pOriginalPopoutButtonDrawItem(pThis, pDIS);
    }
}

extern void** g_ppStatBarWnd;

void UpdateStatusZoomControls() {
    void* pStatBar = nullptr;
    if (g_ppStatBarWnd && *g_ppStatBarWnd) {
        pStatBar = *g_ppStatBarWnd;
    } else if (g_paintExeBase && g_buildNumber == 19045) {
        void** ppStatBar = (void**)(g_paintExeBase + 0xDD680);
        if (ppStatBar && *ppStatBar) pStatBar = *ppStatBar;
    }
    if (!pStatBar) return;
    void* pZoomOut = (char*)pStatBar + 1120;
    void* pZoomIn = (char*)pStatBar + 896;

    if (pOriginalCreateGDIObjects) {
        pOriginalCreateGDIObjects(pZoomOut, g_settings.workspaceColor);
        pOriginalCreateGDIObjects(pZoomIn, g_settings.workspaceColor);
    }
    if (pOriginalUpdateIcons && *((void**)pStatBar + 49)) {
        pOriginalUpdateIcons(pZoomOut, 6471);
        pOriginalUpdateIcons(pZoomIn, 6476);
    }
    HWND hStatusBar = *((HWND*)pStatBar + 8);
    if (hStatusBar && IsWindow(hStatusBar)) {
        InvalidateRect(hStatusBar, NULL, TRUE);
    }
}

#pragma endregion Zoom Button Hooks

#pragma region Status Bar & Zoom Controls Theming

LRESULT CALLBACK ZoomControlSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, g_hWorkspaceBrush);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (hdc) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            HWND hChild = GetWindow(hWnd, GW_CHILD);
            while (hChild) {
                if (IsWindowVisible(hChild)) {
                    RECT rcChild;
                    GetWindowRect(hChild, &rcChild);
                    MapWindowPoints(NULL, hWnd, (POINT*)&rcChild, 2);
                    ExcludeClipRect(hdc, rcChild.left, rcChild.top, rcChild.right, rcChild.bottom);
                }
                hChild = GetWindow(hChild, GW_HWNDNEXT);
            }
            FillRect(hdc, &rc, g_hWorkspaceBrush);
            EndPaint(hWnd, &ps);
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(220, 220, 220));
        SetBkColor(hdc, g_settings.workspaceColor);
        SetDCBrushColor(hdc, g_settings.workspaceColor);
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)g_hWorkspaceBrush;
    }
    case WM_PRINTCLIENT: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, g_hWorkspaceBrush);
        return 0;
    }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK TrackbarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK StaticSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void ThemeStatusBarChild(HWND hWndChild) {
    if (!hWndChild || !IsWindow(hWndChild)) return;

    WCHAR className[128] = L"";
    GetClassNameW(hWndChild, className, ARRAYSIZE(className));
    LONG_PTR id = GetWindowLongPtrW(hWndChild, GWLP_ID);

    if (_AllowDarkModeForWindow) {
        _AllowDarkModeForWindow(hWndChild, true);
    }

    if (wcsncmp(className, L"Afx:", 4) == 0 || id == 0xD001) {
        SetWindowLongPtrW(hWndChild, GWL_STYLE, GetWindowLongPtrW(hWndChild, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        WindhawkUtils::SetWindowSubclassFromAnyThread(hWndChild, ZoomControlSubclassProc, 0);
        InvalidateRect(hWndChild, NULL, TRUE);
    }
    else if (wcsicmp(className, L"msctls_trackbar32") == 0 || id == 0xD006) {
        SetWindowTheme(hWndChild, L"DarkMode_Explorer", NULL);
        WindhawkUtils::SetWindowSubclassFromAnyThread(hWndChild, TrackbarSubclassProc, 0);
        InvalidateRect(hWndChild, NULL, TRUE);
    }
    else if (wcsicmp(className, L"Static") == 0 || id == 0xD003) {
        SetWindowTheme(hWndChild, L"DarkMode_Explorer", NULL);
        WindhawkUtils::SetWindowSubclassFromAnyThread(hWndChild, StaticSubclassProc, 0);
        InvalidateRect(hWndChild, NULL, TRUE);
    }
    else if (wcsicmp(className, L"Button") == 0 || id == 0xD004 || id == 0xD005) {
        SetWindowTheme(hWndChild, L"DarkMode_Explorer", NULL);
        InvalidateRect(hWndChild, NULL, TRUE);
    }
}

BOOL CALLBACK SubclassStatusBarChildrenEnumProc(HWND hWndChild, LPARAM lParam) {
    ThemeStatusBarChild(hWndChild);
    return TRUE;
}

void ThemeStatusBarAndChildren(HWND hStatusBar) {
    if (!hStatusBar || !IsWindow(hStatusBar)) return;
    g_hStatusBarWnd = hStatusBar;

    if (_AllowDarkModeForWindow) {
        _AllowDarkModeForWindow(hStatusBar, true);
    }
    SetWindowTheme(hStatusBar, L"DarkMode_Explorer", NULL);
    SetWindowLongPtrW(hStatusBar, GWL_STYLE, GetWindowLongPtrW(hStatusBar, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    WindhawkUtils::SetWindowSubclassFromAnyThread(hStatusBar, StatusBarSubclassProc, 0);

    EnumChildWindows(hStatusBar, SubclassStatusBarChildrenEnumProc, 0);
    UpdateStatusZoomControls();
}

LRESULT CALLBACK StatusBarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (hdc) {
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            int w = clientRect.right - clientRect.left;
            int h = clientRect.bottom - clientRect.top;

            if (w > 0 && h > 0) {
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
                HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

                // Exclude all child windows (Zoom controls) so we NEVER paint over them
                HWND hChild = GetWindow(hWnd, GW_CHILD);
                while (hChild) {
                    if (IsWindowVisible(hChild)) {
                        RECT rcChild;
                        GetWindowRect(hChild, &rcChild);
                        MapWindowPoints(NULL, hWnd, (POINT*)&rcChild, 2);
                        ExcludeClipRect(memDC, rcChild.left, rcChild.top, rcChild.right, rcChild.bottom);
                        ExcludeClipRect(hdc, rcChild.left, rcChild.top, rcChild.right, rcChild.bottom);
                    }
                    hChild = GetWindow(hChild, GW_HWNDNEXT);
                }

                FillRect(memDC, &clientRect, g_hWorkspaceBrush);

                INT blockCoord[32];
                INT blockCount = (INT)SendMessageW(hWnd, SB_GETPARTS, ARRAYSIZE(blockCoord), (LPARAM)blockCoord);

                HFONT hFont = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
                HGDIOBJ hOldFont = hFont ? SelectObject(memDC, hFont) : nullptr;
                SetBkMode(memDC, TRANSPARENT);
                SetTextColor(memDC, RGB(220, 220, 220));

                for (INT i = 0; i < blockCount; i++) {
                    RECT blockRect;
                    if (!SendMessageW(hWnd, SB_GETRECT, i, (LPARAM)&blockRect))
                        continue;

                    HICON hIcon = (HICON)SendMessageW(hWnd, SB_GETICON, i, 0);
                    int textOffset = 4;
                    if (hIcon) {
                        int iconY = blockRect.top + (blockRect.bottom - blockRect.top - 16) / 2;
                        DrawIconEx(memDC, blockRect.left + 3, iconY, hIcon, 16, 16, 0, NULL, DI_NORMAL);
                        textOffset += 21;
                    }

                    RECT textRect = blockRect;
                    textRect.left += textOffset;

                    WCHAR buffer[MAX_PATH] = L"";
                    SendMessageW(hWnd, SB_GETTEXTW, i, (LPARAM)buffer);

                    DrawTextW(memDC, buffer, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_HIDEPREFIX);
                }

                // Dark size gripper dots
                RECT sizeGripRect;
                sizeGripRect.left = clientRect.right - GetSystemMetrics(SM_CXHSCROLL);
                sizeGripRect.top = clientRect.bottom - GetSystemMetrics(SM_CYVSCROLL);
                sizeGripRect.right = clientRect.right;
                sizeGripRect.bottom = clientRect.bottom;
                FillRect(memDC, &sizeGripRect, g_hWorkspaceBrush);

                COLORREF dotDark = RGB(55, 55, 55);
                COLORREF dotLight = RGB(115, 115, 115);
                for (int i = 1; i <= 3; i++) {
                    for (int j = 1; j <= (4 - i); j++) {
                        int x = sizeGripRect.right - (i * 4) + 1;
                        int y = sizeGripRect.bottom - (j * 4) + 1;
                        SetPixel(memDC, x, y, dotDark);
                        SetPixel(memDC, x + 1, y + 1, dotLight);
                    }
                }

                if (hOldFont) SelectObject(memDC, hOldFont);

                BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

                SelectObject(memDC, oldBmp);
                DeleteObject(memBmp);
                DeleteDC(memDC);
            }

            EndPaint(hWnd, &ps);
            return 0;
        }
        break;
    }
    case WM_SIZE: {
        UpdateStatusZoomControls();
        break;
    }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

#pragma endregion Status Bar & Zoom Controls Theming

#pragma region Ribbon Theming

typedef HRESULT (STDMETHODCALLTYPE *FlushPendingInvalidations_t)(void* pThis);

void ApplyRibbonDarkMode(void* pFramework) {
    if (!pFramework) return;
    IUnknown* pUnk = (IUnknown*)pFramework;
    IPropertyStore* pPropStore = nullptr;
    HRESULT hr = pUnk->QueryInterface(IID_IPropertyStore_Val, (void**)&pPropStore);
    ModLog(L"ApplyRibbonDarkMode: QueryInterface(IPropertyStore) = 0x%08X, pPropStore = %p", hr, pPropStore);
    if (SUCCEEDED(hr) && pPropStore) {
        PROPVARIANT varDark;
        PropVariantInit(&varDark);
        varDark.vt = VT_BOOL;
        varDark.boolVal = VARIANT_TRUE;
        HRESULT hrDark = pPropStore->SetValue(UI_PKEY_DarkModeRibbon, varDark);

        PROPVARIANT varBg;
        PropVariantInit(&varBg);
        varBg.vt = VT_UI4;
        varBg.ulVal = UI_MAKE_HSB(0, 0, g_settings.ribbonBrightness);

        PROPVARIANT varText;
        PropVariantInit(&varText);
        varText.vt = VT_UI4;
        varText.ulVal = UI_MAKE_HSB(0, 0, 245);

        HRESULT hrBg = pPropStore->SetValue(UI_PKEY_GlobalBackgroundColor, varBg);
        HRESULT hrTx = pPropStore->SetValue(UI_PKEY_GlobalTextColor, varText);
        HRESULT hrCommit = pPropStore->Commit();
        pPropStore->Release();

        ModLog(L"Ribbon IPropertyStore: SetDark=0x%08X, SetBg=0x%08X, SetTx=0x%08X, Commit=0x%08X (brightness=%d)", 
               hrDark, hrBg, hrTx, hrCommit, g_settings.ribbonBrightness);
    }

    void** vtbl = *(void***)pFramework;
    if (vtbl && vtbl[10]) {
        FlushPendingInvalidations_t pfnFlush = (FlushPendingInvalidations_t)vtbl[10];
        pfnFlush(pFramework);
    }
}

void* GetRibbonFramework() {
    if (g_pRibbonFramework) return g_pRibbonFramework;
    if (g_paintExeBase) {
        void** ppFramework = (void**)(g_paintExeBase + 0xDE910);
        if (ppFramework && *ppFramework) {
            g_pRibbonFramework = *ppFramework;
            return g_pRibbonFramework;
        }
    }
    return nullptr;
}

void DrawTopBlackLine(HWND hWnd);

using LoadUI_t = HRESULT (STDMETHODCALLTYPE *)(void* pThis, HINSTANCE hInstance, LPCWSTR pszResourceName);
LoadUI_t pOriginalLoadUI = nullptr;

HRESULT STDMETHODCALLTYPE LoadUIHook(void* pThis, HINSTANCE hInstance, LPCWSTR pszResourceName) {
    HRESULT hr = pOriginalLoadUI(pThis, hInstance, pszResourceName);
    ModLog(L"LoadUIHook: resName = %s, hr = 0x%08X", pszResourceName ? pszResourceName : L"(null)", hr);
    if (SUCCEEDED(hr)) {
        g_pRibbonFramework = pThis;
        ApplyRibbonDarkMode(pThis);
        if (g_hMainWnd && IsWindow(g_hMainWnd)) {
            SetTitleBarDarkMode(g_hMainWnd, TRUE);
            SetWindowPos(g_hMainWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            DrawTopBlackLine(g_hMainWnd);
            g_topLineTimerTicks = 60;
            SetTimer(g_hMainWnd, 9998, 50, NULL);
        }
    }
    return hr;
}

using CoCreateInstance_t = decltype(&CoCreateInstance);
CoCreateInstance_t pOriginalCoCreateInstanceCombase = nullptr;
CoCreateInstance_t pOriginalCoCreateInstanceOle32 = nullptr;

HRESULT WINAPI CoCreateInstanceHookCommon(
    CoCreateInstance_t pOriginal,
    REFCLSID rclsid,
    LPUNKNOWN pUnkOuter,
    DWORD dwClsContext,
    REFIID riid,
    LPVOID *ppv)
{
    HRESULT hr = pOriginal(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv) {
        if (InlineIsEqualGUID(rclsid, CLSID_UIRibbonFramework_Val) &&
            InlineIsEqualGUID(riid, IID_IUIFramework_Val)) {
            g_pRibbonFramework = *ppv;
            void** vtbl = *(void***)(*ppv);
            if (vtbl && !pOriginalLoadUI) {
                Wh_SetFunctionHook(vtbl[5], (void*)LoadUIHook, (void**)&pOriginalLoadUI);
                ModLog(L"Hooked IUIFramework::LoadUI from CoCreateInstance (%p)", *ppv);
            }
        }
    }
    return hr;
}

HRESULT WINAPI CoCreateInstanceHookCombase(
    REFCLSID rclsid,
    LPUNKNOWN pUnkOuter,
    DWORD dwClsContext,
    REFIID riid,
    LPVOID *ppv)
{
    return CoCreateInstanceHookCommon(pOriginalCoCreateInstanceCombase, rclsid, pUnkOuter, dwClsContext, riid, ppv);
}

HRESULT WINAPI CoCreateInstanceHookOle32(
    REFCLSID rclsid,
    LPUNKNOWN pUnkOuter,
    DWORD dwClsContext,
    REFIID riid,
    LPVOID *ppv)
{
    return CoCreateInstanceHookCommon(pOriginalCoCreateInstanceOle32, rclsid, pUnkOuter, dwClsContext, riid, ppv);
}

#pragma endregion Ribbon Theming

#pragma region Main Window Subclass

BOOL CALLBACK ThemeChildWindowsEnumProc(HWND hWnd, LPARAM lParam) {
    WCHAR szClass[128] = L"";
    GetClassNameW(hWnd, szClass, ARRAYSIZE(szClass));
    if (wcsnicmp(szClass, L"UIRibbon", 8) == 0 ||
        wcsnicmp(szClass, L"NetUI", 5) == 0 ||
        wcsicmp(szClass, L"NUIPane") == 0 ||
        wcsicmp(szClass, L"MSPaintView") == 0) {
        return TRUE; // Do not corrupt Ribbon or Paint View theme cache
    }

    if (_AllowDarkModeForWindow) {
        _AllowDarkModeForWindow(hWnd, true);
    }
    SetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
    return TRUE;
}

void ThemeWindowTree(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;
    if (_AllowDarkModeForWindow) {
        _AllowDarkModeForWindow(hWnd, true);
    }
    EnumChildWindows(hWnd, ThemeChildWindowsEnumProc, 0);
}

LRESULT CALLBACK PaintViewSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_ERASEBKGND:
        if (g_inSizeMove) return 1;
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ImgWndSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_ERASEBKGND:
        if (g_inSizeMove) return 1;
        break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void DrawTopBlackLine(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;
    HDC hdc = GetWindowDC(hWnd);
    if (hdc) {
        RECT rcWnd;
        GetWindowRect(hWnd, &rcWnd);
        int w = rcWnd.right - rcWnd.left;
        if (w > 0) {
            RECT rcTop = { 0, 0, w, 2 };
            FillRect(hdc, &rcTop, (HBRUSH)GetStockObject(BLACK_BRUSH));
            GdiFlush();
        }
        ReleaseDC(hWnd, hdc);
    }
}

LRESULT CALLBACK MainFrameSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, DWORD_PTR dwRefData) {
    LRESULT lr = 0;
    if (g_win32DarkModeSupported && UAHDarkModeWndProc(hWnd, uMsg, wParam, lParam, &lr)) {
        return lr;
    }

    switch (uMsg) {
    case WM_ERASEBKGND:
        // Prevent DefWindowProc from painting the white class brush during resize
        return 1;

    case WM_PAINT: {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        DrawTopBlackLine(hWnd);
        return lr;
    }

    case WM_NCACTIVATE:
    case WM_NCPAINT: {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        DrawTopBlackLine(hWnd);
        return lr;
    }

    case WM_ACTIVATEAPP:
    case WM_ACTIVATE: {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        DrawTopBlackLine(hWnd);
        return lr;
    }

    case WM_WINDOWPOSCHANGED: {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        DrawTopBlackLine(hWnd);
        return lr;
    }

    case WM_MOVE:
    case WM_SIZE: {
        LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        DrawTopBlackLine(hWnd);
        return lr;
    }

    case WM_ENTERSIZEMOVE: {
        g_inSizeMove = true;
        break;
    }

    case WM_EXITSIZEMOVE: {
        g_inSizeMove = false;
        InvalidateRect(hWnd, NULL, TRUE);
        DrawTopBlackLine(hWnd);
        break;
    }

    case WM_WINDOWPOSCHANGING: {
        break;
    }

    case WM_SHOWWINDOW: {
        ThemeWindowTree(hWnd);
        SetTitleBarDarkMode(hWnd, TRUE);
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        DrawTopBlackLine(hWnd);
        g_topLineTimerTicks = 60;
        SetTimer(hWnd, 9998, 50, NULL);
        break;
    }

    case WM_TIMER: {
        if (wParam == 9998) {
            DrawTopBlackLine(hWnd);
            if (--g_topLineTimerTicks <= 0) {
                KillTimer(hWnd, 9998);
                g_topLineTimerTicks = 0;
            }
            return 0;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(220, 220, 220));
        SetBkColor(hdc, g_settings.workspaceColor);
        SetDCBrushColor(hdc, g_settings.workspaceColor);
        return (INT_PTR)g_hWorkspaceBrush;
    }
    case WM_SETTINGCHANGE: {
        if (_RefreshImmersiveColorPolicyState) {
            _RefreshImmersiveColorPolicyState();
        }
        if (_FlushMenuThemes) {
            _FlushMenuThemes();
        }
        SetTitleBarDarkMode(hWnd, TRUE);
        void* pFramework = GetRibbonFramework();
        if (pFramework) {
            ApplyRibbonDarkMode(pFramework);
        }
        ThemeWindowTree(hWnd);
        break;
    }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

#pragma endregion Main Window Subclass

#pragma region Canvas & Workspace Hooks

// Hook GdiGradientFill to render dark workspace surrounding canvas
using GdiGradientFill_t = decltype(&GdiGradientFill);
GdiGradientFill_t pOriginalGdiGradientFill = nullptr;

BOOL WINAPI GdiGradientFillHook(HDC hdc, PTRIVERTEX pVertex, ULONG nVertex, PVOID pMesh, ULONG nMesh, ULONG ulMode) {
    if (g_darkModeEnabled && pVertex && nVertex >= 2) {
        LONG minX = pVertex[0].x, maxX = pVertex[0].x;
        LONG minY = pVertex[0].y, maxY = pVertex[0].y;
        for (ULONG i = 1; i < nVertex; i++) {
            if (pVertex[i].x < minX) minX = pVertex[i].x;
            if (pVertex[i].x > maxX) maxX = pVertex[i].x;
            if (pVertex[i].y < minY) minY = pVertex[i].y;
            if (pVertex[i].y > maxY) maxY = pVertex[i].y;
        }
        RECT rc = { minX, minY, maxX, maxY };
        FillRect(hdc, &rc, g_hWorkspaceBrush);
        return TRUE;
    }
    return pOriginalGdiGradientFill(hdc, pVertex, nVertex, pMesh, nMesh, ulMode);
}

// Hook CreateSolidBrush to intercept white background brushes inside mspaint.exe
using CreateSolidBrush_t = decltype(&CreateSolidBrush);
CreateSolidBrush_t pOriginalCreateSolidBrush = nullptr;

HBRUSH WINAPI CreateSolidBrushHook(COLORREF color) {
    if (g_darkModeEnabled && g_settings.darkCanvas) {
        if ((color & 0x00FFFFFF) == 0x00FFFFFF) {
            uintptr_t ret = (uintptr_t)__builtin_return_address(0);
            if (ret >= g_paintExeBase && ret < g_paintExeBase + g_paintExeSize) {
                ModLog(L"CreateSolidBrushHook: replaced white brush with dark canvas color (caller: %p)", (void*)ret);
                return pOriginalCreateSolidBrush(g_settings.canvasColor);
            }
        }
    }
    return pOriginalCreateSolidBrush(color);
}

// Hook PatBlt to spawn canvas dark
using PatBlt_t = decltype(&PatBlt);
PatBlt_t pOriginalPatBlt = nullptr;

BOOL WINAPI PatBltHook(HDC hdc, int x, int y, int w, int h, DWORD rop) {
    if (g_darkModeEnabled && g_settings.darkCanvas && rop == PATCOPY && x == 0 && y == 0 && w > 0 && h > 0) {
        uintptr_t ret = (uintptr_t)__builtin_return_address(0);
        if (ret >= g_paintExeBase && ret < g_paintExeBase + g_paintExeSize) {
            HBRUSH hCurBrush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
            if (hCurBrush) {
                LOGBRUSH lb = {};
                if (GetObjectW(hCurBrush, sizeof(lb), &lb) && lb.lbStyle == BS_SOLID) {
                    if ((lb.lbColor & 0x00FFFFFF) == 0x00FFFFFF) {
                        ModLog(L"PatBltHook: replaced white PatBlt %dx%d with dark canvas brush (caller: %p)", w, h, (void*)ret);
                        HGDIOBJ hOld = SelectObject(hdc, g_hCanvasBrush);
                        BOOL res = pOriginalPatBlt(hdc, x, y, w, h, rop);
                        SelectObject(hdc, hOld);
                        return res;
                    }
                }
            }
        }
    }
    return pOriginalPatBlt(hdc, x, y, w, h, rop);
}

// Symbols for CColors::ResetColors, SetDrawColor, SetEraseColor
using ResetColors_t = void (__fastcall *)(void* pThis, int a2, bool a3);
ResetColors_t pOriginalResetColors = nullptr;

using SetDrawColor_t = void (__fastcall *)(COLORREF color);
SetDrawColor_t pOriginalSetDrawColor = nullptr;

using SetEraseColor_t = void (__fastcall *)(COLORREF color);
SetEraseColor_t pOriginalSetEraseColor = nullptr;

void __fastcall ResetColorsHook(void* pThis, int a2, bool a3) {
    pOriginalResetColors(pThis, a2, a3);
    if (g_darkModeEnabled && g_settings.darkCanvas) {
        ModLog(L"ResetColorsHook: resetting colors to dark canvas / white pencil");
        if (pOriginalSetDrawColor) {
            pOriginalSetDrawColor(RGB(255, 255, 255));
        }
        if (pOriginalSetEraseColor) {
            pOriginalSetEraseColor(g_settings.canvasColor);
        }
    }
}

using ClearImg_t = int (__fastcall *)(void* img);
ClearImg_t pOriginalClearImg = nullptr;

int __fastcall ClearImgHook(void* img) {
    ModLog(L"ClearImgHook: img = %p", img);
    int res = pOriginalClearImg(img);
    if (g_darkModeEnabled && g_settings.darkCanvas && img) {
        HDC hdc = *((HDC*)((char*)img + 24));
        int w = *((int*)((char*)img + 88));
        int h = *((int*)((char*)img + 92));
        ModLog(L"ClearImgHook: Painting canvas hdc=%p, %dx%d with dark brush %p (color 0x%06X)", 
               hdc, w, h, g_hCanvasBrush, g_settings.canvasColor);
        if (hdc && w > 0 && h > 0) {
            RECT rc = { 0, 0, w, h };
            FillRect(hdc, &rc, g_hCanvasBrush);
        }

        // Initialize drawing colors: pencil = White, eraser = dark canvas
        if (pOriginalSetDrawColor) {
            pOriginalSetDrawColor(RGB(255, 255, 255));
        }
        if (pOriginalSetEraseColor) {
            pOriginalSetEraseColor(g_settings.canvasColor);
        }
        extern COLORREF* g_pCrRight;
        extern COLORREF* g_pCrLeft;
        if (g_pCrLeft) {
            *g_pCrLeft = RGB(255, 255, 255);
        } else if (g_paintExeBase && g_buildNumber == 19045) {
            *(COLORREF*)(g_paintExeBase + 0xDFDF4) = RGB(255, 255, 255);
        }
        if (g_pCrRight) {
            *g_pCrRight = g_settings.canvasColor;
        } else if (g_paintExeBase && g_buildNumber == 19045) {
            *(COLORREF*)(g_paintExeBase + 0xDC644) = g_settings.canvasColor;
        }
    }
    return res;
}

using DrawDropShadow_t = void (__fastcall *)(HDC hdc, const RECT* pRect, const RECT* pClipRect, int shadowType);
DrawDropShadow_t pOriginalDrawDropShadow = nullptr;

void __fastcall DrawDropShadowHook(HDC hdc, const RECT* pRect, const RECT* pClipRect, int shadowType) {
    if (g_darkModeEnabled && pRect) {
        RECT rc = *pRect;
        if (rc.left > rc.right) { LONG t = rc.left; rc.left = rc.right; rc.right = t; }
        if (rc.top > rc.bottom) { LONG t = rc.top; rc.top = rc.bottom; rc.bottom = t; }
        FillRect(hdc, &rc, g_hWorkspaceBrush);
        return;
    }
    if (pOriginalDrawDropShadow) {
        pOriginalDrawDropShadow(hdc, pRect, pClipRect, shadowType);
    }
}

using OnEraseBkgnd_t = int (__fastcall *)(void* pThis, void* pDC);
OnEraseBkgnd_t pOriginalOnEraseBkgnd = nullptr;

int __fastcall OnEraseBkgndHook(void* pThis, void* pDC) {
    if (g_inSizeMove) {
        return 1;
    }
    if (pOriginalOnEraseBkgnd) {
        return pOriginalOnEraseBkgnd(pThis, pDC);
    }
    return 1;
}

COLORREF* g_pCrRight = nullptr;
COLORREF* g_pCrLeft = nullptr;
void** g_ppStatBarWnd = nullptr;

const WindhawkUtils::SYMBOL_HOOK g_symbolHooks[] = {
    {
        {
            L"ClearImg",
            L"?ClearImg@@YAHPEAUIMG@@@Z",
            L"int __cdecl ClearImg(struct IMG *)"
        },
        &pOriginalClearImg,
        ClearImgHook,
        true
    },
    {
        {
            L"CColors::ResetColors",
            L"?ResetColors@CColors@@QEAAXH_N@Z",
            L"void __cdecl CColors::ResetColors(int,bool)"
        },
        &pOriginalResetColors,
        ResetColorsHook,
        true
    },
    {
        {
            L"SetDrawColor",
            L"?SetDrawColor@@YAXK@Z",
            L"void __cdecl SetDrawColor(unsigned long)"
        },
        &pOriginalSetDrawColor,
        nullptr,
        true
    },
    {
        {
            L"SetEraseColor",
            L"?SetEraseColor@@YAXK@Z",
            L"void __cdecl SetEraseColor(unsigned long)"
        },
        &pOriginalSetEraseColor,
        nullptr,
        true
    },
    {
        {
            L"CWindowDecorator::DrawDropShadow",
            L"?DrawDropShadow@CWindowDecorator@@SAXPEAUHDC__@@AEBVCRect@@1W4SHADOWRECTTYPE@@@Z",
            L"public: static void __cdecl CWindowDecorator::DrawDropShadow(struct HDC__ *,class CRect const &,class CRect const &,enum SHADOWRECTTYPE)"
        },
        &pOriginalDrawDropShadow,
        DrawDropShadowHook,
        true
    },
    {
        {
            L"CImgWnd::OnEraseBkgnd",
            L"?OnEraseBkgnd@CImgWnd@@IEAAHPEAVCDC@@@Z",
            L"protected: int __cdecl CImgWnd::OnEraseBkgnd(class CDC *)"
        },
        (void**)&pOriginalOnEraseBkgnd,
        (void*)OnEraseBkgndHook,
        true
    },
    {
        {
            L"CAppletPopoutButton::CreateGDIObjects",
            L"?CreateGDIObjects@CAppletPopoutButton@@IEAAXK@Z",
            L"void __cdecl CAppletPopoutButton::CreateGDIObjects(unsigned long)",
            L"protected: void __cdecl CAppletPopoutButton::CreateGDIObjects(unsigned long)"
        },
        &pOriginalCreateGDIObjects,
        CreateGDIObjectsHook,
        true
    },
    {
        {
            L"CAppletPopoutButton::UpdateIcons",
            L"?UpdateIcons@CAppletPopoutButton@@QEAAXIH@Z",
            L"public: void __cdecl CAppletPopoutButton::UpdateIcons(unsigned int,int)",
            L"void __cdecl CAppletPopoutButton::UpdateIcons(unsigned int,int)"
        },
        (void**)&pOriginalUpdateIcons,
        nullptr,
        true
    },
    {
        {
            L"CStatBar::UpdateZoomControlIcons",
            L"?UpdateZoomControlIcons@CStatBar@@AEAAXXZ",
            L"private: void __cdecl CStatBar::UpdateZoomControlIcons(void)",
            L"void __cdecl CStatBar::UpdateZoomControlIcons(void)"
        },
        &pOriginalUpdateZoomControlIcons,
        UpdateZoomControlIconsHook,
        true
    },
    {
        {
            L"CAppletPopoutButton::DrawItem",
            L"?DrawItem@CAppletPopoutButton@@MEAAXPEAUtagDRAWITEMSTRUCT@@@Z",
            L"protected: virtual void __cdecl CAppletPopoutButton::DrawItem(struct tagDRAWITEMSTRUCT *)",
            L"void __cdecl CAppletPopoutButton::DrawItem(struct tagDRAWITEMSTRUCT *)"
        },
        &pOriginalPopoutButtonDrawItem,
        PopoutButtonDrawItemHook,
        true
    },
    {
        {
            L"crRight",
            L"?crRight@@3KA"
        },
        (void**)&g_pCrRight,
        nullptr,
        true
    },
    {
        {
            L"crLeft",
            L"?crLeft@@3KA"
        },
        (void**)&g_pCrLeft,
        nullptr,
        true
    },
    {
        {
            L"g_pStatBarWnd"
        },
        (void**)&g_ppStatBarWnd,
        nullptr,
        true
    }
};

#pragma endregion Canvas & Workspace Hooks

#pragma region Dark Task Dialog

struct DARK_TASK_DIALOG_STATE {
    const TASKDIALOGCONFIG* pConfig;
    int selectedButtonId;
    bool isModalRunning;
    HWND hDlg;
    HFONT hTitleFont;
    HFONT hBodyFont;
    HFONT hBtnFont;
    HBRUSH hTopBrush;
    HBRUSH hBottomBrush;
    HPEN hBorderPen;

    struct ButtonInfo {
        int id;
        WCHAR text[64];
        HWND hWnd;
        RECT rect;
        bool isDefault;
        bool isHovered;
        bool isPressed;
    } buttons[8];
    int buttonCount;
    int defaultButtonId;
    int cancelButtonId;

    WCHAR szTitle[128];
    WCHAR szInstruction[512];
    WCHAR szContent[1024];

    int topAreaHeight;
    int bottomAreaHeight;
};

void ResolveTaskDialogString(HINSTANCE hInst, PCWSTR pszSource, WCHAR* outBuf, size_t outCount) {
    if (!pszSource) {
        outBuf[0] = 0;
        return;
    }
    if (IS_INTRESOURCE(pszSource)) {
        UINT resId = (UINT)(uintptr_t)pszSource;
        if (!hInst || !LoadStringW(hInst, resId, outBuf, (int)outCount)) {
            if (!LoadStringW(g_hPaintExe, resId, outBuf, (int)outCount)) {
                outBuf[0] = 0;
            }
        }
    } else {
        wcsncpy_s(outBuf, outCount, pszSource, _TRUNCATE);
    }
}

LRESULT CALLBACK DarkButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    DARK_TASK_DIALOG_STATE* pState = (DARK_TASK_DIALOG_STATE*)dwRefData;
    int btnId = (int)uIdSubclass;

    DARK_TASK_DIALOG_STATE::ButtonInfo* pBtn = nullptr;
    if (pState) {
        for (int i = 0; i < pState->buttonCount; i++) {
            if (pState->buttons[i].id == btnId) {
                pBtn = &pState->buttons[i];
                break;
            }
        }
    }

    switch (uMsg) {
    case WM_MOUSEMOVE: {
        if (pBtn && !pBtn->isHovered) {
            pBtn->isHovered = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        if (pBtn) {
            pBtn->isHovered = false;
            pBtn->isPressed = false;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        if (pBtn) {
            pBtn->isPressed = true;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }
    case WM_LBUTTONUP: {
        if (pBtn) {
            pBtn->isPressed = false;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (hdc && pBtn && pState) {
            RECT rc;
            GetClientRect(hWnd, &rc);

            bool isFocused = (GetFocus() == hWnd);
            COLORREF bgColor = RGB(45, 45, 45);
            COLORREF borderColor = RGB(75, 75, 75);
            COLORREF textColor = RGB(240, 240, 240);

            if (pBtn->isPressed) {
                bgColor = RGB(30, 30, 30);
                borderColor = RGB(60, 60, 60);
                textColor = RGB(200, 200, 200);
            } else if (pBtn->isHovered) {
                bgColor = RGB(60, 60, 60);
                borderColor = RGB(105, 105, 105);
                textColor = RGB(255, 255, 255);
            } else if (pBtn->isDefault || isFocused) {
                bgColor = RGB(50, 50, 50);
                borderColor = isFocused ? RGB(0, 120, 215) : RGB(90, 90, 90);
            }

            HBRUSH hBr = CreateSolidBrush(bgColor);
            FillRect(hdc, &rc, hBr);
            DeleteObject(hBr);

            HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
            HGDIOBJ hOldPen = SelectObject(hdc, hPen);
            HGDIOBJ hOldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, hOldPen);
            SelectObject(hdc, hOldBr);
            DeleteObject(hPen);

            if (isFocused) {
                RECT rcFocus = rc;
                InflateRect(&rcFocus, -3, -3);
                DrawFocusRect(hdc, &rcFocus);
            }

            HFONT hFont = pState->hBtnFont;
            HGDIOBJ hOldFont = hFont ? SelectObject(hdc, hFont) : NULL;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, textColor);
            DrawTextW(hdc, pBtn->text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (hOldFont) SelectObject(hdc, hOldFont);

            EndPaint(hWnd, &ps);
            return 0;
        }
        break;
    }
    case WM_ERASEBKGND:
        return 1;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK DarkTaskDialogWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    DARK_TASK_DIALOG_STATE* pState = (DARK_TASK_DIALOG_STATE*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        pState = (DARK_TASK_DIALOG_STATE*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
        pState->hDlg = hWnd;

        if (_AllowDarkModeForWindow) {
            _AllowDarkModeForWindow(hWnd, true);
        }
        SetTitleBarDarkMode(hWnd, TRUE);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        int clientWidth = rcClient.right - rcClient.left;

        int btnWidth = 84;
        int btnHeight = 26;
        int btnSpacing = 8;
        int totalBtnWidth = pState->buttonCount * btnWidth + (pState->buttonCount - 1) * btnSpacing;
        int btnStartX = clientWidth - totalBtnWidth - 16;
        int btnY = pState->topAreaHeight + (pState->bottomAreaHeight - btnHeight) / 2;

        for (int i = 0; i < pState->buttonCount; i++) {
            auto& b = pState->buttons[i];
            int x = btnStartX + i * (btnWidth + btnSpacing);
            b.rect = { x, btnY, x + btnWidth, btnY + btnHeight };

            DWORD btnStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | (b.isDefault ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
            b.hWnd = CreateWindowExW(
                0, L"BUTTON", b.text, btnStyle,
                b.rect.left, b.rect.top, btnWidth, btnHeight,
                hWnd, (HMENU)(UINT_PTR)b.id, g_hPaintExe, NULL
            );

            if (b.hWnd) {
                SetWindowSubclass(b.hWnd, DarkButtonSubclassProc, b.id, (DWORD_PTR)pState);
                if (b.isDefault) {
                    SetFocus(b.hWnd);
                }
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        if (pState) {
            for (int i = 0; i < pState->buttonCount; i++) {
                if (pState->buttons[i].id == wmId) {
                    pState->selectedButtonId = wmId;
                    pState->isModalRunning = false;
                    DestroyWindow(hWnd);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_CLOSE: {
        if (pState) {
            pState->selectedButtonId = pState->cancelButtonId;
            pState->isModalRunning = false;
        }
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (hdc && pState) {
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            int clientWidth = rcClient.right - rcClient.left;
            int clientHeight = rcClient.bottom - rcClient.top;

            // 1. Top area: dark canvas brush (#202020)
            RECT rcTop = { 0, 0, clientWidth, pState->topAreaHeight };
            FillRect(hdc, &rcTop, pState->hTopBrush);

            // 2. Bottom bar: dark workspace brush (#282828)
            RECT rcBottom = { 0, pState->topAreaHeight, clientWidth, clientHeight };
            FillRect(hdc, &rcBottom, pState->hBottomBrush);

            // 3. Separator line at top of bottom bar
            HPEN hOldPen = (HPEN)SelectObject(hdc, pState->hBorderPen);
            MoveToEx(hdc, 0, pState->topAreaHeight, NULL);
            LineTo(hdc, clientWidth, pState->topAreaHeight);
            SelectObject(hdc, hOldPen);

            // 4. Draw Main Instruction Text (white, Segoe UI)
            if (pState->szInstruction[0]) {
                HGDIOBJ hOldFont = SelectObject(hdc, pState->hTitleFont);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));

                RECT rcText = { 24, 24, clientWidth - 24, pState->topAreaHeight - 12 };
                DrawTextW(hdc, pState->szInstruction, -1, &rcText, DT_LEFT | DT_TOP | DT_WORDBREAK);
                SelectObject(hdc, hOldFont);
            }

            // 5. Draw Content Text (if present)
            if (pState->szContent[0]) {
                HGDIOBJ hOldFont = SelectObject(hdc, pState->hBodyFont);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(200, 200, 200));

                RECT rcContent = { 24, 60, clientWidth - 24, pState->topAreaHeight - 12 };
                DrawTextW(hdc, pState->szContent, -1, &rcContent, DT_LEFT | DT_TOP | DT_WORDBREAK);
                SelectObject(hdc, hOldFont);
            }

            EndPaint(hWnd, &ps);
            return 0;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

HRESULT ShowDarkTaskDialog(const TASKDIALOGCONFIG* pTaskConfig, int* pnButton) {
    if (!pTaskConfig) return E_INVALIDARG;

    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DarkTaskDialogWndProc;
        wc.hInstance = g_hPaintExe ? g_hPaintExe : GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"DarkPaintTaskDialog";
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    DARK_TASK_DIALOG_STATE state = {};
    state.pConfig = pTaskConfig;
    state.isModalRunning = true;
    state.selectedButtonId = IDCANCEL;

    ResolveTaskDialogString(pTaskConfig->hInstance, pTaskConfig->pszWindowTitle, state.szTitle, ARRAYSIZE(state.szTitle));
    if (state.szTitle[0] == 0) wcscpy_s(state.szTitle, L"Paint");

    ResolveTaskDialogString(pTaskConfig->hInstance, pTaskConfig->pszMainInstruction, state.szInstruction, ARRAYSIZE(state.szInstruction));
    ResolveTaskDialogString(pTaskConfig->hInstance, pTaskConfig->pszContent, state.szContent, ARRAYSIZE(state.szContent));

    state.buttonCount = 0;
    state.defaultButtonId = pTaskConfig->nDefaultButton;
    state.cancelButtonId = IDCANCEL;

    if (pTaskConfig->pButtons && pTaskConfig->cButtons > 0) {
        for (UINT i = 0; i < pTaskConfig->cButtons && state.buttonCount < 8; i++) {
            auto& b = state.buttons[state.buttonCount++];
            b.id = pTaskConfig->pButtons[i].nButtonID;
            ResolveTaskDialogString(pTaskConfig->hInstance, pTaskConfig->pButtons[i].pszButtonText, b.text, ARRAYSIZE(b.text));
            b.isDefault = (state.defaultButtonId == b.id) || (state.defaultButtonId == 0 && state.buttonCount == 1);
        }
    }

    if (pTaskConfig->dwCommonButtons & TDCBF_OK_BUTTON) {
        auto& b = state.buttons[state.buttonCount++];
        b.id = IDOK;
        wcscpy_s(b.text, L"OK");
        b.isDefault = (state.defaultButtonId == IDOK);
    }
    if (pTaskConfig->dwCommonButtons & TDCBF_YES_BUTTON) {
        auto& b = state.buttons[state.buttonCount++];
        b.id = IDYES;
        wcscpy_s(b.text, L"Yes");
        b.isDefault = (state.defaultButtonId == IDYES);
    }
    if (pTaskConfig->dwCommonButtons & TDCBF_NO_BUTTON) {
        auto& b = state.buttons[state.buttonCount++];
        b.id = IDNO;
        wcscpy_s(b.text, L"No");
        b.isDefault = (state.defaultButtonId == IDNO);
    }
    if (pTaskConfig->dwCommonButtons & TDCBF_CANCEL_BUTTON) {
        auto& b = state.buttons[state.buttonCount++];
        b.id = IDCANCEL;
        wcscpy_s(b.text, L"Cancel");
        b.isDefault = (state.defaultButtonId == IDCANCEL);
        state.cancelButtonId = IDCANCEL;
    }
    if (pTaskConfig->dwCommonButtons & TDCBF_RETRY_BUTTON) {
        auto& b = state.buttons[state.buttonCount++];
        b.id = IDRETRY;
        wcscpy_s(b.text, L"Retry");
        b.isDefault = (state.defaultButtonId == IDRETRY);
    }
    if (pTaskConfig->dwCommonButtons & TDCBF_CLOSE_BUTTON) {
        auto& b = state.buttons[state.buttonCount++];
        b.id = IDCLOSE;
        wcscpy_s(b.text, L"Close");
        b.isDefault = (state.defaultButtonId == IDCLOSE);
    }

    if (state.buttonCount == 0) {
        auto& b = state.buttons[state.buttonCount++];
        b.id = IDOK;
        wcscpy_s(b.text, L"OK");
        b.isDefault = true;
        state.cancelButtonId = IDOK;
    }

    HDC hScreenDC = GetDC(NULL);
    int dpi = GetDeviceCaps(hScreenDC, LOGPIXELSY);
    int titleFontSize = -MulDiv(12, dpi, 72);
    int bodyFontSize = -MulDiv(9, dpi, 72);

    state.hTitleFont = CreateFontW(
        titleFontSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    state.hBodyFont = CreateFontW(
        bodyFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    state.hBtnFont = state.hBodyFont;

    RECT rcMeasure = { 0, 0, 360, 0 };
    HGDIOBJ hOldFont = SelectObject(hScreenDC, state.hTitleFont);
    DrawTextW(hScreenDC, state.szInstruction, -1, &rcMeasure, DT_CALCRECT | DT_WORDBREAK);
    SelectObject(hScreenDC, hOldFont);
    ReleaseDC(NULL, hScreenDC);

    int textHeight = rcMeasure.bottom - rcMeasure.top;
    if (textHeight < 24) textHeight = 24;

    state.topAreaHeight = 24 + textHeight + 24;
    state.bottomAreaHeight = 52;

    int totalButtonsWidth = state.buttonCount * 84 + (state.buttonCount - 1) * 8;
    int clientWidth = (380 > (totalButtonsWidth + 40)) ? 380 : (totalButtonsWidth + 40);
    int clientHeight = state.topAreaHeight + state.bottomAreaHeight;

    state.hTopBrush = CreateSolidBrush(g_settings.canvasColor);
    state.hBottomBrush = CreateSolidBrush(g_settings.workspaceColor);
    state.hBorderPen = CreatePen(PS_SOLID, 1, RGB(55, 55, 55));

    DWORD dwStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    DWORD dwExStyle = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;

    RECT rcWnd = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRectEx(&rcWnd, dwStyle, FALSE, dwExStyle);
    int wndWidth = rcWnd.right - rcWnd.left;
    int wndHeight = rcWnd.bottom - rcWnd.top;

    int x = (GetSystemMetrics(SM_CXSCREEN) - wndWidth) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - wndHeight) / 2;
    if (pTaskConfig->hwndParent && IsWindow(pTaskConfig->hwndParent)) {
        RECT rcParent;
        GetWindowRect(pTaskConfig->hwndParent, &rcParent);
        x = rcParent.left + (rcParent.right - rcParent.left - wndWidth) / 2;
        y = rcParent.top + (rcParent.bottom - rcParent.top - wndHeight) / 2;
    }

    HWND hDlg = CreateWindowExW(
        dwExStyle, L"DarkPaintTaskDialog", state.szTitle, dwStyle,
        x, y, wndWidth, wndHeight,
        pTaskConfig->hwndParent, NULL, g_hPaintExe, &state
    );

    if (!hDlg) {
        DeleteObject(state.hTopBrush);
        DeleteObject(state.hBottomBrush);
        DeleteObject(state.hBorderPen);
        if (state.hTitleFont) DeleteObject(state.hTitleFont);
        if (state.hBodyFont) DeleteObject(state.hBodyFont);
        return E_FAIL;
    }

    HWND hParent = pTaskConfig->hwndParent;
    if (hParent && IsWindow(hParent)) {
        EnableWindow(hParent, FALSE);
    }

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG msg;
    while (state.isModalRunning && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            state.selectedButtonId = state.cancelButtonId;
            state.isModalRunning = false;
            DestroyWindow(hDlg);
            break;
        }
        if (IsDialogMessageW(hDlg, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hParent && IsWindow(hParent)) {
        EnableWindow(hParent, TRUE);
        SetActiveWindow(hParent);
    }

    DeleteObject(state.hTopBrush);
    DeleteObject(state.hBottomBrush);
    DeleteObject(state.hBorderPen);
    if (state.hTitleFont) DeleteObject(state.hTitleFont);
    if (state.hBodyFont) DeleteObject(state.hBodyFont);

    if (pnButton) {
        *pnButton = state.selectedButtonId;
    }

    return S_OK;
}

using TaskDialogIndirect_t = HRESULT (WINAPI *)(
    const TASKDIALOGCONFIG *pTaskConfig,
    int *pnButton,
    int *pnRadioButton,
    BOOL *pfVerificationFlagChecked
);
TaskDialogIndirect_t pOriginalTaskDialogIndirect = nullptr;

HRESULT WINAPI TaskDialogIndirectHook(
    const TASKDIALOGCONFIG *pTaskConfig,
    int *pnButton,
    int *pnRadioButton,
    BOOL *pfVerificationFlagChecked)
{
    ModLog(L"TaskDialogIndirectHook called: pTaskConfig=%p, hwndParent=%p", 
           pTaskConfig, pTaskConfig ? pTaskConfig->hwndParent : NULL);

    if (g_darkModeEnabled && pTaskConfig) {
        if (pTaskConfig->pfCallback == nullptr) {
            int buttonId = 0;
            HRESULT hr = ShowDarkTaskDialog(pTaskConfig, &buttonId);
            if (SUCCEEDED(hr)) {
                if (pnButton) *pnButton = buttonId;
                if (pnRadioButton && pTaskConfig->nDefaultRadioButton) {
                    *pnRadioButton = pTaskConfig->nDefaultRadioButton;
                }
                if (pfVerificationFlagChecked) {
                    *pfVerificationFlagChecked = FALSE;
                }
                return hr;
            }
        }
    }

    return pOriginalTaskDialogIndirect(pTaskConfig, pnButton, pnRadioButton, pfVerificationFlagChecked);
}

#pragma endregion Dark Task Dialog

#pragma region Window Creation Hook

using CreateWindowExW_t = decltype(&CreateWindowExW);
CreateWindowExW_t pOriginalCreateWindowExW = nullptr;

using CreateWindowExA_t = decltype(&CreateWindowExA);
CreateWindowExA_t pOriginalCreateWindowExA = nullptr;

HWND WINAPI CreateWindowExWHook(
    DWORD dwExStyle,
    LPCWSTR lpClassName,
    LPCWSTR lpWindowName,
    DWORD dwStyle,
    int X,
    int Y,
    int nWidth,
    int nHeight,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    LPVOID lpParam)
{
    HWND hWnd = pOriginalCreateWindowExW(
        dwExStyle,
        lpClassName,
        lpWindowName,
        dwStyle,
        X,
        Y,
        nWidth,
        nHeight,
        hWndParent,
        hMenu,
        hInstance,
        lpParam
    );

    if (!hWnd) return hWnd;

    BOOL bTextualClassName = ((ULONG_PTR)lpClassName & ~(ULONG_PTR)0xffff) != 0;

    if (bTextualClassName) {
        if (wcsicmp(lpClassName, L"MSPaintApp") == 0) {
            g_hMainWnd = hWnd;
            if (_AllowDarkModeForWindow) {
                _AllowDarkModeForWindow(hWnd, true);
            }
            SetTitleBarDarkMode(hWnd, TRUE);
            SetWindowLongPtrW(hWnd, GWL_STYLE, GetWindowLongPtrW(hWnd, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
            WindhawkUtils::SetWindowSubclassFromAnyThread(hWnd, MainFrameSubclassProc, 0);
            ThemeWindowTree(hWnd);
            ModLog(L"Themed MSPaintApp: %p", hWnd);

            void* pFramework = GetRibbonFramework();
            if (pFramework) {
                ApplyRibbonDarkMode(pFramework);
            }
            SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            DrawTopBlackLine(hWnd);
            g_topLineTimerTicks = 60;
            SetTimer(hWnd, 9998, 50, NULL);
        }
        else if (wcsicmp(lpClassName, L"MSPaintView") == 0) {
            SetWindowLongPtrW(hWnd, GWL_STYLE, GetWindowLongPtrW(hWnd, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
            WindhawkUtils::SetWindowSubclassFromAnyThread(hWnd, PaintViewSubclassProc, 0);
            ModLog(L"Created MSPaintView: %p", hWnd);
        }
        else if (wcsicmp(lpClassName, L"msctls_statusbar32") == 0) {
            ThemeStatusBarAndChildren(hWnd);
            ModLog(L"Themed msctls_statusbar32: %p", hWnd);
        }
        else if (wcsicmp(lpClassName, L"msctls_trackbar32") == 0) {
            ThemeStatusBarChild(hWnd);
        }
        else if (wcsncmp(lpClassName, L"Afx:", 4) == 0 && (UINT_PTR)hMenu == 0xD001) {
            if (hWndParent && IsWindow(hWndParent) && !g_hStatusBarWnd) {
                ThemeStatusBarAndChildren(hWndParent);
            }
            ThemeStatusBarChild(hWnd);
        }
        else if (g_hStatusBarWnd && (hWndParent == g_hStatusBarWnd || IsChild(g_hStatusBarWnd, hWndParent))) {
            ThemeStatusBarChild(hWnd);
        }
        else if (wcsicmp(lpClassName, L"UIRibbonCommandBar") == 0 ||
                 wcsicmp(lpClassName, L"NetUIHWND") == 0 ||
                 wcsicmp(lpClassName, L"NUIPane") == 0 ||
                 wcsicmp(lpClassName, L"UIRibbonWorkPane") == 0 ||
                 wcsicmp(lpClassName, L"UIRibbonCommandBarDock") == 0) {
            if (_AllowDarkModeForWindow) {
                _AllowDarkModeForWindow(hWnd, true);
            }
            void* pFramework = GetRibbonFramework();
            if (pFramework) {
                ApplyRibbonDarkMode(pFramework);
            }
            if (g_hMainWnd && IsWindow(g_hMainWnd)) {
                DrawTopBlackLine(g_hMainWnd);
                g_topLineTimerTicks = 60;
                SetTimer(g_hMainWnd, 9998, 50, NULL);
            }
        }
        else if (wcsicmp(lpClassName, L"#32770") == 0) {
            if (_AllowDarkModeForWindow) {
                _AllowDarkModeForWindow(hWnd, true);
            }
            SetTitleBarDarkMode(hWnd, TRUE);
            ThemeWindowTree(hWnd);
        }
        else if (hWndParent && IsWindow(hWndParent)) {
            WCHAR parentClass[64] = L"";
            GetClassNameW(hWndParent, parentClass, ARRAYSIZE(parentClass));
            if (wcsicmp(parentClass, L"MSPaintView") == 0) {
                SetWindowLongPtrW(hWnd, GWL_STYLE, GetWindowLongPtrW(hWnd, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
                WindhawkUtils::SetWindowSubclassFromAnyThread(hWnd, ImgWndSubclassProc, 0);
                ModLog(L"Subclassed MSPaintView child (CImgWnd): %p", hWnd);
            }
            else if (g_hMainWnd && (hWndParent == g_hMainWnd || IsChild(g_hMainWnd, hWndParent))) {
                if (_AllowDarkModeForWindow) {
                    _AllowDarkModeForWindow(hWnd, true);
                }
                SetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
            }
        }
    }

    return hWnd;
}

HWND WINAPI CreateWindowExAHook(
    DWORD dwExStyle,
    LPCSTR lpClassName,
    LPCSTR lpWindowName,
    DWORD dwStyle,
    int X,
    int Y,
    int nWidth,
    int nHeight,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    LPVOID lpParam)
{
    HWND hWnd = pOriginalCreateWindowExA(
        dwExStyle,
        lpClassName,
        lpWindowName,
        dwStyle,
        X,
        Y,
        nWidth,
        nHeight,
        hWndParent,
        hMenu,
        hInstance,
        lpParam
    );

    if (!hWnd) return hWnd;

    if (g_hMainWnd && (hWndParent == g_hMainWnd || IsChild(g_hMainWnd, hWndParent))) {
        if (_AllowDarkModeForWindow) {
            _AllowDarkModeForWindow(hWnd, true);
        }
        SetWindowTheme(hWnd, L"DarkMode_Explorer", NULL);
    }

    return hWnd;
}

#pragma endregion Window Creation Hook

#pragma region Windows 11 WinUI Hook

using RoGetActivationFactory_t = decltype(&RoGetActivationFactory);
RoGetActivationFactory_t origRoGetActivationFactory = nullptr;

static bool ignoreHooking = false;

HRESULT RoGetActivationFactoryHook(HSTRING activatableClassId, REFIID iid, void **factory)
{
    if (!ignoreHooking && activatableClassId) 
    {
        PCWSTR pszClass = WindowsGetStringRawBuffer(activatableClassId, NULL);
        if (pszClass && wcsicmp(pszClass, L"Windows.UI.Xaml.Application") == 0) 
        {
            ignoreHooking = true;
            try
            {
                winrt::Windows::UI::Xaml::Application::Current().RequestedTheme(
                    winrt::Windows::UI::Xaml::ApplicationTheme::Dark
                );
            } catch (...) { }
            ignoreHooking = false;
        }
    }

    return origRoGetActivationFactory(activatableClassId, iid, factory);
}

#pragma endregion Windows 11 WinUI Hook

BOOL Wh_ModInit(void)
{
    ModLog(L"=== Init MSPaint Dark Mod ===");
    g_hPaintExe = GetModuleHandleW(NULL);
    if (g_hPaintExe) {
        g_paintExeBase = (uintptr_t)g_hPaintExe;
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)g_paintExeBase;
        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(g_paintExeBase + dos->e_lfanew);
        g_paintExeSize = nt->OptionalHeader.SizeOfImage;
        ModLog(L"Paint EXE Base: %p, Size: 0x%X", (void*)g_paintExeBase, (unsigned int)g_paintExeSize);
    }
    LoadSettings();

    // 1. WinRT hook for Windows 11 Paint
    HMODULE winrtModule = GetModuleHandleW(L"api-ms-win-core-winrt-l1-1-0.dll");
    if (winrtModule) {
        void* roAc = (void*)GetProcAddress(winrtModule, "RoGetActivationFactory");
        if (roAc) {
            Wh_SetFunctionHook(roAc, (void*)RoGetActivationFactoryHook, (void**)&origRoGetActivationFactory);
            ModLog(L"Registered WinRT RoGetActivationFactory hook");
        }
    }

    // 2. Win32 Dark Mode for Windows 10 Paint
    InitWin32DarkMode();

    // Directly preset crRight in .data to canvasColor if Paint is loaded
    if (g_settings.darkCanvas) {
        if (g_pCrRight) {
            *g_pCrRight = g_settings.canvasColor;
            ModLog(L"Preset crRight to 0x%08X", g_settings.canvasColor);
        } else if (g_paintExeBase && g_buildNumber == 19045) {
            COLORREF* pCrRight = (COLORREF*)(g_paintExeBase + 0xDC644);
            *pCrRight = g_settings.canvasColor;
            ModLog(L"Preset crRight at %p to 0x%08X", pCrRight, g_settings.canvasColor);
        }
    }

    // Hook CreateWindowExW and CreateWindowExA for window discovery and theming
    Wh_SetFunctionHook((void*)CreateWindowExW, (void*)CreateWindowExWHook, (void**)&pOriginalCreateWindowExW);
    Wh_SetFunctionHook((void*)CreateWindowExA, (void*)CreateWindowExAHook, (void**)&pOriginalCreateWindowExA);

    // Hook CoCreateInstance in combase.dll and ole32.dll for Ribbon theming
    HMODULE hCombase = GetModuleHandleW(L"combase.dll");
    if (hCombase) {
        void* pCombaseCoCreate = (void*)GetProcAddress(hCombase, "CoCreateInstance");
        if (pCombaseCoCreate) {
            Wh_SetFunctionHook(pCombaseCoCreate, (void*)CoCreateInstanceHookCombase, (void**)&pOriginalCoCreateInstanceCombase);
            ModLog(L"Hooked combase!CoCreateInstance");
        }
    }
    HMODULE hOle32 = GetModuleHandleW(L"ole32.dll");
    if (hOle32) {
        void* pOle32CoCreate = (void*)GetProcAddress(hOle32, "CoCreateInstance");
        if (pOle32CoCreate) {
            Wh_SetFunctionHook(pOle32CoCreate, (void*)CoCreateInstanceHookOle32, (void**)&pOriginalCoCreateInstanceOle32);
            ModLog(L"Hooked ole32!CoCreateInstance");
        }
    }

    // Hook GdiGradientFill for dark workspace background
    HMODULE hGdi32 = GetModuleHandleW(L"gdi32.dll");
    if (hGdi32) {
        void* pGdiGradientFill = (void*)GetProcAddress(hGdi32, "GdiGradientFill");
        if (pGdiGradientFill) {
            Wh_SetFunctionHook(pGdiGradientFill, (void*)GdiGradientFillHook, (void**)&pOriginalGdiGradientFill);
        }
    }

    // Hook CreateSolidBrush & PatBlt for dark canvas spawning
    Wh_SetFunctionHook((void*)CreateSolidBrush, (void*)CreateSolidBrushHook, (void**)&pOriginalCreateSolidBrush);
    Wh_SetFunctionHook((void*)PatBlt, (void*)PatBltHook, (void**)&pOriginalPatBlt);

    // Hook TaskDialogIndirect in comctl32.dll for dark save/close prompts
    HMODULE hComCtl32 = GetModuleHandleW(L"comctl32.dll");
    if (hComCtl32) {
        void* pTaskDialogIndirect = (void*)GetProcAddress(hComCtl32, "TaskDialogIndirect");
        if (pTaskDialogIndirect) {
            Wh_SetFunctionHook(pTaskDialogIndirect, (void*)TaskDialogIndirectHook, (void**)&pOriginalTaskDialogIndirect);
            ModLog(L"Hooked comctl32!TaskDialogIndirect (%p)", pTaskDialogIndirect);
        }
    }

    // Symbol hooks for palette reset (Color 1 & 2), drop shadow, and zoom controls
    bool symRes = WindhawkUtils::HookSymbols(g_hPaintExe, g_symbolHooks, ARRAYSIZE(g_symbolHooks));
    ModLog(L"WindhawkUtils::HookSymbols returned: %d, pOriginalDrawDropShadow: %p, pOriginalCreateGDIObjects: %p", 
           symRes ? 1 : 0, pOriginalDrawDropShadow, pOriginalCreateGDIObjects);

    if (!pOriginalDrawDropShadow && g_paintExeBase && g_buildNumber == 19045) {
        void* pDrawDropShadow = (void*)(g_paintExeBase + 0x64A30);
        Wh_SetFunctionHook(pDrawDropShadow, (void*)DrawDropShadowHook, (void**)&pOriginalDrawDropShadow);
        ModLog(L"Directly hooked DrawDropShadow via RVA: %p", pDrawDropShadow);
    }
    if (!pOriginalCreateGDIObjects && g_paintExeBase && g_buildNumber == 19045) {
        void* pCreateGDIObjects = (void*)(g_paintExeBase + 0x682D8);
        Wh_SetFunctionHook(pCreateGDIObjects, (void*)CreateGDIObjectsHook, (void**)&pOriginalCreateGDIObjects);
        ModLog(L"Directly hooked CreateGDIObjects via RVA: %p", pCreateGDIObjects);
    }
    if (!pOriginalUpdateIcons && g_paintExeBase && g_buildNumber == 19045) {
        pOriginalUpdateIcons = (UpdateIcons_t)(g_paintExeBase + 0x683BC);
        ModLog(L"Resolved UpdateIcons via RVA: %p", pOriginalUpdateIcons);
    }
    if (!pOriginalUpdateZoomControlIcons && g_paintExeBase && g_buildNumber == 19045) {
        void* pUpdateZoomControlIcons = (void*)(g_paintExeBase + 0xA048);
        Wh_SetFunctionHook(pUpdateZoomControlIcons, (void*)UpdateZoomControlIconsHook, (void**)&pOriginalUpdateZoomControlIcons);
        ModLog(L"Directly hooked UpdateZoomControlIcons via RVA: %p", pUpdateZoomControlIcons);
    }
    if (!pOriginalPopoutButtonDrawItem && g_paintExeBase && g_buildNumber == 19045) {
        void* pDrawItem = (void*)(g_paintExeBase + 0x68680);
        Wh_SetFunctionHook(pDrawItem, (void*)PopoutButtonDrawItemHook, (void**)&pOriginalPopoutButtonDrawItem);
        ModLog(L"Directly hooked PopoutButtonDrawItem via RVA: %p", pDrawItem);
    }
    if (!pOriginalOnEraseBkgnd && g_paintExeBase && g_buildNumber == 19045) {
        void* pOnEraseBkgnd = (void*)(g_paintExeBase + 0x2EF40);
        Wh_SetFunctionHook(pOnEraseBkgnd, (void*)OnEraseBkgndHook, (void**)&pOriginalOnEraseBkgnd);
        ModLog(L"Directly hooked OnEraseBkgnd via RVA: %p", pOnEraseBkgnd);
    }

    UpdateStatusZoomControls();

    // If MSPaintApp is already running, theme it immediately
    HWND hExisting = FindWindowW(L"MSPaintApp", NULL);
    if (hExisting) {
        g_hMainWnd = hExisting;
        SetWindowLongPtrW(hExisting, GWL_STYLE, GetWindowLongPtrW(hExisting, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        ThemeWindowTree(hExisting);
        SetTitleBarDarkMode(hExisting, TRUE);
        WindhawkUtils::SetWindowSubclassFromAnyThread(hExisting, MainFrameSubclassProc, 0);

        HWND hExistingStatusBar = FindWindowExW(hExisting, NULL, L"msctls_statusbar32", NULL);
        if (hExistingStatusBar) {
            ThemeStatusBarAndChildren(hExistingStatusBar);
        }

        HWND hExistingView = FindWindowExW(hExisting, NULL, L"MSPaintView", NULL);
        if (hExistingView) {
            SetWindowLongPtrW(hExistingView, GWL_STYLE, GetWindowLongPtrW(hExistingView, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
            WindhawkUtils::SetWindowSubclassFromAnyThread(hExistingView, PaintViewSubclassProc, 0);

            HWND hExistingImgWnd = FindWindowExW(hExistingView, NULL, NULL, NULL);
            if (hExistingImgWnd) {
                SetWindowLongPtrW(hExistingImgWnd, GWL_STYLE, GetWindowLongPtrW(hExistingImgWnd, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
                WindhawkUtils::SetWindowSubclassFromAnyThread(hExistingImgWnd, ImgWndSubclassProc, 0);
            }
        }

        void* pFramework = GetRibbonFramework();
        if (pFramework) {
            ApplyRibbonDarkMode(pFramework);
        }

        UpdateStatusZoomControls();
        SetWindowPos(hExisting, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        DrawTopBlackLine(hExisting);
        g_topLineTimerTicks = 60;
        SetTimer(hExisting, 9998, 50, NULL);
        InvalidateRect(hExisting, NULL, TRUE);
    }

    return TRUE;
}

void Wh_ModUninit(void)
{
    ModLog(L"=== Uninit MSPaint Dark Mod ===");

    if (g_hMainWnd && IsWindow(g_hMainWnd)) {
        SetTitleBarDarkMode(g_hMainWnd, FALSE);
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(g_hMainWnd, MainFrameSubclassProc);
    }

    if (g_hStatusBarWnd && IsWindow(g_hStatusBarWnd)) {
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(g_hStatusBarWnd, StatusBarSubclassProc);
    }

    if (g_hWorkspaceBrush) {
        DeleteObject(g_hWorkspaceBrush);
        g_hWorkspaceBrush = NULL;
    }

    if (g_hCanvasBrush) {
        DeleteObject(g_hCanvasBrush);
        g_hCanvasBrush = NULL;
    }

    if (g_menuTheme) {
        CloseThemeData(g_menuTheme);
        g_menuTheme = NULL;
    }
}

void Wh_ModSettingsChanged(void)
{
    ModLog(L"=== SettingsChanged ===");
    LoadSettings();

    void* pFramework = GetRibbonFramework();
    if (pFramework) {
        ApplyRibbonDarkMode(pFramework);
    }

    UpdateStatusZoomControls();

    if (g_hMainWnd && IsWindow(g_hMainWnd)) {
        InvalidateRect(g_hMainWnd, NULL, TRUE);
    }
}

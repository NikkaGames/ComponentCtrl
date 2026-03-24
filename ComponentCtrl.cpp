#include "framework.h"
#include "ComponentCtrl.h"

#include <commctrl.h>
#include <initguid.h>
#include <setupapi.h>
#include <strsafe.h>
#include <winioctl.h>
#include <vector>
#include <string>
#include <cstdarg>

#include <windowsx.h>

#include "../aw22xxx_led/Public.h"
#include "../TouchScreen/inc/common.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#define MAX_LOADSTRING 100
#define CONFIG_TABLE_BUFFER_SIZE (64u * 1024u)
#define GOODIX_REPORT_RATE_120HZ 0
#define GOODIX_REPORT_RATE_240HZ 1
#define GOODIX_REPORT_RATE_360HZ 2
#define GOODIX_REPORT_RATE_480HZ 3
#define GOODIX_REPORT_RATE_960HZ 4
#define UI_INITIAL_CLIENT_WIDTH 920
#define UI_INITIAL_CLIENT_HEIGHT 700
#define UI_MIN_CLIENT_WIDTH 760
#define UI_MIN_CLIENT_HEIGHT 560
#define UI_MARGIN 20
#define UI_LED_CARD_TOP 82
#define UI_LED_CARD_HEIGHT 300
#define UI_TOUCH_CARD_HEIGHT 180
#define UI_STATUS_CARD_HEIGHT 116
#define UI_CARD_GAP 18
#define UI_SCROLL_STEP 48
#define UI_LED_ACTION_WIDTH 188
#define UI_TOUCH_ACTION_WIDTH 144
#define UI_CONTROL_HEIGHT 32

static const COLORREF kColorWindowBackground = RGB(242, 245, 248);
static const COLORREF kColorCardBackground = RGB(255, 255, 255);
static const COLORREF kColorCardBorder = RGB(225, 231, 238);
static const COLORREF kColorTextPrimary = RGB(26, 32, 44);
static const COLORREF kColorTextMuted = RGB(96, 108, 128);
static const COLORREF kColorTextAccent = RGB(17, 94, 89);

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

struct APP_STATE {
    HANDLE Device;
    HANDLE TouchDevice;
    HWND MainWindow;
    HWND ConfigCombo;
    HWND ApplyButton;
    HWND RefreshButton;
    HWND OffButton;
    HWND RgbCheck;
    HWND InfoText;
    HWND TouchRateCombo;
    HWND TouchApplyButton;
    HWND TouchInfoText;
    HWND StatusText;
    HFONT TitleFont;
    HFONT SectionFont;
    HFONT BodyFont;
    HFONT SmallFont;
    HBRUSH WindowBrush;
    HBRUSH CardBrush;
    int ScrollY;
    int ClientWidth;
    int ClientHeight;
    std::vector<AW22XXX_CONFIG_DESCRIPTOR> Configs;
};

static APP_STATE gAppState = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
static BOOL         GetTouchDevicePath(_Out_ std::wstring& DevicePath);
static BOOL         EnsureTouchDeviceOpen();

typedef struct _UI_LAYOUT {
    RECT HeaderTitleRect;
    RECT HeaderSubtitleRect;
    RECT LedCardRect;
    RECT TouchCardRect;
    RECT StatusCardRect;
    RECT ConfigComboRect;
    RECT ApplyButtonRect;
    RECT RefreshButtonRect;
    RECT OffButtonRect;
    RECT RgbCheckRect;
    RECT InfoTextRect;
    RECT TouchRateComboRect;
    RECT TouchApplyButtonRect;
    RECT TouchInfoTextRect;
    RECT StatusTextRect;
    int ContentHeight;
} UI_LAYOUT, *PUI_LAYOUT;

static RECT
MakeRect(
    LONG Left,
    LONG Top,
    LONG Right,
    LONG Bottom
    )
{
    RECT rect = { Left, Top, Right, Bottom };
    return rect;
}

static HFONT
CreateUiFont(
    _In_ LONG Height,
    _In_ LONG Weight
    )
{
    return CreateFontW(
        Height,
        0,
        0,
        0,
        Weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        L"Segoe UI");
}

static void
DestroyUiResources()
{
    if (gAppState.TitleFont != nullptr)
    {
        DeleteObject(gAppState.TitleFont);
        gAppState.TitleFont = nullptr;
    }
    if (gAppState.SectionFont != nullptr)
    {
        DeleteObject(gAppState.SectionFont);
        gAppState.SectionFont = nullptr;
    }
    if (gAppState.BodyFont != nullptr)
    {
        DeleteObject(gAppState.BodyFont);
        gAppState.BodyFont = nullptr;
    }
    if (gAppState.SmallFont != nullptr)
    {
        DeleteObject(gAppState.SmallFont);
        gAppState.SmallFont = nullptr;
    }
    if (gAppState.WindowBrush != nullptr)
    {
        DeleteObject(gAppState.WindowBrush);
        gAppState.WindowBrush = nullptr;
    }
    if (gAppState.CardBrush != nullptr)
    {
        DeleteObject(gAppState.CardBrush);
        gAppState.CardBrush = nullptr;
    }
}

static void
InitializeUiResources()
{
    if (gAppState.TitleFont == nullptr)
    {
        gAppState.TitleFont = CreateUiFont(-24, FW_SEMIBOLD);
        gAppState.SectionFont = CreateUiFont(-16, FW_SEMIBOLD);
        gAppState.BodyFont = CreateUiFont(-16, FW_NORMAL);
        gAppState.SmallFont = CreateUiFont(-14, FW_NORMAL);
        gAppState.WindowBrush = CreateSolidBrush(kColorWindowBackground);
        gAppState.CardBrush = CreateSolidBrush(kColorCardBackground);
    }
}

static void
ApplyFont(
    _In_opt_ HWND Control,
    _In_opt_ HFONT Font
    )
{
    if ((Control != nullptr) && (Font != nullptr))
    {
        SendMessageW(Control, WM_SETFONT, (WPARAM)Font, TRUE);
    }
}

static int
ClampInt(
    int Value,
    int Minimum,
    int Maximum
    )
{
    if (Value < Minimum)
    {
        return Minimum;
    }
    if (Value > Maximum)
    {
        return Maximum;
    }
    return Value;
}

static void
ComputeUiLayout(
    _In_ int ClientWidth,
    _In_ int ScrollY,
    _Out_ PUI_LAYOUT Layout
    )
{
    int cardLeft;
    int cardRight;
    int ledInnerLeft;
    int ledInnerRight;
    int ledActionLeft;
    int ledMainRight;
    int touchInnerLeft;
    int touchInnerRight;

    ZeroMemory(Layout, sizeof(*Layout));

    int ledCardTop;
    int touchCardTop;
    int statusCardTop;

    cardLeft = UI_MARGIN;
    cardRight = max(ClientWidth - UI_MARGIN, cardLeft + 680);
    ledCardTop = UI_LED_CARD_TOP;
    touchCardTop = ledCardTop + UI_LED_CARD_HEIGHT + UI_CARD_GAP;
    statusCardTop = touchCardTop + UI_TOUCH_CARD_HEIGHT + UI_CARD_GAP;

    Layout->HeaderTitleRect = MakeRect(cardLeft, 18 - ScrollY, cardRight, 52 - ScrollY);
    Layout->HeaderSubtitleRect = MakeRect(cardLeft, 48 - ScrollY, cardRight, 74 - ScrollY);

    Layout->LedCardRect = MakeRect(
        cardLeft,
        ledCardTop - ScrollY,
        cardRight,
        ledCardTop + UI_LED_CARD_HEIGHT - ScrollY);
    Layout->TouchCardRect = MakeRect(
        cardLeft,
        touchCardTop - ScrollY,
        cardRight,
        touchCardTop + UI_TOUCH_CARD_HEIGHT - ScrollY);
    Layout->StatusCardRect = MakeRect(
        cardLeft,
        statusCardTop - ScrollY,
        cardRight,
        statusCardTop + UI_STATUS_CARD_HEIGHT - ScrollY);

    ledInnerLeft = Layout->LedCardRect.left + 22;
    ledInnerRight = Layout->LedCardRect.right - 22;
    ledActionLeft = ledInnerRight - UI_LED_ACTION_WIDTH;
    ledMainRight = ledActionLeft - 16;

    Layout->ConfigComboRect = MakeRect(
        ledInnerLeft,
        Layout->LedCardRect.top + 62,
        ledMainRight,
        Layout->LedCardRect.top + 62 + UI_CONTROL_HEIGHT);
    Layout->RgbCheckRect = MakeRect(
        ledInnerLeft,
        Layout->LedCardRect.top + 102,
        ledMainRight,
        Layout->LedCardRect.top + 126);
    Layout->ApplyButtonRect = MakeRect(
        ledActionLeft,
        Layout->LedCardRect.top + 60,
        ledInnerRight,
        Layout->LedCardRect.top + 60 + UI_CONTROL_HEIGHT);
    Layout->RefreshButtonRect = MakeRect(
        ledActionLeft,
        Layout->LedCardRect.top + 100,
        ledInnerRight,
        Layout->LedCardRect.top + 100 + UI_CONTROL_HEIGHT);
    Layout->OffButtonRect = MakeRect(
        ledActionLeft,
        Layout->LedCardRect.top + 140,
        ledInnerRight,
        Layout->LedCardRect.top + 140 + UI_CONTROL_HEIGHT);
    Layout->InfoTextRect = MakeRect(
        ledInnerLeft,
        Layout->LedCardRect.top + 192,
        ledInnerRight,
        Layout->LedCardRect.bottom - 22);

    touchInnerLeft = Layout->TouchCardRect.left + 22;
    touchInnerRight = Layout->TouchCardRect.right - 22;
    Layout->TouchRateComboRect = MakeRect(
        touchInnerLeft,
        Layout->TouchCardRect.top + 62,
        touchInnerLeft + 220,
        Layout->TouchCardRect.top + 62 + UI_CONTROL_HEIGHT);
    Layout->TouchApplyButtonRect = MakeRect(
        Layout->TouchRateComboRect.right + 18,
        Layout->TouchCardRect.top + 62,
        Layout->TouchRateComboRect.right + 18 + UI_TOUCH_ACTION_WIDTH,
        Layout->TouchCardRect.top + 62 + UI_CONTROL_HEIGHT);
    Layout->TouchInfoTextRect = MakeRect(
        touchInnerLeft,
        Layout->TouchCardRect.top + 118,
        touchInnerRight,
        Layout->TouchCardRect.bottom - 20);

    Layout->StatusTextRect = MakeRect(
        Layout->StatusCardRect.left + 22,
        Layout->StatusCardRect.top + 46,
        Layout->StatusCardRect.right - 22,
        Layout->StatusCardRect.bottom - 20);

    Layout->ContentHeight = statusCardTop + UI_STATUS_CARD_HEIGHT + UI_MARGIN;
}

static void
MoveChildControl(
    _In_opt_ HWND Control,
    _In_ const RECT& Rect
    )
{
    if (Control != nullptr)
    {
        MoveWindow(
            Control,
            Rect.left,
            Rect.top,
            Rect.right - Rect.left,
            Rect.bottom - Rect.top,
            FALSE);
    }
}

static void
UpdateScrollBar(
    _In_ HWND Window,
    _In_ int ClientHeight,
    _In_ int ContentHeight
    )
{
    SCROLLINFO si;
    int maxScroll;

    maxScroll = max(ContentHeight - ClientHeight, 0);
    gAppState.ScrollY = ClampInt(gAppState.ScrollY, 0, maxScroll);

    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = max(ContentHeight - 1, 0);
    si.nPage = (UINT)max(ClientHeight, 0);
    si.nPos = gAppState.ScrollY;
    SetScrollInfo(Window, SB_VERT, &si, TRUE);
    ShowScrollBar(Window, SB_VERT, maxScroll > 0);
}

static void
ApplyLayout(
    _In_ HWND Window,
    _In_ BOOL RedrawWindowNow
    )
{
    RECT clientRect;
    UI_LAYOUT layout;

    GetClientRect(Window, &clientRect);
    gAppState.ClientWidth = clientRect.right - clientRect.left;
    gAppState.ClientHeight = clientRect.bottom - clientRect.top;

    ComputeUiLayout(gAppState.ClientWidth, 0, &layout);
    UpdateScrollBar(Window, gAppState.ClientHeight, layout.ContentHeight);
    ComputeUiLayout(gAppState.ClientWidth, gAppState.ScrollY, &layout);

    MoveChildControl(gAppState.ConfigCombo, layout.ConfigComboRect);
    MoveChildControl(gAppState.ApplyButton, layout.ApplyButtonRect);
    MoveChildControl(gAppState.RefreshButton, layout.RefreshButtonRect);
    MoveChildControl(gAppState.OffButton, layout.OffButtonRect);
    MoveChildControl(gAppState.RgbCheck, layout.RgbCheckRect);
    MoveChildControl(gAppState.InfoText, layout.InfoTextRect);
    MoveChildControl(gAppState.TouchRateCombo, layout.TouchRateComboRect);
    MoveChildControl(gAppState.TouchApplyButton, layout.TouchApplyButtonRect);
    MoveChildControl(gAppState.TouchInfoText, layout.TouchInfoTextRect);
    MoveChildControl(gAppState.StatusText, layout.StatusTextRect);

    if (RedrawWindowNow)
    {
        RedrawWindow(
            Window,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
}

static void
ScrollToPosition(
    _In_ HWND Window,
    _In_ int NewScrollY
    )
{
    RECT clientRect;
    UI_LAYOUT layout;
    int maxScroll;

    GetClientRect(Window, &clientRect);
    ComputeUiLayout(clientRect.right - clientRect.left, 0, &layout);
    maxScroll = max(layout.ContentHeight - (clientRect.bottom - clientRect.top), 0);
    NewScrollY = ClampInt(NewScrollY, 0, maxScroll);
    if (NewScrollY == gAppState.ScrollY)
    {
        return;
    }

    gAppState.ScrollY = NewScrollY;
    ApplyLayout(Window, TRUE);
}

static void
DrawRoundedCard(
    _In_ HDC Dc,
    _In_ const RECT& Rect
    )
{
    HPEN borderPen;
    HGDIOBJ oldPen;
    HGDIOBJ oldBrush;

    borderPen = CreatePen(PS_SOLID, 1, kColorCardBorder);
    oldPen = SelectObject(Dc, borderPen);
    oldBrush = SelectObject(Dc, gAppState.CardBrush);
    RoundRect(Dc, Rect.left, Rect.top, Rect.right, Rect.bottom, 16, 16);
    SelectObject(Dc, oldBrush);
    SelectObject(Dc, oldPen);
    DeleteObject(borderPen);
}

static void
DrawUiChrome(
    _In_ HWND Window,
    _In_ HDC Dc
    )
{
    RECT clientRect;
    UI_LAYOUT layout;
    RECT headerRect;
    HFONT oldFont;
    COLORREF oldTextColor;
    int oldBkMode;

    GetClientRect(Window, &clientRect);
    FillRect(Dc, &clientRect, gAppState.WindowBrush);
    ComputeUiLayout(clientRect.right - clientRect.left, gAppState.ScrollY, &layout);

    DrawRoundedCard(Dc, layout.LedCardRect);
    DrawRoundedCard(Dc, layout.TouchCardRect);
    DrawRoundedCard(Dc, layout.StatusCardRect);

    oldBkMode = SetBkMode(Dc, TRANSPARENT);
    oldTextColor = SetTextColor(Dc, kColorTextPrimary);

    headerRect = layout.HeaderTitleRect;
    oldFont = (HFONT)SelectObject(Dc, gAppState.TitleFont);
    DrawTextW(Dc, L"Component Control", -1, &headerRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(Dc, gAppState.SmallFont);
    SetTextColor(Dc, kColorTextMuted);
    headerRect = layout.HeaderSubtitleRect;
    DrawTextW(
        Dc,
        L"Manage AW22 fan lighting and GT9916R touchscreen settings from one place.",
        -1,
        &headerRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(Dc, gAppState.SectionFont);
    SetTextColor(Dc, kColorTextAccent);
    headerRect = MakeRect(layout.LedCardRect.left + 18, layout.LedCardRect.top + 14, layout.LedCardRect.right - 18, layout.LedCardRect.top + 38);
    DrawTextW(Dc, L"AW22 Lighting", -1, &headerRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    headerRect = MakeRect(layout.TouchCardRect.left + 18, layout.TouchCardRect.top + 14, layout.TouchCardRect.right - 18, layout.TouchCardRect.top + 38);
    DrawTextW(Dc, L"GT9916R Touchscreen", -1, &headerRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    headerRect = MakeRect(layout.StatusCardRect.left + 18, layout.StatusCardRect.top + 14, layout.StatusCardRect.right - 18, layout.StatusCardRect.top + 38);
    DrawTextW(Dc, L"Status", -1, &headerRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(Dc, gAppState.SmallFont);
    SetTextColor(Dc, kColorTextMuted);
    headerRect = MakeRect(layout.LedCardRect.left + 24, layout.LedCardRect.top + 48, layout.LedCardRect.right - 24, layout.LedCardRect.top + 66);
    DrawTextW(Dc, L"LED config", -1, &headerRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    headerRect = MakeRect(layout.TouchCardRect.left + 24, layout.TouchCardRect.top + 48, layout.TouchCardRect.right - 24, layout.TouchCardRect.top + 66);
    DrawTextW(Dc, L"Sampling rate", -1, &headerRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(Dc, oldFont);
    SetTextColor(Dc, oldTextColor);
    SetBkMode(Dc, oldBkMode);
}

static std::wstring
FormatWin32Error(
    _In_ DWORD Error
    )
{
    WCHAR message[512];

    message[0] = L'\0';
    if (FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            Error,
            0,
            message,
            ARRAYSIZE(message),
            nullptr) == 0)
    {
        (void)StringCchPrintfW(message, ARRAYSIZE(message), L"Win32 error 0x%08lx", Error);
    }

    return std::wstring(message);
}

static std::wstring
AsciiToWide(
    _In_z_ PCSTR Text
    )
{
    int wideLength;
    std::wstring result;

    if (Text == nullptr)
    {
        return std::wstring();
    }

    wideLength = MultiByteToWideChar(CP_ACP, 0, Text, -1, nullptr, 0);
    if (wideLength <= 1)
    {
        return std::wstring();
    }

    result.resize((size_t)wideLength - 1u);
    (void)MultiByteToWideChar(CP_ACP, 0, Text, -1, &result[0], wideLength);
    return result;
}

static std::wstring
FormatConfigDisplayName(
    _In_ const AW22XXX_CONFIG_DESCRIPTOR& Config
    )
{
    WCHAR buffer[256];
    std::wstring name;

    name = AsciiToWide(Config.Name);
    if (name.empty())
    {
        name.assign(L"<unnamed>");
    }

    (void)StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"%ls (0x%02lx)",
        name.c_str(),
        Config.ConfigId);
    return std::wstring(buffer);
}

static PCWSTR
TouchReportRateLabel(
    _In_ ULONG ReportRateLevel
    )
{
    switch (ReportRateLevel)
    {
    case GOODIX_REPORT_RATE_120HZ:
        return L"120 Hz";
    case GOODIX_REPORT_RATE_240HZ:
        return L"240 Hz";
    case GOODIX_REPORT_RATE_480HZ:
        return L"480 Hz";
    case GOODIX_REPORT_RATE_960HZ:
        return L"960 Hz";
    default:
        return L"Unknown";
    }
}

static PCWSTR
Aw22ChipTypeLabel(
    _In_ UCHAR ChipType
    )
{
    switch (ChipType)
    {
    case AW22XXX_CHIPTYPE_22118:
        return L"AW22118";
    case AW22XXX_CHIPTYPE_22127:
        return L"AW22127";
    default:
        return L"Unknown";
    }
}

static std::wstring
FormatAw22ChipStatus(
    _In_ const AW22XXX_DEVICE_INFORMATION& Information
    )
{
    WCHAR buffer[160];
    PCWSTR baseState;
    PCWSTR resetSuffix;

    if (((Information.Flags & AW22XXX_DEVICE_FLAG_INITIALIZED) != 0u)
        && ((Information.Flags & AW22XXX_DEVICE_FLAG_POWERED) != 0u))
    {
        baseState = L"Ready";
    }
    else if ((Information.Flags & AW22XXX_DEVICE_FLAG_POWERED) != 0u)
    {
        baseState = L"Powered";
    }
    else if ((Information.Flags & AW22XXX_DEVICE_FLAG_INITIALIZED) != 0u)
    {
        baseState = L"Initialized";
    }
    else
    {
        baseState = L"Unavailable";
    }

    resetSuffix = ((Information.Flags & AW22XXX_DEVICE_FLAG_SOFTRESET_VERIFIED) != 0u)
        ? L", reset verified"
        : L"";

    (void)StringCchPrintfW(
        buffer,
        ARRAYSIZE(buffer),
        L"%ls%ls",
        baseState,
        resetSuffix);
    return std::wstring(buffer);
}

static void
SetStatusText(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    ...
    )
{
    WCHAR buffer[512];
    va_list args;

    buffer[0] = L'\0';
    va_start(args, Format);
    (void)StringCchVPrintfW(buffer, ARRAYSIZE(buffer), Format, args);
    va_end(args);

    if (gAppState.StatusText != nullptr)
    {
        SetWindowTextW(gAppState.StatusText, buffer);
    }
}

static void
SetInfoText(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    ...
    )
{
    WCHAR buffer[1024];
    va_list args;

    buffer[0] = L'\0';
    va_start(args, Format);
    (void)StringCchVPrintfW(buffer, ARRAYSIZE(buffer), Format, args);
    va_end(args);

    if (gAppState.InfoText != nullptr)
    {
        SetWindowTextW(gAppState.InfoText, buffer);
    }
}

static void
EnableInteractiveControls(
    _In_ BOOL EnableApply,
    _In_ BOOL EnableDeviceActions,
    _In_ BOOL EnableTouchControls
    )
{
    if (gAppState.ConfigCombo != nullptr)
    {
        EnableWindow(gAppState.ConfigCombo, EnableApply);
    }
    if (gAppState.ApplyButton != nullptr)
    {
        EnableWindow(gAppState.ApplyButton, EnableApply);
    }
    if (gAppState.RefreshButton != nullptr)
    {
        EnableWindow(gAppState.RefreshButton, TRUE);
    }
    if (gAppState.OffButton != nullptr)
    {
        EnableWindow(gAppState.OffButton, EnableDeviceActions);
    }
    if (gAppState.RgbCheck != nullptr)
    {
        EnableWindow(gAppState.RgbCheck, EnableDeviceActions);
    }
    if (gAppState.TouchRateCombo != nullptr)
    {
        EnableWindow(gAppState.TouchRateCombo, EnableTouchControls);
    }
    if (gAppState.TouchApplyButton != nullptr)
    {
        EnableWindow(gAppState.TouchApplyButton, EnableTouchControls);
    }
}

static BOOL
GetFirstDevicePath(
    _Out_ std::wstring& DevicePath
    )
{
    HDEVINFO deviceInfoSet;
    SP_DEVICE_INTERFACE_DATA interfaceData;
    DWORD requiredLength;
    std::vector<BYTE> detailBuffer;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W detailData;
    BOOL ok;

    DevicePath.clear();
    deviceInfoSet = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_AW22XXX_LED,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    interfaceData.cbSize = sizeof(interfaceData);
    ok = SetupDiEnumDeviceInterfaces(
        deviceInfoSet,
        nullptr,
        &GUID_DEVINTERFACE_AW22XXX_LED,
        0,
        &interfaceData);
    if (!ok)
    {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return FALSE;
    }

    requiredLength = 0;
    (void)SetupDiGetDeviceInterfaceDetailW(
        deviceInfoSet,
        &interfaceData,
        nullptr,
        0,
        &requiredLength,
        nullptr);
    if (requiredLength < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
    {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    detailBuffer.resize(requiredLength);
    detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
    detailData->cbSize = sizeof(*detailData);

    ok = SetupDiGetDeviceInterfaceDetailW(
        deviceInfoSet,
        &interfaceData,
        detailData,
        requiredLength,
        nullptr,
        nullptr);
    if (ok)
    {
        DevicePath.assign(detailData->DevicePath);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return ok;
}

static void
CloseDeviceHandle()
{
    if (gAppState.Device != INVALID_HANDLE_VALUE)
    {
        CloseHandle(gAppState.Device);
        gAppState.Device = INVALID_HANDLE_VALUE;
    }
}

static void
CloseTouchDeviceHandle()
{
    if (gAppState.TouchDevice != INVALID_HANDLE_VALUE)
    {
        CloseHandle(gAppState.TouchDevice);
        gAppState.TouchDevice = INVALID_HANDLE_VALUE;
    }
}

static BOOL
EnsureDeviceOpen()
{
    std::wstring devicePath;

    if (gAppState.Device != INVALID_HANDLE_VALUE)
    {
        return TRUE;
    }

    if (!GetFirstDevicePath(devicePath))
    {
        SetStatusText(L"Driver interface not found: %ls", FormatWin32Error(GetLastError()).c_str());
        return FALSE;
    }

    gAppState.Device = CreateFileW(
        devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (gAppState.Device == INVALID_HANDLE_VALUE)
    {
        SetStatusText(L"Open driver failed: %ls", FormatWin32Error(GetLastError()).c_str());
        return FALSE;
    }

    return TRUE;
}

static BOOL
ReadTouchReportRateLevel(
    _Out_ ULONG* ReportRateLevel
    )
{
    GOODIX_TOUCH_REPORT_RATE_STATE state;
    DWORD bytesReturned;

    *ReportRateLevel = GOODIX_REPORT_RATE_240HZ;

    if (!EnsureTouchDeviceOpen())
    {
        return FALSE;
    }

    ZeroMemory(&state, sizeof(state));
    bytesReturned = 0;
    if (!DeviceIoControl(
            gAppState.TouchDevice,
            IOCTL_GOODIX_TOUCH_GET_REPORT_RATE,
            nullptr,
            0,
            &state,
            sizeof(state),
            &bytesReturned,
            nullptr))
    {
        SetStatusText(L"Read touch report rate failed: %ls", FormatWin32Error(GetLastError()).c_str());
        return FALSE;
    }

    if (bytesReturned < sizeof(state))
    {
        SetStatusText(L"Touch report rate reply too small (%lu bytes)", bytesReturned);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    *ReportRateLevel = state.PersistentLevel;
    return TRUE;
}

static BOOL
GetTouchDevicePath(
    _Out_ std::wstring& DevicePath
    )
{
    DevicePath.assign(GOODIX_TOUCH_CONTROL_WIN32_PATH);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static BOOL
GetTouchDeviceInterfacePath(
    _Out_ std::wstring& DevicePath
    )
{
    HDEVINFO deviceInfoSet;
    SP_DEVICE_INTERFACE_DATA interfaceData;
    DWORD requiredLength;
    std::vector<BYTE> detailBuffer;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W detailData;
    BOOL ok;

    DevicePath.clear();
    deviceInfoSet = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_GOODIX_TOUCH_CONTROL,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    interfaceData.cbSize = sizeof(interfaceData);
    ok = SetupDiEnumDeviceInterfaces(
        deviceInfoSet,
        nullptr,
        &GUID_DEVINTERFACE_GOODIX_TOUCH_CONTROL,
        0,
        &interfaceData);
    if (!ok)
    {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return FALSE;
    }

    requiredLength = 0;
    (void)SetupDiGetDeviceInterfaceDetailW(
        deviceInfoSet,
        &interfaceData,
        nullptr,
        0,
        &requiredLength,
        nullptr);
    if (requiredLength < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
    {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    detailBuffer.resize(requiredLength);
    detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
    detailData->cbSize = sizeof(*detailData);

    ok = SetupDiGetDeviceInterfaceDetailW(
        deviceInfoSet,
        &interfaceData,
        detailData,
        requiredLength,
        nullptr,
        nullptr);
    if (ok)
    {
        DevicePath.assign(detailData->DevicePath);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return ok;
}

static BOOL
EnsureTouchDeviceOpen()
{
    std::wstring devicePath;

    if (gAppState.TouchDevice != INVALID_HANDLE_VALUE)
    {
        return TRUE;
    }

    if (!GetTouchDevicePath(devicePath))
    {
        SetStatusText(L"Touch driver interface not found: %ls", FormatWin32Error(GetLastError()).c_str());
        return FALSE;
    }

    gAppState.TouchDevice = CreateFileW(
        devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (gAppState.TouchDevice == INVALID_HANDLE_VALUE)
    {
        DWORD openError = GetLastError();

        if (openError == ERROR_FILE_NOT_FOUND || openError == ERROR_PATH_NOT_FOUND)
        {
            if (GetTouchDeviceInterfacePath(devicePath))
            {
                gAppState.TouchDevice = CreateFileW(
                    devicePath.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);
                if (gAppState.TouchDevice != INVALID_HANDLE_VALUE)
                {
                    return TRUE;
                }

                openError = GetLastError();
            }
        }

        SetStatusText(L"Open touch driver failed: %ls", FormatWin32Error(openError).c_str());
        SetLastError(openError);
        return FALSE;
    }

    return TRUE;
}

static INT
PopulateTouchRateCombo(
    _In_ ULONG SelectedLevel
    )
{
    struct TOUCH_RATE_OPTION {
        ULONG Level;
        PCWSTR Label;
    };
    static const TOUCH_RATE_OPTION kOptions[] = {
        { GOODIX_REPORT_RATE_120HZ, L"120 Hz" },
        { GOODIX_REPORT_RATE_240HZ, L"240 Hz" },
        { GOODIX_REPORT_RATE_480HZ, L"480 Hz" },
        { GOODIX_REPORT_RATE_960HZ, L"960 Hz" },
    };
    INT selectedIndex = -1;

    SendMessageW(gAppState.TouchRateCombo, CB_RESETCONTENT, 0, 0);
    for (INT i = 0; i < ARRAYSIZE(kOptions); i++)
    {
        INT comboIndex = (INT)SendMessageW(gAppState.TouchRateCombo, CB_ADDSTRING, 0, (LPARAM)kOptions[i].Label);
        if (comboIndex < 0)
        {
            continue;
        }

        SendMessageW(gAppState.TouchRateCombo, CB_SETITEMDATA, (WPARAM)comboIndex, (LPARAM)kOptions[i].Level);
        if (kOptions[i].Level == SelectedLevel)
        {
            selectedIndex = comboIndex;
        }
    }

    if ((selectedIndex < 0) && (SendMessageW(gAppState.TouchRateCombo, CB_GETCOUNT, 0, 0) > 0))
    {
        selectedIndex = 1;
    }

    if (selectedIndex >= 0)
    {
        SendMessageW(gAppState.TouchRateCombo, CB_SETCURSEL, (WPARAM)selectedIndex, 0);
    }

    return selectedIndex;
}

static BOOL
ApplyTouchReportRate(
    _In_ ULONG ReportRateLevel
    )
{
    GOODIX_REPORT_RATE_CONTROL controlInfo;
    DWORD bytesReturned;

    if (!EnsureTouchDeviceOpen())
    {
        return FALSE;
    }

    ZeroMemory(&controlInfo, sizeof(controlInfo));
    controlInfo.Level = ReportRateLevel;
    bytesReturned = 0;

    if (!DeviceIoControl(
            gAppState.TouchDevice,
            IOCTL_GOODIX_TOUCH_SET_REPORT_RATE,
            &controlInfo,
            sizeof(controlInfo),
            nullptr,
            0,
            &bytesReturned,
            nullptr))
    {
        SetStatusText(L"Set touch report rate failed: %ls", FormatWin32Error(GetLastError()).c_str());
        return FALSE;
    }

    return TRUE;
}

static BOOL
SendIoctl(
    _In_ DWORD Ioctl,
    _In_reads_bytes_opt_(InputLength) const void* InputBuffer,
    _In_ DWORD InputLength,
    _Out_writes_bytes_to_opt_(OutputLength, *BytesReturned) void* OutputBuffer,
    _In_ DWORD OutputLength,
    _Out_opt_ DWORD* BytesReturned
    )
{
    DWORD localBytesReturned;

    if (!EnsureDeviceOpen())
    {
        return FALSE;
    }

    localBytesReturned = 0;
    if (BytesReturned == nullptr)
    {
        BytesReturned = &localBytesReturned;
    }

    if (DeviceIoControl(
            gAppState.Device,
            Ioctl,
            const_cast<void*>(InputBuffer),
            InputLength,
            OutputBuffer,
            OutputLength,
            BytesReturned,
            nullptr))
    {
        return TRUE;
    }

    SetStatusText(L"IOCTL 0x%08lx failed: %ls", Ioctl, FormatWin32Error(GetLastError()).c_str());
    return FALSE;
}

static BOOL
FetchDeviceInformation(
    _Out_ PAW22XXX_DEVICE_INFORMATION Information
    )
{
    DWORD bytesReturned;

    ZeroMemory(Information, sizeof(*Information));
    if (!SendIoctl(
            IOCTL_AW22XXX_GET_INFORMATION,
            nullptr,
            0,
            Information,
            sizeof(*Information),
            &bytesReturned))
    {
        return FALSE;
    }

    return bytesReturned >= sizeof(*Information);
}

static BOOL
FetchConfigTable(
    _Out_ std::vector<AW22XXX_CONFIG_DESCRIPTOR>& Configs
    )
{
    std::vector<BYTE> buffer;
    PAW22XXX_CONFIG_TABLE table;
    DWORD bytesReturned;
    size_t requiredSize;

    Configs.clear();
    buffer.resize(CONFIG_TABLE_BUFFER_SIZE);
    if (!SendIoctl(
            IOCTL_AW22XXX_GET_CONFIG_TABLE,
            nullptr,
            0,
            buffer.data(),
            (DWORD)buffer.size(),
            &bytesReturned))
    {
        return FALSE;
    }

    if (bytesReturned < sizeof(AW22XXX_CONFIG_TABLE))
    {
        SetStatusText(L"Config table reply too small (%lu bytes)", bytesReturned);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    table = reinterpret_cast<PAW22XXX_CONFIG_TABLE>(buffer.data());
    requiredSize = FIELD_OFFSET(AW22XXX_CONFIG_TABLE, Entries) +
        ((size_t)table->Count * sizeof(AW22XXX_CONFIG_DESCRIPTOR));
    if ((table->Version != AW22XXX_CONFIG_TABLE_VERSION)
        || (requiredSize > bytesReturned)
        || (requiredSize > buffer.size()))
    {
        SetStatusText(L"Config table reply invalid (version=%lu count=%lu)", table->Version, table->Count);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    Configs.assign(table->Entries, table->Entries + table->Count);
    return TRUE;
}

static INT
PopulateConfigCombo(
    _In_ ULONG SelectedConfigId
    )
{
    size_t index;
    INT selectedIndex;
    INT availableCount;

    SendMessageW(gAppState.ConfigCombo, CB_RESETCONTENT, 0, 0);
    selectedIndex = -1;
    availableCount = 0;

    for (index = 0; index < gAppState.Configs.size(); index++)
    {
        const AW22XXX_CONFIG_DESCRIPTOR& config = gAppState.Configs[index];
        std::wstring displayName;
        INT comboIndex;

        if ((config.Flags & AW22XXX_CONFIG_FLAG_AVAILABLE) == 0u)
        {
            continue;
        }

        displayName = FormatConfigDisplayName(config);
        comboIndex = (INT)SendMessageW(gAppState.ConfigCombo, CB_ADDSTRING, 0, (LPARAM)displayName.c_str());
        if (comboIndex < 0)
        {
            continue;
        }

        availableCount++;
        SendMessageW(gAppState.ConfigCombo, CB_SETITEMDATA, (WPARAM)comboIndex, (LPARAM)config.ConfigId);
        if (config.ConfigId == SelectedConfigId)
        {
            selectedIndex = comboIndex;
        }
    }

    if ((selectedIndex < 0) && (SendMessageW(gAppState.ConfigCombo, CB_GETCOUNT, 0, 0) > 0))
    {
        selectedIndex = 0;
    }
    if (selectedIndex >= 0)
    {
        SendMessageW(gAppState.ConfigCombo, CB_SETCURSEL, (WPARAM)selectedIndex, 0);
    }

    return availableCount;
}

static void
RefreshUi()
{
    AW22XXX_DEVICE_INFORMATION information;
    INT availableConfigCount;
    ULONG touchReportRateLevel = GOODIX_REPORT_RATE_240HZ;
    BOOL ledAvailable;
    BOOL touchAvailable;

    ledAvailable = FetchConfigTable(gAppState.Configs) && FetchDeviceInformation(&information);
    touchAvailable = ReadTouchReportRateLevel(&touchReportRateLevel);

    if (ledAvailable)
    {
        std::wstring chipStatus;

        availableConfigCount = PopulateConfigCombo(information.CurrentConfigId);
        SendMessageW(
            gAppState.RgbCheck,
            BM_SETCHECK,
            information.UseRgbOverride ? BST_CHECKED : BST_UNCHECKED,
            0);
        chipStatus = FormatAw22ChipStatus(information);

        SetInfoText(
            L"Chip Status: %ls\r\n"
            L"Chip ID: 0x%02x\r\n"
            L"Chip Type: %ls\r\n"
            L"Selected Config: 0x%02lx\r\n"
            L"Available Configs: %lu / %zu\r\n"
            L"IMAX: 0x%02x\r\n"
            L"Tasks: 0x%02x / 0x%02x\r\n"
            L"Flags: 0x%02x",
            chipStatus.c_str(),
            information.ChipIdRegister,
            Aw22ChipTypeLabel(information.ChipType),
            information.CurrentConfigId,
            (ULONG)availableConfigCount,
            gAppState.Configs.size(),
            information.ImaxCode,
            information.Task0,
            information.Task1,
            information.Flags);
    }
    else
    {
        availableConfigCount = 0;
        SetInfoText(L"AW22 LED driver not available.");
    }

    if (touchAvailable)
    {
        std::wstring touchInfo;

        PopulateTouchRateCombo(touchReportRateLevel);
        touchInfo = std::wstring(L"Touchscreen report rate: ")
            + TouchReportRateLabel(touchReportRateLevel)
            + L"\r\nApplies immediately and persists across reboot.";
        SetWindowTextW(gAppState.TouchInfoText, touchInfo.c_str());
    }
    else
    {
        PopulateTouchRateCombo(GOODIX_REPORT_RATE_240HZ);
        SetWindowTextW(gAppState.TouchInfoText, L"Goodix GT9916R touchscreen driver not available.");
    }

    EnableInteractiveControls(
        ledAvailable && (availableConfigCount > 0),
        ledAvailable,
        touchAvailable);

    if (ledAvailable || touchAvailable)
    {
        if (ledAvailable && (availableConfigCount == 0))
        {
            SetStatusText(L"Touchscreen ready. LED driver responded without available configs.");
        }
        else
        {
            SetStatusText(L"Ready");
        }
    }
    else
    {
        SetStatusText(L"No supported devices found.");
    }
}

static void
ApplySelectedConfig()
{
    AW22XXX_CONFIG_REQUEST request;
    LRESULT selectedIndex;
    LRESULT configId;

    selectedIndex = SendMessageW(gAppState.ConfigCombo, CB_GETCURSEL, 0, 0);
    if (selectedIndex == CB_ERR)
    {
        SetStatusText(L"No config selected.");
        return;
    }

    configId = SendMessageW(gAppState.ConfigCombo, CB_GETITEMDATA, (WPARAM)selectedIndex, 0);
    if (configId == CB_ERR)
    {
        SetStatusText(L"Selected config has no id.");
        return;
    }

    ZeroMemory(&request, sizeof(request));
    request.ConfigId = (ULONG)configId;
    request.UseRgbOverride =
        (SendMessageW(gAppState.RgbCheck, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1u : 0u;

    if (!SendIoctl(IOCTL_AW22XXX_APPLY_CONFIG, &request, sizeof(request), nullptr, 0, nullptr))
    {
        return;
    }

    SetStatusText(L"Applied config 0x%02lx.", request.ConfigId);
    RefreshUi();
}

static void
TurnLedsOff()
{
    if (!SendIoctl(IOCTL_AW22XXX_LED_OFF, nullptr, 0, nullptr, 0, nullptr))
    {
        return;
    }

    SetStatusText(L"LED output disabled.");
    RefreshUi();
}

static void
ApplySelectedTouchRate()
{
    LRESULT selectedIndex;
    LRESULT reportRateLevel;

    selectedIndex = SendMessageW(gAppState.TouchRateCombo, CB_GETCURSEL, 0, 0);
    if (selectedIndex == CB_ERR)
    {
        SetStatusText(L"No touch report rate selected.");
        return;
    }

    reportRateLevel = SendMessageW(gAppState.TouchRateCombo, CB_GETITEMDATA, (WPARAM)selectedIndex, 0);
    if (reportRateLevel == CB_ERR)
    {
        SetStatusText(L"Selected touch report rate is invalid.");
        return;
    }

    if (!ApplyTouchReportRate((ULONG)reportRateLevel))
    {
        return;
    }

    SetStatusText(L"Applied touch report rate %ls.", TouchReportRateLabel((ULONG)reportRateLevel));
    RefreshUi();
}

static void
CreateMainControls(
    _In_ HWND Window
    )
{
    gAppState.ConfigCombo = CreateWindowExW(
        0,
        L"COMBOBOX",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        42,
        138,
        458,
        260,
        Window,
        (HMENU)IDC_CONFIG_COMBO,
        hInst,
        nullptr);

    gAppState.ApplyButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Apply Config",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        526,
        136,
        188,
        32,
        Window,
        (HMENU)IDC_APPLY_BUTTON,
        hInst,
        nullptr);

    gAppState.RefreshButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Refresh",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        526,
        176,
        188,
        32,
        Window,
        (HMENU)IDC_REFRESH_BUTTON,
        hInst,
        nullptr);

    gAppState.OffButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Disable LED",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        526,
        216,
        188,
        32,
        Window,
        (HMENU)IDC_OFF_BUTTON,
        hInst,
        nullptr);

    gAppState.RgbCheck = CreateWindowExW(
        0,
        L"BUTTON",
        L"Use RGB override",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        42,
        178,
        220,
        24,
        Window,
        (HMENU)IDC_RGB_CHECK,
        hInst,
        nullptr);

    gAppState.InfoText = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL | SS_NOPREFIX,
        42,
        204,
        456,
        88,
        Window,
        (HMENU)IDC_INFO_TEXT,
        hInst,
        nullptr);

    gAppState.TouchRateCombo = CreateWindowExW(
        0,
        L"COMBOBOX",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        42,
        398,
        220,
        200,
        Window,
        (HMENU)IDC_TOUCH_RATE_COMBO,
        hInst,
        nullptr);

    gAppState.TouchApplyButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Apply Rate",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        280,
        396,
        144,
        32,
        Window,
        (HMENU)IDC_TOUCH_APPLY_BUTTON,
        hInst,
        nullptr);

    gAppState.TouchInfoText = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL | SS_NOPREFIX,
        42,
        434,
        672,
        26,
        Window,
        (HMENU)IDC_TOUCH_INFO_TEXT,
        hInst,
        nullptr);

    gAppState.StatusText = CreateWindowExW(
        0,
        L"STATIC",
        L"Connecting...",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL | SS_NOPREFIX,
        42,
        506,
        672,
        26,
        Window,
        (HMENU)IDC_STATUS_TEXT,
        hInst,
        nullptr);

    ApplyFont(gAppState.ConfigCombo, gAppState.BodyFont);
    ApplyFont(gAppState.ApplyButton, gAppState.BodyFont);
    ApplyFont(gAppState.RefreshButton, gAppState.BodyFont);
    ApplyFont(gAppState.OffButton, gAppState.BodyFont);
    ApplyFont(gAppState.RgbCheck, gAppState.BodyFont);
    ApplyFont(gAppState.InfoText, gAppState.SmallFont);
    ApplyFont(gAppState.TouchRateCombo, gAppState.BodyFont);
    ApplyFont(gAppState.TouchApplyButton, gAppState.BodyFont);
    ApplyFont(gAppState.TouchInfoText, gAppState.SmallFont);
    ApplyFont(gAppState.StatusText, gAppState.SmallFont);
}

int APIENTRY
wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow
    )
{
    MSG msg;
    HACCEL hAccelTable;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    InitCommonControlsEx(&icc);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_COMPONENTCTRL, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_COMPONENTCTRL));
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!TranslateAcceleratorW(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return (int)msg.wParam;
}

ATOM
MyRegisterClass(
    HINSTANCE hInstance
    )
{
    WNDCLASSEXW wcex;

    ZeroMemory(&wcex, sizeof(wcex));
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_COMPONENTCTRL));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_COMPONENTCTRL);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL
InitInstance(
    HINSTANCE hInstance,
    int nCmdShow
    )
{
    HWND hWnd;
    DWORD windowStyle;
    RECT windowRect;

    hInst = hInstance;
    windowStyle = WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN;
    windowRect = MakeRect(0, 0, UI_INITIAL_CLIENT_WIDTH, UI_INITIAL_CLIENT_HEIGHT);
    AdjustWindowRect(&windowRect, windowStyle, TRUE);
    hWnd = CreateWindowExW(
        WS_EX_COMPOSITED,
        szWindowClass,
        szTitle,
        windowStyle,
        CW_USEDEFAULT,
        0,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);
    if (hWnd == nullptr)
    {
        return FALSE;
    }

    gAppState.MainWindow = hWnd;
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

LRESULT CALLBACK
WndProc(
    HWND hWnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
    )
{
    switch (message)
    {
    case WM_CREATE:
        gAppState.ScrollY = 0;
        InitializeUiResources();
        CreateMainControls(hWnd);
        EnableInteractiveControls(FALSE, FALSE, FALSE);
        ApplyLayout(hWnd, FALSE);
        RefreshUi();
        ApplyLayout(hWnd, TRUE);
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT minTrackRect;
        DWORD windowStyle;

        windowStyle = (DWORD)GetWindowLongPtrW(hWnd, GWL_STYLE);
        minTrackRect = MakeRect(0, 0, UI_MIN_CLIENT_WIDTH, UI_MIN_CLIENT_HEIGHT);
        AdjustWindowRect(&minTrackRect, windowStyle, TRUE);
        minMaxInfo->ptMinTrackSize.x = minTrackRect.right - minTrackRect.left;
        minMaxInfo->ptMinTrackSize.y = minTrackRect.bottom - minTrackRect.top;
        return 0;
    }

    case WM_SIZE:
        ApplyLayout(hWnd, TRUE);
        return 0;

    case WM_VSCROLL:
    {
        SCROLLINFO si;
        int newPos;

        ZeroMemory(&si, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hWnd, SB_VERT, &si);
        newPos = si.nPos;

        switch (LOWORD(wParam))
        {
        case SB_TOP:
            newPos = si.nMin;
            break;
        case SB_BOTTOM:
            newPos = si.nMax;
            break;
        case SB_LINEUP:
            newPos -= UI_SCROLL_STEP;
            break;
        case SB_LINEDOWN:
            newPos += UI_SCROLL_STEP;
            break;
        case SB_PAGEUP:
            newPos -= max(gAppState.ClientHeight - UI_SCROLL_STEP, UI_SCROLL_STEP);
            break;
        case SB_PAGEDOWN:
            newPos += max(gAppState.ClientHeight - UI_SCROLL_STEP, UI_SCROLL_STEP);
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            newPos = si.nTrackPos;
            break;
        default:
            return 0;
        }

        ScrollToPosition(hWnd, newPos);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        short wheelDelta;

        wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (wheelDelta != 0)
        {
            ScrollToPosition(hWnd, gAppState.ScrollY - ((wheelDelta / WHEEL_DELTA) * UI_SCROLL_STEP));
            return 0;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc;

        hdc = BeginPaint(hWnd, &ps);
        DrawUiChrome(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND control = (HWND)lParam;

        if (control == gAppState.StatusText)
        {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, kColorCardBackground);
            SetTextColor(hdc, kColorTextPrimary);
            return (LRESULT)gAppState.CardBrush;
        }
        else if ((control == gAppState.InfoText) || (control == gAppState.TouchInfoText))
        {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, kColorCardBackground);
            SetTextColor(hdc, kColorTextMuted);
            return (LRESULT)gAppState.CardBrush;
        }
        else
        {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, kColorTextPrimary);
        }

        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_APPLY_BUTTON:
            ApplySelectedConfig();
            return 0;

        case IDC_REFRESH_BUTTON:
            CloseDeviceHandle();
            CloseTouchDeviceHandle();
            RefreshUi();
            return 0;

        case IDC_OFF_BUTTON:
            TurnLedsOff();
            return 0;

        case IDC_TOUCH_APPLY_BUTTON:
            ApplySelectedTouchRate();
            return 0;

        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            return 0;

        case IDM_EXIT:
            DestroyWindow(hWnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        CloseDeviceHandle();
        CloseTouchDeviceHandle();
        DestroyUiResources();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

INT_PTR CALLBACK
About(
    HWND hDlg,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
    )
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if ((LOWORD(wParam) == IDOK) || (LOWORD(wParam) == IDCANCEL))
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

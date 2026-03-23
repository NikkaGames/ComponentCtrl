#include "framework.h"
#include "ComponentCtrl.h"

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
#define MAX_LOADSTRING 100
#define CONFIG_TABLE_BUFFER_SIZE (64u * 1024u)
#define GOODIX_REPORT_RATE_120HZ 0
#define GOODIX_REPORT_RATE_240HZ 1
#define GOODIX_REPORT_RATE_360HZ 2
#define GOODIX_REPORT_RATE_480HZ 3
#define GOODIX_REPORT_RATE_960HZ 4

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
    std::vector<AW22XXX_CONFIG_DESCRIPTOR> Configs;
};

static APP_STATE gAppState = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
static BOOL         GetTouchDevicePath(_Out_ std::wstring& DevicePath);
static BOOL         EnsureTouchDeviceOpen();

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
        availableConfigCount = PopulateConfigCombo(information.CurrentConfigId);
        SendMessageW(
            gAppState.RgbCheck,
            BM_SETCHECK,
            information.UseRgbOverride ? BST_CHECKED : BST_UNCHECKED,
            0);

        SetInfoText(
            L"Chip ID: 0x%02x\r\n"
            L"Chip Type: %u\r\n"
            L"Selected Config: 0x%02lx\r\n"
            L"Available Configs: %lu / %zu\r\n"
            L"IMAX: 0x%02x\r\n"
            L"Tasks: 0x%02x / 0x%02x\r\n"
            L"Flags: 0x%02x",
            information.ChipIdRegister,
            information.ChipType,
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
    (void)CreateWindowExW(
        0,
        L"STATIC",
        L"Config:",
        WS_CHILD | WS_VISIBLE,
        16,
        18,
        64,
        20,
        Window,
        nullptr,
        hInst,
        nullptr);

    gAppState.ConfigCombo = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"COMBOBOX",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        84,
        14,
        320,
        320,
        Window,
        (HMENU)IDC_CONFIG_COMBO,
        hInst,
        nullptr);

    gAppState.ApplyButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Apply",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        416,
        14,
        104,
        28,
        Window,
        (HMENU)IDC_APPLY_BUTTON,
        hInst,
        nullptr);

    gAppState.RefreshButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Refresh",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        416,
        52,
        104,
        28,
        Window,
        (HMENU)IDC_REFRESH_BUTTON,
        hInst,
        nullptr);

    gAppState.OffButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Off",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        416,
        90,
        104,
        28,
        Window,
        (HMENU)IDC_OFF_BUTTON,
        hInst,
        nullptr);

    gAppState.RgbCheck = CreateWindowExW(
        0,
        L"BUTTON",
        L"Use RGB override",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        84,
        52,
        200,
        24,
        Window,
        (HMENU)IDC_RGB_CHECK,
        hInst,
        nullptr);

    gAppState.InfoText = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        16,
        90,
        388,
        120,
        Window,
        (HMENU)IDC_INFO_TEXT,
        hInst,
        nullptr);

    (void)CreateWindowExW(
        0,
        L"STATIC",
        L"Touch rate:",
        WS_CHILD | WS_VISIBLE,
        16,
        226,
        92,
        20,
        Window,
        nullptr,
        hInst,
        nullptr);

    gAppState.TouchRateCombo = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"COMBOBOX",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        112,
        222,
        160,
        200,
        Window,
        (HMENU)IDC_TOUCH_RATE_COMBO,
        hInst,
        nullptr);

    gAppState.TouchApplyButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Set Rate",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        288,
        222,
        116,
        28,
        Window,
        (HMENU)IDC_TOUCH_APPLY_BUTTON,
        hInst,
        nullptr);

    gAppState.TouchInfoText = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        16,
        262,
        504,
        48,
        Window,
        (HMENU)IDC_TOUCH_INFO_TEXT,
        hInst,
        nullptr);

    gAppState.StatusText = CreateWindowExW(
        0,
        L"STATIC",
        L"Connecting...",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        16,
        322,
        504,
        20,
        Window,
        (HMENU)IDC_STATUS_TEXT,
        hInst,
        nullptr);
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

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

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
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
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

    hInst = hInstance;
    hWnd = CreateWindowW(
        szWindowClass,
        szTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        0,
        552,
        420,
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
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_CREATE:
        CreateMainControls(hWnd);
        EnableInteractiveControls(FALSE, FALSE, FALSE);
        RefreshUi();
        return 0;

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

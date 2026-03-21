#include "framework.h"
#include "ComponentCtrl.h"

#include <initguid.h>
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <shellapi.h>
#include <strsafe.h>
#include <vector>
#include <string>
#include <cwchar>

#include "../aw22xxx_led/Public.h"

#pragma comment(lib, "setupapi.lib")

namespace {

std::wstring
FormatWin32Error(
    _In_ DWORD Error
    )
{
    wchar_t message[512];

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
        StringCchPrintfW(message, ARRAYSIZE(message), L"Win32 error 0x%08lx", Error);
    }

    return std::wstring(message);
}

bool
ParseUlong(
    _In_ PCWSTR Text,
    _Out_ ULONG* Value
    )
{
    wchar_t* endPtr;
    unsigned long parsedValue;

    if ((Text == nullptr) || (Value == nullptr))
    {
        return false;
    }

    endPtr = nullptr;
    parsedValue = wcstoul(Text, &endPtr, 0);
    if ((endPtr == Text) || (endPtr == nullptr) || (*endPtr != L'\0'))
    {
        return false;
    }

    *Value = (ULONG)parsedValue;
    return true;
}

void
PrintUsage()
{
    fwprintf(stdout,
        L"Usage:\n"
        L"  ComponentCtrl info\n"
        L"  ComponentCtrl list-configs\n"
        L"  ComponentCtrl apply-config <configId> [rgbOverride]\n"
        L"  ComponentCtrl apply-effect <effectId> [rgbOverride]\n"
        L"  ComponentCtrl set-imax <imaxCode>\n"
        L"  ComponentCtrl init\n"
        L"  ComponentCtrl off\n"
        L"  ComponentCtrl read <page> <register>\n"
        L"  ComponentCtrl write <page> <register> <value>\n");
}

bool
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
        return false;
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
        return false;
    }

    requiredLength = 0;
    (void)SetupDiGetDeviceInterfaceDetailW(
        deviceInfoSet,
        &interfaceData,
        nullptr,
        0,
        &requiredLength,
        nullptr);
    if (requiredLength == 0)
    {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
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
    return ok ? true : false;
}

HANDLE
OpenDevice()
{
    std::wstring devicePath;

    if (!GetFirstDevicePath(devicePath))
    {
        return INVALID_HANDLE_VALUE;
    }

    return CreateFileW(
        devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
}

bool
SendIoctl(
    _In_ HANDLE Device,
    _In_ DWORD Ioctl,
    _In_reads_bytes_opt_(InputLength) const void* InputBuffer,
    _In_ DWORD InputLength,
    _Out_writes_bytes_to_opt_(OutputLength, *BytesReturned) void* OutputBuffer,
    _In_ DWORD OutputLength,
    _Out_opt_ DWORD* BytesReturned
    )
{
    DWORD localBytesReturned;

    localBytesReturned = 0;
    if (BytesReturned == nullptr)
    {
        BytesReturned = &localBytesReturned;
    }

    return DeviceIoControl(
        Device,
        Ioctl,
        const_cast<void*>(InputBuffer),
        InputLength,
        OutputBuffer,
        OutputLength,
        BytesReturned,
        nullptr) ? true : false;
}

int
CommandInfo(
    _In_ HANDLE Device
    )
{
    AW22XXX_DEVICE_INFORMATION information;
    DWORD bytesReturned;

    ZeroMemory(&information, sizeof(information));
    if (!SendIoctl(
            Device,
            IOCTL_AW22XXX_GET_INFORMATION,
            nullptr,
            0,
            &information,
            sizeof(information),
            &bytesReturned))
    {
        fwprintf(stderr, L"IOCTL_AW22XXX_GET_INFORMATION failed: %ls\n", FormatWin32Error(GetLastError()).c_str());
        return 1;
    }

    fwprintf(stdout, L"ChipId=0x%02x ChipType=%u CurrentPage=0x%02x\n",
        information.ChipIdRegister,
        information.ChipType,
        information.CurrentPage);
    fwprintf(stdout, L"Imax=0x%02x Brightness=0x%02x Task0=0x%02x Task1=0x%02x\n",
        information.ImaxCode,
        information.Brightness,
        information.Task0,
        information.Task1);
    fwprintf(stdout, L"Flags=0x%02x Effect=%u ConfigId=0x%02lx RgbOverride=%u\n",
        information.Flags,
        information.Effect,
        information.CurrentConfigId,
        information.UseRgbOverride);
    fwprintf(stdout, L"SequencePairLimit=%lu ConfigCount=%lu\n",
        information.SequencePairLimit,
        information.ConfigCount);
    return 0;
}

int
CommandListConfigs(
    _In_ HANDLE Device
    )
{
    std::vector<BYTE> buffer;
    PAW22XXX_CONFIG_TABLE table;
    DWORD bytesReturned;
    ULONG index;

    buffer.resize(64 * 1024);
    if (!SendIoctl(
            Device,
            IOCTL_AW22XXX_GET_CONFIG_TABLE,
            nullptr,
            0,
            buffer.data(),
            (DWORD)buffer.size(),
            &bytesReturned))
    {
        fwprintf(stderr, L"IOCTL_AW22XXX_GET_CONFIG_TABLE failed: %ls\n", FormatWin32Error(GetLastError()).c_str());
        return 1;
    }

    table = reinterpret_cast<PAW22XXX_CONFIG_TABLE>(buffer.data());
    fwprintf(stdout, L"Version=%lu Count=%lu\n", table->Version, table->Count);
    for (index = 0; index < table->Count; index++)
    {
        const AW22XXX_CONFIG_DESCRIPTOR& entry = table->Entries[index];
        fwprintf(stdout,
            L"0x%02lx  %-28hs  pairs=%-4lu  flags=0x%02lx\n",
            entry.ConfigId,
            entry.Name,
            entry.PairCount,
            entry.Flags);
    }

    return 0;
}

int
CommandApplyConfig(
    _In_ HANDLE Device,
    _In_ PCWSTR ConfigIdText,
    _In_opt_ PCWSTR RgbOverrideText
    )
{
    AW22XXX_CONFIG_REQUEST request;
    ULONG configId;
    ULONG rgbOverride;

    if (!ParseUlong(ConfigIdText, &configId))
    {
        fwprintf(stderr, L"Invalid config id: %ls\n", ConfigIdText);
        return 1;
    }

    rgbOverride = 0;
    if ((RgbOverrideText != nullptr) && !ParseUlong(RgbOverrideText, &rgbOverride))
    {
        fwprintf(stderr, L"Invalid rgbOverride value: %ls\n", RgbOverrideText);
        return 1;
    }

    ZeroMemory(&request, sizeof(request));
    request.ConfigId = configId;
    request.UseRgbOverride = rgbOverride ? 1u : 0u;
    if (!SendIoctl(
            Device,
            IOCTL_AW22XXX_APPLY_CONFIG,
            &request,
            sizeof(request),
            nullptr,
            0,
            nullptr))
    {
        fwprintf(stderr, L"IOCTL_AW22XXX_APPLY_CONFIG failed: %ls\n", FormatWin32Error(GetLastError()).c_str());
        return 1;
    }

    fwprintf(stdout, L"Applied config 0x%02lx (rgbOverride=%lu)\n", configId, rgbOverride);
    return 0;
}

int
CommandApplyEffect(
    _In_ HANDLE Device,
    _In_ PCWSTR EffectIdText,
    _In_opt_ PCWSTR RgbOverrideText
    )
{
    AW22XXX_EFFECT_REQUEST request;
    ULONG effectId;
    ULONG rgbOverride;

    if (!ParseUlong(EffectIdText, &effectId))
    {
        fwprintf(stderr, L"Invalid effect id: %ls\n", EffectIdText);
        return 1;
    }

    rgbOverride = 0;
    if ((RgbOverrideText != nullptr) && !ParseUlong(RgbOverrideText, &rgbOverride))
    {
        fwprintf(stderr, L"Invalid rgbOverride value: %ls\n", RgbOverrideText);
        return 1;
    }

    ZeroMemory(&request, sizeof(request));
    request.Effect = (UCHAR)effectId;
    request.UseRgbOverride = rgbOverride ? 1u : 0u;
    if (!SendIoctl(
            Device,
            IOCTL_AW22XXX_APPLY_EFFECT,
            &request,
            sizeof(request),
            nullptr,
            0,
            nullptr))
    {
        fwprintf(stderr, L"IOCTL_AW22XXX_APPLY_EFFECT failed: %ls\n", FormatWin32Error(GetLastError()).c_str());
        return 1;
    }

    fwprintf(stdout, L"Applied effect 0x%02lx (rgbOverride=%lu)\n", effectId, rgbOverride);
    return 0;
}

int
CommandSetImax(
    _In_ HANDLE Device,
    _In_ PCWSTR ImaxText
    )
{
    AW22XXX_IMAX_REQUEST request;
    ULONG imaxCode;

    if (!ParseUlong(ImaxText, &imaxCode))
    {
        fwprintf(stderr, L"Invalid IMAX code: %ls\n", ImaxText);
        return 1;
    }

    ZeroMemory(&request, sizeof(request));
    request.ImaxCode = (UCHAR)imaxCode;
    if (!SendIoctl(Device, IOCTL_AW22XXX_SET_IMAX, &request, sizeof(request), nullptr, 0, nullptr))
    {
        fwprintf(stderr, L"IOCTL_AW22XXX_SET_IMAX failed: %ls\n", FormatWin32Error(GetLastError()).c_str());
        return 1;
    }

    fwprintf(stdout, L"Set IMAX to 0x%02lx\n", imaxCode);
    return 0;
}

int
CommandRead(
    _In_ HANDLE Device,
    _In_ PCWSTR PageText,
    _In_ PCWSTR RegisterText
    )
{
    AW22XXX_REGISTER_ACCESS request;
    AW22XXX_REGISTER_ACCESS response;
    ULONG page;
    ULONG reg;
    DWORD bytesReturned;

    if (!ParseUlong(PageText, &page) || !ParseUlong(RegisterText, &reg))
    {
        fwprintf(stderr, L"Invalid page/register\n");
        return 1;
    }

    ZeroMemory(&request, sizeof(request));
    ZeroMemory(&response, sizeof(response));
    request.Page = (UCHAR)page;
    request.Register = (UCHAR)reg;
    if (!SendIoctl(
            Device,
            IOCTL_AW22XXX_READ_REGISTER,
            &request,
            sizeof(request),
            &response,
            sizeof(response),
            &bytesReturned))
    {
        fwprintf(stderr, L"IOCTL_AW22XXX_READ_REGISTER failed: %ls\n", FormatWin32Error(GetLastError()).c_str());
        return 1;
    }

    fwprintf(stdout, L"page=0x%02x reg=0x%02x value=0x%02x\n", response.Page, response.Register, response.Value);
    return 0;
}

int
CommandWrite(
    _In_ HANDLE Device,
    _In_ PCWSTR PageText,
    _In_ PCWSTR RegisterText,
    _In_ PCWSTR ValueText
    )
{
    AW22XXX_REGISTER_ACCESS request;
    ULONG page;
    ULONG reg;
    ULONG value;

    if (!ParseUlong(PageText, &page)
        || !ParseUlong(RegisterText, &reg)
        || !ParseUlong(ValueText, &value))
    {
        fwprintf(stderr, L"Invalid page/register/value\n");
        return 1;
    }

    ZeroMemory(&request, sizeof(request));
    request.Page = (UCHAR)page;
    request.Register = (UCHAR)reg;
    request.Value = (UCHAR)value;
    if (!SendIoctl(Device, IOCTL_AW22XXX_WRITE_REGISTER, &request, sizeof(request), nullptr, 0, nullptr))
    {
        fwprintf(stderr, L"IOCTL_AW22XXX_WRITE_REGISTER failed: %ls\n", FormatWin32Error(GetLastError()).c_str());
        return 1;
    }

    fwprintf(stdout, L"Wrote page=0x%02lx reg=0x%02lx value=0x%02lx\n", page, reg, value);
    return 0;
}

} // namespace

int APIENTRY
wmain(
    _In_ int Argc,
    _In_reads_(Argc) PWSTR* Argv
    )
{
    HANDLE device;
    std::wstring command;
    int exitCode;

    if (Argc < 2)
    {
        PrintUsage();
        return 1;
    }

    command.assign(Argv[1]);
    for (wchar_t& ch : command)
    {
        ch = (wchar_t)towlower(ch);
    }

    if ((command == L"help") || (command == L"--help") || (command == L"-h"))
    {
        PrintUsage();
        return 0;
    }

    device = OpenDevice();
    if (device == INVALID_HANDLE_VALUE)
    {
        fwprintf(stderr, L"Failed to open AW22XXX device: %ls\n", FormatWin32Error(GetLastError()).c_str());
        return 1;
    }

    exitCode = 0;
    if (command == L"info")
    {
        exitCode = CommandInfo(device);
    }
    else if (command == L"list-configs")
    {
        exitCode = CommandListConfigs(device);
    }
    else if (command == L"apply-config")
    {
        if ((Argc < 3) || (Argc > 4))
        {
            PrintUsage();
            exitCode = 1;
        }
        else
        {
            exitCode = CommandApplyConfig(device, Argv[2], (Argc > 3) ? Argv[3] : nullptr);
        }
    }
    else if (command == L"apply-effect")
    {
        if ((Argc < 3) || (Argc > 4))
        {
            PrintUsage();
            exitCode = 1;
        }
        else
        {
            exitCode = CommandApplyEffect(device, Argv[2], (Argc > 3) ? Argv[3] : nullptr);
        }
    }
    else if (command == L"set-imax")
    {
        if (Argc != 3)
        {
            PrintUsage();
            exitCode = 1;
        }
        else
        {
            exitCode = CommandSetImax(device, Argv[2]);
        }
    }
    else if (command == L"off")
    {
        if (!SendIoctl(device, IOCTL_AW22XXX_LED_OFF, nullptr, 0, nullptr, 0, nullptr))
        {
            fwprintf(stderr, L"IOCTL_AW22XXX_LED_OFF failed: %ls\n", FormatWin32Error(GetLastError()).c_str());
            exitCode = 1;
        }
    }
    else if (command == L"init")
    {
        if (!SendIoctl(device, IOCTL_AW22XXX_INITIALIZE, nullptr, 0, nullptr, 0, nullptr))
        {
            fwprintf(stderr, L"IOCTL_AW22XXX_INITIALIZE failed: %ls\n", FormatWin32Error(GetLastError()).c_str());
            exitCode = 1;
        }
    }
    else if (command == L"read")
    {
        if (Argc != 4)
        {
            PrintUsage();
            exitCode = 1;
        }
        else
        {
            exitCode = CommandRead(device, Argv[2], Argv[3]);
        }
    }
    else if (command == L"write")
    {
        if (Argc != 5)
        {
            PrintUsage();
            exitCode = 1;
        }
        else
        {
            exitCode = CommandWrite(device, Argv[2], Argv[3], Argv[4]);
        }
    }
    else
    {
        fwprintf(stderr, L"Unknown command: %ls\n", Argv[1]);
        PrintUsage();
        exitCode = 1;
    }

    CloseHandle(device);
    return exitCode;
}

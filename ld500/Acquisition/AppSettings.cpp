#include "AppSettings.h"
#include <windows.h>

namespace {
    const wchar_t* kRegistryPath = L"Software\\LD500";
    const wchar_t* kAngleOffsetValueName = L"AngleOffsetDegrees";
    const wchar_t* kTrackingEnabledValueName = L"TrackingEnabled";
    const wchar_t* kShadowCastEnabledValueName = L"ShadowCastEnabled";

    // Reads a DWORD value from the app's registry key; returns false (outValue untouched) if the
    // key/value doesn't exist or isn't a DWORD.
    bool ReadRegistryDword(const wchar_t* valueName, DWORD& outValue) {
        HKEY hKey;
        bool found = false;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD size = sizeof(outValue);
            DWORD type = 0;
            if (RegQueryValueExW(hKey, valueName, NULL, &type, reinterpret_cast<LPBYTE>(&outValue), &size) == ERROR_SUCCESS
                && type == REG_DWORD) {
                found = true;
            }
            RegCloseKey(hKey);
        }
        return found;
    }

    // Writes a DWORD value to the app's registry key, creating the key if it doesn't exist yet.
    void WriteRegistryDword(const wchar_t* valueName, DWORD value) {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
            RegCloseKey(hKey);
        }
    }
}

double LoadAngleOffsetDegrees() {
    DWORD value = 0;
    double result = ReadRegistryDword(kAngleOffsetValueName, value) ? static_cast<double>(value) : 0.0;
    if (result < 0.0 || result >= 360.0) result = 0.0;
    return result;
}

void SaveAngleOffsetDegrees(double offsetDegrees) {
    WriteRegistryDword(kAngleOffsetValueName, static_cast<DWORD>(offsetDegrees + 0.5)); // round to nearest whole degree
}

bool LoadTrackingEnabled() {
    DWORD value = 0;
    return ReadRegistryDword(kTrackingEnabledValueName, value) && value != 0;
}

void SaveTrackingEnabled(bool enabled) {
    WriteRegistryDword(kTrackingEnabledValueName, enabled ? 1 : 0);
}

bool LoadShadowCastEnabled() {
    DWORD value = 0;
    return ReadRegistryDword(kShadowCastEnabledValueName, value) && value != 0;
}

void SaveShadowCastEnabled(bool enabled) {
    WriteRegistryDword(kShadowCastEnabledValueName, enabled ? 1 : 0);
}

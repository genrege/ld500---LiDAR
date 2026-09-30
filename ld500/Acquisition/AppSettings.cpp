#include "AppSettings.h"
#include <windows.h>

namespace {
    const wchar_t* kRegistryPath = L"Software\\LD500";
    const wchar_t* kAngleOffsetValueName = L"AngleOffsetDegrees";
    const wchar_t* kTrackingEnabledValueName = L"TrackingEnabled";
    const wchar_t* kShadowCastEnabledValueName = L"ShadowCastEnabled";
    const wchar_t* kPersistenceEnabledValueName = L"PersistenceEnabled";
    const wchar_t* kGridSizeCellsValueName = L"GridSizeCells";
    const wchar_t* kMinClusterCellsValueName = L"MinClusterCells";
    const wchar_t* kMaxMatchDistCellsValueName = L"MaxMatchDistCells";
    const wchar_t* kMaxMissedFramesValueName = L"MaxMissedFrames";
    const wchar_t* kMinConfirmFramesValueName = L"MinConfirmFrames";
    const wchar_t* kMaxStaticPersistenceForTrackingValueName = L"MaxStaticPersistenceForTracking";

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

bool LoadPersistenceEnabled() {
    DWORD value = 0;
    return !ReadRegistryDword(kPersistenceEnabledValueName, value) || value != 0; // default: on
}

void SavePersistenceEnabled(bool enabled) {
    WriteRegistryDword(kPersistenceEnabledValueName, enabled ? 1 : 0);
}

int LoadGridSizeCells() {
    DWORD value = 0;
    int result = ReadRegistryDword(kGridSizeCellsValueName, value) ? static_cast<int>(value) : 1000;
    return result > 0 ? result : 1000;
}

void SaveGridSizeCells(int cells) {
    WriteRegistryDword(kGridSizeCellsValueName, static_cast<DWORD>(cells));
}

int LoadMinClusterCells() {
    DWORD value = 0;
    return ReadRegistryDword(kMinClusterCellsValueName, value) ? static_cast<int>(value) : 20;
}

void SaveMinClusterCells(int cells) {
    WriteRegistryDword(kMinClusterCellsValueName, static_cast<DWORD>(cells));
}

double LoadMaxMatchDistCells() {
    DWORD value = 0;
    return ReadRegistryDword(kMaxMatchDistCellsValueName, value) ? static_cast<double>(value) : 60.0;
}

void SaveMaxMatchDistCells(double cells) {
    WriteRegistryDword(kMaxMatchDistCellsValueName, static_cast<DWORD>(cells + 0.5));
}

int LoadMaxMissedFrames() {
    DWORD value = 0;
    return ReadRegistryDword(kMaxMissedFramesValueName, value) ? static_cast<int>(value) : 20;
}

void SaveMaxMissedFrames(int frames) {
    WriteRegistryDword(kMaxMissedFramesValueName, static_cast<DWORD>(frames));
}

int LoadMinConfirmFrames() {
    DWORD value = 0;
    return ReadRegistryDword(kMinConfirmFramesValueName, value) ? static_cast<int>(value) : 8;
}

void SaveMinConfirmFrames(int frames) {
    WriteRegistryDword(kMinConfirmFramesValueName, static_cast<DWORD>(frames));
}

double LoadMaxStaticPersistenceForTracking() {
    DWORD value = 0;
    return ReadRegistryDword(kMaxStaticPersistenceForTrackingValueName, value) ? static_cast<double>(value) : 8.0;
}

void SaveMaxStaticPersistenceForTracking(double persistence) {
    WriteRegistryDword(kMaxStaticPersistenceForTrackingValueName, static_cast<DWORD>(persistence + 0.5));
}

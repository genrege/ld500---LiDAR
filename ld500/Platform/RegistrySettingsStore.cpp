#include "RegistrySettingsStore.h"
#include <windows.h>

namespace {
    const wchar_t* kRegistryPath = L"Software\\LD500";

    std::wstring WidenUtf8(const std::string& narrow) {
        if (narrow.empty()) return {};
        int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), static_cast<int>(narrow.size()), NULL, 0);
        std::wstring result(sizeNeeded, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), static_cast<int>(narrow.size()), result.data(), sizeNeeded);
        return result;
    }

    std::string NarrowUtf8(const std::wstring& wide) {
        if (wide.empty()) return {};
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), NULL, 0, NULL, NULL);
        std::string result(sizeNeeded, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), sizeNeeded, NULL, NULL);
        return result;
    }

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

    void WriteRegistryDword(const wchar_t* valueName, DWORD value) {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
            RegCloseKey(hKey);
        }
    }

    bool ReadRegistryString(const wchar_t* valueName, std::wstring& outValue) {
        HKEY hKey;
        bool found = false;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t buffer[256] = { 0 };
            DWORD size = sizeof(buffer);
            DWORD type = 0;
            if (RegQueryValueExW(hKey, valueName, NULL, &type, reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS
                && type == REG_SZ) {
                outValue = buffer;
                found = true;
            }
            RegCloseKey(hKey);
        }
        return found;
    }

    void WriteRegistryString(const wchar_t* valueName, const std::wstring& value) {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
    }
}

int RegistrySettingsStore::GetInt(const char* key, int defaultValue) {
    DWORD value = 0;
    return ReadRegistryDword(WidenUtf8(key).c_str(), value) ? static_cast<int>(value) : defaultValue;
}

void RegistrySettingsStore::SetInt(const char* key, int value) {
    WriteRegistryDword(WidenUtf8(key).c_str(), static_cast<DWORD>(value));
}

double RegistrySettingsStore::GetDouble(const char* key, double defaultValue) {
    DWORD value = 0;
    return ReadRegistryDword(WidenUtf8(key).c_str(), value) ? static_cast<double>(value) : defaultValue;
}

void RegistrySettingsStore::SetDouble(const char* key, double value) {
    WriteRegistryDword(WidenUtf8(key).c_str(), static_cast<DWORD>(value + 0.5)); // round to nearest whole unit
}

bool RegistrySettingsStore::GetBool(const char* key, bool defaultValue) {
    DWORD value = 0;
    return ReadRegistryDword(WidenUtf8(key).c_str(), value) ? (value != 0) : defaultValue;
}

void RegistrySettingsStore::SetBool(const char* key, bool value) {
    WriteRegistryDword(WidenUtf8(key).c_str(), value ? 1 : 0);
}

std::string RegistrySettingsStore::GetString(const char* key, const std::string& defaultValue) {
    std::wstring value;
    return ReadRegistryString(WidenUtf8(key).c_str(), value) ? NarrowUtf8(value) : defaultValue;
}

void RegistrySettingsStore::SetString(const char* key, const std::string& value) {
    WriteRegistryString(WidenUtf8(key).c_str(), WidenUtf8(value));
}

std::unique_ptr<ISettingsStore> CreatePlatformSettingsStore() {
    return std::make_unique<RegistrySettingsStore>();
}

#pragma once

// Portable settings-persistence abstraction used only by the ImGui shell (both Windows and
// macOS). The legacy Win32 shell keeps using Acquisition/AppSettings.h's registry-backed free
// functions directly, untouched. Key names match the legacy registry value names so the two
// shells share persisted settings on Windows.

#include <memory>
#include <string>

class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;

    virtual int GetInt(const char* key, int defaultValue) = 0;
    virtual void SetInt(const char* key, int value) = 0;

    virtual double GetDouble(const char* key, double defaultValue) = 0;
    virtual void SetDouble(const char* key, double value) = 0;

    virtual bool GetBool(const char* key, bool defaultValue) = 0;
    virtual void SetBool(const char* key, bool value) = 0;

    virtual std::string GetString(const char* key, const std::string& defaultValue) = 0;
    virtual void SetString(const char* key, const std::string& value) = 0;
};

// Constructs the ISettingsStore implementation for the current platform
// (RegistrySettingsStore on Windows, FileSettingsStore on macOS/Linux).
std::unique_ptr<ISettingsStore> CreatePlatformSettingsStore();

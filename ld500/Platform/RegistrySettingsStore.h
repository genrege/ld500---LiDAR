#pragma once

// Windows registry-backed implementation of ISettingsStore (HKCU\Software\LD500), independent
// of the legacy Acquisition/AppSettings.h free functions used by the Win32 shell (unmodified).

#include "ISettingsStore.h"

class RegistrySettingsStore : public ISettingsStore {
public:
    int GetInt(const char* key, int defaultValue) override;
    void SetInt(const char* key, int value) override;

    double GetDouble(const char* key, double defaultValue) override;
    void SetDouble(const char* key, double value) override;

    bool GetBool(const char* key, bool defaultValue) override;
    void SetBool(const char* key, bool value) override;

    std::string GetString(const char* key, const std::string& defaultValue) override;
    void SetString(const char* key, const std::string& value) override;
};

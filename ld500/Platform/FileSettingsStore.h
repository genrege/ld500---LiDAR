#pragma once

// JSON-file-backed implementation of ISettingsStore for macOS/Linux, stored at
// ~/Library/Application Support/LD500/settings.json (macOS) or ~/.config/ld500/settings.json
// (Linux fallback).

#include "ISettingsStore.h"
#include <nlohmann/json.hpp>

class FileSettingsStore : public ISettingsStore {
public:
    FileSettingsStore();

    int GetInt(const char* key, int defaultValue) override;
    void SetInt(const char* key, int value) override;

    double GetDouble(const char* key, double defaultValue) override;
    void SetDouble(const char* key, double value) override;

    bool GetBool(const char* key, bool defaultValue) override;
    void SetBool(const char* key, bool value) override;

    std::string GetString(const char* key, const std::string& defaultValue) override;
    void SetString(const char* key, const std::string& value) override;

private:
    std::string m_FilePath;

    void Save();
};

#include "FileSettingsStore.h"
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

namespace {
    // Builds and ensures the existence of ~/Library/Application Support/LD500 (macOS) or
    // ~/.config/ld500 (Linux fallback, in case this is ever built there too).
    std::string ResolveSettingsFilePath() {
        const char* home = std::getenv("HOME");
        std::string homeDir = home ? home : "";

#if defined(__APPLE__)
        std::string dir = homeDir + "/Library/Application Support/LD500";
#else
        std::string dir = homeDir + "/.config/ld500";
#endif
        mkdir(homeDir.c_str(), 0755); // no-op if it already exists; ignored if home itself doesn't need creating
#if defined(__APPLE__)
        mkdir((homeDir + "/Library/Application Support").c_str(), 0755);
#else
        mkdir((homeDir + "/.config").c_str(), 0755);
#endif
        mkdir(dir.c_str(), 0755);

        return dir + "/settings.json";
    }
}

FileSettingsStore::FileSettingsStore() {
    m_FilePath = ResolveSettingsFilePath();
}

int FileSettingsStore::GetInt(const char* key, int defaultValue) {
    std::ifstream in(m_FilePath);
    if (!in) return defaultValue;
    nlohmann::json data;
    in >> data;
    return data.value(key, defaultValue);
}

void FileSettingsStore::SetInt(const char* key, int value) {
    std::ifstream in(m_FilePath);
    nlohmann::json data;
    if (in) in >> data;
    data[key] = value;
    std::ofstream(m_FilePath) << data.dump(2);
}

double FileSettingsStore::GetDouble(const char* key, double defaultValue) {
    std::ifstream in(m_FilePath);
    if (!in) return defaultValue;
    nlohmann::json data;
    in >> data;
    return data.value(key, defaultValue);
}

void FileSettingsStore::SetDouble(const char* key, double value) {
    std::ifstream in(m_FilePath);
    nlohmann::json data;
    if (in) in >> data;
    data[key] = value;
    std::ofstream(m_FilePath) << data.dump(2);
}

bool FileSettingsStore::GetBool(const char* key, bool defaultValue) {
    std::ifstream in(m_FilePath);
    if (!in) return defaultValue;
    nlohmann::json data;
    in >> data;
    return data.value(key, defaultValue);
}

void FileSettingsStore::SetBool(const char* key, bool value) {
    std::ifstream in(m_FilePath);
    nlohmann::json data;
    if (in) in >> data;
    data[key] = value;
    std::ofstream(m_FilePath) << data.dump(2);
}

std::string FileSettingsStore::GetString(const char* key, const std::string& defaultValue) {
    std::ifstream in(m_FilePath);
    if (!in) return defaultValue;
    nlohmann::json data;
    in >> data;
    return data.value(key, defaultValue);
}

void FileSettingsStore::SetString(const char* key, const std::string& value) {
    std::ifstream in(m_FilePath);
    nlohmann::json data;
    if (in) in >> data;
    data[key] = value;
    std::ofstream(m_FilePath) << data.dump(2);
}

std::unique_ptr<ISettingsStore> CreatePlatformSettingsStore() {
    return std::make_unique<FileSettingsStore>();
}

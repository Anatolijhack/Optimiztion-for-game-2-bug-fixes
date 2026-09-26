#pragma once
#include <Windows.h>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>

class ConfigStore
{
private:
    std::wstring configPath;
    std::unordered_map<std::wstring, std::wstring> values;

    static std::wstring getExeDirectory()
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring fullPath(path);
        size_t pos = fullPath.find_last_of(L"\\/");
        return (pos != std::wstring::npos) ? fullPath.substr(0, pos + 1) : L"";
    }

public:
    ConfigStore()
    {
        configPath = getExeDirectory() + L"config.ini";
        load();
    }

    void load()
    {
        std::wifstream f(configPath);
        if (!f.is_open()) return;

        std::wstring line;
        while (std::getline(f, line))
        {
            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring key = line.substr(0, eq);
            std::wstring val = line.substr(eq + 1);
            values[key] = val;
        }
    }

    void save()
    {
        std::wofstream f(configPath, std::ios::trunc);
        for (auto& [key, val] : values)
            f << key << L"=" << val << L"\n";
    }

    void setDouble(const std::wstring& key, double value)
    {
        values[key] = std::to_wstring(value);
    }

    double getDouble(const std::wstring& key, double defaultValue) const
    {
        auto it = values.find(key);
        if (it == values.end()) return defaultValue;
        try { return std::stod(it->second); }
        catch (...) { return defaultValue; }
    }

    void setInt(const std::wstring& key, int value)
    {
        values[key] = std::to_wstring(value);
    }

    int getInt(const std::wstring& key, int defaultValue) const
    {
        auto it = values.find(key);
        if (it == values.end()) return defaultValue;
        try { return std::stoi(it->second); }
        catch (...) { return defaultValue; }
    }

    bool has(const std::wstring& key) const
    {
        return values.find(key) != values.end();
    }
};
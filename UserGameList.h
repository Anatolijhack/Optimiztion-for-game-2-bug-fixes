#pragma once
#include <Windows.h>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

class UserGameList
{
private:
    static std::wstring getExeDirectory()
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring fullPath(path);
        size_t pos = fullPath.find_last_of(L"\\/");
        return (pos != std::wstring::npos) ? fullPath.substr(0, pos + 1) : L"";
    }

    static std::wstring getPath()
    {
        return getExeDirectory() + L"user_games.txt";
    }

public:
    // Читает список имён процессов, добавленных пользователем.
    // Формат файла — одно имя exe на строку, например:
    //   stalker2.exe
    //   valorant.exe
    static std::vector<std::wstring> load()
    {
        std::vector<std::wstring> result;
        std::wifstream f(getPath());
        if (!f.is_open()) return result;

        std::wstring line;
        while (std::getline(f, line))
        {
            // trim пробелов
            line.erase(0, line.find_first_not_of(L" \t\r\n"));
            line.erase(line.find_last_not_of(L" \t\r\n") + 1);
            if (line.empty() || line[0] == L'#') continue; // пустые строки и комментарии пропускаем
            result.push_back(line);
        }
        return result;
    }

    // Добавляет новую игру в файл, если её там ещё нет
    static bool add(const std::wstring& processName)
    {
        auto existing = load();
        for (auto& name : existing)
        {
            if (_wcsicmp(name.c_str(), processName.c_str()) == 0)
                return false; // уже есть
        }

        std::wofstream f(getPath(), std::ios::app);
        f << processName << L"\n";
        return true;
    }
};
#pragma once
#include <Windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <sstream>
#include <vector>
#include <fstream>
#include "FrameTimeMonitor.h"

class PresentMonCapture
{
private:
private:
    std::wstring getExeDirectory()
    {
        std::vector<wchar_t> buffer(32768);

        DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size())
        );

        if (length == 0)
        {
            DWORD error = GetLastError();

            logDebug(
                "[getExeDirectory] GetModuleFileNameW failed, error=" +
                std::to_string(error)
            );

            return L"";
        }

        if (length >= buffer.size())
        {
            logDebug(
                "[getExeDirectory] Path is too long / truncated"
            );

            return L"";
        }

        std::wstring fullPath(buffer.data(), length);

        logDebugW(
            L"[getExeDirectory] Full EXE path: [" +
            fullPath +
            L"]"
        );

        size_t pos = fullPath.find_last_of(L"\\/");

        if (pos == std::wstring::npos)
        {
            logDebugW(
                L"[getExeDirectory] Cannot find directory separator"
            );

            return L"";
        }

        std::wstring directory = fullPath.substr(0, pos + 1);

        logDebugW(
            L"[getExeDirectory] EXE directory: [" +
            directory +
            L"]"
        );

        return directory;
    }
    static void logDebugW(const std::wstring& msg)
    {
        std::wofstream f("presentmon_debug.log", std::ios::app);
        f << msg << std::endl;
    }
    HANDLE hProcess = nullptr;
    HANDLE hThread = nullptr;
    HANDLE hReadPipe = nullptr;
    HANDLE hWritePipe = nullptr;
    std::thread readerThread;
    std::atomic<bool> running{ false };
    FrameTimeMonitor* monitor = nullptr;

    int msBetweenPresentsColumnIndex = -1;

    // Диагностика — пишем в файл, чтобы понять, что реально происходит
    static void logDebug(const std::string& msg)
    {
        std::ofstream f("presentmon_debug.log", std::ios::app);
        f << msg << std::endl;
    }

    void readerLoop()
    {
        char buffer[4096];
        std::string leftover;
        bool headerParsed = false;
        int linesReceived = 0;

        logDebug("[readerLoop] started");

        DWORD bytesRead = 0;
        while (running.load())
        {
            BOOL ok = ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
            if (!ok || bytesRead == 0)
            {
                logDebug("[readerLoop] ReadFile returned no data, exiting loop. ok=" +
                    std::to_string(ok) + " bytesRead=" + std::to_string(bytesRead));

                // Проверяем, жив ли ещё сам процесс PresentMon и с каким кодом он завершился
                if (hProcess)
                {
                    DWORD exitCode = 0;
                    if (GetExitCodeProcess(hProcess, &exitCode))
                        logDebug("[readerLoop] PresentMon exit code: " + std::to_string(exitCode));
                }
                break;
            }

            buffer[bytesRead] = '\0';
            leftover += buffer;

            size_t pos;
            while ((pos = leftover.find('\n')) != std::string::npos)
            {
                std::string line = leftover.substr(0, pos);
                leftover.erase(0, pos + 1);

                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                if (line.empty()) continue;

                linesReceived++;

                if (!headerParsed)
                {
                    // Ждём именно настоящий CSV header.
                    // PresentMon может сначала вывести warning/info в stdout.
                    if (line.find("MsBetweenPresents") == std::string::npos)
                    {
                        if (linesReceived <= 10)
                        {
                            logDebug(
                                "[readerLoop] skipping non-CSV line: " +
                                line
                            );
                        }

                        continue;
                    }

                    logDebug(
                        "[readerLoop] CSV header found: " +
                        line
                    );

                    std::stringstream ss(line);
                    std::string col;
                    int idx = 0;

                    while (std::getline(ss, col, ','))
                    {
                        // Убираем возможные пробелы
                        while (!col.empty() &&
                            (col.front() == ' ' || col.front() == '\t'))
                        {
                            col.erase(col.begin());
                        }

                        while (!col.empty() &&
                            (col.back() == ' ' || col.back() == '\t'))
                        {
                            col.pop_back();
                        }

                        if (col == "MsBetweenPresents")
                        {
                            msBetweenPresentsColumnIndex = idx;
                            break;
                        }

                        ++idx;
                    }

                    logDebug(
                        "[readerLoop] MsBetweenPresents column index: " +
                        std::to_string(msBetweenPresentsColumnIndex)
                    );

                    headerParsed = true;
                    continue;
                }

                if (msBetweenPresentsColumnIndex < 0)
                {
                    if (linesReceived <= 5) // не спамить бесконечно
                        logDebug("[readerLoop] column not found, skipping line: " + line);
                    continue;
                }

                std::stringstream ss(line);
                std::string field;
                int idx = 0;
                while (std::getline(ss, field, ','))
                {
                    if (idx == msBetweenPresentsColumnIndex)
                    {
                        try
                        {
                            double frameTimeMs = std::stod(field);
                            if (monitor) monitor->onFrame(frameTimeMs);

                            if (linesReceived <= 5)
                                logDebug("[readerLoop] parsed frameTimeMs = " + std::to_string(frameTimeMs));
                        }
                        catch (...)
                        {
                            logDebug("[readerLoop] failed to parse field: " + field);
                        }
                        break;
                    }
                    idx++;
                }
            }
        }

        logDebug("[readerLoop] exited, total lines received: " + std::to_string(linesReceived));
    }

public:
    bool start(DWORD pid, FrameTimeMonitor& targetMonitor)
    {
        if (running.load()) stop();

        monitor = &targetMonitor;
        headerColumnReset();

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
        {
            logDebug("[start] CreatePipe failed, error=" + std::to_string(GetLastError()));
            return false;
        }

        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;

        // Строим полный абсолютный путь к PresentMon рядом с нашим exe
        std::wstring exeDir = getExeDirectory();

     /*   std::wstring presentMonPath =
            L"D:\\Мои проект\\REsurs Manager without Chat  Gpt\\x64\\Release\\PresentMon-2.5.1-x64.exe";*/
        std::wstring presentMonPath =
            exeDir + L"PresentMon-2.5.1-x64.exe";

        logDebugW(
            L"[start] Looking for PresentMon at: " +
            presentMonPath
        );

        // Проверяем, существует ли PresentMon.exe
        DWORD attrs = GetFileAttributesW(
            presentMonPath.c_str()
        );

        if (attrs == INVALID_FILE_ATTRIBUTES)
        {
            DWORD err = GetLastError();

            logDebugW(
                L"[start] PresentMon NOT FOUND: " +
                presentMonPath
            );

            logDebug(
                "[start] GetFileAttributesW error=" +
                std::to_string(err)
            );

            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);

            hReadPipe = nullptr;
            hWritePipe = nullptr;

            return false;
        }

        logDebugW(
            L"[start] PresentMon FOUND: " +
            presentMonPath
        );

        std::wstring cmd =
            L"\"" + presentMonPath + L"\"" +
            L" --process_id " +
            std::to_wstring(pid) +
            L" --output_stdout" +
            L" --stop_existing_session" +
            L" --terminate_on_proc_exit";

        std::vector<wchar_t> buffer(cmd.begin(), cmd.end());
        buffer.push_back(L'\0');

        PROCESS_INFORMATION pi{};
        BOOL result = CreateProcessW(
            presentMonPath.c_str(), // <-- явно указываем lpApplicationName тоже, для надёжности
            buffer.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

        CloseHandle(hWritePipe);
        hWritePipe = nullptr;

        if (!result)
        {
            logDebug("[start] CreateProcessW FAILED, error=" + std::to_string(GetLastError()));
            CloseHandle(hReadPipe);
            hReadPipe = nullptr;
            return false;
        }

        logDebug("[start] CreateProcessW succeeded, PID=" + std::to_string(pid));

        hProcess = pi.hProcess;
        hThread = pi.hThread;

        running = true;
        readerThread = std::thread(&PresentMonCapture::readerLoop, this);

        return true;
    }

    void stop()
    {
        running = false;

        if (hProcess)
        {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
            hProcess = nullptr;
        }
        if (hThread) { CloseHandle(hThread); hThread = nullptr; }
        if (hReadPipe) { CloseHandle(hReadPipe); hReadPipe = nullptr; }

        if (readerThread.joinable())
            readerThread.join();
    }

    void headerColumnReset()
    {
        msBetweenPresentsColumnIndex = -1;
    }

    ~PresentMonCapture()
    {
        stop();
    }
};
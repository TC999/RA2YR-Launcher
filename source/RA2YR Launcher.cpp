// Launcher for Red Alert 2: Yuri's Revenge
// Based on reverse-engineered original launcher (removed packer)
// Author: [Your Name]
// Compile: cl /EHsc /O2 launcher.cpp user32.lib kernel32.lib shell32.lib

#include <windows.h>
#include <shlwapi.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <ctime>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

// -------------------------------------------------------------------
// Configuration keys (as observed in original launcher)
// -------------------------------------------------------------------
const char CONFIG_KEY_RUN[] = "RUN";          // normal command line
const char CONFIG_KEY_RUN2[] = "RUN2";         // alternative command line (if expired)
const char CONFIG_KEY_FLAG[] = "FLAG";         // presence indicates time check is enabled
const char CONFIG_KEY_TIMESTAMP[] = "TIMESTAMP";    // expiration timestamp (seconds since 1970)

// -------------------------------------------------------------------
// Global variables (similar to original)
// -------------------------------------------------------------------
HINSTANCE   g_hInst = nullptr;
LPSTR       g_lpCmdLine = nullptr;
int         g_nCmdShow = 0;
std::string g_exePath;                 // full path of this launcher
std::string g_exeDir;                  // directory containing launcher
std::string g_configFilePath;          // .lcf file path

// -------------------------------------------------------------------
// Helper: get current executable directory and set it as working dir
// -------------------------------------------------------------------
bool SetWorkingDirectoryToExePath()
{
    char buffer[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, buffer, MAX_PATH))
        return false;
    g_exePath = buffer;
    char* lastSlash = strrchr(buffer, '\\');
    if (lastSlash)
        *lastSlash = '\0';
    g_exeDir = buffer;
    SetCurrentDirectoryA(g_exeDir.c_str());
    return true;
}

// -------------------------------------------------------------------
// Simple .lcf parser (key=value, lines starting with '#' are comments)
// -------------------------------------------------------------------
bool ParseLcfFile(const std::string& path, std::unordered_map<std::string, std::string>& kv)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    std::string line;
    while (std::getline(file, line))
    {
        // trim leading/trailing spaces
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // trim key
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        // trim value
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);

        std::transform(key.begin(), key.end(), key.begin(), ::toupper);
        kv[key] = val;
    }
    return true;
}

// -------------------------------------------------------------------
// Protection: single instance via named mutex
// -------------------------------------------------------------------
void InitProtection()
{
    CreateMutexA(nullptr, FALSE, "48BC11BD-C4D7-466b-8A31-C6ABBAD47B3E");
    // original launcher creates mutex but ignores ERROR_ALREADY_EXISTS;
    // game will close it later.
}

// -------------------------------------------------------------------
// Create event for game notification (original used for sync)
// -------------------------------------------------------------------
HANDLE CreateNotifyEvent()
{
    return CreateEventA(nullptr, FALSE, FALSE, "D6E7FC97-64F9-4d28-B52C-754EDF721C6F");
}

// -------------------------------------------------------------------
// Build final command line for game process
// -------------------------------------------------------------------
std::string BuildGameCommandLine(const std::unordered_map<std::string, std::string>& config,
    const std::string& extraArgs, bool grabPatchesMode)
{
    std::string cmdLine;

    if (grabPatchesMode)
    {
        // In grab patches mode, original launcher enumerates and applies patches
        // then exits without starting the game. We'll simply print a message.
        MessageBoxA(nullptr, "Patch grabbing mode is not implemented in this launcher.\n"
            "Original function required PATCHGET.DAT and custom patch format.",
            "Notice", MB_OK | MB_ICONINFORMATION);
        return "";
    }

    // Check time expiration if FLAG exists
    bool useRun2 = false;
    auto itFlag = config.find(CONFIG_KEY_FLAG);
    if (itFlag != config.end())
    {
        auto itTime = config.find(CONFIG_KEY_TIMESTAMP);
        if (itTime != config.end())
        {
            time_t now = time(nullptr);
            time_t expire = static_cast<time_t>(std::stoll(itTime->second));
            if (now > expire)
                useRun2 = true;
        }
    }

    // Determine which command to use
    std::string gameCmd;
    if (useRun2)
    {
        auto it = config.find(CONFIG_KEY_RUN2);
        if (it != config.end())
            gameCmd = it->second;
        else
            gameCmd = config.at(CONFIG_KEY_RUN); // fallback to RUN
    }
    else
    {
        auto it = config.find(CONFIG_KEY_RUN);
        if (it != config.end())
            gameCmd = it->second;
        else
            return ""; // no command
    }

    // Append extra command line arguments from launcher invocation
    if (!extraArgs.empty())
        cmdLine = gameCmd + " " + extraArgs;
    else
        cmdLine = gameCmd;

    return cmdLine;
}

// -------------------------------------------------------------------
// Launch the game process
// -------------------------------------------------------------------
bool LaunchGame(const std::string& cmdLine)
{
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { nullptr };

    // Create mutable copy for CreateProcessA
    std::vector<char> cmdBuffer(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back('\0');

    BOOL success = CreateProcessA(
        nullptr,                 // lpApplicationName (use command line)
        cmdBuffer.data(),
        nullptr, nullptr,
        TRUE,                    // bInheritHandles
        0,                       // dwCreationFlags
        nullptr,                 // lpEnvironment
        nullptr,                 // lpCurrentDirectory (NULL = use our working dir)
        &si, &pi
    );

    if (!success)
    {
        char errorMsg[512];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(),
            0, errorMsg, sizeof(errorMsg), nullptr);
        MessageBoxA(nullptr, errorMsg, "Failed to launch game", MB_OK | MB_ICONERROR);
        return false;
    }

    // Wait for game process to exit (original launcher uses event, but we simplify)
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

// -------------------------------------------------------------------
// Main entry point
// -------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    g_hInst = hInstance;
    g_lpCmdLine = lpCmdLine;
    g_nCmdShow = nShowCmd;

    // Step 1: set working directory to exe location
    if (!SetWorkingDirectoryToExePath())
    {
        MessageBoxA(nullptr, "Failed to get executable path.", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    // Step 2: build config file path (exe name without extension + ".lcf")
    g_configFilePath = g_exePath;
    PathRemoveExtensionA(g_configFilePath.data());
    g_configFilePath += ".lcf";

    // Step 3: parse .lcf file
    std::unordered_map<std::string, std::string> config;
    if (!ParseLcfFile(g_configFilePath, config))
    {
        MessageBoxA(nullptr, "You must run the game from its install directory.\n"
            "Missing or invalid .lcf file.", "Launcher config missing", MB_OK | MB_ICONERROR);
        return -1;
    }

    // Step 4: check for --grabpatches mode
    bool grabPatchesMode = false;
    if (lpCmdLine)
    {
        std::string args(lpCmdLine);
        std::transform(args.begin(), args.end(), args.begin(), ::tolower);
        if (args.find("--grabpatches") != std::string::npos)
            grabPatchesMode = true;
    }

    // Step 5: build final command line (including extra args from lpCmdLine)
    std::string extraArgs = lpCmdLine ? lpCmdLine : "";
    // Remove --grabpatches from extraArgs if present (it's not for game)
    if (grabPatchesMode)
    {
        size_t pos = extraArgs.find("--grabpatches");
        if (pos != std::string::npos)
        {
            extraArgs.erase(pos, 13); // length of "--grabpatches"
            // clean up surrounding spaces
            size_t start = extraArgs.find_first_not_of(" \t");
            if (start == std::string::npos) extraArgs.clear();
            else extraArgs = extraArgs.substr(start);
        }
    }

    std::string gameCmd = BuildGameCommandLine(config, extraArgs, grabPatchesMode);
    if (gameCmd.empty())
    {
        if (!grabPatchesMode)
            MessageBoxA(nullptr, "No RUN command found in .lcf file.", "Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    // Step 6: protection (mutex) and event (original launcher uses event sync)
    InitProtection();
    HANDLE hNotify = CreateNotifyEvent(); // not used in this simplified version

    // Step 7: launch game
    if (!LaunchGame(gameCmd))
        return -1;

    // Step 8: wait for game signal (original waited on event, we already waited on process)
    // Close event handle
    if (hNotify)
        CloseHandle(hNotify);

    return 0;
}
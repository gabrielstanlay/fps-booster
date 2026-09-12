#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "optimizer.h"

#include <windows.h>
#include <appmodel.h>

#include <atomic>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace
{
    // Um pacote do Windows: o nome e usado no PowerShell, a familia na checagem.
    struct AppxTarget
    {
        const wchar_t* name;
        const wchar_t* family;
    };

    const AppxTarget kCortanaTargets[] =
    {
        { L"Microsoft.549981C3F5F10", L"Microsoft.549981C3F5F10_8wekyb3d8bbwe" },
    };

    const AppxTarget kCopilotTargets[] =
    {
        { L"Microsoft.Copilot",                     L"Microsoft.Copilot_8wekyb3d8bbwe" },
        { L"Microsoft.Windows.Ai.Copilot.Provider", L"Microsoft.Windows.Ai.Copilot.Provider_cw5n1h2txyewy" },
    };

    std::thread       g_worker;
    std::atomic<bool> g_busy{ false };
    std::atomic<OptimizationState> g_cortana{ OptimizationState::missing };
    std::atomic<OptimizationState> g_copilot{ OptimizationState::missing };
    std::atomic<bool> g_checked{ false };

    // O pacote esta instalado para o usuario atual?
    bool packageInstalled(const wchar_t* family)
    {
        UINT32 count  = 0;
        UINT32 length = 0;
        const LONG result = ::FindPackagesByPackageFamily(family, PACKAGE_FILTER_HEAD | PACKAGE_FILTER_DIRECT,
                                                          &count, nullptr, &length, nullptr, nullptr);
        return (result == ERROR_SUCCESS || result == ERROR_INSUFFICIENT_BUFFER) && count > 0;
    }

    bool anyInstalled(const AppxTarget* targets, int count)
    {
        for (int i = 0; i < count; ++i)
            if (packageInstalled(targets[i].family))
                return true;
        return false;
    }

    // Refaz a checagem de quais programas ainda estao instalados.
    void refreshStates()
    {
        if (g_cortana.load() != OptimizationState::running)
            g_cortana.store(anyInstalled(kCortanaTargets, static_cast<int>(std::size(kCortanaTargets)))
                            ? OptimizationState::available : OptimizationState::missing);

        if (g_copilot.load() != OptimizationState::running)
            g_copilot.store(anyInstalled(kCopilotTargets, static_cast<int>(std::size(kCopilotTargets)))
                            ? OptimizationState::available : OptimizationState::missing);

        g_checked.store(true);
    }

    // Roda um script do PowerShell escondido e espera ele terminar.
    bool runPowerShell(const std::wstring& script)
    {
        std::wstring command = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"";
        command += script;
        command += L"\"";

        std::vector<wchar_t> buffer(command.begin(), command.end());
        buffer.push_back(L'\0');

        STARTUPINFOW startup = {};
        startup.cb          = sizeof(startup);
        startup.dwFlags     = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process = {};

        if (!::CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                              nullptr, nullptr, &startup, &process))
            return false;

        const DWORD wait = ::WaitForSingleObject(process.hProcess, 180000);
        if (wait == WAIT_TIMEOUT)
            ::TerminateProcess(process.hProcess, 1);

        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
        return wait == WAIT_OBJECT_0;
    }

    // Desinstala os pacotes para todos os usuarios e tira eles da imagem do
    // Windows, para nao voltarem em contas novas.
    bool removeTargets(const AppxTarget* targets, int count)
    {
        std::wstring script;
        for (int i = 0; i < count; ++i)
        {
            const std::wstring name = targets[i].name;
            script += L"Get-AppxPackage -AllUsers -Name '" + name + L"' | Remove-AppxPackage -AllUsers -ErrorAction SilentlyContinue; ";
            script += L"Get-AppxProvisionedPackage -Online | Where-Object { $_.DisplayName -eq '" + name + L"' } | Remove-AppxProvisionedPackage -Online -ErrorAction SilentlyContinue; ";
        }

        runPowerShell(script);

        // o que vale e o resultado: o pacote sumiu ou nao
        return !anyInstalled(targets, count);
    }

    bool setPolicy(HKEY root, const wchar_t* path, const wchar_t* value, DWORD data)
    {
        HKEY key = nullptr;
        if (::RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY,
                              nullptr, &key, nullptr) != ERROR_SUCCESS)
            return false;

        const LSTATUS status = ::RegSetValueExW(key, value, 0, REG_DWORD,
                                                reinterpret_cast<const BYTE*>(&data), sizeof(data));
        ::RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }

    // Politicas que impedem a Cortana de voltar.
    void blockCortana()
    {
        setPolicy(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search", L"AllowCortana", 0);
    }

    // Politicas que desligam o Copilot e tiram o botao da barra de tarefas.
    void blockCopilot()
    {
        setPolicy(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", 1);
        setPolicy(HKEY_CURRENT_USER,  L"Software\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", 1);
        setPolicy(HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"ShowCopilotButton", 0);
    }

    std::atomic<OptimizationState>& stateOf(Optimization item)
    {
        return (item == Optimization::removeCortana) ? g_cortana : g_copilot;
    }
}

OptimizationState optimizationState(Optimization item)
{
    if (!g_checked.load())
        refreshStates();

    return stateOf(item).load();
}

void startOptimization(Optimization item)
{
    if (g_busy.load() || optimizationState(item) != OptimizationState::available)
        return;

    if (g_worker.joinable())
        g_worker.join();

    g_busy.store(true);
    stateOf(item).store(OptimizationState::running);

    g_worker = std::thread([item]()
    {
        bool removed = false;

        if (item == Optimization::removeCortana)
        {
            removed = removeTargets(kCortanaTargets, static_cast<int>(std::size(kCortanaTargets)));
            blockCortana();
        }
        else
        {
            removed = removeTargets(kCopilotTargets, static_cast<int>(std::size(kCopilotTargets)));
            blockCopilot();
        }

        stateOf(item).store(removed ? OptimizationState::done : OptimizationState::failed);
        g_busy.store(false);
    });
}

bool isOptimizing()
{
    return g_busy.load();
}

void shutdownOptimizer()
{
    if (g_worker.joinable())
        g_worker.join();
}

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
    std::thread       g_worker;
    std::atomic<bool> g_busy{ false };

    // Roda um script do PowerShell sem janela e espera terminar.
    // Devolve o codigo de saida do processo, ou -1 se nem abriu / travou.
    int runPowerShell(const std::wstring& script)
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
            return -1;

        const DWORD wait = ::WaitForSingleObject(process.hProcess, 180000);
        DWORD exitCode = static_cast<DWORD>(-1);
        if (wait == WAIT_OBJECT_0)
            ::GetExitCodeProcess(process.hProcess, &exitCode);
        else
            ::TerminateProcess(process.hProcess, 1);

        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
        return static_cast<int>(exitCode);
    }

    // -----------------------------------------------------------------------
    //  Registro
    // -----------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    //  Remocao de pacotes appx
    // -----------------------------------------------------------------------
    enum class RemoveState { available, missing, running, done, failed };

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

    const AppxTarget kGameBarTargets[] =
    {
        { L"Microsoft.XboxGamingOverlay", L"Microsoft.XboxGamingOverlay_8wekyb3d8bbwe" },
        { L"Microsoft.XboxGameOverlay",   L"Microsoft.XboxGameOverlay_8wekyb3d8bbwe" },
    };

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
        return !anyInstalled(targets, count);
    }

    // -----------------------------------------------------------------------
    //  Permanencia por registro (impede a volta do que foi removido)
    // -----------------------------------------------------------------------
    void blockCortana()
    {
        setPolicy(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Search", L"AllowCortana", 0);
    }

    void blockCopilot()
    {
        setPolicy(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", 1);
        setPolicy(HKEY_CURRENT_USER,  L"Software\\Policies\\Microsoft\\Windows\\WindowsCopilot", L"TurnOffWindowsCopilot", 1);
        setPolicy(HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"ShowCopilotButton", 0);
    }

    // Desliga o Game DVR/gravacao em segundo plano, para a Game Bar nao voltar a
    // operar mesmo que algum componente do sistema seja reinstalado.
    void blockGameBar()
    {
        setPolicy(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR", L"AllowGameDVR", 0);
        setPolicy(HKEY_CURRENT_USER,  L"System\\GameConfigStore", L"GameDVR_Enabled", 0);
        setPolicy(HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled", 0);
    }

    // =======================================================================
    //  Tabela de tweaks
    // =======================================================================
    struct RemovalTweak
    {
        const char* title;
        const char* description;
        const char* missingMessage; // tooltip quando nao esta instalado
        const AppxTarget* targets;
        int targetCount;
        void (*block)(); // permanencia por registro
    };

    const RemovalTweak kRemovals[] =
    {
        { "Remover Cortana", "Remove a Cortana permanentemente do seu computador.",
          "A Cortana não foi encontrada no seu computador.",
          kCortanaTargets, static_cast<int>(std::size(kCortanaTargets)), blockCortana },

        { "Remover Copilot", "Remove o Copilot permanentemente do seu computador.",
          "O Copilot não foi encontrado no seu computador.",
          kCopilotTargets, static_cast<int>(std::size(kCopilotTargets)), blockCopilot },

        { "Remover Game Bar", "Remove a Game Bar do Xbox permanentemente do seu computador.",
          "A Game Bar não foi encontrada no seu computador.",
          kGameBarTargets, static_cast<int>(std::size(kGameBarTargets)), blockGameBar },
    };

    constexpr int kCount = static_cast<int>(std::size(kRemovals));

    std::atomic<RemoveState> g_state[kCount];
    std::atomic<bool>        g_checked{ false };

    // Detecta uma vez o que esta instalado (FindPackagesByPackageFamily por tweak).
    void detectStates()
    {
        for (int i = 0; i < kCount; ++i)
            if (g_state[i].load() != RemoveState::running)
                g_state[i].store(anyInstalled(kRemovals[i].targets, kRemovals[i].targetCount)
                                 ? RemoveState::available : RemoveState::missing);
        g_checked.store(true);
    }

    CardInfo cardFor(int index)
    {
        if (!g_checked.load())
            detectStates();

        CardInfo info;
        info.buttonLabel = "Remover";
        switch (g_state[index].load())
        {
        case RemoveState::available: info.tone = CardTone::good;    info.status = "Pronto pra remover";                 info.buttonEnabled = true;  break;
        case RemoveState::running:   info.tone = CardTone::busy;    info.status = "Removendo...";                       info.buttonEnabled = false; break;
        case RemoveState::done:      info.tone = CardTone::good;    info.status = "Removido. Reinicie o computador para concluir."; info.buttonEnabled = false; break;
        case RemoveState::failed:    info.tone = CardTone::bad;     info.status = "Não foi possível remover.";          info.buttonEnabled = true;  break;
        case RemoveState::missing:
        default:                     info.tone = CardTone::neutral; info.status = "Não foi encontrado no seu computador.";
                                     info.buttonEnabled = false;    info.tooltip = kRemovals[index].missingMessage;     break;
        }
        return info;
    }
}

int optimizationCount()
{
    return kCount;
}

OptimizationCard optimizationCard(int index)
{
    if (index < 0 || index >= kCount)
        return {};
    return { kRemovals[index].title, kRemovals[index].description };
}

CardInfo optimizationInfo(int index)
{
    if (index < 0 || index >= kCount)
        return {};
    return cardFor(index);
}

void startOptimization(int index)
{
    if (index < 0 || index >= kCount)
        return;
    if (g_busy.load() || !optimizationInfo(index).buttonEnabled)
        return;

    if (g_worker.joinable())
        g_worker.join();

    g_busy.store(true);
    g_state[index].store(RemoveState::running);

    g_worker = std::thread([index]()
    {
        const RemovalTweak& tweak = kRemovals[index];
        const bool removed = removeTargets(tweak.targets, tweak.targetCount);
        if (tweak.block != nullptr)
            tweak.block();
        g_state[index].store(removed ? RemoveState::done : RemoveState::failed);
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
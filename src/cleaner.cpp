#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "cleaner.h"

#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <objbase.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // Resultado de uma limpeza.
    struct CleanResult
    {
        int filesDeleted = 0; // arquivos/pastas apagados
        int filesLocked  = 0; // itens em uso que foram ignorados
        unsigned long long bytesFreed = 0; // espaco liberado
    };

    using FileFilter = bool (*)(const fs::path&);

    std::thread       g_worker;
    std::atomic<bool> g_busy{ false };
    std::mutex        g_statusMutex;
    std::string       g_status;

    // Caminho da %temp%
    fs::path userTempFolder()
    {
        wchar_t buffer[MAX_PATH + 1] = {};
        const DWORD length = ::GetTempPathW(MAX_PATH, buffer);
        return (length > 0) ? fs::path(buffer) : fs::path();
    }

    // Caminho da pasta do Windows (C:\Windows)
    fs::path windowsFolder()
    {
        wchar_t buffer[MAX_PATH + 1] = {};
        const UINT length = ::GetWindowsDirectoryW(buffer, MAX_PATH);
        return (length > 0) ? fs::path(buffer) : fs::path();
    }

    // Caminho de %localappdata%.
    fs::path localAppDataFolder()
    {
        wchar_t buffer[MAX_PATH + 1] = {};
        const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        return (length > 0 && length <= MAX_PATH) ? fs::path(buffer) : fs::path();
    }

    bool startsWith(const std::wstring& text, const wchar_t* prefix)
    {
        const size_t length = wcslen(prefix);
        return text.size() >= length && _wcsnicmp(text.c_str(), prefix, length) == 0;
    }

    // Filtro: arquivos de rastro do Prefetch.
    bool isPrefetchFile(const fs::path& item)
    {
        return _wcsicmp(item.extension().wstring().c_str(), L".pf") == 0;
    }

    // Filtro: bancos de miniaturas/icones do Explorador de Arquivos.
    bool isThumbnailCacheFile(const fs::path& item)
    {
        const std::wstring name = item.filename().wstring();
        return startsWith(name, L"thumbcache_") || startsWith(name, L"iconcache_");
    }

    // Tamanho de um arquivo, ou soma dos arquivos de uma pasta.
    unsigned long long itemSize(const fs::path& item)
    {
        std::error_code error;
        if (fs::is_regular_file(item, error))
        {
            const std::uintmax_t size = fs::file_size(item, error);
            return error ? 0ull : static_cast<unsigned long long>(size);
        }

        unsigned long long total = 0;
        try
        {
            for (const auto& entry : fs::recursive_directory_iterator(item, fs::directory_options::skip_permission_denied))
            {
                std::error_code entryError;
                if (!entry.is_regular_file(entryError) || entryError)
                    continue;

                const std::uintmax_t size = entry.file_size(entryError);
                if (!entryError)
                    total += static_cast<unsigned long long>(size);
            }
        }
        catch (const std::exception&)
        {
            // pasta protegida ou modificada durante a leitura: ignora
        }
        return total;
    }

    // Tira o atributo "somente leitura" para o arquivo poder ser apagado.
    void clearReadOnly(const fs::path& item)
    {
        std::error_code error;
        fs::permissions(item, fs::perms::owner_write, fs::perm_options::add, error);
    }

    // Apaga tudo que estiver dentro de 'folder' (a pasta em si e mantida).
    // Com 'filter', apaga somente os itens aceitos por ele.
    void clearFolder(const fs::path& folder, CleanResult& result, FileFilter filter = nullptr)
    {
        std::error_code error;
        if (folder.empty() || !fs::is_directory(folder, error))
            return;

        if (!folder.has_parent_path() || folder.parent_path() == folder)
            return;

        std::vector<fs::path> entries;
        try
        {
            for (const auto& entry : fs::directory_iterator(folder, fs::directory_options::skip_permission_denied))
                entries.push_back(entry.path());
        }
        catch (const std::exception&)
        {
            // sem permissao de leitura: nada a fazer
        }

        for (const fs::path& item : entries)
        {
            if (filter != nullptr && !filter(item))
                continue;

            const unsigned long long size = itemSize(item);
            clearReadOnly(item);

            std::error_code removeError;
            const std::uintmax_t removed = fs::remove_all(item, removeError);

            if (removeError || removed == static_cast<std::uintmax_t>(-1))
            {
                result.filesLocked++;
                continue;
            }

            result.filesDeleted += static_cast<int>(removed);
            result.bytesFreed   += size;
        }
    }

    void setStatus(const std::string& text)
    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        g_status = text;
    }

    // Fecha o Explorador de Arquivos, que mantem o cache de miniaturas aberto.
    // Devolve true se algum processo foi fechado.
    bool stopExplorer()
    {
        DWORD currentSession = 0;
        if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &currentSession))
            return false;

        HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        bool stopped = false;
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);

        if (::Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (_wcsicmp(entry.szExeFile, L"explorer.exe") != 0)
                    continue;

                DWORD session = 0;
                if (!::ProcessIdToSessionId(entry.th32ProcessID, &session) || session != currentSession)
                    continue;

                HANDLE process = ::OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                if (process == nullptr)
                    continue;

                if (::TerminateProcess(process, 0))
                    stopped = true;
                ::CloseHandle(process);
            }
            while (::Process32NextW(snapshot, &entry));
        }

        ::CloseHandle(snapshot);
        return stopped;
    }

    // O Windows costuma reabrir o Explorador sozinho; se nao reabrir, abre aqui.
    void startExplorer()
    {
        for (int attempt = 0; attempt < 25; ++attempt)
        {
            if (::GetShellWindow() != nullptr)
                return;
            ::Sleep(200);
        }

        wchar_t command[] = L"explorer.exe";
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};

        if (::CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
        {
            ::CloseHandle(process.hThread);
            ::CloseHandle(process.hProcess);
        }
    }

    // Prints temporarias da Ferramenta de Captura (Win + Shift + S).
    void clearSnippingToolTemp(const fs::path& localAppData, CleanResult& result)
    {
        const fs::path packages = localAppData / L"Packages";

        std::error_code error;
        if (!fs::is_directory(packages, error))
            return;

        try
        {
            for (const auto& entry : fs::directory_iterator(packages, fs::directory_options::skip_permission_denied))
            {
                std::error_code entryError;
                if (entry.is_directory(entryError) && startsWith(entry.path().filename().wstring(), L"Microsoft.ScreenSketch"))
                    clearFolder(entry.path() / L"TempState", result);
            }
        }
        catch (const std::exception&)
        {
            // pasta protegida: ignora
        }
    }

    std::string formatSize(unsigned long long bytes)
    {
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        double value = static_cast<double>(bytes);
        int unit = 0;

        while (value >= 1024.0 && unit < 4)
        {
            value /= 1024.0;
            unit++;
        }

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), (unit == 0) ? "%.0f %s" : "%.1f %s", value, units[unit]);
        return std::string(buffer);
    }

    // =======================================================================
    //  Operacoes de limpeza
    // =======================================================================

    // Apaga os arquivos de %temp% e de C:\Windows\Temp.
    void cleanTemp(CleanResult& result)
    {
        clearFolder(userTempFolder(), result);

        const fs::path windows = windowsFolder();
        if (!windows.empty())
            clearFolder(windows / L"Temp", result);
    }

    // Apaga os arquivos .pf de C:\Windows\Prefetch.
    void cleanPrefetch(CleanResult& result)
    {
        const fs::path windows = windowsFolder();
        if (windows.empty())
            return;

        clearFolder(windows / L"Prefetch", result, isPrefetchFile);
    }

    // Apaga o cache de miniaturas do Explorador e as prints temporarias da
    // Ferramenta de Captura. O explorer é fechado e reaberto para liberar os
    // arquivos, então a barra de tarefas pisca durante essa limpeza.
    void cleanScreenshotsCache(CleanResult& result)
    {
        const fs::path localAppData = localAppDataFolder();
        if (localAppData.empty())
            return;

        clearSnippingToolTemp(localAppData, result);

        const bool explorerStopped = stopExplorer();
        if (explorerStopped)
            ::Sleep(300);

        clearFolder(localAppData / L"Microsoft" / L"Windows" / L"Explorer", result, isThumbnailCacheFile);

        if (explorerStopped)
            startExplorer();
    }

    // Esvazia a Lixeira do Windows.
    void cleanRecycleBin(CleanResult& result)
    {
        const HRESULT com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        // Consulta antes de esvaziar, para saber quanto espaço foi liberado
        SHQUERYRBINFO info = {};
        info.cbSize = sizeof(info);
        const bool counted = SUCCEEDED(::SHQueryRecycleBinW(nullptr, &info));

        const HRESULT emptied = ::SHEmptyRecycleBinW(nullptr, nullptr,
                                                     SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
        if (SUCCEEDED(emptied) && counted)
        {
            result.filesDeleted += static_cast<int>(info.i64NumItems);
            result.bytesFreed   += static_cast<unsigned long long>(info.i64Size);
        }

        if (SUCCEEDED(com))
            ::CoUninitialize();
    }

    // =======================================================================
    //  Funções da aba de limpeza
    // =======================================================================
    struct CleanTask
    {
        const char* label;
        const char* description;
        const char* warning;
        bool defaultOn;
        void (*run)(CleanResult&);
    };

    const CleanTask kCleanTasks[] =
    {
        { "Limpar arquivos temporários", "Apaga todos arquivos de C:\\Windows\\Temp e %temp%.",
          nullptr, true, cleanTemp },

        { "Limpar prefetch", "Apaga todos arquivos de C:\\Windows\\Prefetch.",
          nullptr, true, cleanPrefetch },

        { "Limpar cache de screenshots",
          "Apaga as miniaturas que o Windows gera e usa no Explorador de Arquivos, "
          "junto com as prints temporarias da Ferramenta de Captura.",
          "Essa operação irá piscar sua barra de tarefas, não se assuste, é normal.",
          true, cleanScreenshotsCache },

        { "Esvaziar lixeira", "Apaga todos arquivos da lixeira.",
          "Não habilite essa opção a não ser que tenha certeza de que não há arquivos importantes na lixeira.",
          false, cleanRecycleBin },
    };

    constexpr int kCount = static_cast<int>(std::size(kCleanTasks));

    std::array<bool, kCount> makeDefaults()
    {
        std::array<bool, kCount> defaults{};
        for (int i = 0; i < kCount; ++i)
            defaults[i] = kCleanTasks[i].defaultOn;
        return defaults;
    }
    std::array<bool, kCount> g_selected = makeDefaults();
}

int cleanTaskCount()
{
    return kCount;
}

CleanTaskInfo cleanTaskInfo(int index)
{
    if (index < 0 || index >= kCount)
        return { "", "", nullptr };
    const CleanTask& task = kCleanTasks[index];
    return { task.label, task.description, task.warning };
}

bool cleanTaskEnabled(int index)
{
    return (index >= 0 && index < kCount) && g_selected[index];
}

void setCleanTaskEnabled(int index, bool enabled)
{
    if (index >= 0 && index < kCount)
        g_selected[index] = enabled;
}

void startCleaning()
{
    if (g_busy.load())
        return;

    if (g_worker.joinable())
        g_worker.join();

    g_busy.store(true);
    setStatus("");

    const std::array<bool, kCount> selected = g_selected;
    g_worker = std::thread([selected]()
    {
        CleanResult result;
        for (int i = 0; i < kCount; ++i)
            if (selected[i])
                kCleanTasks[i].run(result);

        std::string text = std::to_string(result.filesDeleted) + " itens apagados, " +
                           formatSize(result.bytesFreed) + " liberados.";
        if (result.filesLocked > 0)
            text += " (" + std::to_string(result.filesLocked) + " em uso)";

        setStatus(text);
        g_busy.store(false);
    });
}

bool isCleaning()
{
    return g_busy.load();
}

std::string cleanStatus()
{
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return g_status;
}

void shutdownCleaner()
{
    if (g_worker.joinable())
        g_worker.join();
}
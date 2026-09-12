#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "cleaner.h"

#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <objbase.h>

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
    // Filtro opcional de arquivos: devolve true para os que devem ser apagados.
    using FileFilter = bool (*)(const fs::path&);

    std::thread       g_worker;
    std::atomic<bool> g_busy{ false };
    std::mutex        g_statusMutex;
    std::string       g_status;

    // Caminho de %TEMP% (pasta temporaria do usuario).
    fs::path userTempFolder()
    {
        wchar_t buffer[MAX_PATH + 1] = {};
        const DWORD length = ::GetTempPathW(MAX_PATH, buffer);
        return (length > 0) ? fs::path(buffer) : fs::path();
    }

    // Caminho da pasta do Windows (normalmente C:\Windows).
    fs::path windowsFolder()
    {
        wchar_t buffer[MAX_PATH + 1] = {};
        const UINT length = ::GetWindowsDirectoryW(buffer, MAX_PATH);
        return (length > 0) ? fs::path(buffer) : fs::path();
    }

    // Caminho de %LOCALAPPDATA%.
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

        // seguranca: nunca trabalhar na raiz de um disco
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
                result.filesLocked++;                       // arquivo em uso pelo Windows
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

                // so o Explorador da sessao atual
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
}

void cleanTemp(CleanResult& result)
{
    clearFolder(userTempFolder(), result);                  // %TEMP%

    const fs::path windows = windowsFolder();
    if (!windows.empty())
        clearFolder(windows / L"Temp", result);             // C:\Windows\Temp (precisa de admin)
}

void cleanPrefetch(CleanResult& result)
{
    const fs::path windows = windowsFolder();
    if (windows.empty())
        return;

    clearFolder(windows / L"Prefetch", result, isPrefetchFile);   // precisa de admin
}

void cleanScreenshotsCache(CleanResult& result)
{
    const fs::path localAppData = localAppDataFolder();
    if (localAppData.empty())
        return;

    // prints temporarias da Ferramenta de Captura
    clearSnippingToolTemp(localAppData, result);

    // As miniaturas ficam abertas pelo Explorador de Arquivos, entao ele e
    // fechado antes da limpeza e volta logo em seguida (a barra de tarefas pisca).
    const bool explorerStopped = stopExplorer();
    if (explorerStopped)
        ::Sleep(300);

    clearFolder(localAppData / L"Microsoft" / L"Windows" / L"Explorer", result, isThumbnailCacheFile);

    if (explorerStopped)
        startExplorer();
}

void cleanRecycleBin(CleanResult& result)
{
    // as funcoes de shell precisam de COM iniciado na thread que chama
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // consulta antes de esvaziar, que e a unica forma de saber quanto foi liberado
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

void startCleaning(const CleanOptions& options)
{
    if (g_busy.load())
        return;

    if (g_worker.joinable())
        g_worker.join();

    g_busy.store(true);
    setStatus("");

    g_worker = std::thread([options]()
    {
        CleanResult result;
        if (options.temp)
            cleanTemp(result);
        if (options.prefetch)
            cleanPrefetch(result);
        if (options.screenshots)
            cleanScreenshotsCache(result);
        if (options.recycleBin)
            cleanRecycleBin(result);

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

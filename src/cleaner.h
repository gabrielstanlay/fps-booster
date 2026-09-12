#pragma once

#include <string>

// ---------------------------------------------------------------------------
//  Funcoes de limpeza do sistema.
// ---------------------------------------------------------------------------

// Resultado acumulado de uma limpeza.
struct CleanResult
{
    int                filesDeleted = 0;   // arquivos/pastas apagados
    int                filesLocked  = 0;   // itens em uso que foram ignorados
    unsigned long long bytesFreed   = 0;   // espaco liberado
};

// O que sera limpo quando o usuario clicar no botao.
struct CleanOptions
{
    bool temp        = false;
    bool prefetch    = false;
    bool screenshots = false;
    bool recycleBin  = false;
};

// Apaga o conteudo de %TEMP% e de C:\Windows\Temp (as pastas em si sao mantidas).
void cleanTemp(CleanResult& result);

// Apaga os arquivos .pf de C:\Windows\Prefetch.
void cleanPrefetch(CleanResult& result);

// Apaga o cache de miniaturas do Explorador de Arquivos (thumbcache/iconcache)
// e as prints temporarias da Ferramenta de Captura do Windows.
// O Explorador e fechado e reaberto para liberar os arquivos, entao a barra
// de tarefas pisca durante essa limpeza.
void cleanScreenshotsCache(CleanResult& result);

// Esvazia a Lixeira do Windows. O que sai dela nao volta.
void cleanRecycleBin(CleanResult& result);

// --- execucao em segundo plano, para nao travar o menu ---------------------
void        startCleaning(const CleanOptions& options);   // dispara a limpeza numa thread
bool        isCleaning();                                 // true enquanto a limpeza roda
std::string cleanStatus();                                // texto do ultimo resultado
void        shutdownCleaner();                            // espera a thread terminar (ao sair)

// --- utilidades ------------------------------------------------------------
std::string formatSize(unsigned long long bytes);         // 1536 -> "1.5 KB"

#pragma once

// ---------------------------------------------------------------------------
//  Funcoes de debloat do Windows.
// ---------------------------------------------------------------------------

enum class Optimization
{
    removeCortana,
    removeCopilot,
};

// Situacao de uma funcao, usada para montar o cartao dela no menu.
enum class OptimizationState
{
    available,   // o programa esta instalado e pode ser removido
    missing,     // nao existe nesta instalacao do Windows
    running,     // removendo agora
    done,        // removido nesta sessao
    failed,      // a remocao nao deu certo
};

OptimizationState optimizationState(Optimization item);

// --- execucao em segundo plano, para nao travar o menu ---------------------
void startOptimization(Optimization item);   // dispara a remocao numa thread
bool isOptimizing();                         // true enquanto alguma esta rodando
void shutdownOptimizer();                    // espera a thread terminar (ao sair)

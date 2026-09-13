#pragma once

#include <string>

// ---------------------------------------------------------------------------
//  Sistema de limpeza do Windows.
//
//  Cada tarefa de limpeza é descrita numa tabela dentro do cleaner.cpp. O menu
//  nao conhece as tarefas uma a uma: pergunta quantas existem e pede os dados de
//  cada uma. Para adicionar uma limpeza nova, basta uma linha nessa tabela.
// ---------------------------------------------------------------------------

// Dados de uma tarefa de limpeza, para o menu desenhar sem conhecer cada uma.
struct CleanTaskInfo
{
    const char* label;          // nome mostrado
    const char* description;    // explicacao curta
    const char* warning;        // aviso do triangulo vermelho, ou nullptr
};

int           cleanTaskCount();                       // quantas tarefas existem
CleanTaskInfo cleanTaskInfo(int index);               // dados de uma tarefa
bool          cleanTaskEnabled(int index);            // se esta marcada
void          setCleanTaskEnabled(int index, bool enabled);

void          startCleaning();       // limpa as tarefas marcadas, numa thread
bool          isCleaning();          // true enquanto a limpeza roda
std::string   cleanStatus();         // texto do ultimo resultado
void          shutdownCleaner();     // espera a thread terminar (ao sair)
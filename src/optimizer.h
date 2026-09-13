#pragma once

#include <string>

// ---------------------------------------------------------------------------
//  Sistema de otimizacoes: alterações persistentes no Windows (debloat).
//
//  Cada tweak e descrito numa tabela dentro do optimizer.cpp. O menu nao conhece
//  os tweaks um a um: pergunta quantos existem e pede os dados de cada um. Para
//  adicionar um debloat novo, basta uma linha nessa tabela.
// ---------------------------------------------------------------------------

// Cor do circulo e do texto de situação de um cartão.
enum class CardTone
{
    neutral,   // cinza
    good,      // verde
    busy,      // amarelo
    bad,       // vermelho
};

// Situação dinamica de um cartão (muda conforme a ação roda).
struct CardInfo
{
    CardTone    tone          = CardTone::neutral;
    std::string status;                             // texto de situação
    const char* buttonLabel   = "";                 // texto do botão
    bool        buttonEnabled = false;              // botão clicavel
    const char* tooltip       = nullptr;            // aviso quando o botão esta bloqueado
};

// Descrição estatica de um cartão (nome + explicação).
struct OptimizationCard
{
    const char* title       = "";
    const char* description = "";
};

int              optimizationCount();               // quantos tweaks existem
OptimizationCard optimizationCard(int index);       // nome e descrição (estaticos)
CardInfo         optimizationInfo(int index);       // situação (dinamica)

void startOptimization(int index);   // dispara a ação numa thread
bool isOptimizing();                 // true enquanto alguma ação roda
void shutdownOptimizer();            // espera a thread terminar (ao sair)
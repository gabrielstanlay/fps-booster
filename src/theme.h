#pragma once

#include "imgui.h"

// ---------------------------------------------------------------------------
//  Paleta de cores do menu.
//  Os valores sao definidos no topo do int main() (src/main.cpp).
// ---------------------------------------------------------------------------
struct Theme
{
    // janela
    ImVec4 background;              // fundo geral
    ImVec4 panel;                   // fundo dos paineis
    ImVec4 border;                  // linhas divisorias / bordas
    ImVec4 accent;                  // cor de destaque

    // texto
    ImVec4 text;                    // texto normal
    ImVec4 textDim;                 // texto secundario

    // botoes
    ImVec4 button;
    ImVec4 buttonHovered;
    ImVec4 buttonActive;

    // botao de liga/desliga (toggle)
    ImVec4 toggleOff;               // fundo quando desligado
    ImVec4 toggleHovered;           // fundo com o mouse em cima
    ImVec4 toggleOn;                // fundo quando ligado
    ImVec4 toggleKnob;              // bolinha que desliza

    // barra de rolagem
    ImVec4 scrollbarGrab;
    ImVec4 scrollbarGrabHovered;

    // tabs laterais
    ImVec4 tabHovered;              // fundo da tab em hover / ativa
    ImVec4 tabIcon;                 // icone da tab inativa
    ImVec4 tabIconActive;           // icone da tab ativa
};

extern Theme g_theme;

// Converte 0xRRGGBB (ex: 0x131313) para ImVec4.
ImVec4 hexColor(unsigned int rgb, float alpha = 1.0f);

// Mistura duas cores (t = 0 devolve "a", t = 1 devolve "b").
ImVec4 mixColor(const ImVec4& a, const ImVec4& b, float t);

// Aplica as cores e o estilo do g_theme dentro do ImGui.
void applyTheme();

// Troca a cor principal do menu em tempo real (destaque + interruptores).
// Usada pelo seletor de cor da aba de configuracoes.
void setAccentColor(const ImVec4& color);

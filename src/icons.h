#pragma once

#include "imgui.h"

// ---------------------------------------------------------------------------
//  Icones desenhados em vetor (nao precisa de fonte de icones nem de imagens).
//  center = centro do icone na tela, size = largura/altura total em pixels.
// ---------------------------------------------------------------------------
namespace icons
{
    void brush  (ImDrawList* draw, ImVec2 center, float size, ImU32 color); // limpeza      (tab cleaning)
    void speed  (ImDrawList* draw, ImVec2 center, float size, ImU32 color); // velocimetro  (tab optimization)
    void gear   (ImDrawList* draw, ImVec2 center, float size, ImU32 color); // configuracao (tab settings)
    void warning(ImDrawList* draw, ImVec2 center, float size, ImU32 color); // triangulo de aviso "!"
}

// Assinatura usada pelas tabs laterais.
using IconFunction = void (*)(ImDrawList*, ImVec2, float, ImU32);

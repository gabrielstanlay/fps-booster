#pragma once

// Janela do programa, usada pela barra de titulo que o menu desenha.
struct AppWindow
{
    void* handle  = nullptr;    // HWND da janela
    bool  running = true;       // vira false quando o usuario clica no "X"
};

// Cores, fontes e estilo do menu. Chamar uma vez, depois de iniciar o ImGui.
void initMenu(float dpiScale);

// Desenha o menu inteiro. Uma vez por frame.
void drawMenu(AppWindow& window);

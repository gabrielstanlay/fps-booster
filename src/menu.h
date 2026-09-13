#pragma once

struct AppWindow
{
    void* handle  = nullptr; // HWND da janela
    bool  running = true;
};

void initMenu(float dpiScale);
void drawMenu(AppWindow& window);
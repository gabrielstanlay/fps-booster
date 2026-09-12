#include "theme.h"

Theme g_theme;

ImVec4 hexColor(unsigned int rgb, float alpha)
{
    const float r = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
    const float g = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
    const float b = static_cast<float>(rgb & 0xFF) / 255.0f;
    return ImVec4(r, g, b, alpha);
}

ImVec4 mixColor(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

// Copia as cores do g_theme para o estilo do ImGui (sem mexer nos tamanhos).
static void applyColors()
{
    ImVec4* colors = ImGui::GetStyle().Colors;

    const ImVec4 transparent = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_WindowBg]              = g_theme.background;
    colors[ImGuiCol_ChildBg]               = transparent;   // cada painel pinta o proprio fundo
    colors[ImGuiCol_PopupBg]               = g_theme.panel;
    colors[ImGuiCol_Border]                = g_theme.border;
    colors[ImGuiCol_BorderShadow]          = transparent;

    colors[ImGuiCol_Text]                  = g_theme.text;
    colors[ImGuiCol_TextDisabled]          = g_theme.textDim;

    // widgets com moldura (o toggle e desenhado a mao, mas segue as mesmas cores)
    colors[ImGuiCol_FrameBg]               = g_theme.toggleOff;
    colors[ImGuiCol_FrameBgHovered]        = g_theme.toggleHovered;
    colors[ImGuiCol_FrameBgActive]         = g_theme.toggleHovered;
    colors[ImGuiCol_CheckMark]             = g_theme.toggleKnob;
    colors[ImGuiCol_CheckboxSelectedBg]    = g_theme.toggleOn;

    colors[ImGuiCol_Button]                = g_theme.button;
    colors[ImGuiCol_ButtonHovered]         = g_theme.buttonHovered;
    colors[ImGuiCol_ButtonActive]          = g_theme.buttonActive;

    colors[ImGuiCol_Header]                = g_theme.button;
    colors[ImGuiCol_HeaderHovered]         = g_theme.buttonHovered;
    colors[ImGuiCol_HeaderActive]          = g_theme.buttonActive;

    colors[ImGuiCol_Separator]             = g_theme.border;
    colors[ImGuiCol_SeparatorHovered]      = g_theme.border;
    colors[ImGuiCol_SeparatorActive]       = g_theme.accent;

    colors[ImGuiCol_ScrollbarBg]           = transparent;
    colors[ImGuiCol_ScrollbarGrab]         = g_theme.scrollbarGrab;
    colors[ImGuiCol_ScrollbarGrabHovered]  = g_theme.scrollbarGrabHovered;
    colors[ImGuiCol_ScrollbarGrabActive]   = g_theme.scrollbarGrabHovered;

    colors[ImGuiCol_SliderGrab]            = g_theme.accent;
    colors[ImGuiCol_SliderGrabActive]      = g_theme.accent;
    colors[ImGuiCol_NavCursor]             = transparent;
}

void applyTheme()
{
    applyColors();

    ImGuiStyle& style = ImGui::GetStyle();

    // formas e espacamentos
    style.WindowRounding     = 0.0f;
    style.ChildRounding      = 8.0f;
    style.FrameRounding      = 4.0f;
    style.PopupRounding      = 6.0f;
    style.GrabRounding       = 4.0f;
    style.ScrollbarRounding  = 6.0f;

    style.WindowBorderSize   = 0.0f;
    style.ChildBorderSize    = 1.0f;
    style.FrameBorderSize    = 0.0f;
    style.PopupBorderSize    = 1.0f;

    style.WindowPadding      = ImVec2(14.0f, 12.0f);
    style.FramePadding       = ImVec2(8.0f, 5.0f);
    style.ItemSpacing        = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing   = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize      = 10.0f;
    style.GrabMinSize        = 14.0f;

    style.WindowMenuButtonPosition = ImGuiDir_None;
}

void setAccentColor(const ImVec4& color)
{
    g_theme.accent   = color;
    g_theme.toggleOn = color;
    applyColors();          // so as cores: os tamanhos ja estao escalados pelo DPI
}

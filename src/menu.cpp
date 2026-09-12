#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "menu.h"

#include "cleaner.h"
#include "icons.h"
#include "theme.h"

#include "imgui.h"

#include <windows.h>
#include <filesystem>
#include <string>

namespace
{
    // -----------------------------------------------------------------------
    //  Medidas da interface (em pixels, antes da escala de DPI)
    // -----------------------------------------------------------------------
    constexpr float kHeaderHeight   = 46.0f;    // altura da barra de titulo
    constexpr float kSidebarWidth   = 64.0f;    // largura da coluna das tabs
    constexpr float kTabHeight      = 48.0f;    // altura de cada tab
    constexpr float kIconSize       = 20.0f;    // tamanho dos icones das tabs
    constexpr float kContentPad     = 16.0f;    // margem em volta dos paineis
    constexpr float kPanelSpacing   = 14.0f;    // espaco entre um painel e outro
    constexpr float kPanelPadding   = 16.0f;    // margem interna dos paineis
    constexpr float kPanelRounding  = 8.0f;
    constexpr float kTitleGap       = 9.0f;     // espaco entre o titulo do painel e a divisoria
    constexpr float kFooterHeight   = 56.0f;    // rodape fixo do painel (onde fica o botao)
    constexpr int   kMaxColumns     = 2;        // maximo de paineis lado a lado

    constexpr float kRowPadding     = 12.0f;    // margem de cima e de baixo de cada funcao
    constexpr float kToggleWidth    = 38.0f;    // tamanho do botao de liga/desliga
    constexpr float kToggleHeight   = 20.0f;
    constexpr float kSwatchSize     = 24.0f;    // tamanho do quadrado do seletor de cor
    constexpr float kSwatchRounding = 7.0f;     // arredondamento dos cantos dele

    constexpr float kItemSpacingX   = 10.0f;    // espaco entre itens dentro do painel
    constexpr float kItemSpacingY   = 8.0f;

    constexpr float kFontTitle      = 16.0f;    // titulo dos paineis
    constexpr float kFontSmall      = 13.0f;    // descricoes
    constexpr float kFontLogo       = 19.0f;    // nome do programa na barra de titulo

    constexpr float kRgbSpeed       = 0.15f;    // voltas por segundo do modo RGB

    // -----------------------------------------------------------------------
    //  Estado
    // -----------------------------------------------------------------------
    enum class Tab { cleaning, optimization, settings };

    // O que esta marcado em cada interruptor do menu.
    struct Options
    {
        bool cleanTemp        = true;
        bool cleanPrefetch    = true;
        bool cleanScreenshots = true;
        bool emptyRecycleBin  = false;
        bool pinWindow        = false;
        bool rgbMode          = false;
    };

    Tab        g_tab    = Tab::cleaning;
    Options    g_options;
    AppWindow* g_window = nullptr;

    float   g_uiScale  = 1.0f;
    ImFont* g_fontBold = nullptr;
    ImFont* g_fontLogo = nullptr;

    float px(float value)          { return value * g_uiScale; }
    ImU32 u32(const ImVec4& color) { return ImGui::GetColorU32(color); }

    // Aproxima 'current' de 'target' na velocidade dada (animacoes suaves).
    void animate(float* current, float target, float speed)
    {
        const float step  = ImGui::GetIO().DeltaTime * speed;
        const float delta = target - *current;
        *current += (delta > step) ? step : (delta < -step ? -step : delta);
    }

    // -----------------------------------------------------------------------
    //  Widgets
    // -----------------------------------------------------------------------

    // Botao com a cor de destaque.
    bool accentButton(const char* label, const ImVec2& size)
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        g_theme.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mixColor(g_theme.accent, hexColor(0xFFFFFF), 0.15f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  mixColor(g_theme.accent, hexColor(0x000000), 0.15f));
        ImGui::PushStyleColor(ImGuiCol_Text,          hexColor(0xFFFFFF));
        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(4);
        return pressed;
    }

    // O botao e desenhado sem texto, e o icone e o texto sao escritos por cima,
    // medidos juntos para o conjunto ficar centralizado.
    bool accentIconButton(const char* id, const char* label, IconFunction icon, const ImVec2& size)
    {
        ImGui::PushID(id);
        const bool pressed = accentButton("##botao", size);
        ImGui::PopID();

        const ImVec2 min      = ImGui::GetItemRectMin();
        const ImVec2 max      = ImGui::GetItemRectMax();
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const float  iconSize = px(14.0f);
        const float  gap      = px(8.0f);
        const float  startX   = (min.x + max.x - iconSize - gap - textSize.x) * 0.5f;
        const float  centerY  = (min.y + max.y) * 0.5f;
        const ImU32  color    = u32(hexColor(0xFFFFFF));

        ImDrawList* draw = ImGui::GetWindowDrawList();
        icon(draw, ImVec2(startX + iconSize * 0.5f, centerY), iconSize, color);
        draw->AddText(ImVec2(startX + iconSize + gap, centerY - textSize.y * 0.5f), color, label);
        return pressed;
    }

    // Interruptor com a bolinha deslizando. Devolve true quando muda.
    bool toggleSwitch(const char* id, bool* value)
    {
        const float width  = px(kToggleWidth);
        const float height = px(kToggleHeight);
        const float radius = height * 0.5f;

        ImGui::PushID(id);
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const bool   pressed  = ImGui::InvisibleButton("##toggle", ImVec2(width, height));
        if (pressed)
            *value = !*value;
        const bool hovered = ImGui::IsItemHovered();

        float* slide = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("slide"), *value ? 1.0f : 0.0f);
        animate(slide, *value ? 1.0f : 0.0f, 12.0f);

        const ImVec4 off   = hovered ? g_theme.toggleHovered : g_theme.toggleOff;
        const ImVec4 on    = hovered ? mixColor(g_theme.toggleOn, hexColor(0xFFFFFF), 0.12f) : g_theme.toggleOn;
        const ImVec4 track = mixColor(off, on, *slide);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(position, ImVec2(position.x + width, position.y + height), u32(track), radius);
        draw->AddCircleFilled(ImVec2(position.x + radius + (width - height) * (*slide), position.y + radius),
                              radius - px(3.0f), u32(g_theme.toggleKnob));

        ImGui::PopID();
        return pressed;
    }

    // Quadrado de cantos arredondados que abre o seletor de cores.
    bool colorSwatch(const char* id, ImVec4* color)
    {
        const float size     = px(kSwatchSize);
        const float rounding = px(kSwatchRounding);

        ImGui::PushID(id);
        const ImVec2 position = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##cor", ImVec2(size, size)))
            ImGui::OpenPopup("##seletor");
        const bool hovered = ImGui::IsItemHovered();

        ImDrawList*  draw   = ImGui::GetWindowDrawList();
        const ImVec2 bottom = ImVec2(position.x + size, position.y + size);
        draw->AddRectFilled(position, bottom, u32(*color), rounding);
        draw->AddRect(position, bottom, u32(hovered ? hexColor(0xFFFFFF) : g_theme.border),
                      rounding, 0, px(1.5f));

        bool changed = false;
        if (ImGui::BeginPopup("##seletor"))
        {
            changed = ImGui::ColorPicker3("##picker", &color->x,
                                          ImGuiColorEditFlags_DisplayHex |
                                          ImGuiColorEditFlags_NoSidePreview |
                                          ImGuiColorEditFlags_NoSmallPreview);
            ImGui::EndPopup();
        }

        ImGui::PopID();
        return changed;
    }

    // Linha de texto pequena e discreta.
    void hintText(const ImVec4& color, const char* text)
    {
        ImGui::PushFont(nullptr, kFontSmall);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    // -----------------------------------------------------------------------
    //  Linhas do painel
    // -----------------------------------------------------------------------

    // Medidas de uma linha, guardadas entre o inicio e o fim dela.
    struct Row
    {
        ImVec2 position;
        float  width  = 0.0f;
        float  height = 0.0f;
    };

    // Desenha o nome, a descricao e a divisoria de uma linha, e deixa o cursor
    // no lugar do controle da direita (interruptor, seletor de cor, etc).
    // Com 'warning', aparece um triangulo vermelho ao lado do nome que mostra
    // o texto do aviso quando o mouse passa por cima.
    Row beginRow(const char* label, const char* description, const char* warning,
                 float controlWidth, float controlHeight)
    {
        Row row;
        row.position = ImGui::GetCursorScreenPos();
        row.width    = ImGui::GetContentRegionAvail().x;

        const float startY    = ImGui::GetCursorPosY();
        const float padding   = px(kRowPadding);
        const float textWidth = row.width - controlWidth - px(20.0f);

        // a altura depende da descricao, que pode ocupar mais de uma linha
        const float titleHeight = ImGui::GetTextLineHeight();
        float descriptionHeight = 0.0f;
        if (description != nullptr)
        {
            ImGui::PushFont(nullptr, kFontSmall);
            descriptionHeight = px(4.0f) + ImGui::CalcTextSize(description, nullptr, false, textWidth).y;
            ImGui::PopFont();
        }
        row.height = padding * 2.0f + titleHeight + descriptionHeight;

        // reserva a linha inteira; o conteudo e desenhado por cima
        ImGui::Dummy(ImVec2(row.width, row.height));

        // divisoria entre uma linha e outra (a primeira do painel nao tem)
        if (startY > 0.5f)
            ImGui::GetWindowDrawList()->AddLine(row.position,
                                                ImVec2(row.position.x + row.width, row.position.y),
                                                u32(g_theme.border), 1.0f);

        ImGui::SetCursorScreenPos(ImVec2(row.position.x, row.position.y + padding));
        ImGui::PushFont(g_fontBold, 0.0f);
        ImGui::TextUnformatted(label);
        ImGui::PopFont();

        if (warning != nullptr)
        {
            const float iconSize = px(15.0f);

            ImGui::SameLine(0.0f, px(8.0f));
            ImGui::PushID(label);
            const ImVec2 iconPosition = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##aviso", ImVec2(iconSize, titleHeight));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            icons::warning(ImGui::GetWindowDrawList(),
                           ImVec2(iconPosition.x + iconSize * 0.5f, iconPosition.y + titleHeight * 0.5f),
                           iconSize, u32(hexColor(0xE5484D)));

            if (hovered)
                ImGui::SetTooltip("%s", warning);
        }

        if (description != nullptr)
        {
            ImGui::SetCursorScreenPos(ImVec2(row.position.x, row.position.y + padding + titleHeight + px(4.0f)));
            ImGui::PushFont(nullptr, kFontSmall);
            ImGui::PushStyleColor(ImGuiCol_Text, g_theme.textDim);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
            ImGui::TextUnformatted(description);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        // controle encostado na direita e centralizado na altura da linha
        ImGui::SetCursorScreenPos(ImVec2(row.position.x + row.width - controlWidth,
                                         row.position.y + (row.height - controlHeight) * 0.5f));
        return row;
    }

    // Fecha a linha: a proxima comeca colada no fim desta.
    void endRow(const Row& row)
    {
        ImGui::SetCursorScreenPos(ImVec2(row.position.x, row.position.y + row.height));
    }

    // Linha com interruptor. Devolve true quando o usuario mexe nele.
    bool option(const char* label, const char* description, bool* value, const char* warning = nullptr)
    {
        const Row  row     = beginRow(label, description, warning, px(kToggleWidth), px(kToggleHeight));
        const bool changed = toggleSwitch(label, value);
        endRow(row);
        return changed;
    }

    // Linha com seletor de cor. Devolve true quando a cor muda.
    bool colorOption(const char* label, const char* description, ImVec4* color)
    {
        const Row  row     = beginRow(label, description, nullptr, px(kSwatchSize), px(kSwatchSize));
        const bool changed = colorSwatch(label, color);
        endRow(row);
        return changed;
    }

    // -----------------------------------------------------------------------
    //  Conteudo dos paineis
    // -----------------------------------------------------------------------
    void drawCleaningPanel()
    {
        option("Limpar arquivos temporários", "Apaga todos arquivos de C:\\Windows\\Temp e %temp%.",
               &g_options.cleanTemp);

        option("Limpar prefetch", "Apaga todos arquivos de C:\\Windows\\Prefetch.",
               &g_options.cleanPrefetch);

        option("Limpar cache de screenshots",
               "Apaga as miniaturas que o Windows gera e usa no Explorador de Arquivos, "
               "junto com as prints temporarias da Ferramenta de Captura.",
               &g_options.cleanScreenshots,
               "Essa operação irá piscar sua barra de tarefas, não se assuste, é normal.");

        option("Esvaziar lixeira", "Apaga todos arquivos da lixeira.",
               &g_options.emptyRecycleBin,
               "Não habilite essa opção a não ser que tenha certeza de que não há arquivos importantes na lixeira.");
    }

    // Rodape do painel de limpeza: resultado a esquerda, botao a direita.
    void drawCleaningFooter()
    {
        const float buttonWidth  = ImGui::CalcTextSize("Limpar selecionados").x + px(56.0f);
        const float buttonHeight = px(32.0f);
        const float startX       = ImGui::GetCursorPosX();
        const float startY       = ImGui::GetCursorPosY();
        const float areaWidth    = ImGui::GetContentRegionAvail().x;
        const float areaHeight   = ImGui::GetContentRegionAvail().y;

        const std::string status = cleanStatus();
        if (!status.empty())
        {
            ImGui::PushFont(nullptr, kFontSmall);
            ImGui::SetCursorPos(ImVec2(startX, startY + (areaHeight - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, g_theme.textDim);
            ImGui::PushTextWrapPos(startX + areaWidth - buttonWidth - px(12.0f));
            ImGui::TextUnformatted(status.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        const bool busy    = isCleaning();
        const bool nothing = !g_options.cleanTemp && !g_options.cleanPrefetch &&
                             !g_options.cleanScreenshots && !g_options.emptyRecycleBin;

        ImGui::SetCursorPos(ImVec2(startX + areaWidth - buttonWidth, startY + (areaHeight - buttonHeight) * 0.5f));
        ImGui::BeginDisabled(busy || nothing);
        if (accentIconButton("limpar", busy ? "Limpando..." : "Limpar selecionados", icons::brush,
                             ImVec2(buttonWidth, buttonHeight)))
        {
            CleanOptions options;
            options.temp        = g_options.cleanTemp;
            options.prefetch    = g_options.cleanPrefetch;
            options.screenshots = g_options.cleanScreenshots;
            options.recycleBin  = g_options.emptyRecycleBin;
            startCleaning(options);
        }
        ImGui::EndDisabled();
    }

    void drawOptimizationPanel()
    {
        hintText(g_theme.textDim, "As otimizacoes entram aqui.");
    }

    // Fixa ou solta a janela na frente das outras.
    void applyPinWindow()
    {
        HWND handle = static_cast<HWND>(g_window->handle);
        if (handle != nullptr)
            ::SetWindowPos(handle, g_options.pinWindow ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void drawConfigsPanel()
    {
       if (option("Fixar janela", "Mantem a janela fixada na frente dos outros programas.", &g_options.pinWindow))
            applyPinWindow();

       ImGui::BeginDisabled(g_options.rgbMode);
       ImVec4 menuColor = g_theme.accent;
       if (colorOption("Cor do menu",
           "Muda a cor de destaque do menu.",
           &menuColor))
           setAccentColor(menuColor);
       ImGui::EndDisabled();

       option("Modo RGB", "Fica trocando a cor do menu, até ser desligado.", &g_options.rgbMode);
    }

    // Passeia com a cor do menu pelo arco-iris enquanto o modo RGB estiver ligado,
    // e volta pra cor anterior quando ele for desligado.
    void updateRgbMode()
    {
        static bool   running = false;
        static ImVec4 savedColor;
        static float  hue = 0.0f;

        if (g_options.rgbMode)
        {
            if (!running)
            {
                savedColor = g_theme.accent;
                running    = true;
            }

            hue += ImGui::GetIO().DeltaTime * kRgbSpeed;
            if (hue > 1.0f)
                hue -= 1.0f;

            ImVec4 color(0.0f, 0.0f, 0.0f, 1.0f);
            ImGui::ColorConvertHSVtoRGB(hue, 0.90f, 1.0f, color.x, color.y, color.z);
            setAccentColor(color);
        }
        else if (running)
        {
            running = false;
            setAccentColor(savedColor);
        }
    }

    // -----------------------------------------------------------------------
    //  Paineis (as "subtabs" de cada tab)
    // -----------------------------------------------------------------------
    struct Panel
    {
        const char* id;         // identificador interno
        const char* title;      // titulo mostrado no topo do painel
        void      (*draw)();    // conteudo (rola quando nao cabe)
        void      (*footer)();  // rodape fixo, ou nullptr se nao tiver
    };

    const Panel kCleaningPanels[]     = { { "cleaning",     "Limpeza",       drawCleaningPanel,     drawCleaningFooter } };
    const Panel kOptimizationPanels[] = { { "optimization", "Otimizações",   drawOptimizationPanel, nullptr } };
    const Panel kSettingsPanels[]     = { { "configs",      "Configurações", drawConfigsPanel,      nullptr } };

    // Abre um painel: fundo proprio, titulo, divisoria e area rolavel.
    void beginPanel(const char* id, const char* title, const ImVec2& size, float footerHeight)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, g_theme.panel);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(kPanelRounding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(kPanelPadding), px(kPanelPadding * 0.85f)));
        ImGui::BeginChild(id, size, ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(kItemSpacingX), px(kItemSpacingY)));

        // titulo e divisoria com espacamento proprio, para ficarem bem juntos
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(kItemSpacingX), 0.0f));
        ImGui::PushFont(g_fontBold, kFontTitle);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0.0f, px(kTitleGap)));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, px(2.0f)));
        ImGui::PopStyleVar();

        // area dos itens: ganha scrollbar sozinha quando o conteudo nao cabe
        ImGui::BeginChild("##items", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None);
    }

    void endPanel(void (*footer)())
    {
        ImGui::EndChild();      // area dos itens

        if (footer != nullptr)
        {
            ImGui::Separator();
            footer();
        }

        ImGui::PopStyleVar();
        ImGui::EndChild();      // painel
    }

    // Distribui os paineis em colunas, com o mesmo espacamento entre eles.
    void drawPanels(const Panel* panels, int count)
    {
        if (count <= 0)
            return;

        const float  spacing = px(kPanelSpacing);
        const ImVec2 area    = ImGui::GetContentRegionAvail();
        const int    columns = (count < kMaxColumns) ? count : kMaxColumns;
        const int    rows    = (count + columns - 1) / columns;

        const ImVec2 size((area.x - spacing * (columns - 1)) / columns,
                          (area.y - spacing * (rows - 1)) / rows);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
        for (int i = 0; i < count; ++i)
        {
            if (i % columns != 0)
                ImGui::SameLine();

            const float footerHeight = (panels[i].footer != nullptr) ? px(kFooterHeight) : 0.0f;
            beginPanel(panels[i].id, panels[i].title, size, footerHeight);
            panels[i].draw();
            endPanel(panels[i].footer);
        }
        ImGui::PopStyleVar();
    }

    // -----------------------------------------------------------------------
    //  Coluna das tabs
    // -----------------------------------------------------------------------

    // Uma tab lateral: icone centralizado, fundo animado e barra de destaque.
    bool tabButton(const char* id, IconFunction icon, bool selected, const char* tooltip)
    {
        const ImVec2 size(ImGui::GetContentRegionAvail().x, px(kTabHeight));

        ImGui::PushID(id);
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const bool   pressed  = ImGui::InvisibleButton("##tab", size);
        const bool   hovered  = ImGui::IsItemHovered();

        float* fade = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("fade"), 0.0f);
        animate(fade, (selected || hovered) ? 1.0f : 0.0f, 10.0f);

        ImDrawList*  draw   = ImGui::GetWindowDrawList();
        const ImVec2 bottom = ImVec2(position.x + size.x, position.y + size.y);

        if (*fade > 0.01f)
        {
            ImVec4 background = g_theme.tabHovered;
            background.w *= *fade;
            draw->AddRectFilled(position, bottom, u32(background));
        }

        if (selected)
        {
            const float barHeight = size.y * 0.46f;
            const float top       = position.y + (size.y - barHeight) * 0.5f;
            draw->AddRectFilled(ImVec2(position.x, top),
                                ImVec2(position.x + px(3.0f), top + barHeight),
                                u32(g_theme.accent), px(2.0f));
        }

        const ImVec4 iconColor = selected ? g_theme.tabIconActive
                                          : mixColor(g_theme.tabIcon, g_theme.text, *fade);
        icon(draw, ImVec2(position.x + size.x * 0.5f, position.y + size.y * 0.5f), px(kIconSize), u32(iconColor));

        if (hovered)
            ImGui::SetTooltip("%s", tooltip);

        ImGui::PopID();
        return pressed;
    }

    void drawSidebar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("sidebar", ImVec2(px(kSidebarWidth), 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, px(4.0f)));
        ImGui::Dummy(ImVec2(0.0f, px(8.0f)));

        if (tabButton("cleaning", icons::brush, g_tab == Tab::cleaning, "Limpeza"))
            g_tab = Tab::cleaning;
        if (tabButton("optimization", icons::speed, g_tab == Tab::optimization, "Otimizações"))
            g_tab = Tab::optimization;
        if (tabButton("settings", icons::gear, g_tab == Tab::settings, "Configurações"))
            g_tab = Tab::settings;

        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    // -----------------------------------------------------------------------
    //  Barra de titulo
    // -----------------------------------------------------------------------

    // Move a janela enquanto o usuario arrasta o ultimo item criado.
    void dragWindow()
    {
        static bool  dragging = false;
        static POINT grabbedCursor = {};
        static POINT grabbedWindow = {};

        HWND handle = static_cast<HWND>(g_window->handle);
        if (handle == nullptr)
            return;

        if (ImGui::IsItemActivated())
        {
            RECT rect = {};
            ::GetCursorPos(&grabbedCursor);
            ::GetWindowRect(handle, &rect);
            grabbedWindow.x = rect.left;
            grabbedWindow.y = rect.top;
            dragging = true;
        }

        if (dragging && ImGui::IsItemActive())
        {
            POINT cursor = {};
            ::GetCursorPos(&cursor);
            ::SetWindowPos(handle, nullptr,
                           grabbedWindow.x + (cursor.x - grabbedCursor.x),
                           grabbedWindow.y + (cursor.y - grabbedCursor.y),
                           0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        if (ImGui::IsItemDeactivated())
            dragging = false;
    }

    // Botao de minimizar (isClose = false) ou de fechar (isClose = true).
    bool windowButton(const char* id, bool isClose)
    {
        const ImVec2 size(px(34.0f), px(28.0f));

        ImGui::PushID(id);
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const bool   pressed  = ImGui::InvisibleButton("##button", size);
        const bool   hovered  = ImGui::IsItemHovered();

        ImDrawList*  draw   = ImGui::GetWindowDrawList();
        const ImVec2 bottom = ImVec2(position.x + size.x, position.y + size.y);

        if (hovered)
            draw->AddRectFilled(position, bottom, u32(isClose ? hexColor(0xD03B3B) : g_theme.button), px(5.0f));

        const ImVec2 center(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
        const float  arm       = px(4.5f);
        const float  thickness = px(1.4f);
        const ImU32  color     = u32(hovered ? hexColor(0xFFFFFF) : g_theme.textDim);

        if (isClose)
        {
            draw->AddLine(ImVec2(center.x - arm, center.y - arm), ImVec2(center.x + arm, center.y + arm), color, thickness);
            draw->AddLine(ImVec2(center.x - arm, center.y + arm), ImVec2(center.x + arm, center.y - arm), color, thickness);
        }
        else
        {
            draw->AddLine(ImVec2(center.x - arm, center.y), ImVec2(center.x + arm, center.y), color, thickness);
        }

        ImGui::PopID();
        return pressed;
    }

    void drawHeader()
    {
        const float height       = px(kHeaderHeight);
        const float buttonsWidth = px(34.0f) * 2.0f + px(2.0f) + px(12.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("header", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();

        const ImVec2 origin = ImGui::GetWindowPos();
        const float  width  = ImGui::GetWindowWidth();

        // area livre para arrastar a janela
        const float dragWidth = (width - buttonsWidth > 1.0f) ? (width - buttonsWidth) : 1.0f;
        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        ImGui::InvisibleButton("##drag", ImVec2(dragWidth, height));
        dragWindow();

        // marcador colorido + nome do programa
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(origin.x + px(18.0f), origin.y + height * 0.5f - px(7.0f)),
                                                  ImVec2(origin.x + px(21.0f), origin.y + height * 0.5f + px(7.0f)),
                                                  u32(g_theme.accent), px(2.0f));

        ImGui::PushFont(g_fontLogo, kFontLogo);
        ImGui::SetCursorPos(ImVec2(px(31.0f), (height - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextUnformatted("FPS BOOSTER");
        ImGui::PopFont();

        const float afterTitle = ImGui::GetItemRectMax().x - origin.x;
        ImGui::PushFont(nullptr, kFontSmall);
        ImGui::SetCursorPos(ImVec2(afterTitle + px(8.0f), (height - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, g_theme.textDim);
        ImGui::TextUnformatted("alpha v0.0.5");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(width - buttonsWidth, (height - px(28.0f)) * 0.5f));
        if (windowButton("minimize", false))
            ::ShowWindow(static_cast<HWND>(g_window->handle), SW_MINIMIZE);

        ImGui::SameLine(0.0f, px(2.0f));
        if (windowButton("close", true))
            g_window->running = false;

        ImGui::EndChild();
    }

    // -----------------------------------------------------------------------
    //  Corpo: tabs na esquerda, paineis na direita
    // -----------------------------------------------------------------------
    void drawContent()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(kContentPad), px(kContentPad)));
        ImGui::BeginChild("content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();

        switch (g_tab)
        {
        case Tab::cleaning:     drawPanels(kCleaningPanels,     IM_ARRAYSIZE(kCleaningPanels));     break;
        case Tab::optimization: drawPanels(kOptimizationPanels, IM_ARRAYSIZE(kOptimizationPanels)); break;
        case Tab::settings:     drawPanels(kSettingsPanels,     IM_ARRAYSIZE(kSettingsPanels));     break;
        }

        ImGui::EndChild();
    }

    void drawBody()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("body", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();

        // divisoria entre as tabs e os paineis
        const ImVec2 origin  = ImGui::GetWindowPos();
        const ImVec2 size    = ImGui::GetWindowSize();
        const float  divider = origin.x + px(kSidebarWidth);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(divider, origin.y),
                                            ImVec2(divider, origin.y + size.y),
                                            u32(g_theme.border), 1.0f);

        drawSidebar();
        ImGui::SameLine(0.0f, 1.0f);
        drawContent();

        ImGui::EndChild();
    }
}

void initMenu(float dpiScale)
{
    g_uiScale = dpiScale;

    // =======================================================================
    //  CORES DO MENU  (formato 0xRRGGBB) - mude aqui para trocar a aparencia
    // =======================================================================
    g_theme.background           = hexColor(0x131313);  // fundo da janela e da coluna das tabs
    g_theme.panel                = hexColor(0x1D1D1D);  // fundo dos paineis (contraste)
    g_theme.border               = hexColor(0x272727);  // linhas divisorias e borda dos paineis
    g_theme.accent               = hexColor(0xFF143D);  // cor de destaque (tab ativa, botao principal)

    g_theme.text                 = hexColor(0xE8E8E8);  // texto normal
    g_theme.textDim              = hexColor(0x7C7C7C);  // texto secundario / descricoes

    g_theme.button               = hexColor(0x232323);  // botao normal
    g_theme.buttonHovered        = hexColor(0x2C2C2C);  // botao com o mouse em cima
    g_theme.buttonActive         = hexColor(0x353535);  // botao sendo clicado

    g_theme.toggleOff            = hexColor(0x262626);  // interruptor desligado
    g_theme.toggleHovered        = hexColor(0x303030);  // interruptor com o mouse em cima
    g_theme.toggleOn             = hexColor(0xFF143D);  // interruptor ligado (acompanha a cor de destaque)
    g_theme.toggleKnob           = hexColor(0xFFFFFF);  // bolinha do interruptor

    g_theme.scrollbarGrab        = hexColor(0x2E2E2E);  // barra de rolagem
    g_theme.scrollbarGrabHovered = hexColor(0x3D3D3D);  // barra de rolagem com o mouse em cima

    g_theme.tabHovered           = hexColor(0x1D1D1D);  // fundo da tab lateral em hover / ativa
    g_theme.tabIcon              = hexColor(0x6E6E6E);  // icone da tab inativa
    g_theme.tabIconActive        = hexColor(0xFFFFFF);  // icone da tab ativa
    // =======================================================================

    applyTheme();

    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 15.0f;
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;

    // Fontes
    const char* regularFont = "C:\\Windows\\Fonts\\segoeui.ttf"; // Fonte padrão
    const char* boldFont    = "C:\\Windows\\Fonts\\seguisb.ttf";
    const char* logoFont    = "C:\\Windows\\Fonts\\bahnschrift.ttf"; // Fonte do nome "FPS BOOSTER"
    std::error_code fontError;

    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    if (std::filesystem::exists(regularFont, fontError))
        fonts->AddFontFromFileTTF(regularFont);
    if (std::filesystem::exists(boldFont, fontError))
        g_fontBold = fonts->AddFontFromFileTTF(boldFont);
    if (std::filesystem::exists(logoFont, fontError))
        g_fontLogo = fonts->AddFontFromFileTTF(logoFont);
}

void drawMenu(AppWindow& window)
{
    g_window = &window;
    updateRgbMode();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##menu", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);

    drawHeader();

    // divisoria abaixo da barra de titulo
    const ImVec2 origin       = ImGui::GetWindowPos();
    const float  headerBottom = px(kHeaderHeight);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x, origin.y + headerBottom),
                                        ImVec2(origin.x + ImGui::GetWindowWidth(), origin.y + headerBottom),
                                        u32(g_theme.border), 1.0f);

    ImGui::SetCursorPos(ImVec2(0.0f, headerBottom + 1.0f));
    drawBody();

    // borda fina em volta da janela
    ImGui::GetWindowDrawList()->AddRect(viewport->Pos,
                                        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                                        u32(g_theme.border));

    ImGui::End();
}

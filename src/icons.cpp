#include "icons.h"

#include <math.h>

namespace
{
    constexpr float kPi = 3.14159265358979f;

    // Converte coordenadas locais (-0.5 .. 0.5) para a tela.
    ImVec2 point(const ImVec2& center, float size, float x, float y)
    {
        return ImVec2(center.x + x * size, center.y + y * size);
    }

    float strokeWidth(float size)
    {
        const float width = size * 0.085f;
        return width < 1.0f ? 1.0f : width;
    }
}

namespace icons
{
    // Pincel de limpeza inclinado a 45 graus, em contorno (mesmo estilo dos outros).
    void brush(ImDrawList* draw, ImVec2 center, float size, ImU32 color)
    {
        const float diagonal = 0.70710678f;             // cos/sen de 45 graus
        const ImVec2 axis(diagonal, -diagonal);         // sentido do cabo
        const ImVec2 side(diagonal, diagonal);          // perpendicular ao cabo
        const float thickness = strokeWidth(size);

        // 'along' anda no sentido do cabo, 'across' anda para os lados.
        auto at = [&](float along, float across)
        {
            return ImVec2(center.x + (axis.x * along + side.x * across) * size,
                          center.y + (axis.y * along + side.y * across) * size);
        };

        // cerdas (contorno)
        const ImVec2 head[4] = { at(-0.08f, -0.18f), at(-0.08f, 0.18f),
                                 at(-0.48f,  0.25f), at(-0.48f, -0.25f) };
        draw->AddPolyline(head, 4, color, thickness, ImDrawFlags_Closed);

        // faixa que prende as cerdas
        draw->AddLine(at(-0.26f, -0.215f), at(-0.26f, 0.215f), color, thickness);

        // cabo
        draw->AddLine(at(-0.06f, 0.0f), at(0.46f, 0.0f), color, thickness);
    }

    // Velocimetro com o ponteiro no maximo (estilo turbo/boost).
    void speed(ImDrawList* draw, ImVec2 center, float size, ImU32 color)
    {
        const float  thickness = strokeWidth(size);
        const float  radius    = size * 0.42f;
        const ImVec2 pivot(center.x, center.y + size * 0.17f);

        // mostrador
        draw->PathArcTo(pivot, radius, kPi * 0.88f, kPi * 2.12f, 28);
        draw->PathStroke(color, 0, thickness);

        // ponteiro apontando para cima e para a direita
        const float angle = -kPi * 0.27f;
        draw->AddLine(pivot, ImVec2(pivot.x + cosf(angle) * radius * 0.78f,
                                    pivot.y + sinf(angle) * radius * 0.78f), color, thickness);

        // eixo do ponteiro
        draw->AddCircleFilled(pivot, thickness, color);
    }

    // Engrenagem: contorno com dentes + furo no meio.
    void gear(ImDrawList* draw, ImVec2 center, float size, ImU32 color)
    {
        const int   teeth       = 8;
        const float outerRadius = size * 0.50f;
        const float innerRadius = size * 0.355f;
        const float holeRadius  = size * 0.145f;
        const float pitch       = (2.0f * kPi) / teeth;
        const float thickness   = strokeWidth(size);

        ImVec2 shape[teeth * 4];
        int count = 0;
        for (int i = 0; i < teeth; ++i)
        {
            const float base = i * pitch;
            const float angles[4] = { base - pitch * 0.17f, base + pitch * 0.17f,
                                      base + pitch * 0.32f, base + pitch * 0.68f };
            const float radius[4] = { outerRadius, outerRadius, innerRadius, innerRadius };

            for (int j = 0; j < 4; ++j)
                shape[count++] = ImVec2(center.x + cosf(angles[j]) * radius[j],
                                        center.y + sinf(angles[j]) * radius[j]);
        }

        draw->AddPolyline(shape, count, color, thickness, ImDrawFlags_Closed);
        draw->AddCircle(center, holeRadius, color, 0, thickness);
    }

    // Triangulo de aviso com "!" no meio.
    void warning(ImDrawList* draw, ImVec2 center, float size, ImU32 color)
    {
        const float thickness = strokeWidth(size);

        const ImVec2 triangle[3] = { point(center, size,  0.00f, -0.46f),
                                     point(center, size,  0.50f,  0.40f),
                                     point(center, size, -0.50f,  0.40f) };
        draw->AddPolyline(triangle, 3, color, thickness, ImDrawFlags_Closed);

        // Exclamacao em retangulos: AddLine desloca meio pixel e deixaria o
        // tracinho torto em relacao ao ponto de baixo.
        const float half   = thickness * 0.5f;
        const ImVec2 top    = point(center, size, 0.0f, -0.17f);
        const ImVec2 bottom = point(center, size, 0.0f,  0.10f);
        draw->AddRectFilled(ImVec2(top.x - half, top.y), ImVec2(top.x + half, bottom.y), color);

        const ImVec2 dot = point(center, size, 0.0f, 0.26f);
        draw->AddRectFilled(ImVec2(dot.x - half, dot.y - half), ImVec2(dot.x + half, dot.y + half), color, half);
    }
}

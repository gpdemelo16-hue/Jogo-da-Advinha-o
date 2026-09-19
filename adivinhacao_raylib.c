/*
 * ============================================================================
 *  JOGO DA ADIVINHAÇÃO - versão gráfica com raylib
 * ============================================================================
 *  Baseado no jogo original de terminal. A lógica foi preservada:
 *    - Número secreto de 1 a 100
 *    - 3 dificuldades (tentativas / divisão / multiplicador do original)
 *    - Começa com 100 pontos; cada erro tira pontos
 *    - Chute negativo ou maior que 100 é inválido e NÃO gasta tentativa
 *    - Acertou = vitória | pontos <= 0 ou tentativas acabaram = derrota
 *
 *  IMPORTANTE: salve este arquivo em UTF-8 (para os acentos aparecerem).
 *
 *  Compilar (Linux):
 *    gcc adivinhacao_raylib.c -o adivinhacao -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
 *  Compilar (Windows / MinGW / w64devkit):
 *    gcc adivinhacao_raylib.c -o adivinhacao.exe -lraylib -lopengl32 -lgdi32 -lwinmm
 *  Compilar (macOS):
 *    clang adivinhacao_raylib.c -o adivinhacao -lraylib -framework IOKit -framework Cocoa -framework OpenGL
 * ============================================================================
 */

#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ------------------------------ CONSTANTES ------------------------------- */
#define SCREEN_WIDTH   900
#define SCREEN_HEIGHT  640

#define MAX_ATTEMPTS     20      /* maior número de tentativas (nível Fácil) */
#define MAX_PARTICLES    500
#define MAX_BUBBLES      24
#define MAX_FLOAT_TEXTS  8
#define MAX_BUTTONS      4

#define START_POINTS     100.0f  /* pontos iniciais (igual ao original) */
#define MIN_NUMBER       1
#define MAX_NUMBER       100

/* Layout do painel central da tela de jogo */
#define PANEL_X   280.0f
#define PANEL_W   580.0f
#define PANEL_CX  (PANEL_X + PANEL_W / 2.0f)

/*
 * Fórmula de pontos perdidos.
 *  0 = corrigida:  |chute - secreto| * multiplicador / divisão
 *  1 = ORIGINAL:   |chute - secreto * multiplicador| / divisão
 * (Veja a explicação no final da resposta sobre o motivo da correção.)
 */
#define USE_ORIGINAL_PENALTY_FORMULA 0

/* --------------------------------- TIPOS --------------------------------- */
typedef enum {
    STATE_MENU,
    STATE_DIFFICULTY,
    STATE_RULES,
    STATE_PLAYING,
    STATE_WIN,
    STATE_LOSE
} GameState;

typedef enum { MOOD_IDLE, MOOD_WORRIED, MOOD_HAPPY, MOOD_SAD } MascotMood;

typedef enum { GUESS_TOO_LOW, GUESS_TOO_HIGH, GUESS_CORRECT } GuessResult;

/* Configuração de cada nível (valores idênticos aos do código original) */
typedef struct {
    const char *name;
    int   totalAttempts;
    float divisor;        /* "divisao" do original */
    float multiplier;     /* "multi" do original   */
    Color color;
} Difficulty;

static const Difficulty DIFFICULTIES[3] = {
    { "Fácil",   20, 3.0f, 1.0f, { 70, 190, 120, 255 } },
    { "Médio",   15, 2.0f, 1.5f, { 235, 160,  30, 255 } },
    { "Difícil",  6, 1.0f, 2.0f, { 230,  80,  90, 255 } }
};

typedef struct {
    Rectangle rect;
    char  label[32];
    char  subLabel[64];
    Color color;
    float hover;    /* 0..1 animado */
    float press;    /* 0..1 animado */
    float appear;   /* 0..1 animação de entrada */
} Button;

typedef struct {
    Vector2 position, velocity;
    float life, maxLife, size, gravity, rotation, spin;
    Color color;
    bool  square, active;
} Particle;

typedef struct {
    Vector2 pos;
    float radius, speed, phase;
    bool  isQuestion;
} Bubble;

typedef struct {
    char  text[24];
    Vector2 pos;
    float life;
    Color color;
    bool  active;
} FloatText;

typedef struct {
    int value;
    GuessResult result;
    float age;      /* usado na animação de "pop" do chip */
} GuessEntry;

typedef struct {
    /* --- estado geral e transições --- */
    GameState state, nextState;
    bool  changing;         /* true enquanto a tela escurece para trocar de estado */
    float fade;             /* 0 = transparente, 1 = tela toda preta */
    float stateTime;        /* tempo desde que entrou no estado atual */
    float time;             /* tempo total (para ondas/senoides) */
    bool  shouldQuit;

    /* --- botões da tela atual --- */
    Button buttons[MAX_BUTTONS];
    int    buttonCount;

    /* --- dados da rodada (equivalem às variáveis do original) --- */
    int   difficultyIndex;
    int   secretNumber;         /* num_secreto */
    float points;               /* pontos */
    float displayedPoints;      /* pontos mostrados (animados) */
    int   attemptsUsed;         /* i */
    int   totalAttempts;        /* totaltentativas */
    float divisor, multiplier;  /* divisao, multi */
    GuessEntry history[MAX_ATTEMPTS];
    bool  lostByPoints;

    /* --- entrada do jogador --- */
    char inputText[6];
    int  inputLength;

    /* --- faixa possível do número (dica visual) --- */
    int   lowBound, highBound;
    float shownLow, shownHigh;

    /* --- mensagens --- */
    char  message[96];
    char  subMessage[64];
    Color messageColor;
    float messageTime;

    /* --- efeitos --- */
    float shake, inputShake, pipPop, emitTimer, scoreCount;
    bool  endPending;
    float endDelay;
    GameState endState;

    /* --- mascote --- */
    MascotMood mood;
    float moodTimer, mascotBounce;

    /* --- decoração --- */
    Color bgTop, bgBottom;
    Particle  particles[MAX_PARTICLES];
    Bubble    bubbles[MAX_BUBBLES];
    FloatText floatTexts[MAX_FLOAT_TEXTS];
} Game;

/* ------------------------- PROTÓTIPOS PRINCIPAIS -------------------------- */
static void EnterState(Game *g, GameState newState);
static void ChangeState(Game *g, GameState newState);

/* ============================================================================
 *  FUNÇÕES AUXILIARES (matemática, cores, texto)
 * ========================================================================== */

/* Curva com "quique" no final: ótima para botões e chips aparecendo. */
static float EaseOutBack(float t)
{
    t = Clamp(t, 0.0f, 1.0f);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

/* Curva suave que desacelera no final. */
static float EaseOutCubic(float t)
{
    t = Clamp(t, 0.0f, 1.0f);
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

static Color LerpColor(Color a, Color b, float t)
{
    t = Clamp(t, 0.0f, 1.0f);
    Color c;
    c.r = (unsigned char)Lerp((float)a.r, (float)b.r, t);
    c.g = (unsigned char)Lerp((float)a.g, (float)b.g, t);
    c.b = (unsigned char)Lerp((float)a.b, (float)b.b, t);
    c.a = (unsigned char)Lerp((float)a.a, (float)b.a, t);
    return c;
}

/* Desenha/mede texto com a fonte padrão da raylib aceitando tamanho float. */
static void DrawTextF(const char *text, float x, float y, float size, Color color)
{
    DrawTextEx(GetFontDefault(), text, (Vector2){ x, y }, size, size / 10.0f, color);
}

static float MeasureTextF(const char *text, float size)
{
    return MeasureTextEx(GetFontDefault(), text, size, size / 10.0f).x;
}

static void DrawTextCentered(const char *text, float cx, float y, float size, Color color)
{
    DrawTextF(text, cx - MeasureTextF(text, size) / 2.0f, y, size, color);
}

/* Diminui o tamanho da fonte até o texto caber na largura máxima. */
static float FitFontSize(const char *text, float size, float maxWidth)
{
    while (size > 12.0f && MeasureTextF(text, size) > maxWidth) size -= 1.0f;
    return size;
}

/* Tamanho em bytes de um caractere UTF-8 (para acentos não quebrarem). */
static int Utf8CharLength(unsigned char first)
{
    if (first < 0x80) return 1;
    if ((first & 0xE0) == 0xC0) return 2;
    if ((first & 0xF0) == 0xE0) return 3;
    if ((first & 0xF8) == 0xF0) return 4;
    return 1;
}

/* Texto que aparece letra por letra ("máquina de escrever"). */
static void DrawTypewriter(const char *text, float x, float y, float size, Color color,
                           float elapsed, float charsPerSecond, bool centered)
{
    if (elapsed <= 0.0f) return;

    int visibleChars = (int)(elapsed * charsPerSecond);
    char buffer[160];
    int bytes = 0, chars = 0;

    while (text[bytes] != '\0' && chars < visibleChars) {
        bytes += Utf8CharLength((unsigned char)text[bytes]);
        chars++;
    }
    if (bytes > 159) bytes = 159;
    memcpy(buffer, text, bytes);
    buffer[bytes] = '\0';

    /* Centraliza usando a largura do texto COMPLETO para ele não "andar". */
    float drawX = centered ? x - MeasureTextF(text, size) / 2.0f : x;
    DrawTextF(buffer, drawX, y, size, color);
}

/* Texto onde cada letra sobe e desce em onda (usado nos títulos). */
static void DrawWaveText(const char *text, float cx, float y, float size, Color color,
                         bool rainbow, float time, float amplitude)
{
    float x = cx - MeasureTextF(text, size) / 2.0f;
    int i = 0, index = 0;

    while (text[i] != '\0') {
        int len = Utf8CharLength((unsigned char)text[i]);
        char letter[5] = { 0 };
        memcpy(letter, &text[i], len);

        float offsetY = sinf(time * 4.0f + index * 0.5f) * amplitude;
        Color letterColor = color;
        if (rainbow) {
            letterColor = Fade(ColorFromHSV(fmodf(time * 60.0f + index * 25.0f, 360.0f), 0.55f, 1.0f),
                               color.a / 255.0f);
        }
        DrawTextF(letter, x + 3, y + offsetY + 4, size, Fade(BLACK, 0.35f * color.a / 255.0f)); /* sombra */
        DrawTextF(letter, x, y + offsetY, size, letterColor);

        x += MeasureTextF(letter, size) + size / 10.0f;
        i += len;
        index++;
    }
}

/* ============================================================================
 *  PARTÍCULAS, TEXTOS FLUTUANTES E BOLHAS
 * ========================================================================== */

static void ClearEffects(Game *g)
{
    memset(g->particles, 0, sizeof(g->particles));
    memset(g->floatTexts, 0, sizeof(g->floatTexts));
}

static void SpawnParticle(Game *g, Vector2 pos, Vector2 vel, float life, float size,
                          Color color, float gravity, bool square)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->particles[i];
        if (!p->active) {
            p->active = true;
            p->position = pos;
            p->velocity = vel;
            p->life = p->maxLife = life;
            p->size = size;
            p->color = color;
            p->gravity = gravity;
            p->square = square;
            p->rotation = (float)GetRandomValue(0, 360);
            p->spin = (float)GetRandomValue(-360, 360);
            return;
        }
    }
}

/* Explosão de partículas em todas as direções (acerto/erro). */
static void SpawnBurst(Game *g, Vector2 pos, int count, Color color, float maxSpeed)
{
    for (int i = 0; i < count; i++) {
        float angle = (float)GetRandomValue(0, 359) * DEG2RAD;
        float speed = (float)GetRandomValue((int)(maxSpeed * 0.3f), (int)maxSpeed);
        Vector2 vel = { cosf(angle) * speed, sinf(angle) * speed };
        float life = 0.6f + GetRandomValue(0, 60) / 100.0f;
        SpawnParticle(g, pos, vel, life, (float)GetRandomValue(4, 8), color, 300.0f, GetRandomValue(0, 1));
    }
}

/* Confete colorido caindo do topo da tela (vitória). */
static void SpawnConfetti(Game *g, int count)
{
    static const Color palette[6] = {
        { 255, 90, 90, 255 }, { 255, 210, 60, 255 }, { 90, 220, 130, 255 },
        { 90, 170, 255, 255 }, { 200, 120, 255, 255 }, { 255, 150, 210, 255 }
    };
    for (int i = 0; i < count; i++) {
        Vector2 pos = { (float)GetRandomValue(0, SCREEN_WIDTH), -10.0f };
        Vector2 vel = { (float)GetRandomValue(-80, 80), (float)GetRandomValue(80, 220) };
        SpawnParticle(g, pos, vel, 4.5f, (float)GetRandomValue(6, 11),
                      palette[GetRandomValue(0, 5)], 60.0f, true);
    }
}

/* Chuva caindo (derrota). */
static void SpawnRain(Game *g, int count)
{
    for (int i = 0; i < count; i++) {
        Vector2 pos = { (float)GetRandomValue(0, SCREEN_WIDTH), -10.0f };
        Vector2 vel = { -30.0f, (float)GetRandomValue(350, 500) };
        SpawnParticle(g, pos, vel, 2.0f, 3.0f, (Color){ 150, 170, 220, 255 }, 200.0f, false);
    }
}

static void UpdateParticles(Game *g, float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->particles[i];
        if (!p->active) continue;

        p->life -= dt;
        if (p->life <= 0.0f) { p->active = false; continue; }

        p->velocity.y += p->gravity * dt;
        p->position.x += p->velocity.x * dt;
        p->position.y += p->velocity.y * dt;
        p->rotation   += p->spin * dt;
    }
}

static void DrawParticles(const Game *g)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &g->particles[i];
        if (!p->active) continue;

        float alpha = Clamp(p->life / (p->maxLife * 0.4f), 0.0f, 1.0f); /* some no final da vida */
        Color c = Fade(p->color, alpha);

        if (p->square) {
            Rectangle r = { p->position.x, p->position.y, p->size * 1.6f, p->size };
            DrawRectanglePro(r, (Vector2){ r.width / 2.0f, r.height / 2.0f }, p->rotation, c);
        } else {
            DrawCircleV(p->position, p->size * 0.7f, c);
        }
    }
}

static void SpawnFloatText(Game *g, const char *text, Vector2 pos, Color color)
{
    for (int i = 0; i < MAX_FLOAT_TEXTS; i++) {
        FloatText *f = &g->floatTexts[i];
        if (!f->active) {
            f->active = true;
            snprintf(f->text, sizeof(f->text), "%s", text);
            f->pos = pos;
            f->life = 1.3f;
            f->color = color;
            return;
        }
    }
}

static void UpdateFloatTexts(Game *g, float dt)
{
    for (int i = 0; i < MAX_FLOAT_TEXTS; i++) {
        FloatText *f = &g->floatTexts[i];
        if (!f->active) continue;
        f->life -= dt;
        f->pos.y -= 55.0f * dt;
        if (f->life <= 0.0f) f->active = false;
    }
}

static void DrawFloatTexts(const Game *g)
{
    for (int i = 0; i < MAX_FLOAT_TEXTS; i++) {
        const FloatText *f = &g->floatTexts[i];
        if (!f->active) continue;
        float alpha = Clamp(f->life * 2.0f, 0.0f, 1.0f);
        DrawTextCentered(f->text, f->pos.x + 2, f->pos.y + 2, 34, Fade(BLACK, 0.5f * alpha));
        DrawTextCentered(f->text, f->pos.x, f->pos.y, 34, Fade(f->color, alpha));
    }
}

static void InitBubbles(Game *g)
{
    for (int i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &g->bubbles[i];
        b->pos = (Vector2){ (float)GetRandomValue(0, SCREEN_WIDTH), (float)GetRandomValue(0, SCREEN_HEIGHT) };
        b->radius = (float)GetRandomValue(12, 42);
        b->speed = (float)GetRandomValue(12, 45);
        b->phase = (float)GetRandomValue(0, 628) / 100.0f;
        b->isQuestion = (i % 4 == 0);
    }
}

static void UpdateBubbles(Game *g, float dt)
{
    for (int i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &g->bubbles[i];
        b->pos.y -= b->speed * dt;
        if (b->pos.y < -b->radius * 2.0f) {           /* saiu por cima: volta por baixo */
            b->pos.y = SCREEN_HEIGHT + b->radius;
            b->pos.x = (float)GetRandomValue(0, SCREEN_WIDTH);
        }
    }
}

/* Cores do fundo mudam suavemente conforme o estado do jogo. */
static void UpdateBackground(Game *g, float dt)
{
    Color topTarget, bottomTarget;
    switch (g->state) {
        case STATE_WIN:     topTarget = (Color){ 20, 110, 100, 255 }; bottomTarget = (Color){ 70, 190, 120, 255 }; break;
        case STATE_LOSE:    topTarget = (Color){ 60, 20, 45, 255 };   bottomTarget = (Color){ 20, 15, 35, 255 };   break;
        case STATE_PLAYING: topTarget = (Color){ 25, 35, 100, 255 };  bottomTarget = (Color){ 70, 60, 160, 255 };  break;
        default:            topTarget = (Color){ 45, 20, 100, 255 };  bottomTarget = (Color){ 115, 50, 165, 255 }; break;
    }
    float t = Clamp(dt * 3.0f, 0.0f, 1.0f);
    g->bgTop = LerpColor(g->bgTop, topTarget, t);
    g->bgBottom = LerpColor(g->bgBottom, bottomTarget, t);
}

static void DrawBackground(const Game *g)
{
    /* Gradiente vertical (um pouco maior que a tela por causa do tremor). */
    DrawRectangleGradientV(-20, -20, SCREEN_WIDTH + 40, SCREEN_HEIGHT + 40, g->bgTop, g->bgBottom);

    for (int i = 0; i < MAX_BUBBLES; i++) {
        const Bubble *b = &g->bubbles[i];
        float x = b->pos.x + sinf(g->time * 0.8f + b->phase) * 18.0f;   /* balanço lateral */
        Vector2 c = { x, b->pos.y };

        DrawCircleV(c, b->radius, Fade(WHITE, 0.06f));
        DrawRing(c, b->radius - 2.0f, b->radius, 0.0f, 360.0f, 24, Fade(WHITE, 0.12f));
        if (b->isQuestion)
            DrawTextCentered("?", x, b->pos.y - b->radius * 0.7f, b->radius * 1.4f, Fade(WHITE, 0.16f));
    }
}

/* ============================================================================
 *  MASCOTE (feito só com formas da raylib)
 * ========================================================================== */
static void DrawMascot(const Game *g, Vector2 center, float scale)
{
    float t = g->time;
    Color dark = { 35, 30, 70, 255 };
    Vector2 mouse = GetMousePosition();

    /* Pulo: alegre = pulos contínuos; palpite recente = um pulinho. */
    float jump = 0.0f;
    if (g->mood == MOOD_HAPPY)          jump = fabsf(sinf(t * 6.0f)) * 28.0f * scale;
    else if (g->mascotBounce > 0.0f)    jump = sinf(g->mascotBounce * PI) * 40.0f * scale;

    float radius = 70.0f * scale;
    float rx = radius * (1.0f - 0.03f * sinf(t * 3.0f));   /* "respiração" */
    float ry = radius * (1.0f + 0.03f * sinf(t * 3.0f));
    float bx = center.x;
    float by = center.y - jump;

    if (g->mood == MOOD_SAD) { ry *= 0.92f; by += 8.0f * scale; }

    Color bodyColor;
    switch (g->mood) {
        case MOOD_HAPPY:   bodyColor = (Color){ 255, 190, 60, 255 };  break;
        case MOOD_WORRIED: bodyColor = (Color){ 130, 150, 240, 255 }; break;
        case MOOD_SAD:     bodyColor = (Color){ 120, 135, 170, 255 }; break;
        default:           bodyColor = (Color){ 90, 170, 255, 255 };  break;
    }

    /* Brilho atrás do mascote quando está feliz. */
    if (g->mood == MOOD_HAPPY) {
        float glow = 130.0f * scale + 10.0f * sinf(t * 4.0f);
        for (int i = 0; i < 5; i++)
            DrawCircleV((Vector2){ bx, by }, glow * (1.0f - i * 0.15f), Fade(GOLD, 0.08f));
    }

    /* Sombra no chão (diminui quando ele pula). */
    float shadowW = rx * (1.0f - Clamp(jump / (120.0f * scale), 0.0f, 0.5f));
    DrawEllipse((int)bx, (int)(center.y + radius + 10.0f * scale), shadowW, 12.0f * scale, Fade(BLACK, 0.3f));

    /* Pés */
    DrawEllipse((int)(bx - 28.0f * scale), (int)(by + ry - 2.0f), 20.0f * scale, 10.0f * scale, dark);
    DrawEllipse((int)(bx + 28.0f * scale), (int)(by + ry - 2.0f), 20.0f * scale, 10.0f * scale, dark);

    /* Antena balançando */
    Vector2 antennaBase = { bx, by - ry + 4.0f };
    Vector2 antennaTip = { bx + sinf(t * 3.0f) * 10.0f * scale, by - ry - 38.0f * scale };
    DrawLineEx(antennaBase, antennaTip, 4.0f * scale, dark);
    DrawCircleV(antennaTip, 9.0f * scale, (g->mood == MOOD_HAPPY) ? YELLOW : (Color){ 255, 100, 110, 255 });

    /* Corpo + reflexo */
    DrawEllipse((int)bx, (int)by, rx, ry, bodyColor);
    DrawEllipse((int)(bx - rx * 0.35f), (int)(by - ry * 0.5f), rx * 0.22f, ry * 0.12f, Fade(WHITE, 0.3f));

    /* Olhos (piscam de tempos em tempos e os olhos seguem o mouse) */
    bool blinking = fmodf(t, 3.2f) < 0.12f;
    float eyeY = by - 12.0f * scale;
    for (int side = -1; side <= 1; side += 2) {
        float ex = bx + side * 26.0f * scale;
        Vector2 eye = { ex, eyeY };

        if (blinking) {
            DrawLineEx((Vector2){ ex - 13.0f * scale, eyeY }, (Vector2){ ex + 13.0f * scale, eyeY }, 4.0f * scale, dark);
        } else {
            DrawCircleV(eye, 16.0f * scale, WHITE);
            Vector2 dir = Vector2Subtract(mouse, eye);
            float len = Vector2Length(dir);
            if (len > 0.0f) dir = Vector2Scale(dir, fminf(len, 6.0f * scale) / len);
            Vector2 pupil = Vector2Add(eye, dir);
            DrawCircleV(pupil, 8.0f * scale, dark);
            DrawCircleV((Vector2){ pupil.x - 2.5f * scale, pupil.y - 2.5f * scale }, 2.5f * scale, WHITE);
        }

        /* Sobrancelhas de preocupação */
        if (g->mood == MOOD_WORRIED || g->mood == MOOD_SAD) {
            Vector2 inner = { ex - side * 10.0f * scale, eyeY - 30.0f * scale };
            Vector2 outer = { ex + side * 10.0f * scale, eyeY - 22.0f * scale };
            DrawLineEx(inner, outer, 3.5f * scale, dark);
        }
    }

    /* Lágrima quando triste */
    if (g->mood == MOOD_SAD) {
        float drop = fmodf(t * 40.0f, 40.0f);
        DrawCircleV((Vector2){ bx - 26.0f * scale, eyeY + 18.0f * scale + drop * scale },
                    4.0f * scale, (Color){ 120, 190, 255, 220 });
    }

    /* Bochechas */
    if (g->mood == MOOD_IDLE || g->mood == MOOD_HAPPY) {
        DrawCircleV((Vector2){ bx - 42.0f * scale, by + 12.0f * scale }, 9.0f * scale, Fade((Color){ 255, 130, 160, 255 }, 0.6f));
        DrawCircleV((Vector2){ bx + 42.0f * scale, by + 12.0f * scale }, 9.0f * scale, Fade((Color){ 255, 130, 160, 255 }, 0.6f));
    }

    /* Boca (muda conforme o humor). Ângulos: 0 = direita, 90 = embaixo. */
    Vector2 mouth = { bx, by + 20.0f * scale };
    switch (g->mood) {
        case MOOD_HAPPY:
            DrawCircleSector(mouth, 20.0f * scale, 0.0f, 180.0f, 24, dark);
            DrawCircleSector((Vector2){ mouth.x, mouth.y + 6.0f * scale }, 11.0f * scale, 0.0f, 180.0f, 20, (Color){ 255, 120, 140, 255 });
            break;
        case MOOD_WORRIED:
            DrawCircleV((Vector2){ mouth.x, mouth.y + 8.0f * scale }, 7.0f * scale, dark);
            break;
        case MOOD_SAD:
            DrawRing((Vector2){ mouth.x, mouth.y + 22.0f * scale }, 12.0f * scale, 16.0f * scale, 200.0f, 340.0f, 16, dark);
            break;
        default:
            DrawRing(mouth, 11.0f * scale, 15.0f * scale, 20.0f, 160.0f, 16, dark);
            break;
    }
}

/* ============================================================================
 *  BOTÕES
 * ========================================================================== */
static void AddButton(Game *g, const char *label, const char *subLabel, Rectangle rect, Color color)
{
    if (g->buttonCount >= MAX_BUTTONS) return;
    Button *b = &g->buttons[g->buttonCount++];
    memset(b, 0, sizeof(*b));
    b->rect = rect;
    b->color = color;
    snprintf(b->label, sizeof(b->label), "%s", label);
    snprintf(b->subLabel, sizeof(b->subLabel), "%s", subLabel ? subLabel : "");
}

/* Cria os botões de cada tela. */
static void SetupButtons(Game *g, GameState s)
{
    g->buttonCount = 0;
    Color green = { 70, 190, 120, 255 }, blue = { 80, 130, 240, 255 }, red = { 230, 80, 90, 255 };

    switch (s) {
        case STATE_MENU:
            AddButton(g, "JOGAR",  NULL, (Rectangle){ 480, 300, 320, 68 }, green);
            AddButton(g, "REGRAS", NULL, (Rectangle){ 480, 384, 320, 68 }, blue);
            AddButton(g, "SAIR",   NULL, (Rectangle){ 480, 468, 320, 68 }, red);
            break;

        case STATE_DIFFICULTY:
            for (int i = 0; i < 3; i++) {
                const Difficulty *d = &DIFFICULTIES[i];
                char sub[64];
                snprintf(sub, sizeof(sub), "%d tentativas  -  penalidade %.2fx", d->totalAttempts, d->multiplier / d->divisor);
                char label[32];
                snprintf(label, sizeof(label), "%s", d->name);
                /* rótulo em maiúsculas simples (ASCII); acentos ficam como estão */
                for (int k = 0; label[k]; k++) if (label[k] >= 'a' && label[k] <= 'z') label[k] -= 32;
                AddButton(g, label, sub, (Rectangle){ 240, 150.0f + i * 100.0f, 420, 84 }, d->color);
            }
            AddButton(g, "VOLTAR", NULL, (Rectangle){ 350, 480, 200, 56 }, blue);
            break;

        case STATE_RULES:
            AddButton(g, "VOLTAR", NULL, (Rectangle){ 350, 530, 200, 56 }, blue);
            break;

        case STATE_PLAYING:
            AddButton(g, "CHUTAR", NULL, (Rectangle){ PANEL_CX + 20, 335, 180, 60 }, green);
            AddButton(g, "MENU",   NULL, (Rectangle){ 20, 20, 110, 40 }, blue);
            break;

        case STATE_WIN:
        case STATE_LOSE:
            AddButton(g, "JOGAR NOVAMENTE", NULL, (Rectangle){ 30, 540, 270, 56 }, green);
            AddButton(g, "VOLTAR AO MENU",  NULL, (Rectangle){ 315, 540, 270, 56 }, blue);
            AddButton(g, "SAIR",            NULL, (Rectangle){ 600, 540, 270, 56 }, red);
            break;
    }
}

/* Atualiza hover/clique. Retorna o índice do botão clicado ou -1. */
static int UpdateButtons(Game *g, float dt)
{
    Vector2 mouse = GetMousePosition();
    int clicked = -1;
    bool anyHover = false;
    float smooth = Clamp(dt * 14.0f, 0.0f, 1.0f);

    for (int i = 0; i < g->buttonCount; i++) {
        Button *b = &g->buttons[i];

        /* Entrada escalonada: cada botão aparece um pouco depois do anterior. */
        b->appear = Clamp((g->stateTime - i * 0.08f) / 0.35f, 0.0f, 1.0f);

        bool hovered = (b->appear >= 0.6f) && CheckCollisionPointRec(mouse, b->rect);
        b->hover = Lerp(b->hover, hovered ? 1.0f : 0.0f, smooth);
        b->press = Lerp(b->press, (hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ? 1.0f : 0.0f, smooth);

        if (hovered) anyHover = true;
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !g->changing && clicked < 0)
            clicked = i;
    }

    SetMouseCursor(anyHover ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
    return clicked;
}

static void DrawButton(const Button *b)
{
    if (b->appear <= 0.0f) return;

    float pop = EaseOutBack(b->appear);                                  /* quique na entrada */
    float scale = pop * (1.0f + 0.07f * b->hover - 0.06f * b->press);    /* cresce no hover, encolhe no clique */
    float alpha = Clamp(b->appear * 1.5f, 0.0f, 1.0f);

    float w = b->rect.width * scale, h = b->rect.height * scale;
    Rectangle r = {
        b->rect.x + (b->rect.width - w) / 2.0f,
        b->rect.y + (b->rect.height - h) / 2.0f - b->hover * 3.0f,
        w, h
    };

    Color base = LerpColor(b->color, WHITE, 0.25f * b->hover);

    DrawRectangleRounded((Rectangle){ r.x + 4, r.y + 8, r.width, r.height }, 0.35f, 10, Fade(BLACK, 0.35f * alpha)); /* sombra */
    DrawRectangleRounded(r, 0.35f, 10, Fade(base, alpha));
    DrawRectangleRounded((Rectangle){ r.x + 6, r.y + 5, r.width - 12, r.height * 0.42f }, 0.5f, 10, Fade(WHITE, 0.18f * alpha)); /* brilho */

    float cx = r.x + r.width / 2.0f;
    float labelSize = FitFontSize(b->label, (b->subLabel[0] ? 30.0f : 30.0f) * scale, r.width - 24.0f);

    if (b->subLabel[0]) {
        float subSize = FitFontSize(b->subLabel, 17.0f * scale, r.width - 24.0f);
        DrawTextCentered(b->label, cx + 2, r.y + r.height * 0.16f + 2, labelSize, Fade(BLACK, 0.3f * alpha));
        DrawTextCentered(b->label, cx, r.y + r.height * 0.16f, labelSize, Fade(WHITE, alpha));
        DrawTextCentered(b->subLabel, cx, r.y + r.height * 0.64f, subSize, Fade(WHITE, 0.9f * alpha));
    } else {
        float ty = r.y + (r.height - labelSize) / 2.0f;
        DrawTextCentered(b->label, cx + 2, ty + 2, labelSize, Fade(BLACK, 0.3f * alpha));
        DrawTextCentered(b->label, cx, ty, labelSize, Fade(WHITE, alpha));
    }
}

static void DrawButtons(const Game *g)
{
    for (int i = 0; i < g->buttonCount; i++) DrawButton(&g->buttons[i]);
}

/* ============================================================================
 *  LÓGICA DO JOGO (aproveitada do código original)
 * ========================================================================== */

static void SetMessage(Game *g, const char *text, const char *sub, Color color)
{
    snprintf(g->message, sizeof(g->message), "%s", text);
    snprintf(g->subMessage, sizeof(g->subMessage), "%s", sub ? sub : "");
    g->messageColor = color;
    g->messageTime = 0.0f;
}

static void ClearInput(Game *g)
{
    g->inputText[0] = '\0';
    g->inputLength = 0;
}

/* Equivale ao "pontosperdidos" do código original. */
static float CalculatePointsLost(int guess, int secret, float multiplier, float divisor)
{
#if USE_ORIGINAL_PENALTY_FORMULA
    return (float)abs((int)(guess - secret * multiplier)) / divisor;
#else
    return fabsf((float)(guess - secret)) * multiplier / divisor;
#endif
}

/* Prepara uma nova rodada (equivale ao início do main original). */
static void StartNewGame(Game *g)
{
    const Difficulty *d = &DIFFICULTIES[g->difficultyIndex];

    g->secretNumber = GetRandomValue(MIN_NUMBER, MAX_NUMBER);   /* (rand() % 100) + 1 */
    g->points = START_POINTS;
    g->displayedPoints = START_POINTS;
    g->attemptsUsed = 0;
    g->totalAttempts = d->totalAttempts;
    g->divisor = d->divisor;
    g->multiplier = d->multiplier;
    g->lostByPoints = false;
    memset(g->history, 0, sizeof(g->history));

    ClearInput(g);
    g->lowBound = MIN_NUMBER;  g->highBound = MAX_NUMBER;
    g->shownLow = (float)MIN_NUMBER;  g->shownHigh = (float)MAX_NUMBER;

    SetMessage(g, "", "", WHITE);
    g->shake = g->inputShake = g->pipPop = 0.0f;
    g->endPending = false;
    g->mood = MOOD_IDLE;
    g->mascotBounce = 0.0f;
    ClearEffects(g);
}

/* Processa o palpite digitado (equivale ao corpo do "for" do original). */
static void SubmitGuess(Game *g)
{
    if (g->endPending) return;

    /* Nada digitado */
    if (g->inputLength == 0 || strcmp(g->inputText, "-") == 0) {
        SetMessage(g, "Digite um número primeiro!", "", ORANGE);
        g->inputShake = 10.0f;
        return;
    }

    int guess = atoi(g->inputText);

    /* Validações do original: não consomem tentativa */
    if (guess < 0) {
        SetMessage(g, "Você não pode chutar números negativos", "", ORANGE);
        g->inputShake = 10.0f;  g->shake = 5.0f;
        ClearInput(g);
        return;
    }
    if (guess > MAX_NUMBER) {
        SetMessage(g, "Você não pode chutar números maiores que 100", "", ORANGE);
        g->inputShake = 10.0f;  g->shake = 5.0f;
        ClearInput(g);
        return;
    }

    /* Palpite válido: gasta uma tentativa */
    GuessEntry *entry = &g->history[g->attemptsUsed];
    g->attemptsUsed++;
    entry->value = guess;
    entry->age = 0.0f;
    g->pipPop = 1.0f;
    g->mascotBounce = 1.0f;
    ClearInput(g);

    Vector2 effectPos = { PANEL_CX - 100.0f, 365.0f };   /* centro da caixa de texto */

    if (guess == g->secretNumber) {
        /* ACERTOU */
        entry->result = GUESS_CORRECT;
        SetMessage(g, "Parabéns, você acertou!", "", (Color){ 120, 255, 160, 255 });
        g->mood = MOOD_HAPPY;
        SpawnBurst(g, effectPos, 70, GOLD, 520.0f);
        SpawnConfetti(g, 80);
        g->endPending = true;
        g->endDelay = 1.6f;
        g->endState = STATE_WIN;
        return;
    }

    /* ERROU: dica de maior/menor + atualiza faixa possível */
    const char *hint;
    if (guess > g->secretNumber) {
        entry->result = GUESS_TOO_HIGH;
        hint = "Seu chute foi MAIOR que o número secreto";
        if (guess - 1 < g->highBound) g->highBound = guess - 1;
    } else {
        entry->result = GUESS_TOO_LOW;
        hint = "Seu chute foi MENOR que o número secreto";
        if (guess + 1 > g->lowBound) g->lowBound = guess + 1;
    }

    float lost = CalculatePointsLost(guess, g->secretNumber, g->multiplier, g->divisor);
    g->points -= lost;

    char sub[64];
    snprintf(sub, sizeof(sub), "Pontos perdidos: %.1f", lost);
    SetMessage(g, hint, sub, (Color){ 255, 140, 120, 255 });

    char floating[24];
    snprintf(floating, sizeof(floating), "-%.1f", lost);
    SpawnFloatText(g, floating, (Vector2){ PANEL_CX, 200.0f }, (Color){ 255, 110, 110, 255 });
    SpawnBurst(g, effectPos, 25, (Color){ 255, 110, 110, 255 }, 300.0f);

    g->shake = 12.0f;
    g->mood = MOOD_WORRIED;
    g->moodTimer = 1.0f;

    /* Condições de derrota (iguais ao original) */
    if (g->points <= 0.0f) {
        g->points = 0.0f;
        g->lostByPoints = true;
        g->endPending = true;
    } else if (g->attemptsUsed >= g->totalAttempts) {
        g->endPending = true;
    }

    if (g->endPending) {
        g->endDelay = 1.4f;
        g->endState = STATE_LOSE;
        g->mood = MOOD_SAD;
    }
}

/* Lê os caracteres digitados (só dígitos, e '-' no começo). */
static void HandleTextInput(Game *g)
{
    int key = GetCharPressed();
    while (key > 0) {
        bool isDigit = (key >= '0' && key <= '9');
        bool isMinus = (key == '-' && g->inputLength == 0);
        if ((isDigit || isMinus) && g->inputLength < 4) {
            g->inputText[g->inputLength++] = (char)key;
            g->inputText[g->inputLength] = '\0';
        }
        key = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) && g->inputLength > 0) {
        g->inputText[--g->inputLength] = '\0';
    }
}

/* ============================================================================
 *  ESTADOS E TRANSIÇÕES
 * ========================================================================== */

/* Pede a troca de tela: escurece, troca e clareia (feito em UpdateTransition). */
static void ChangeState(Game *g, GameState newState)
{
    if (g->changing) return;
    g->nextState = newState;
    g->changing = true;
}

/* Executado no instante em que a tela está totalmente escura. */
static void EnterState(Game *g, GameState newState)
{
    g->state = newState;
    g->stateTime = 0.0f;
    g->emitTimer = 0.0f;
    g->scoreCount = 0.0f;
    SetupButtons(g, newState);

    switch (newState) {
        case STATE_PLAYING:
            StartNewGame(g);
            break;
        case STATE_WIN:
            g->mood = MOOD_HAPPY;
            SpawnBurst(g, (Vector2){ SCREEN_WIDTH / 2.0f, 300.0f }, 80, GOLD, 550.0f);
            break;
        case STATE_LOSE:
            g->mood = MOOD_SAD;
            break;
        default:
            g->mood = MOOD_IDLE;
            ClearEffects(g);
            break;
    }
}

static void UpdateTransition(Game *g, float dt)
{
    const float speed = 3.5f;
    if (g->changing) {
        g->fade += dt * speed;
        if (g->fade >= 1.0f) {
            g->fade = 1.0f;
            g->changing = false;
            EnterState(g, g->nextState);
        }
    } else if (g->fade > 0.0f) {
        g->fade = fmaxf(0.0f, g->fade - dt * speed);
    }
}

/* ------------------------------- Atualização ------------------------------ */
static void UpdateMenu(Game *g, int clicked)
{
    if (clicked == 0) ChangeState(g, STATE_DIFFICULTY);
    else if (clicked == 1) ChangeState(g, STATE_RULES);
    else if (clicked == 2) g->shouldQuit = true;
}

static void UpdateDifficulty(Game *g, int clicked)
{
    if (clicked >= 0 && clicked <= 2) {
        g->difficultyIndex = clicked;
        ChangeState(g, STATE_PLAYING);
    } else if (clicked == 3 || IsKeyPressed(KEY_ESCAPE)) {
        ChangeState(g, STATE_MENU);
    }
}

static void UpdateRules(Game *g, int clicked)
{
    if (clicked == 0 || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER))
        ChangeState(g, STATE_MENU);
}

static void UpdatePlaying(Game *g, float dt, int clicked)
{
    if (!g->endPending && !g->changing) {
        HandleTextInput(g);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || clicked == 0) SubmitGuess(g);
    }
    if (clicked == 1 || IsKeyPressed(KEY_ESCAPE)) ChangeState(g, STATE_MENU);

    /* Animações suaves dos indicadores */
    float smooth = Clamp(dt * 6.0f, 0.0f, 1.0f);
    g->displayedPoints = Lerp(g->displayedPoints, g->points, smooth);
    g->shownLow  = Lerp(g->shownLow,  (float)g->lowBound,  smooth);
    g->shownHigh = Lerp(g->shownHigh, (float)g->highBound, smooth);
    g->pipPop = fmaxf(0.0f, g->pipPop - dt * 3.0f);

    for (int i = 0; i < g->attemptsUsed; i++) g->history[i].age += dt;

    /* Depois de acertar/perder, espera um pouquinho antes de mudar de tela */
    if (g->endPending) {
        g->endDelay -= dt;
        if (g->endDelay <= 0.0f) {
            g->endPending = false;
            ChangeState(g, g->endState);
        }
    }
}

static void UpdateEndScreen(Game *g, float dt, int clicked, bool won)
{
    if (clicked == 0 || IsKeyPressed(KEY_R)) ChangeState(g, STATE_PLAYING);   /* mesma dificuldade */
    else if (clicked == 1 || IsKeyPressed(KEY_ESCAPE)) ChangeState(g, STATE_MENU);
    else if (clicked == 2) g->shouldQuit = true;

    /* Pontuação "sobe" contando depois de um pequeno atraso */
    if (g->stateTime > 0.8f)
        g->scoreCount += (g->points - g->scoreCount) * fminf(1.0f, dt * 4.0f);

    /* Confete (vitória) ou chuva (derrota) contínuos */
    g->emitTimer -= dt;
    if (g->emitTimer <= 0.0f) {
        g->emitTimer = 0.05f;
        if (won) SpawnConfetti(g, 3);
        else     SpawnRain(g, 3);
    }
}

/* Atualização geral, chamada uma vez por frame. */
static void UpdateGame(Game *g, float dt)
{
    g->time += dt;
    g->stateTime += dt;
    g->messageTime += dt;

    /* Decaimento dos efeitos */
    g->shake = fmaxf(0.0f, g->shake - dt * 40.0f);
    g->inputShake = fmaxf(0.0f, g->inputShake - dt * 30.0f);
    g->mascotBounce = fmaxf(0.0f, g->mascotBounce - dt * 2.0f);

    if (g->mood == MOOD_WORRIED) {
        g->moodTimer -= dt;
        if (g->moodTimer <= 0.0f) g->mood = MOOD_IDLE;
    }

    UpdateBackground(g, dt);
    UpdateBubbles(g, dt);
    UpdateParticles(g, dt);
    UpdateFloatTexts(g, dt);
    UpdateTransition(g, dt);

    int clicked = UpdateButtons(g, dt);

    switch (g->state) {
        case STATE_MENU:       UpdateMenu(g, clicked); break;
        case STATE_DIFFICULTY: UpdateDifficulty(g, clicked); break;
        case STATE_RULES:      UpdateRules(g, clicked); break;
        case STATE_PLAYING:    UpdatePlaying(g, dt, clicked); break;
        case STATE_WIN:        UpdateEndScreen(g, dt, clicked, true); break;
        case STATE_LOSE:       UpdateEndScreen(g, dt, clicked, false); break;
    }
}

/* ============================================================================
 *  DESENHO DAS TELAS
 * ========================================================================== */
static void DrawMenu(const Game *g)
{
    float alpha = EaseOutCubic(g->stateTime / 0.6f);

    DrawWaveText("JOGO DA", SCREEN_WIDTH / 2.0f, 40, 44, Fade(WHITE, alpha), false, g->time, 6.0f);
    DrawWaveText("ADIVINHAÇÃO", SCREEN_WIDTH / 2.0f, 95, 80, Fade(WHITE, alpha), true, g->time, 8.0f);

    DrawTypewriter("Será que você descobre o número secreto?", SCREEN_WIDTH / 2.0f, 205, 24,
                   Fade(WHITE, 0.9f), g->stateTime - 0.6f, 30.0f, true);

    DrawMascot(g, (Vector2){ 230, 400 }, 1.2f);

    DrawTextCentered("Use o mouse para clicar nos botões", SCREEN_WIDTH / 2.0f, 610, 16, Fade(WHITE, 0.5f));
}

static void DrawDifficulty(const Game *g)
{
    float alpha = EaseOutCubic(g->stateTime / 0.5f);
    DrawWaveText("ESCOLHA O NÍVEL", SCREEN_WIDTH / 2.0f, 50, 52, Fade(WHITE, alpha), true, g->time, 5.0f);
    DrawTextCentered("Quanto maior o nível, menos chances e mais pontos perdidos", SCREEN_WIDTH / 2.0f, 112, 18, Fade(WHITE, 0.8f * alpha));
}

static void DrawRules(const Game *g)
{
    static const char *lines[] = {
        "1. O jogo possui 3 dificuldades: quanto maior o nível,",
        "    menos tentativas e mais pontos são retirados.",
        "2. Você deve chutar números de 1 a 100.",
        "    Números negativos são inválidos (sem gastar tentativa).",
        "3. Você começa com 100 pontos. A cada erro você perde",
        "    pontos conforme a distância até o número secreto.",
        "4. Se os pontos chegarem a 0 ou as tentativas acabarem,",
        "    você perde. Se acertar, você vence!"
    };
    const int lineCount = 8;

    float alpha = EaseOutCubic(g->stateTime / 0.5f);
    DrawWaveText("COMO JOGAR", SCREEN_WIDTH / 2.0f, 45, 56, Fade(WHITE, alpha), true, g->time, 5.0f);

    DrawRectangleRounded((Rectangle){ 50, 130, 800, 370 }, 0.05f, 12, Fade(BLACK, 0.3f * alpha));

    float startTime = 0.4f;
    for (int i = 0; i < lineCount; i++) {
        DrawTypewriter(lines[i], 85, 155.0f + i * 40.0f, 21, WHITE, g->stateTime - startTime, 60.0f, false);
        startTime += (float)strlen(lines[i]) / 60.0f;
    }
}

/* Barra "faixa possível": mostra entre quais números o segredo está. */
static void DrawRangeBar(const Game *g, float x, float y, float width)
{
    float lowX  = x + ((g->shownLow  - 1.0f) / 99.0f) * width;
    float highX = x + ((g->shownHigh - 1.0f) / 99.0f) * width;
    float segmentW = fmaxf(highX - lowX, 10.0f);

    DrawTextF("1", x, y - 18, 14, Fade(WHITE, 0.6f));
    DrawTextF("100", x + width - MeasureTextF("100", 14), y - 18, 14, Fade(WHITE, 0.6f));

    DrawRectangleRounded((Rectangle){ x, y, width, 10 }, 0.5f, 8, Fade(WHITE, 0.15f));
    DrawRectangleRounded((Rectangle){ lowX, y - 3, segmentW, 16 }, 0.5f, 8, (Color){ 90, 230, 150, 255 });

    /* Marcadores dos palpites já feitos */
    for (int i = 0; i < g->attemptsUsed; i++) {
        float v = Clamp((float)g->history[i].value, 1.0f, 100.0f);
        float mx = x + ((v - 1.0f) / 99.0f) * width;
        Color c = (g->history[i].result == GUESS_CORRECT) ? GOLD :
                  (g->history[i].result == GUESS_TOO_HIGH) ? (Color){ 235, 110, 90, 255 } : (Color){ 90, 150, 240, 255 };
        DrawCircleV((Vector2){ mx, y + 5 }, 5.0f, c);
    }

    DrawTextCentered(TextFormat("O número está entre %d e %d", g->lowBound, g->highBound),
                     x + width / 2.0f, y + 22, 18, Fade(WHITE, 0.85f));
}

static void DrawPlaying(const Game *g)
{
    const Difficulty *d = &DIFFICULTIES[g->difficultyIndex];

    /* Painel central e selo de dificuldade */
    DrawRectangleRounded((Rectangle){ PANEL_X - 10, 70, PANEL_W + 20, 550 }, 0.05f, 12, Fade(BLACK, 0.28f));
    DrawRectangleRounded((Rectangle){ 20, 75, 240, 44 }, 0.4f, 10, d->color);
    DrawTextCentered(TextFormat("Nível %s", d->name), 140, 86, 24, WHITE);

    DrawMascot(g, (Vector2){ 140, 400 }, 1.0f);

    /* Cabeçalho */
    DrawTypewriter("Chute um número de 1 a 100", PANEL_CX, 85, 26, WHITE, g->stateTime - 0.3f, 40.0f, true);

    /* Indicador de tentativas: texto + bolinhas */
    DrawTextF("Tentativas", PANEL_X, 125, 20, WHITE);
    const char *attemptsText = TextFormat("%d/%d", g->attemptsUsed, g->totalAttempts);
    DrawTextF(attemptsText, PANEL_X + PANEL_W - MeasureTextF(attemptsText, 20), 125, 20, WHITE);

    float spacing = fminf(30.0f, (PANEL_W - 20.0f) / (float)g->totalAttempts);
    float startX = PANEL_CX - spacing * (g->totalAttempts - 1) / 2.0f;
    for (int i = 0; i < g->totalAttempts; i++) {
        bool used = (i < g->attemptsUsed);
        float pop = (i == g->attemptsUsed - 1) ? g->pipPop : 0.0f;
        Color c = Fade(WHITE, 0.25f);
        if (used) c = (g->history[i].result == GUESS_CORRECT) ? (Color){ 90, 230, 140, 255 } : ORANGE;
        DrawCircleV((Vector2){ startX + i * spacing, 165 }, 9.0f + pop * 7.0f, c);
    }

    /* Barra de pontos */
    DrawTextF("Pontos", PANEL_X, 190, 20, WHITE);
    const char *pointsText = TextFormat("%.0f", g->displayedPoints);
    DrawTextF(pointsText, PANEL_X + PANEL_W - MeasureTextF(pointsText, 24), 187, 24, GOLD);

    Rectangle barBg = { PANEL_X, 218, PANEL_W, 22 };
    DrawRectangleRounded(barBg, 0.5f, 10, Fade(WHITE, 0.15f));
    float ratio = Clamp(g->displayedPoints / START_POINTS, 0.0f, 1.0f);
    float fillW = PANEL_W * ratio;
    if (fillW >= 12.0f) {
        Color barColor = (ratio > 0.5f)
            ? LerpColor((Color){ 250, 220, 60, 255 }, (Color){ 80, 220, 130, 255 }, (ratio - 0.5f) * 2.0f)
            : LerpColor((Color){ 240, 80, 80, 255 }, (Color){ 250, 220, 60, 255 }, ratio * 2.0f);
        DrawRectangleRounded((Rectangle){ PANEL_X, 218, fillW, 22 }, 0.5f, 10, barColor);
        DrawRectangleRounded((Rectangle){ PANEL_X + 4, 221, fillW - 8, 7 }, 0.5f, 8, Fade(WHITE, 0.25f));
    }

    /* Faixa possível */
    DrawRangeBar(g, PANEL_X, 268, PANEL_W);

    /* Caixa de digitação (treme quando o chute é inválido) */
    float shakeX = sinf(g->time * 60.0f) * g->inputShake;
    Rectangle box = { PANEL_CX - 200 + shakeX, 335, 200, 60 };
    float pulse = 0.5f + 0.5f * sinf(g->time * 4.0f);
    Color border = LerpColor(LerpColor(SKYBLUE, WHITE, pulse * 0.5f), RED, Clamp(g->inputShake / 8.0f, 0.0f, 1.0f));
    DrawRectangleRounded((Rectangle){ box.x - 3, box.y - 3, box.width + 6, box.height + 6 }, 0.3f, 10, border);
    DrawRectangleRounded(box, 0.3f, 10, (Color){ 25, 20, 60, 255 });

    float boxCx = box.x + box.width / 2.0f;
    if (g->inputLength > 0) {
        float tw = MeasureTextF(g->inputText, 40);
        DrawTextF(g->inputText, boxCx - tw / 2.0f, box.y + 10, 40, WHITE);
        if (fmodf(g->time, 1.0f) < 0.5f)   /* cursor piscando */
            DrawRectangle((int)(boxCx + tw / 2.0f + 4), (int)box.y + 12, 3, 36, SKYBLUE);
    } else {
        DrawTextCentered("?", boxCx, box.y + 10, 40, Fade(WHITE, 0.3f));
    }

    /* Mensagem de feedback (máquina de escrever) */
    if (g->message[0] != '\0') {
        float size = FitFontSize(g->message, 26.0f, PANEL_W);
        DrawTypewriter(g->message, PANEL_CX, 415, size, g->messageColor, g->messageTime, 60.0f, true);
    }
    if (g->subMessage[0] != '\0')
        DrawTextCentered(g->subMessage, PANEL_CX, 448, 20, Fade(WHITE, Clamp((g->messageTime - 0.4f) * 3.0f, 0.0f, 1.0f)));

    /* Histórico de chutes: "chips" que aparecem com quique */
    const float chipW = 66.0f, chipH = 36.0f, chipGap = 6.0f;
    float chipsStartX = PANEL_CX - (8 * chipW + 7 * chipGap) / 2.0f;
    for (int i = 0; i < g->attemptsUsed; i++) {
        const GuessEntry *e = &g->history[i];
        int row = i / 8, col = i % 8;
        float cx = chipsStartX + col * (chipW + chipGap) + chipW / 2.0f;
        float cy = 490.0f + row * 42.0f + chipH / 2.0f;
        float s = EaseOutBack(e->age / 0.35f);

        Color c = (e->result == GUESS_CORRECT) ? (Color){ 70, 190, 120, 255 } :
                  (e->result == GUESS_TOO_HIGH) ? (Color){ 225, 100, 85, 255 } : (Color){ 80, 130, 230, 255 };
        Rectangle r = { cx - chipW / 2.0f * s, cy - chipH / 2.0f * s, chipW * s, chipH * s };
        DrawRectangleRounded(r, 0.4f, 8, c);
        DrawTextF(TextFormat("%d", e->value), r.x + 7.0f * s, cy - 10.0f * s, 20.0f * s, WHITE);

        Vector2 arrow = { r.x + r.width - 11.0f * s, cy };
        if (e->result == GUESS_TOO_HIGH)     DrawPoly(arrow, 3, 6.0f * s, 90.0f, WHITE);   /* seta p/ baixo: "vá mais baixo" */
        else if (e->result == GUESS_TOO_LOW) DrawPoly(arrow, 3, 6.0f * s, -90.0f, WHITE);  /* seta p/ cima */
        else                                 DrawCircleV(arrow, 5.0f * s, WHITE);
    }

    DrawTextF("ESC: voltar ao menu", 20, 610, 16, Fade(WHITE, 0.5f));
}

static void DrawEndScreen(const Game *g, bool won)
{
    Color titleColor = won ? GOLD : (Color){ 255, 90, 90, 255 };
    const char *title = won ? "VOCÊ VENCEU!" : "GAME OVER";

    float pop = Clamp(EaseOutBack(g->stateTime / 0.7f), 0.05f, 1.15f);
    DrawWaveText(title, SCREEN_WIDTH / 2.0f, 55, 70.0f * pop, titleColor, false, g->time, won ? 6.0f : 2.0f);

    const char *reason = won ? "Parabéns, você acertou!"
                             : (g->lostByPoints ? "Seus pontos chegaram a 0. Você perdeu! Tente novamente!"
                                                : "As tentativas acabaram. Você perdeu! Tente novamente!");
    DrawTypewriter(reason, SCREEN_WIDTH / 2.0f, 140, 22, WHITE, g->stateTime - 0.6f, 35.0f, true);

    DrawMascot(g, (Vector2){ SCREEN_WIDTH / 2.0f, 300 }, 1.0f);

    float fadeIn = Clamp((g->stateTime - 0.8f) * 2.0f, 0.0f, 1.0f);
    DrawTextCentered(TextFormat("O número secreto era %d", g->secretNumber), SCREEN_WIDTH / 2.0f, 402, 28, Fade(WHITE, fadeIn));
    DrawTextCentered(TextFormat("Você fez %.1f pontos", g->scoreCount), SCREEN_WIDTH / 2.0f, 440, 34, Fade(GOLD, fadeIn));
    DrawTextCentered(TextFormat("Tentativas usadas: %d de %d", g->attemptsUsed, g->totalAttempts),
                     SCREEN_WIDTH / 2.0f, 488, 20, Fade(WHITE, 0.85f * fadeIn));

    DrawTextCentered("R: jogar de novo   |   ESC: menu", SCREEN_WIDTH / 2.0f, 612, 16, Fade(WHITE, 0.5f));
}

/* Desenho geral, chamado uma vez por frame. */
static void DrawGame(const Game *g)
{
    BeginDrawing();
    ClearBackground(BLACK);

    /* Tremor de tela: a câmera balança aleatoriamente enquanto "shake" > 0 */
    Camera2D camera = { 0 };
    camera.zoom = 1.0f;
    camera.offset = (Vector2){ GetRandomValue(-100, 100) / 100.0f * g->shake,
                               GetRandomValue(-100, 100) / 100.0f * g->shake };

    BeginMode2D(camera);
        DrawBackground(g);

        switch (g->state) {
            case STATE_MENU:       DrawMenu(g); break;
            case STATE_DIFFICULTY: DrawDifficulty(g); break;
            case STATE_RULES:      DrawRules(g); break;
            case STATE_PLAYING:    DrawPlaying(g); break;
            case STATE_WIN:        DrawEndScreen(g, true); break;
            case STATE_LOSE:       DrawEndScreen(g, false); break;
        }

        DrawButtons(g);
        DrawParticles(g);
        DrawFloatTexts(g);
    EndMode2D();

    /* Cortina preta usada nas transições entre telas */
    if (g->fade > 0.0f) DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, EaseOutCubic(g->fade)));

    EndDrawing();
}

/* ============================================================================
 *  INICIALIZAÇÃO E MAIN
 * ========================================================================== */
static void InitGame(Game *g)
{
    memset(g, 0, sizeof(*g));
    g->bgTop = (Color){ 45, 20, 100, 255 };
    g->bgBottom = (Color){ 115, 50, 165, 255 };
    InitBubbles(g);
    EnterState(g, STATE_MENU);
    g->fade = 1.0f;     /* começa preto e clareia (fade-in inicial) */
}

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Jogo da Adivinhação");
    
    //Ícone da janela (desenhado por código)
    Image icon = GenImageColor(64, 64, BLANK);
    ImageDrawCircle(&icon, 32, 32, 28, (Color){ 90, 170, 255, 255 });   /* corpo */
    ImageDrawCircle(&icon, 22, 28, 7, WHITE);                           /* olho esquerdo */
    ImageDrawCircle(&icon, 42, 28, 7, WHITE);                           /* olho direito */
    ImageDrawCircle(&icon, 22, 28, 3, (Color){ 35, 30, 70, 255 });      /* pupilas */
    ImageDrawCircle(&icon, 42, 28, 3, (Color){ 35, 30, 70, 255 });
    SetWindowIcon(icon);
    UnloadImage(icon);
    /*-----------------------------------------------------------------*/
    
    SetExitKey(KEY_NULL);                       /* ESC volta ao menu em vez de fechar */
    SetTargetFPS(60);
    SetRandomSeed((unsigned int)time(NULL));    /* substitui srand(time(0)) */

    Game game;
    InitGame(&game);

    while (!game.shouldQuit && !WindowShouldClose()) {
        float dt = fminf(GetFrameTime(), 0.05f);   /* limita saltos de tempo (ex.: janela arrastada) */
        UpdateGame(&game, dt);
        DrawGame(&game);
    }

    CloseWindow();
    return 0;
}

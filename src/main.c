#include "raylib.h"
#include "raymath.h"

#define MIN(x, y) ((x < y) ? x : y)
#define MAX(x, y) ((x > y) ? x : y)
#define NULL ((void *)0)

#define FMTBOOL(b) ((b) ? "On" : "Off")

#define MAXBOXTIMER 2100.0

#define NATIVE_W 273.0
#define NATIVE_H 153.0

static Texture2D ATLAS;
static Texture2D DEATH;
static Sound SND_WIND;
static Sound SND_CLICK;
static Sound SND_JUMPSCARE;
static Sound SND_POPSTATIC;

#define MAX_NOTIFS 8

struct State {
    double boxtimer;
    int difficulty;
    float time_at_zero;

    float windspeed;
    bool winding;

    /*
struct {
    struct Notification {
        char *text;
        int size;
        double timer;
        double duration;
        int id;
    } list[MAX_NOTIFS];
    unsigned int count;
} notifications;
    */
    struct Notification {
        char *text;
        int size;
        double timer;
        double duration;
    } notification;

    bool music_playing;
    char *custom_music_path;
    Music custom_music;

    bool dead;

    int win_w, win_h;
    float scale_w, scale_h;

    // fun stuff
    bool show_time_to_unwind;
    bool unlock_difficulty;
    bool autowind;
    bool win_transparent;
};

#define INFO 0
#define HELP1 1
#define HELP2 2

void set_notification(struct State *state, const char *text, int size, double duration) {
    char *alloctext = MemAlloc(TextLength(text) + 1);
    TextCopy(alloctext, text);

    if (state->notification.text != NULL) {
        MemFree(state->notification.text);
    }

    state->notification = (struct Notification){alloctext, size, 0.0, duration};
}

void update_notification(struct State *state) {
    state->notification.timer += GetFrameTime();

    if (state->notification.timer >= state->notification.duration) {
        MemFree(state->notification.text);
        state->notification.text = NULL;
    }
}

// un-used because overkill for what i need, but kept in case i do need it in the future.
/*
void add_notification(struct State *state, const char *text, int size, double duration, int id) {
    if (state->notifications.count >= MAX_NOTIFS) {
        return;
    }

    char *alloctext = MemAlloc(TextLength(text) + 1);
    TextCopy(alloctext, text);

    struct Notification notif = (struct Notification){alloctext, size, 0.0, duration, id};

    for (int i = 0; i < state->notifications.count; i++) {
        if (state->notifications.list[i].id == id) {
            MemFree(state->notifications.list[i].text);
            state->notifications.list[i] = notif;
            return;
        }
    }

    state->notifications.list[state->notifications.count++] = notif;
}

void update_notifications(struct State *state) {
    if (state->notifications.count == 0) {
        return;
    }

    state->notifications.list[0].timer += GetFrameTime();

    struct Notification cur = state->notifications.list[0];
    if (cur.timer >= cur.duration) {
        MemFree(cur.text);
        for (int i = 0; i < state->notifications.count - 1; i++) {
            state->notifications.list[i] = state->notifications.list[i + 1];
        }
        --state->notifications.count;
    }
}
*/

// the new exponential formula
double decrease_from(int difficulty) {
    double diff = (double)difficulty;
    return ((diff * diff) / 3.2) + 1.0;
}

Color get_difficulty_color(int difficulty) {
    double diff = (double)difficulty;
    if (difficulty <= 20) {
        return (Color){255, 255, 255, 255};
    } else if (difficulty > 20) {
        double m = 255.0 / 80.0;
        unsigned char x = (unsigned char)Clamp(255.0 - diff * m, 0.0, 255.0);
        return (Color){.r = 255, .g = x, .b = 255, .a = 255};
    }
    return (Color){0, 0, 0, 255};
}

static Rectangle STATUS_RECTS[21] = {0};

void init_status_rects() {
    const int statusw = 55;
    const int statusy = 66;
    for (int i = 0; i < 21; i++) {
        STATUS_RECTS[i] = (Rectangle){1 + (i * statusw), statusy, statusw - 1, statusw - 1};
    }
}

void init(struct State *state) {
    SetTargetFPS(60);
    SetConfigFlags(FLAG_WINDOW_TRANSPARENT);
    InitWindow(0, 0, "Don't forget to wind it!");
    SetWindowState(FLAG_WINDOW_ALWAYS_RUN | FLAG_WINDOW_RESIZABLE);
    float _w = (float)GetScreenWidth() / 5.0;
    float _h = (float)GetScreenHeight() / 5.0;

    if (_w <= 300.0 || _h <= 200.0) {
        _w = NATIVE_W;
        _h = NATIVE_H;
    } else {
        _w -= (_w - NATIVE_W * (int)(_w / NATIVE_W));
        _h -= (_h - NATIVE_H * (int)(_h / NATIVE_H));
    }

    TraceLog(LOG_INFO, TextFormat("%f, %f", _w, _h));

    state->win_w = (int)_w;
    state->win_h = (int)_h;

    state->scale_w = state->win_w / NATIVE_W;
    state->scale_h = state->win_h / NATIVE_H;
    SetWindowPosition((GetScreenWidth() / 2) - (state->win_w / 2),
                      (GetScreenHeight() / 2) - (state->win_h / 2));
    SetWindowSize(state->win_w, state->win_h);

    Image icon = LoadImage("assets/icon.png");
    SetWindowIcon(icon);
    SetExitKey(KEY_NULL);

    InitAudioDevice();

    ATLAS = LoadTexture("assets/atlas.png");
    DEATH = LoadTexture("assets/death.png");
    SND_WIND = LoadSound("assets/windup.mp3");
    SND_CLICK = LoadSound("assets/click.mp3");
    SND_JUMPSCARE = LoadSound("assets/jumpscare.mp3");
    SetSoundVolume(SND_JUMPSCARE, 0.6);
    SND_POPSTATIC = LoadSound("assets/popstatic.mp3");
    SetSoundVolume(SND_POPSTATIC, 0.6);

    init_status_rects();
}

#define WIND 0
#define DECDIFF 1
#define INCDIFF 2
#define OKBUTTON 3
#define ENDMUSIC 4
const Rectangle BUTTONS[] = {(Rectangle){90, 26, 156, 65}, (Rectangle){5, 130, 13, 19},
                             (Rectangle){45, 130, 13, 19}, (Rectangle){40, 15, 33, 19},
                             (Rectangle){2, 2, 19, 19}};

void update(struct State *state) {
    state->win_w = GetRenderWidth();
    state->win_h = GetRenderHeight();
    state->scale_w = (float)state->win_w / NATIVE_W;
    state->scale_h = (float)state->win_h / NATIVE_H;

    if (state->dead) {
        if (!IsSoundPlaying(SND_POPSTATIC)) {
            PlaySound(SND_POPSTATIC);
        }
        return;
    }

    double dt = GetFrameTime();
    double current_time = GetTime();

    update_notification(state);

    Vector2 vmouse = Vector2Divide(GetMousePosition(), (Vector2){state->scale_w, state->scale_h});

    if ((IsMouseButtonDown(0) && CheckCollisionPointRec(vmouse, BUTTONS[WIND]) ||
         state->autowind)) {

        state->winding = true;
        state->boxtimer += 300.0 * state->windspeed * dt;
        if (!IsSoundPlaying(SND_WIND))
            PlaySound(SND_WIND);
    } else {
        state->winding = false;
        state->boxtimer -= decrease_from(state->difficulty) * dt;
    }

    state->boxtimer = Clamp(state->boxtimer, 0.0, 2100.0);

    if (state->boxtimer == 0.0) {
        state->time_at_zero += dt;
        if (state->time_at_zero > 5.0 + MAX(10.0 - (double)state->difficulty / 2.0, 0.0)) {
            PauseMusicStream(state->custom_music);
            PlaySound(SND_JUMPSCARE);
            SetWindowTitle("You forgot...");
            state->dead = true;
            return;
        }
    }

    int bound = (state->unlock_difficulty) ? 100 : 20;
    static double last_repeat = 0.0;
    if ((IsMouseButtonDown(0) && current_time - last_repeat > 0.2) || IsMouseButtonPressed(0)) {
        last_repeat = current_time;
        if (CheckCollisionPointRec(vmouse, BUTTONS[DECDIFF])) {
            int change = (IsKeyDown(KEY_LEFT_SHIFT)) ? 5 : 1;
            PlaySound(SND_CLICK);
            state->difficulty = Clamp(state->difficulty - change, 0, bound);
        } else if (CheckCollisionPointRec(vmouse, BUTTONS[INCDIFF])) {
            int change = (IsKeyDown(KEY_LEFT_SHIFT)) ? 5 : 1;
            PlaySound(SND_CLICK);
            state->difficulty = Clamp(state->difficulty + change, 0, bound);
        }
    }

    // mouse clicks

    if (IsMouseButtonPressed(0) && CheckCollisionPointRec(vmouse, BUTTONS[ENDMUSIC]) &&
        state->music_playing) {

        PlaySound(SND_CLICK);
        PauseMusicStream(state->custom_music);
        UnloadMusicStream(state->custom_music);
        state->music_playing = false;
    }

    // keybinds

    if (IsKeyDown(KEY_LEFT_CONTROL)) {
        if (IsKeyPressed(KEY_W)) {
            PlaySound(SND_CLICK);
            if (IsWindowState(FLAG_WINDOW_TOPMOST)) {
                set_notification(state, "Window on top: Off", 16, 1.0);
                ClearWindowState(FLAG_WINDOW_TOPMOST);
            } else {
                SetWindowState(FLAG_WINDOW_TOPMOST);
                set_notification(state, "Window on top: On", 16, 1.0);
            }
        } else if (IsKeyPressed(KEY_T)) {
            PlaySound(SND_CLICK);
            state->show_time_to_unwind = !state->show_time_to_unwind;

            set_notification(
                state, TextFormat("Show time: %s", FMTBOOL(state->show_time_to_unwind)), 16, 1.0);
        } else if (IsKeyPressed(KEY_A)) {
            PlaySound(SND_CLICK);
            state->autowind = !state->autowind;
        } else if (IsKeyPressed(KEY_U)) {
            PlaySound(SND_CLICK);
            state->unlock_difficulty = !state->unlock_difficulty;

            set_notification(state,
                             TextFormat("Unlock difficulty: %s", FMTBOOL(state->unlock_difficulty)),
                             16, 1.0);
        } else if (IsKeyPressed(KEY_X)) {
            PlaySound(SND_CLICK);
            state->win_transparent = !state->win_transparent;

            set_notification(state,
                             TextFormat("Window transparency: %s", FMTBOOL(state->win_transparent)),
                             16, 1.0);
        } else if (IsKeyPressed(KEY_R)) {
            set_notification(state, "Restoring native aspect ratio...", 16, 1.0);
            PlaySound(SND_CLICK);
            double scale_relation = NATIVE_H / NATIVE_W;
            state->win_h = (int)((double)state->win_w * scale_relation);
            SetWindowSize(state->win_w, state->win_h);
        } else if (IsKeyPressed(KEY_ONE)) {
            set_notification(state, "Windup speed: 1x", 16, 1.0);
            PlaySound(SND_CLICK);
            state->windspeed = 1.0;
        } else if (IsKeyPressed(KEY_TWO)) {
            set_notification(state, "Windup speed: 2x", 16, 1.0);
            PlaySound(SND_CLICK);
            state->windspeed = 2.0;
        } else if (IsKeyPressed(KEY_THREE)) {
            set_notification(state, "Windup speed: 3x", 16, 1.0);
            PlaySound(SND_CLICK);
            state->windspeed = 3.0;
        }
    }

    // scrolling

    float scroll = GetMouseWheelMove();
    if (GetMouseWheelMove() != 0) {
        int change = (IsKeyDown(KEY_LEFT_SHIFT)) ? 5 : 1;
        PlaySound(SND_CLICK);
        state->difficulty = Clamp(state->difficulty + (int)scroll * change, 0, bound);
    }

    // music handler

    if (state->music_playing) {
        PlayMusicStream(state->custom_music);
        UpdateMusicStream(state->custom_music);
    }

    if (IsFileDropped()) {
        TraceLog(LOG_INFO, "File dropped");

        FilePathList files = LoadDroppedFiles();
        // i only care about the first one xd
        Music music = LoadMusicStream(files.paths[0]);
        if (IsMusicReady(music)) {
            if (state->music_playing) {
                PauseMusicStream(state->custom_music);
                UnloadMusicStream(state->custom_music);
                MemFree(state->custom_music_path);
                state->custom_music_path = NULL;
            }
            state->custom_music = music;
            state->custom_music_path = MemAlloc(TextLength(files.paths[0]) + 1);
            TextCopy(state->custom_music_path, files.paths[0]);

            state->music_playing = true;
            SetMusicVolume(music, 0.5);
        } else {
            set_notification(state, "Error loading sound file.", 16, 2.0);
        }
        UnloadDroppedFiles(files);
    }
}

#define BACKGROUND ((state->win_transparent) ? (Color){0, 0, 0, 165} : BLACK)

void draw(RenderTexture2D target, struct State *state) {
    BeginTextureMode(target);
    {
        ClearBackground(BACKGROUND);

        if (state->dead) {
            // just draw the puppet and nothing else if ur dead
            DrawTextureRec(DEATH, (Rectangle){0, 0, 273, 153}, (Vector2){0, 0}, WHITE);
            goto done;
        }

        // the wind up button
        if (state->winding) {
            DrawTextureRec(ATLAS, (Rectangle){156, 0, 156, 65},
                           (Vector2){BUTTONS[WIND].x, BUTTONS[WIND].y}, WHITE);
        } else {
            DrawTextureRec(ATLAS, (Rectangle){0, 0, 156, 65},
                           (Vector2){BUTTONS[WIND].x, BUTTONS[WIND].y}, WHITE);
        }

        // the "click & hold" label
        DrawTextureRec(ATLAS, (Rectangle){313, 0, 154, 14}, (Vector2){91, 95}, WHITE);

        // the pie chart status thing
        if (state->boxtimer > 0.0) {
            DrawTextureRec(ATLAS, STATUS_RECTS[(int)(ceil(state->boxtimer / 100.0) - 1)],
                           (Vector2){25, 50}, WHITE);
        }

        // decrease difficulty button
        DrawTextureRec(ATLAS, (Rectangle){467, 0, 13, 19},
                       (Vector2){BUTTONS[DECDIFF].x, BUTTONS[DECDIFF].y}, WHITE);

        // increase difficulty button
        DrawTextureRec(ATLAS, (Rectangle){481, 0, 13, 19},
                       (Vector2){BUTTONS[INCDIFF].x, BUTTONS[INCDIFF].y}, WHITE);

        // draw difficulty label
        Color diff_color = get_difficulty_color(state->difficulty);
        // (state->unlock_difficulty) ? (Color){.r = 130, .g = 130, .b = 255, .a = 255} : WHITE;

        const char *text = TextFormat("%d", state->difficulty);
        DrawText(text, 31 - (MeasureText(text, 14) / 2), 134, 14, diff_color);

        if (state->show_time_to_unwind) {
            // draw time 'til unwind in minutes n seconds
            double seconds = state->boxtimer / decrease_from(state->difficulty);
            int minutes = 0;

            if (seconds >= 60.0) {
                minutes = (int)(seconds / 60.0);
                seconds = seconds - (double)(60.0 * minutes);
            }

            double capacity = MAXBOXTIMER;
            double fillpercent = state->boxtimer / MAXBOXTIMER;

            Color text_color = WHITE;

            if (fillpercent < 0.175) {
                text_color = RED;
            } else if (fillpercent < 0.350) {
                text_color = YELLOW;
            }

            DrawText(TextFormat("Time left: %d min, %.1f sec", minutes, seconds), 64, 134, 14,
                     text_color);
        }

        if (state->notification.text != NULL) {
            const char *text = state->notification.text;
            int size = state->notification.size;
            DrawText(text, 2, 0, size, WHITE);
        } else if (state->music_playing) {
            // draw disable music button and music label
            DrawTextureRec(ATLAS, (Rectangle){569, 0, 19, 19},
                           (Vector2){BUTTONS[ENDMUSIC].x, BUTTONS[ENDMUSIC].y}, WHITE);

            DrawText(TextFormat("%s", GetFileName(state->custom_music_path)), 24, 6, 10, WHITE);
        }

        /*
// alerts, TODO: make this its own system, not just some kinda weird hardcoded thing
if (state->dragndroptip) {
    DrawText("Drag and drop a song to play it!", 2, 0, 16, WHITE);
} else if (state->windowtoptip) {
    DrawText("And CTRL+W to keep this window on top!", 2, 0, 12, WHITE);
} else if (state->show_error) {
    DrawText("Error loading sound file.", 2, 0, 16, WHITE);
} else if (state->music_playing) {
    // draw disable music button and music label
    DrawTextureRec(ATLAS, (Rectangle){569, 0, 19, 19},
                   (Vector2){BUTTONS[ENDMUSIC].x, BUTTONS[ENDMUSIC].y}, WHITE);

    DrawText(TextFormat("%s", GetFileName(state->custom_music_path)), 24, 6, 10, WHITE);
}
        */

        if (state->autowind) {
            // cheat
            DrawText("autowind on", 19, 37, 11, PURPLE);
        }
    };
done:
    EndTextureMode();

    BeginDrawing();
    {
        ClearBackground(BACKGROUND);
        DrawTexturePro(
            target.texture,
            (Rectangle){0.0f, 0.0f, (float)target.texture.width, (float)-target.texture.height},
            (Rectangle){0.0f, 0.0f, (float)state->win_w, (float)state->win_h},
            (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
    };
    EndDrawing();
}

int main(void) {
    struct State state = (struct State){
        .boxtimer = MAXBOXTIMER,
        .difficulty = 5,
        .time_at_zero = 0,
        .winding = false,
        .windspeed = 1.0,

        .music_playing = false,
        .custom_music_path = NULL,

        .show_time_to_unwind = false,
        .unlock_difficulty = false,
        .autowind = false,
        .win_transparent = false,

        .notification = {.text = NULL},
    };

    init(&state);

    RenderTexture2D target = LoadRenderTexture(NATIVE_W, NATIVE_H);

    set_notification(&state, "Drag and drop a song to play it!", 16, 2.0);

    while (!WindowShouldClose()) {
        update(&state);
        draw(target, &state);
    }

    CloseWindow();
}

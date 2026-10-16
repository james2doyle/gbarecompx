// stub_raylib/raylib.h — a minimal raylib API surface for the launcher's
// headless draw-loop harness (launcher_raygui_tests).
//
// The launcher TU (launcher_raygui.cpp) and raygui.h (non-standalone) both
// include "raylib.h"; this test target puts THIS directory first on the
// include path, so they compile against these declarations and link
// against the recording stub TU (launcher_raylib_stub.cpp) — every draw
// call observable, every input scripted, no display, no raylib fetch.
// Types mirror raylib's layouts; constants copy raylib's real values so
// the scripted-key tests read like the real thing.
#ifndef STUB_RAYLIB_H
#define STUB_RAYLIB_H

#include <cstdlib>

// raylib.h's public memory macros (raygui's implementation uses them).
#define RL_MALLOC(sz) malloc(sz)
#define RL_CALLOC(n, sz) calloc(n, sz)
#define RL_REALLOC(ptr, sz) realloc(ptr, sz)
#define RL_FREE(ptr) free(ptr)

typedef struct Vector2 {
    float x;
    float y;
} Vector2;

typedef struct Vector3 {
    float x;
    float y;
    float z;
} Vector3;

typedef struct Color {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} Color;

typedef struct Rectangle {
    float x;
    float y;
    float width;
    float height;
} Rectangle;

typedef struct Image {
    void* data;
    int width;
    int height;
    int mipmaps;
    int format;
} Image;

typedef struct Texture {
    unsigned int id;
    int width;
    int height;
    int mipmaps;
    int format;
} Texture;

typedef Texture Texture2D;

typedef struct GlyphInfo {
    int value;
    int offsetX;
    int offsetY;
    int advanceX;
    Image image;
} GlyphInfo;

typedef struct Font {
    int baseSize;
    int glyphCount;
    int glyphPadding;
    Texture2D texture;
    Rectangle* recs;
    GlyphInfo* glyphs;
} Font;

typedef struct FilePathList {
    unsigned int capacity;
    unsigned int count;
    char** paths;
} FilePathList;

// Keys (raylib's real values — full set: raygui's textbox paths reference
// them, and the harness's scripted keys use the same constants).
#define KEY_NULL 0
#define KEY_APOSTROPHE 39
#define KEY_COMMA 44
#define KEY_MINUS 45
#define KEY_PERIOD 46
#define KEY_SLASH 47
#define KEY_ZERO 48
#define KEY_ONE 49
#define KEY_TWO 50
#define KEY_THREE 51
#define KEY_FOUR 52
#define KEY_FIVE 53
#define KEY_SIX 54
#define KEY_SEVEN 55
#define KEY_EIGHT 56
#define KEY_NINE 57
#define KEY_SEMICOLON 59
#define KEY_EQUAL 61
#define KEY_A 65
#define KEY_B 66
#define KEY_C 67
#define KEY_D 68
#define KEY_E 69
#define KEY_F 70
#define KEY_G 71
#define KEY_H 72
#define KEY_I 73
#define KEY_J 74
#define KEY_K 75
#define KEY_L 76
#define KEY_M 77
#define KEY_N 78
#define KEY_O 79
#define KEY_P 80
#define KEY_Q 81
#define KEY_R 82
#define KEY_S 83
#define KEY_T 84
#define KEY_U 85
#define KEY_V 86
#define KEY_W 87
#define KEY_X 88
#define KEY_Y 89
#define KEY_Z 90
#define KEY_LEFT_BRACKET 91
#define KEY_BACKSLASH 92
#define KEY_RIGHT_BRACKET 93
#define KEY_GRAVE 96
#define KEY_SPACE 32
#define KEY_ESCAPE 256
#define KEY_ENTER 257
#define KEY_TAB 258
#define KEY_BACKSPACE 259
#define KEY_INSERT 260
#define KEY_DELETE 261
#define KEY_RIGHT 262
#define KEY_LEFT 263
#define KEY_DOWN 264
#define KEY_UP 265
#define KEY_PAGE_UP 266
#define KEY_PAGE_DOWN 267
#define KEY_HOME 268
#define KEY_END 269
#define KEY_CAPS_LOCK 280
#define KEY_SCROLL_LOCK 281
#define KEY_NUM_LOCK 282
#define KEY_PRINT_SCREEN 283
#define KEY_PAUSE 284
#define KEY_F1 290
#define KEY_F2 291
#define KEY_F3 292
#define KEY_F4 293
#define KEY_F5 294
#define KEY_F6 295
#define KEY_F7 296
#define KEY_F8 297
#define KEY_F9 298
#define KEY_F10 299
#define KEY_F11 300
#define KEY_F12 301
#define KEY_LEFT_SHIFT 340
#define KEY_LEFT_CONTROL 341
#define KEY_LEFT_ALT 342
#define KEY_LEFT_SUPER 343
#define KEY_RIGHT_SHIFT 345
#define KEY_RIGHT_CONTROL 346
#define KEY_RIGHT_ALT 347
#define KEY_RIGHT_SUPER 348
#define KEY_KP_0 320
#define KEY_KP_1 321
#define KEY_KP_2 322
#define KEY_KP_3 323
#define KEY_KP_4 324
#define KEY_KP_5 325
#define KEY_KP_6 326
#define KEY_KP_7 327
#define KEY_KP_8 328
#define KEY_KP_9 329
#define KEY_KP_DECIMAL 330
#define KEY_KP_DIVIDE 331
#define KEY_KP_MULTIPLY 332
#define KEY_KP_SUBTRACT 333
#define KEY_KP_ADD 334
#define KEY_KP_ENTER 335
#define KEY_KP_EQUAL 336

// Mouse (raylib's real values).
#define MOUSE_LEFT_BUTTON 0
#define MOUSE_RIGHT_BUTTON 1
#define MOUSE_MIDDLE_BUTTON 2

// Gamepad (raylib's real values).
#define GAMEPAD_BUTTON_LEFT_FACE_UP 1
#define GAMEPAD_BUTTON_LEFT_FACE_RIGHT 2
#define GAMEPAD_BUTTON_LEFT_FACE_DOWN 3
#define GAMEPAD_BUTTON_LEFT_FACE_LEFT 4
#define GAMEPAD_BUTTON_RIGHT_FACE_DOWN 5
#define GAMEPAD_BUTTON_RIGHT_FACE_RIGHT 6
#define GAMEPAD_BUTTON_RIGHT_FACE_LEFT 7
#define GAMEPAD_BUTTON_RIGHT_FACE_UP 8

// Colors the launcher/raygui reference.
#define WHITE (Color){255, 255, 255, 255}
#define RED (Color){230, 41, 55, 255}
#define BLANK (Color){0, 0, 0, 0}

// Gamepad axes (raylib's real values).
#define GAMEPAD_AXIS_LEFT_X 0
#define GAMEPAD_AXIS_LEFT_Y 1
#define GAMEPAD_AXIS_RIGHT_X 2
#define GAMEPAD_AXIS_RIGHT_Y 3
#define GAMEPAD_AXIS_LEFT_TRIGGER 4
#define GAMEPAD_AXIS_RIGHT_TRIGGER 5

// Window/system
void InitWindow(int width, int height, const char* title);
void CloseWindow(void);
void SetTargetFPS(int fps);
bool WindowShouldClose(void);
void SetWindowFocused(void);
const char* GetApplicationDirectory(void);
int GetScreenWidth(void);
int GetScreenHeight(void);

// Drawing
void BeginDrawing(void);
void EndDrawing(void);
void ClearBackground(Color color);
void DrawLine(int startPosX, int startPosY, int endPosX, int endPosY,
              Color color);
void DrawText(const char* text, int posX, int posY, int fontSize,
              Color color);
int MeasureText(const char* text, int fontSize);
void DrawTextCodepoint(Font font, int codepoint, Vector2 position,
                       float fontSize, Color tint);
void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest,
                    Vector2 origin, float rotation, Color tint);
void DrawRectangle(int posX, int posY, int width, int height, Color color);
void DrawRectangleRec(Rectangle rec, Color color);
void DrawRectangleRounded(Rectangle rec, float roundness, int segments,
                          Color color);
void DrawRectangleRoundedLinesEx(Rectangle rec, float roundness, int segments,
                                 float lineThick, Color color);
void DrawRectangleLinesEx(Rectangle rec, float lineThick, Color color);
void DrawRectangleGradientEx(Rectangle rec, Color topLeft, Color bottomLeft,
                             Color topRight, Color bottomRight);

// Files/drops
bool IsFileDropped(void);
FilePathList LoadDroppedFiles(void);
void UnloadDroppedFiles(FilePathList files);
Image LoadImage(const char* fileName);
void UnloadImage(Image image);
Texture2D LoadTextureFromImage(Image image);
void UnloadTexture(Texture2D texture);

// Input
bool IsKeyPressed(int key);
bool IsKeyDown(int key);
int GetCharPressed(void);
Vector2 GetMousePosition(void);
float GetMouseWheelMove(void);
bool IsMouseButtonDown(int button);
bool IsMouseButtonPressed(int button);
bool IsMouseButtonReleased(int button);
bool IsGamepadAvailable(int gamepad);
bool IsGamepadButtonPressed(int gamepad, int button);
bool IsGamepadButtonReleased(int gamepad, int button);
bool IsGamepadButtonDown(int gamepad, int button);
int GetGamepadAxisCount(int gamepad);
float GetGamepadAxisMovement(int gamepad, int axis);

// Text helpers raygui's implementation calls.
int GetCodepointNext(const char* text, int* codepointSize);
const char* CodepointToUTF8(int codepoint, int* byteSize);
bool TextIsEqual(const char* text1, const char* text2);
unsigned int TextLength(const char* text);
const char* TextToLower(const char* text);
const char* TextToUpper(const char* text);
const char* TextSubtext(const char* text, int position, int length);
Color ColorAlpha(Color color, float alpha);

// Text/font internals (raygui's bottom layer)
Font GetFontDefault(void);
Font LoadFontEx(const char* fileName, int fontSize, int* codepoints,
                int codepointCount);
void SetShapesTexture(Texture2D texture, Rectangle source);
int GetCodepoint(const char* text, int* codepointSize);
int GetCodepointPrevious(const char* text, int* codepointSize);
int GetGlyphIndex(Font font, int codepoint);
const char* TextFormat(const char* text, ...);
int TextToInteger(const char* text);
float TextToFloat(const char* text);
Color GetColor(int hexValue);
Color Fade(Color color, float alpha);
int ColorToInt(Color color);
bool CheckCollisionPointRec(Vector2 point, Rectangle rec);
char* GetClipboardText(void);
char* LoadFileText(const char* fileName);
void UnloadFileText(char* text);
unsigned char* LoadFileData(const char* fileName, int* dataSize);
void UnloadFileData(unsigned char* data);
int SaveFileData(const char* fileName, void* data, int dataSize);
const char* GetDirectoryPath(const char* filePath);
int* LoadCodepoints(const char* text, int* count);
void UnloadCodepoints(int* codepoints);
unsigned char* DecompressData(const unsigned char* compData, int compDataSize,
                              int* dataSize);

#endif  // STUB_RAYLIB_H

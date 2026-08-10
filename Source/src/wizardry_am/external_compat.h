#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using PhysPt = uintptr_t;

enum AM_RendererFlip : uint32_t {
    AM_FLIP_NONE = 0,
    AM_FLIP_HORIZONTAL = 1u << 0,
    AM_FLIP_VERTICAL = 1u << 1,
};

struct AM_MouseButtonEvent {
    uint8_t button = 0;
    uint8_t clicks = 0;
    int x = 0;
    int y = 0;
};

static constexpr uint8_t AM_BUTTON_LEFT = 1;
static constexpr uint8_t AM_BUTTON_MIDDLE = 2;
static constexpr uint8_t AM_BUTTON_RIGHT = 3;

extern int am_width;
extern int am_height;
extern int map_scroll_x;
extern int map_scroll_y;
extern PhysPt am_loadaddress;

extern bool amw7_pc98_mode;
extern uintptr_t amw7_pc98_anchor;

bool mem_readb_checked(uintptr_t addr, uint8_t* out);
bool mem_readw_checked(uintptr_t addr, uint16_t* out);
void MEM_BlockRead(uintptr_t addr, void* out, size_t len);

void AM_DrawRect(int x, int y, int w, int h, int color);
void AM_DrawOutlineRect(int x, int y, int w, int h, uint32_t color);
void AM_DrawW6Sprite(int x, int y, int w, int h,
                     float u0, float v0, float u1, float v1,
                     bool dark,
                     AM_RendererFlip flip);
void AM_DrawW7Sprite(int x, int y, int w, int h,
                     float u0, float v0, float u1, float v1,
                     bool dark,
                     AM_RendererFlip flip);
void SetAutomapWindowTitle(const char* title);
bool AM_SetClipboardText(const char* text);

void TooltipForAutomapWindow_Show(bool show);
void TooltipForAutomapWindow_SetText(wchar_t* text);
void TooltipForMainWindow_Show(bool show);
void TooltipForMainWindow_SetText(wchar_t* text);

bool InputBox(const wchar_t* title, const wchar_t* hint, wchar_t* buf, int bufSize);
bool ChooseColorDialog(uint32_t& color, uint32_t* palette);
uint16_t GetBitArrayElement(uint8_t* pArray, uint16_t index, uint16_t bitsPerElement);

void AutoMapUpdate();

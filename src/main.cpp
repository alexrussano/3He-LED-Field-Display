#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// ============================================================
// Waveshare ESP32-S3-RGB-Matrix
// 64 x 32 HUB75 panel
// ============================================================

#define PANEL_WIDTH   64
#define PANEL_HEIGHT  32
#define CHAIN_LENGTH  1

// Waveshare ESP32-S3-RGB-Matrix pinout
#define R1_PIN   4
#define G1_PIN   5
#define B1_PIN   6

#define R2_PIN   7
#define G2_PIN   15
#define B2_PIN   16

#define A_PIN    18
#define B_PIN    8
#define C_PIN    3
#define D_PIN    42

// Not used on a 64x32 panel
#define E_PIN    -1

#define LAT_PIN  40
#define OE_PIN   2
#define CLK_PIN  41

MatrixPanel_I2S_DMA *dma_display = nullptr;

// ------------------------------------------------------------
// Colors
//
// Built at compile time so they can live in the state table.
// ------------------------------------------------------------

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define COLOR_NONE    RGB565(0, 0, 0)
#define COLOR_BLACK   RGB565(0, 0, 0)
#define COLOR_TEXT    RGB565(255, 255, 255)
#define COLOR_PURPLE  RGB565(150, 40, 255)
#define COLOR_RED     RGB565(255, 0, 0)
#define COLOR_GREEN   RGB565(0, 255, 0)

// The tip is always neutral; the sample block and the text share one
// accent per state, so each mode reads as a single colour.
#define COLOR_TIP     RGB565(200, 200, 215)

#define ACCENT_IDLE   RGB565(200, 170, 255)     // pale lavender, matches the border
#define ACCENT_FAIL   RGB565(255, 90, 70)       // warm red
#define ACCENT_APPR   RGB565(255, 170, 0)       // amber, closing in
#define ACCENT_TOUCH  RGB565(60, 255, 120)      // green, in contact
#define ACCENT_MOVE   RGB565(0, 200, 255)       // cyan, in transit
#define ACCENT_HOLD   RGB565(0, 235, 195)       // teal, holding steady
#define ACCENT_WDRAW  RGB565(110, 150, 255)     // cool blue, backing off

// The scanning panel is the one inverted state: a white page inside the purple
// frame with black type on it, rather than type on black like everything else.
#define COLOR_WHITE   RGB565(255, 255, 255)

// The idle squid. Pink against the purple frame, so the two never merge.
#define COLOR_SQUID   RGB565(255, 105, 180)

// ------------------------------------------------------------
// Layout
//
// The default GFX font is 6 x 8 px per character at size 1, so a
// 64 px line holds 10 characters. Animated states give up the left
// GUTTER_W pixels to the tip diagram, leaving 9 characters of text,
// so their panel strings are pre-split to fit.
// ------------------------------------------------------------

#define CHAR_W      6
#define CHAR_H      8
#define LINE_GAP    1
#define MAX_LINES   3

#define BORDER_W    2               // thickness of the framed-state border

#define GUTTER_W    10              // width of the tip diagram column

#define TIP_CX      5               // tip centre line, inside the gutter
#define TIP_TOP     2               // underside of the cantilever
#define TIP_HALF_W  2               // half-width of the tip at its base
#define TIP_APEX_Y  17              // lowest pixel of the tip

#define BLOCK_X     1               // the sample: a wide, flat block
#define BLOCK_W     8
#define BLOCK_H     3
#define BLOCK_LOW_Y   (PANEL_HEIGHT - BLOCK_H - 1)  // fully withdrawn
#define BLOCK_TOUCH_Y (TIP_APEX_Y + 1)              // in contact with the apex
#define BLOCK_NEAR_Y  (TIP_APEX_Y + 2)              // one pixel short of contact
#define BLOCK_HELD_Y  (TIP_APEX_Y + 5)              // parked at constant height

#define FRAME_MS    60              // one pixel of travel per frame

// The split layout: a static heading over a word that pulls apart, drifts
// to SPLIT_GAP_MAX, then snaps back together. Slower than FRAME_MS so the
// separation reads as a slow drag rather than a twitch.
#define SPLIT_TOP_Y     7           // baseline row of the heading
#define SPLIT_BOT_Y     16          // baseline row of the splitting word
#define SPLIT_LEFT_X    1           // left edge of the fixed fragment
#define SPLIT_GAP_MAX   10          // pixels of separation before the snap
#define SPLIT_FRAME_MS  120         // one pixel of separation per frame

// The idle squid sits in its own column on the left, leaving 38 px (6
// characters) of text to its right. Its body is 15 x 15 and the tentacles
// bring it to 22 px tall; with the wiggle at full stretch it spans 17 px
// across. It wanders a couple of pixels around SQUID_X / SQUID_Y, and every
// position it can reach clears the border by at least 2 px.
#define SQUID_GUTTER_W  26          // width of the squid's column
#define SQUID_X         6           // rest position, left edge of the body box
#define SQUID_Y         5           // rest position, top of the dome
#define SQUID_FRAME_MS  150         // one wiggle step per frame

// The tentacle wave. Each row of each tentacle is shifted sideways by one of
// these, indexed by (time + row + tentacle), so the offset travels down the
// limb and neighbouring limbs are out of phase with each other.
#define SQUID_WAVE_LEN  6

// Every so often the idle panel swaps IDLE for HELLO OWEN, then swaps back. One
// full cycle is IDLE_TEXT_MS + IDLE_GREET_MS, so the greeting comes round once
// every 30 seconds. Only the text column is repainted; the squid and the border
// carry on untouched.
#define IDLE_TEXT_MS    25000
#define IDLE_GREET_MS   5000

// The scanning panel: SCANNING across the middle of a white page with a small
// squid doing laps around it. The lap is the perimeter of a rectangle inset far
// enough that the squid's 7 x 11 box never touches the border.
//
// Once the border is there the lap has to run closer in than the text, so the
// squid does clip the letters at the top and bottom of its circuit. That is
// handled by repainting the text every frame between the erase and the squid:
// the squid ends up swimming over the type, which is the point.
#define SWIM_BG         COLOR_WHITE // the page the squid swims on
#define SWIM_W          7           // mini squid bounding box
#define SWIM_H          11
#define SWIM_X_MIN      2           // just inside the border
#define SWIM_X_MAX      55          // 55 + 6 = 61, one pixel clear of the border
#define SWIM_Y_MIN      2
#define SWIM_Y_MAX      19          // 19 + 10 = 29, one pixel clear of the border
#define SWIM_FRAME_MS   55          // one pixel of travel per frame, ~8 s a lap

#define SWIM_STEPS      (2 * ((SWIM_X_MAX - SWIM_X_MIN) + (SWIM_Y_MAX - SWIM_Y_MIN)))

// ------------------------------------------------------------
// State table
// ------------------------------------------------------------

enum Layout : uint8_t
{
    LAYOUT_TEXT,    // full-width text, optionally framed
    LAYOUT_ANIM,    // tip diagram on the left, text on the right
    LAYOUT_SPLIT,   // heading over a word that pulls apart and snaps back
    LAYOUT_SQUID,   // drifting squid on the left, text on the right
    LAYOUT_SWIM     // centred text with a small squid swimming laps around it
};

enum Motion : uint8_t
{
    MOTION_NONE,    // sample parked at startBlockY
    MOTION_UP,      // sample rises, resets to the bottom near the apex
    MOTION_DOWN     // sample falls away, resets at the apex
};

struct DisplayState
{
    const char *cmd;                // what you type into the serial monitor
    const char *name;               // full state name, echoed back over serial
    // Panel text, nullptr for unused lines. LAYOUT_SPLIT reads these as
    // { heading, fixed fragment, drifting fragment } instead of as 3 lines.
    const char *lines[MAX_LINES];
    uint8_t layout;
    uint16_t borderColor;           // COLOR_NONE for no border
    uint16_t accentColor;           // text, and the sample block
    uint8_t motion;
    int16_t startBlockY;            // top row of the sample block
    bool acceptsHeight;             // takes a "b5, 100" style value
};

static const DisplayState STATES[] = {
    { "b0", "Idle",              { "IDLE",      nullptr,     nullptr  },
      LAYOUT_SQUID, COLOR_PURPLE, COLOR_TEXT,  MOTION_NONE, 0, false },

    { "b1", "Idle - Withdrawn",  { "IDLE -",    "WITHDRAWN", nullptr  },
      LAYOUT_TEXT, COLOR_PURPLE, ACCENT_IDLE,  MOTION_NONE, 0, false },

    { "b2", "Idle - Aborted",    { "IDLE -",    "ABORTED",   nullptr  },
      LAYOUT_TEXT, COLOR_PURPLE, ACCENT_IDLE,  MOTION_NONE, 0, false },

    { "b3", "approaching",       { "APPROACH-", "ING",       nullptr  },
      LAYOUT_ANIM, COLOR_NONE,   ACCENT_APPR,  MOTION_UP,   BLOCK_LOW_Y,   false },

    { "b4", "Surface Contacted", { "SURFACE",   "CONTACTED", nullptr  },
      LAYOUT_ANIM, COLOR_NONE,   ACCENT_TOUCH, MOTION_NONE, BLOCK_TOUCH_Y, false },

    { "b5", "moving_to_constant_height", { "MOVING TO", "CONSTANT", "HEIGHT" },
      LAYOUT_ANIM, COLOR_NONE,   ACCENT_MOVE,  MOTION_DOWN, BLOCK_TOUCH_Y, true  },

    { "b6", "at_constant_height", { "AT",       "CONSTANT",  "HEIGHT" },
      LAYOUT_ANIM, COLOR_NONE,   ACCENT_HOLD,  MOTION_NONE, BLOCK_HELD_Y,  true  },

    { "b7", "extension_failed",  { "EXTENSION", "FAILED",    nullptr  },
      LAYOUT_TEXT, COLOR_RED,    ACCENT_FAIL,  MOTION_NONE, 0, false },

    { "b8", "Withdrawing",       { "WITHDRAW-", "ING",       nullptr  },
      LAYOUT_ANIM, COLOR_NONE,  ACCENT_WDRAW, MOTION_DOWN, BLOCK_TOUCH_Y, false },

    { "b9", "Collecting Threshold Data", { "COLLECTING", "THRESHOLD", "DATA" },
      LAYOUT_TEXT, COLOR_PURPLE, ACCENT_IDLE,  MOTION_NONE, 0, false },

    // lines are { heading, fixed fragment, drifting fragment }
    { "b10", "Retracting Attocubes", { "RETRACTING", "ATTO",     "CUBES"  },
      LAYOUT_SPLIT, COLOR_NONE,  ACCENT_WDRAW, MOTION_NONE, 0, false },

    // accentColor is the type colour, which here is black on the white page.
    { "b11", "Scanning",          { "SCANNING", nullptr,     nullptr  },
      LAYOUT_SWIM, COLOR_PURPLE, COLOR_BLACK,  MOTION_NONE, 0, false },

    // Three lines rather than "SCAN ENDED" on one: ten characters is exactly 60 px,
    // which leaves the letters touching the 2 px border on both sides.
    { "b12", "Idle - Scan Ended", { "IDLE -",   "SCAN",      "ENDED"  },
      LAYOUT_TEXT, COLOR_PURPLE, ACCENT_IDLE,  MOTION_NONE, 0, false },
};

static const int16_t STATE_COUNT = sizeof(STATES) / sizeof(STATES[0]);

// Longest height string the 9-character animated text column can hold.
#define MAX_HEIGHT_CHARS 9

// -1 means no state is showing (a plain red or green fill).
static int16_t currentState = -1;
static int16_t blockY = 0;
static uint32_t lastFrameMs = 0;
static String inBuf;

// How far the drifting fragment of a LAYOUT_SPLIT state has pulled away.
static int16_t splitOffset = 0;
static uint32_t lastSplitMs = 0;

// Where the idle squid is along its wander, and how far the tentacle wave has
// travelled. The wave advances every frame, the wander every second frame.
static int16_t squidStep = 0;
static int16_t waveStep = 0;
static uint32_t lastSquidMs = 0;

// Whether the idle panel is currently showing the greeting instead of IDLE, and
// when it last changed its mind.
static bool idleGreeting = false;
static uint32_t lastGreetMs = 0;

static const char *IDLE_GREETING[] = { "HELLO", "OWEN" };
static const int16_t IDLE_GREETING_LINES = 2;

// How far round its lap the scanning squid has swum.
static int16_t swimStep = 0;
static uint32_t lastSwimMs = 0;

// Set by a "b5, 100" style command, cleared on every other command.
static bool showHeight = false;
static char heightText[16] = "";

// ------------------------------------------------------------
// Drawing
// ------------------------------------------------------------

static void drawCenteredLine(const char *text, int16_t y, int16_t xStart,
                             int16_t width, uint16_t color)
{
    int16_t len = strlen(text);
    int16_t x = xStart + ((width - (len * CHAR_W)) / 2);

    if (x < xStart)
    {
        x = xStart;
    }

    dma_display->setTextColor(color);
    dma_display->setCursor(x, y);
    dma_display->print(text);
}

// Centers an arbitrary run of lines as a block within the given column. Split out
// of drawTextBlock so the idle greeting, which is not part of any state's line
// table, can be laid out the same way.
static void drawLines(const char *const *lines, int16_t count, int16_t xStart,
                      int16_t width, uint16_t color)
{
    if (count == 0)
    {
        return;
    }

    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);

    int16_t block = (count * CHAR_H) + ((count - 1) * LINE_GAP);
    int16_t y = (PANEL_HEIGHT - block) / 2;

    for (int16_t i = 0; i < count; i++)
    {
        drawCenteredLine(lines[i], y, xStart, width, color);
        y += CHAR_H + LINE_GAP;
    }
}

// Centers the state's 1-3 lines as a block within the given column.
static void drawTextBlock(const DisplayState &state, int16_t xStart, int16_t width)
{
    const char *out[MAX_LINES];
    int16_t count = 0;

    while (count < MAX_LINES && state.lines[count] != nullptr)
    {
        out[count] = state.lines[count];
        count++;
    }

    // Four 8 px lines do not fit on a 32 px panel, so on a state that
    // already fills every row the height replaces the trailing "HEIGHT"
    // line -- "nm" says height clearly enough on its own.
    if (showHeight)
    {
        if (count == MAX_LINES)
        {
            out[count - 1] = heightText;
        }
        else
        {
            out[count] = heightText;
            count++;
        }
    }

    drawLines(out, count, xStart, width, state.accentColor);
}

// Concentric rects, drawn inward from the panel edge.
static void drawBorder(uint16_t color)
{
    for (int16_t i = 0; i < BORDER_W; i++)
    {
        dma_display->drawRect(
            i, i,
            PANEL_WIDTH - (2 * i),
            PANEL_HEIGHT - (2 * i),
            color
        );
    }
}

// A cantilever bar with a narrow tip hanging below it.
static void drawTip()
{
    dma_display->drawFastHLine(BLOCK_X, TIP_TOP, BLOCK_W, COLOR_TIP);
    dma_display->fillTriangle(
        TIP_CX - TIP_HALF_W, TIP_TOP,
        TIP_CX + TIP_HALF_W, TIP_TOP,
        TIP_CX,              TIP_APEX_Y,
        COLOR_TIP
    );
}

static void drawBlock(int16_t y, uint16_t color)
{
    dma_display->fillRect(BLOCK_X, y, BLOCK_W, BLOCK_H, color);
}

// Left edge of the drifting fragment when the word is whole.
static int16_t splitRestX(const DisplayState &state)
{
    return SPLIT_LEFT_X + (strlen(state.lines[1]) * CHAR_W);
}

// Redraws only the drifting fragment: clears everything right of the fixed
// fragment, then reprints at the current offset. The heading and the fixed
// fragment are never touched, so neither of them flickers.
static void drawSplitFragment(const DisplayState &state)
{
    int16_t restX = splitRestX(state);

    dma_display->fillRect(restX, SPLIT_BOT_Y, PANEL_WIDTH - restX, CHAR_H, COLOR_BLACK);

    dma_display->setTextColor(state.accentColor);
    dma_display->setCursor(restX + splitOffset, SPLIT_BOT_Y);
    dma_display->print(state.lines[2]);
}

// A closed loop of offsets from the rest position. Two pixels of sway and one
// of bob, stepped slowly, so the squid looks like it is hovering rather than
// vibrating. Any path staying within +/-2 x and +/-1 y keeps it off the border.
static const int8_t SQUID_PATH[][2] = {
    { 0,  0}, { 1,  0}, { 1, -1}, { 2, -1}, { 2,  0}, { 1,  1},
    { 0,  1}, {-1,  1}, {-1,  0}, {-2,  0}, {-2, -1}, {-1, -1}
};

static const int16_t SQUID_STEPS = sizeof(SQUID_PATH) / sizeof(SQUID_PATH[0]);

static const int8_t SQUID_WAVE[SQUID_WAVE_LEN] = { 0, 1, 1, 0, -1, -1 };

// A domed body with four wiggling tentacles and a pair of eyes. (ox, oy) is
// the top left of the body box; everything is drawn relative to it so the
// whole creature moves by changing those two numbers.
static void drawSquid(int16_t ox, int16_t oy)
{
    // Domed head, then a torso squaring off the sides below the dome's widest
    // row. Together they make a 15 x 15 body - roughly square, like the ghost.
    dma_display->fillCircle(ox + 8, oy + 7, 7, COLOR_SQUID);
    dma_display->fillRect(ox + 1, oy + 7, 15, 8, COLOR_SQUID);

    // Four tentacles, drawn a row at a time so each row can slide sideways.
    // 2 px wide on a 4 px pitch: rows overlap as the wave shifts them, so each
    // limb reads as a continuous ribbon, and even with two neighbours swinging
    // toward each other a pixel of gap survives between them.
    for (int16_t i = 0; i < 4; i++)
    {
        int16_t tx = ox + 1 + (i * 4);
        int16_t len = (i % 2 == 0) ? 7 : 5;

        for (int16_t row = 0; row < len; row++)
        {
            // One step of phase per row and per tentacle. Neighbours are then
            // never more than one step apart, so they can close on each other
            // by at most 1 px of the 2 px between them and never touch.
            int16_t phase = (waveStep + row + i) % SQUID_WAVE_LEN;

            dma_display->fillRect(
                tx + SQUID_WAVE[phase], oy + 15 + row,
                2, 1,
                COLOR_SQUID
            );
        }
    }

    // Eyes: 4 x 4 whites with a 2 x 2 pupil each.
    dma_display->fillRect(ox + 3, oy + 5, 4, 4, COLOR_TEXT);
    dma_display->fillRect(ox + 10, oy + 5, 4, 4, COLOR_TEXT);
    dma_display->fillRect(ox + 4, oy + 6, 2, 2, COLOR_BLACK);
    dma_display->fillRect(ox + 11, oy + 6, 2, 2, COLOR_BLACK);
}

// Clears the squid's column and redraws it at the current step. The clear
// stops short of the border so the frame survives, and the text column is
// never touched.
static void drawSquidFrame()
{
    dma_display->fillRect(
        BORDER_W, BORDER_W,
        SQUID_GUTTER_W - BORDER_W,
        PANEL_HEIGHT - (2 * BORDER_W),
        COLOR_BLACK
    );

    drawSquid(
        SQUID_X + SQUID_PATH[squidStep][0],
        SQUID_Y + SQUID_PATH[squidStep][1]
    );
}

// Repaints only the text column of the idle panel, so IDLE and HELLO OWEN can
// trade places without disturbing the squid. The clear runs to the panel edge, so
// the border is put back afterwards.
static void drawIdleText(const DisplayState &state)
{
    dma_display->fillRect(
        SQUID_GUTTER_W, 0,
        PANEL_WIDTH - SQUID_GUTTER_W, PANEL_HEIGHT,
        COLOR_BLACK
    );

    if (idleGreeting)
    {
        drawLines(IDLE_GREETING, IDLE_GREETING_LINES,
                  SQUID_GUTTER_W, PANEL_WIDTH - SQUID_GUTTER_W, state.accentColor);
    }
    else
    {
        drawTextBlock(state, SQUID_GUTTER_W, PANEL_WIDTH - SQUID_GUTTER_W);
    }

    if (state.borderColor != COLOR_NONE)
    {
        drawBorder(state.borderColor);
    }
}

// The scanning squid: the same creature as the idle one, shrunk to a 7 x 11 box so
// it fits in the margin around the text. (ox, oy) is the top left of that box.
static void drawMiniSquid(int16_t ox, int16_t oy)
{
    // A 7 x 7 body - a dome with a short torso squaring off underneath it.
    dma_display->fillCircle(ox + 3, oy + 3, 3, COLOR_SQUID);
    dma_display->fillRect(ox, oy + 3, 7, 4, COLOR_SQUID);

    // Three 1 px tentacles on a 2 px pitch, waved the same way as the big squid:
    // one step of phase per row and per limb, so the ripple runs down each tentacle
    // and neighbours stay out of step. Adjacent wave values differ by at most 1, so
    // two limbs can close to 1 px apart but never merge.
    for (int16_t i = 0; i < 3; i++)
    {
        int16_t tx = ox + 1 + (i * 2);

        for (int16_t row = 0; row < 4; row++)
        {
            int16_t phase = (waveStep + row + i) % SQUID_WAVE_LEN;

            dma_display->drawPixel(tx + SQUID_WAVE[phase], oy + 7 + row, COLOR_SQUID);
        }
    }

    // Eyes: 2 x 2 whites with a single dark pixel each.
    dma_display->fillRect(ox + 1, oy + 2, 2, 2, COLOR_TEXT);
    dma_display->fillRect(ox + 4, oy + 2, 2, 2, COLOR_TEXT);
    dma_display->drawPixel(ox + 2, oy + 3, COLOR_BLACK);
    dma_display->drawPixel(ox + 5, oy + 3, COLOR_BLACK);
}

// Where the swimming squid sits at a given point in its lap. The lap runs
// clockwise: across the top, down the right, back across the bottom, up the left.
static void swimPos(int16_t step, int16_t *x, int16_t *y)
{
    int16_t w = SWIM_X_MAX - SWIM_X_MIN;
    int16_t h = SWIM_Y_MAX - SWIM_Y_MIN;

    if (step < w)
    {
        *x = SWIM_X_MIN + step;
        *y = SWIM_Y_MIN;
    }
    else if (step < (w + h))
    {
        *x = SWIM_X_MAX;
        *y = SWIM_Y_MIN + (step - w);
    }
    else if (step < ((2 * w) + h))
    {
        *x = SWIM_X_MAX - (step - w - h);
        *y = SWIM_Y_MAX;
    }
    else
    {
        *x = SWIM_X_MIN;
        *y = SWIM_Y_MAX - (step - (2 * w) - h);
    }
}

static void drawSwimSquid()
{
    int16_t x = 0;
    int16_t y = 0;

    swimPos(swimStep, &x, &y);
    drawMiniSquid(x, y);
}

// Blanks the squid's box at its current position, back to the white page. This can
// take a bite out of the text, which is why the caller repaints it afterwards.
static void eraseSwimSquid()
{
    int16_t x = 0;
    int16_t y = 0;

    swimPos(swimStep, &x, &y);
    dma_display->fillRect(x, y, SWIM_W, SWIM_H, SWIM_BG);
}

// Heading on top, the splitting word below it.
static void drawSplit(const DisplayState &state)
{
    dma_display->setTextSize(1);
    dma_display->setTextWrap(false);

    drawCenteredLine(state.lines[0], SPLIT_TOP_Y, 0, PANEL_WIDTH, state.accentColor);

    dma_display->setTextColor(state.accentColor);
    dma_display->setCursor(SPLIT_LEFT_X, SPLIT_BOT_Y);
    dma_display->print(state.lines[1]);

    drawSplitFragment(state);
}

// Full repaint. Called once when a state is selected; the animation
// then only touches the sample block, so the text never flickers.
static void showState(int16_t index)
{
    const DisplayState &state = STATES[index];

    currentState = index;
    blockY = state.startBlockY;
    lastFrameMs = millis();

    splitOffset = 0;
    lastSplitMs = millis();

    squidStep = 0;
    waveStep = 0;
    lastSquidMs = millis();

    idleGreeting = false;
    lastGreetMs = millis();

    swimStep = 0;
    lastSwimMs = millis();

    dma_display->fillScreen(COLOR_BLACK);

    if (state.borderColor != COLOR_NONE)
    {
        drawBorder(state.borderColor);
    }

    if (state.layout == LAYOUT_ANIM)
    {
        drawTextBlock(state, GUTTER_W, PANEL_WIDTH - GUTTER_W);
        drawTip();
        drawBlock(blockY, state.accentColor);
    }
    else if (state.layout == LAYOUT_SPLIT)
    {
        drawSplit(state);
    }
    else if (state.layout == LAYOUT_SQUID)
    {
        drawIdleText(state);
        drawSquidFrame();
    }
    else if (state.layout == LAYOUT_SWIM)
    {
        // Lay the white page down inside the frame first, then type on it. The
        // border was already drawn above and the fill stops short of it.
        dma_display->fillRect(
            BORDER_W, BORDER_W,
            PANEL_WIDTH - (2 * BORDER_W),
            PANEL_HEIGHT - (2 * BORDER_W),
            SWIM_BG
        );

        drawTextBlock(state, 0, PANEL_WIDTH);
        drawSwimSquid();
    }
    else
    {
        drawTextBlock(state, 0, PANEL_WIDTH);
    }
}

// Advances the sample block one pixel per frame, if the state moves.
static void animate()
{
    if (currentState < 0)
    {
        return;
    }

    const DisplayState &state = STATES[currentState];

    // The scanning squid swims a lap around the text.
    if (state.layout == LAYOUT_SWIM)
    {
        if ((millis() - lastSwimMs) < SWIM_FRAME_MS)
        {
            return;
        }

        lastSwimMs = millis();

        eraseSwimSquid();

        swimStep++;

        if (swimStep >= SWIM_STEPS)
        {
            swimStep = 0;
        }

        waveStep++;

        if (waveStep >= SQUID_WAVE_LEN)
        {
            waveStep = 0;
        }

        // Put back whatever the erase clipped off the letters, then draw the squid
        // on top of them. Reprinting text only sets the glyph pixels, so the ones
        // that were not disturbed are written with the value they already had and
        // nothing flickers.
        drawTextBlock(state, 0, PANEL_WIDTH);
        drawSwimSquid();
        return;
    }

    // The idle squid hovers around its rest position.
    if (state.layout == LAYOUT_SQUID)
    {
        // The greeting runs on its own clock, checked every pass so it does not
        // inherit the wiggle's frame rate.
        if ((millis() - lastGreetMs) >= (uint32_t)(idleGreeting ? IDLE_GREET_MS : IDLE_TEXT_MS))
        {
            lastGreetMs = millis();
            idleGreeting = !idleGreeting;
            drawIdleText(state);
        }

        if ((millis() - lastSquidMs) < SQUID_FRAME_MS)
        {
            return;
        }

        lastSquidMs = millis();

        // The tentacles wiggle at every frame; the body drifts at half that,
        // so the wander stays gentle while the limbs stay lively.
        waveStep++;

        if (waveStep >= (SQUID_WAVE_LEN * 2))
        {
            waveStep = 0;
        }

        if ((waveStep % 2) == 0)
        {
            squidStep++;

            if (squidStep >= SQUID_STEPS)
            {
                squidStep = 0;
            }
        }

        drawSquidFrame();
        return;
    }

    // "cubes" pulls away from "Atto" one pixel at a time, then snaps back.
    if (state.layout == LAYOUT_SPLIT)
    {
        if ((millis() - lastSplitMs) < SPLIT_FRAME_MS)
        {
            return;
        }

        lastSplitMs = millis();

        splitOffset++;

        if (splitOffset > SPLIT_GAP_MAX)
        {
            splitOffset = 0;
        }

        drawSplitFragment(state);
        return;
    }

    if (state.layout != LAYOUT_ANIM || state.motion == MOTION_NONE)
    {
        return;
    }

    if ((millis() - lastFrameMs) < FRAME_MS)
    {
        return;
    }

    lastFrameMs = millis();

    drawBlock(blockY, COLOR_BLACK);

    if (state.motion == MOTION_UP)
    {
        blockY--;

        // One pixel short of the apex, drop back to the bottom.
        if (blockY < BLOCK_NEAR_Y)
        {
            blockY = BLOCK_LOW_Y;
        }
    }
    else
    {
        blockY++;

        // Off the bottom, restart from the apex.
        if (blockY > BLOCK_LOW_Y)
        {
            blockY = BLOCK_TOUCH_Y;
        }
    }

    drawBlock(blockY, state.accentColor);
}

// ------------------------------------------------------------
// Serial
// ------------------------------------------------------------

static bool isNumber(const String &value)
{
    bool digit = false;
    bool dot = false;

    for (uint16_t i = 0; i < value.length(); i++)
    {
        char c = value[i];

        if (c == '+' || c == '-')
        {
            if (i != 0)
            {
                return false;
            }
        }
        else if (c == '.')
        {
            if (dot)
            {
                return false;
            }

            dot = true;
        }
        else if (c >= '0' && c <= '9')
        {
            digit = true;
        }
        else
        {
            return false;
        }
    }

    return digit;
}

// Fills heightText and sets showHeight, or explains why it could not.
static void setHeight(const String &value)
{
    String v = value;

    // "b5, 100nm" is as natural to type as "b5, 100".
    if (v.endsWith("nm"))
    {
        v = v.substring(0, v.length() - 2);
        v.trim();
    }

    if (!isNumber(v))
    {
        Serial.print("Not a number: ");
        Serial.println(value);
        return;
    }

    snprintf(heightText, sizeof(heightText), "%s nm", v.c_str());

    // Drop the space before giving up on a long value.
    if (strlen(heightText) > MAX_HEIGHT_CHARS)
    {
        snprintf(heightText, sizeof(heightText), "%snm", v.c_str());
    }

    if (strlen(heightText) > MAX_HEIGHT_CHARS)
    {
        Serial.print("Too many digits to display: ");
        Serial.println(value);
        return;
    }

    showHeight = true;
}

static void handleCommand(String cmd)
{
    cmd.trim();
    cmd.toLowerCase();

    if (cmd.length() == 0)
    {
        return;
    }

    // Split "b5, 100" into the command and its value. A space works too.
    String arg = "";
    int16_t sep = cmd.indexOf(',');

    if (sep < 0)
    {
        sep = cmd.indexOf(' ');
    }

    if (sep >= 0)
    {
        arg = cmd.substring(sep + 1);
        arg.trim();

        cmd = cmd.substring(0, sep);
        cmd.trim();
    }

    for (int16_t i = 0; i < STATE_COUNT; i++)
    {
        if (cmd == STATES[i].cmd)
        {
            showHeight = false;

            if (arg.length() > 0)
            {
                if (STATES[i].acceptsHeight)
                {
                    setHeight(arg);
                }
                else
                {
                    Serial.print("No height shown for ");
                    Serial.println(STATES[i].cmd);
                }
            }

            showState(i);

            Serial.print(STATES[i].name);

            if (showHeight)
            {
                Serial.print(" @ ");
                Serial.print(heightText);
            }

            Serial.println();
            return;
        }
    }

    if (cmd == "red")
    {
        currentState = -1;
        showHeight = false;
        dma_display->fillScreen(COLOR_RED);
        Serial.println("RED");
        return;
    }

    if (cmd == "green")
    {
        currentState = -1;
        showHeight = false;
        dma_display->fillScreen(COLOR_GREEN);
        Serial.println("GREEN");
        return;
    }

    Serial.print("Unknown command: ");
    Serial.println(cmd);
    Serial.println("Type b0-b12 (b5/b6 accept 'b5, 100'), 'red' or 'green'.");
}

// Non-blocking line reader, so the animation keeps running.
static void readSerial()
{
    while (Serial.available())
    {
        char c = Serial.read();

        if (c == '\n' || c == '\r')
        {
            handleCommand(inBuf);
            inBuf = "";
        }
        else
        {
            inBuf += c;
        }
    }
}

// ------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("Starting HUB75...");

    HUB75_I2S_CFG::i2s_pins pins = {
        R1_PIN,
        G1_PIN,
        B1_PIN,

        R2_PIN,
        G2_PIN,
        B2_PIN,

        A_PIN,
        B_PIN,
        C_PIN,
        D_PIN,
        E_PIN,

        LAT_PIN,
        OE_PIN,
        CLK_PIN
    };

    HUB75_I2S_CFG mxconfig(
        PANEL_WIDTH,
        PANEL_HEIGHT,
        CHAIN_LENGTH,
        pins
    );

    // Try FM6126A / ICN2038S initialization.
    mxconfig.driver = HUB75_I2S_CFG::FM6126A;

    // Some panels work better with this clock phase.
    mxconfig.clkphase = false;

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);

    if (!dma_display->begin())
    {
        Serial.println("HUB75 begin FAILED!");
        while (true)
        {
            delay(1000);
        }
    }

    Serial.println("HUB75 begin succeeded!");

    dma_display->setBrightness8(128);

    // Boot into Idle.
    showState(0);

    Serial.println("Ready. Type b0-b12, or 'b5, 100' to name a height in nm.");
}

void loop()
{
    readSerial();
    animate();
}

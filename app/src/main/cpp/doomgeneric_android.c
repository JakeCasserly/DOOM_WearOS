#include <time.h>

#include "AndroidRenderer.h"

#include "doomkeys.h"
#include "doomgeneric.h"
#include "doomstat.h"

#include <android/looper.h>
#define KEYQUEUE_SIZE 16

static int screen_x, screen_y;

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static bool pointer_touched_in(int x, int y, int x2, int y2, int *id)
{
    for (int i = 0; i < 8; ++i)
    {
        if (!button_down[i]) continue;  // Skip inactive touches

        // Check if this touch is within the bounds
        if ((x < button_x[i] && button_x[i] < x2) && (y < button_y[i] && button_y[i] < y2))
        {
            *id = i;
            return true;  // Found a touch in this area
        }
    }
    return false;  // No touch found in this area
}

static void addKeyToQueue(int pressed, unsigned char key)
{
    unsigned short keyData = (pressed << 8) | key;

    s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
    s_KeyQueueWriteIndex++;
    s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
}

static void VirtualButton(int x, int y, int button_id, unsigned char keycode)
{
    static bool pressed[3] = { false, false, false };
    int lw = x + 100;
    int lh = y + 100;

    int touch_id;
    bool is_touching = pointer_touched_in(x, y, lw, lh, &touch_id);

    // Only send events on state CHANGE
    if (is_touching && !pressed[button_id]) {
        addKeyToQueue(1, keycode);
        pressed[button_id] = true;
    }
    else if (!is_touching && pressed[button_id]) {
        addKeyToQueue(0, keycode);
        pressed[button_id] = false;
    }

    if (pressed[button_id])
        RenderCircle(x, y, 50, 0x4c4c4cff);
    else
        RenderCircle(x, y, 50, 0x808080ff);
}

static bool pointer_touched_within_x_bound(int x, bool less, int *id) {
    bool touched = false;
    for (int i = 0; i < 8; ++i)
    {
        // Only check touches that are actually active
        if (!button_down[i]) continue;

        if (less) {
            touched = (x > button_x[i] && button_x[i] > 0);
        }
        else {
            touched = (x < button_x[i] && button_x[i] > 0);
        }
        *id = i;
        if (touched) break;
    }
    return touched;
}

static bool pointer_touched_within_y_bound(int y, bool less, int *id) {
    bool touched = false;
    for (int i = 0; i < 8; ++i)
    {
        // Only check touches that are actually active
        if (!button_down[i]) continue;

        if (less) {
            touched = (y > button_y[i] && button_y[i] > 0);
        }
        else {
            touched = (y < button_y[i] && button_y[i] > 0);
        }
        *id = i;
        if (touched) break;
    }
    return touched;
}

static void Movement(void) {
    static bool forward = false;
    static bool backward = false;
    static bool left = false;
    static bool right = false;

    int id1, id2, id3, id4;

    if (pointer_touched_within_x_bound(100, true, &id1)) {
        if (!left) { addKeyToQueue(1, KEY_LEFTARROW); left = true; }
    } else {
        if (left) { addKeyToQueue(0, KEY_LEFTARROW); left = false; }
    }

    if (pointer_touched_within_x_bound(340, false, &id2)) {
        if (!right) { addKeyToQueue(1, KEY_RIGHTARROW); right = true; }
    } else {
        if (right) { addKeyToQueue(0, KEY_RIGHTARROW); right = false; }
    }

    if (pointer_touched_within_y_bound(100, true, &id3)) {
        if (!forward) { addKeyToQueue(1, KEY_UPARROW); forward = true; }
    } else {
        if (forward) { addKeyToQueue(0, KEY_UPARROW); forward = false; }
    }

    if (pointer_touched_within_y_bound(325, false, &id4)) {
        if (!backward) { addKeyToQueue(1, KEY_DOWNARROW); backward = true; }
    } else {
        if (backward) { addKeyToQueue(0, KEY_DOWNARROW); backward = false; }
    }
}

static void VirtualJoystick(void)
{
    // make center of joystick do nothing
    // https://apply.joinpatch.org/referral
    static bool forward = false;
    static bool backward = false;
    static bool left = false;
    static bool right = false;
    RenderCircle(-50, screen_y - 200, 100, 0x4c4c4cff);
    int id;
    if (pointer_touched_in(screen_x/18, screen_y-300, screen_x/18+200, screen_y-100, &id))
    {
        RenderCircle(motion_x[id] - 80, motion_y[id] - 80, 80, 0x808080ff);
        if (motion_y[id] < screen_y-250) {
            if (!forward) {
                addKeyToQueue(1, KEY_UPARROW);
                forward = true;
            }
            if (backward) {
                addKeyToQueue(0, KEY_DOWNARROW);
                backward = false;
            }
        } else if (screen_y-150 < motion_y[id]) {
            if (!backward) {
                addKeyToQueue(1, KEY_DOWNARROW);
                backward = true;
            }
            if (forward) {
                addKeyToQueue(0, KEY_UPARROW);
                forward = false;
            }
        }

        if (motion_x[id] > screen_x/18+150) {
            if (!right) {
                addKeyToQueue(1, KEY_RIGHTARROW);
                right = true;
            }
            if (left) {
                addKeyToQueue(0, KEY_LEFTARROW);
                left = false;
            }
        } else if (screen_x/18+50 > motion_x[id]) {
            if (!left) {
                addKeyToQueue(1, KEY_LEFTARROW);
                left = true;
            }
            if (right) {
                addKeyToQueue(0, KEY_RIGHTARROW);
                right = false;
            }
        } else {
            goto middle;
        }
    }
    else
    {
        RenderCircle(screen_x / 16, screen_y - 280, 80, 0x808080ff);

        if (forward) {
            addKeyToQueue(0, KEY_UPARROW);
            forward = false;
        }
        if (backward) {
            addKeyToQueue(0, KEY_DOWNARROW);
            backward = false;
        }

        middle:
        if (right) {
            addKeyToQueue(0, KEY_RIGHTARROW);
            right = false;
        }
        if (left) {
            addKeyToQueue(0, KEY_LEFTARROW);
            left = false;
        }
    }
}

void DG_Init(void)
{
    GetScreenDimensions(&screen_x, &screen_y);
    printf("Screen: %dx%d\n", screen_x, screen_y);
    printf("Fire button at: %d,%d\n", screen_x-200, screen_y-320);
    printf("Movement bounds: x<100, x>340, y<100, y>325\n");
}

void DG_DrawFrame(void)
{
    ClearFrame();
    RenderImage(DG_ScreenBuffer, 0,
                0, 320, 200);
    Movement();

    // if (menuactive)
        VirtualButton(screen_x-200, screen_y-225, 0, KEY_ENTER);
    //else
        VirtualButton(screen_x-200, screen_y-320, 1, KEY_FIRE);

    VirtualButton(screen_x-400, screen_y-300, 2, KEY_USE);
    //VirtualButton(screen_x-55, screen_y-100, 2, KEY_ESCAPE);
    HandleInput();
    SwapBuffers();
}

void DG_SleepMs(uint32_t ms)
{
    struct timespec req = {
            .tv_sec = 0,
            .tv_nsec = (long) ms*1000000
    };
    nanosleep(&req, NULL);
}

uint32_t DG_GetTicksMs(void)
{
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);

    return (tp.tv_sec * 1000) + (tp.tv_nsec / 1000000); /* return milliseconds */
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
        return 0; //key queue is empty

    unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex++;
    s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

    *pressed = keyData >> 8;
    *doomKey = keyData & 0xFF;

    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    (void) title;
}

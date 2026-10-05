#ifndef CORE_PLATFORM_FRAMEINPUT_H
#define CORE_PLATFORM_FRAMEINPUT_H

#include "core/platform/Key.h"

// One frame worth of input, with no OS detail in it.
//
// Flow: the platform layer fills this while it pumps messages -> the game layer reads it and turns
// it into intents (see game/InputMapping.h) -> the platform layer clears the per-frame parts again
// via Win32Window::endFrame(). Splitting "who collects" from "who interprets" is what makes the
// key-to-intent mapping unit-testable without a window.
//
// States:
//   * key_down    - level: still held right now (movement, sprint)
//   * key_pressed - edge: went down during this frame (toggles, one-shot actions)
struct FrameInput
{
    bool key_down[kKeyCount] = {};
    bool key_pressed[kKeyCount] = {};

    bool mouse_left_down = false;
    int mouse_x = 0;         // cursor position inside the client area, in pixels
    int mouse_y = 0;
    int mouse_delta_x = 0;   // movement since the previous frame (used for mouse look)
    int mouse_delta_y = 0;
    float wheel_steps = 0.0f;  // wheel notches accumulated during this frame (+ = away from the user)

    int client_width = 0;    // client area size, kept up to date by the platform layer
    int client_height = 0;
    bool resized = false;    // set when the client area changed during this frame

    bool close_requested = false;  // window close button, Alt+F4 or Escape

    bool isDown(Key key) const { return key_down[keyIndex(key)]; }
    bool wasPressed(Key key) const { return key_pressed[keyIndex(key)]; }

    // Clear the per-frame parts (edges, mouse delta, wheel, resize flag, close request).
    // Held state and the cursor position survive: they describe the situation, not the frame.
    void clearPerFrame()
    {
        for (int i = 0; i < kKeyCount; ++i)
        {
            key_pressed[i] = false;
        }
        mouse_delta_x = 0;
        mouse_delta_y = 0;
        wheel_steps = 0.0f;
        resized = false;
        close_requested = false;
    }
};

#endif  // CORE_PLATFORM_FRAMEINPUT_H

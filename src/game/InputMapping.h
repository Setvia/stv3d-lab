#ifndef GAME_INPUTMAPPING_H
#define GAME_INPUTMAPPING_H

#include "core/platform/FrameInput.h"
#include "game/Character.h"

// Which key means what.
//
// The platform layer collects raw input (platform/win32/Win32Window -> FrameInput), and this layer
// interprets it. Keeping the two apart is what makes the mapping unit-testable without a window:
// tests feed a FrameInput in and check the resulting intents.
//
//   W / A / S / D, arrow keys  - walk relative to the camera heading
//   Shift                      - sprint (9 m/s instead of 5 m/s)
//   Space / E                  - jump intent (the controller has no physics yet)
//   F5                         - switch FPV <-> TPV (not Tab: a window may use that for focus)
//   R                          - reset the character and the camera
//   Escape, close button       - quit
//
// C / Q ("down") are recognised by the platform layer but map to nothing: the character has no
// vertical movement yet, so CharacterController::InputState has no field for it.
namespace InputMapping
{

CharacterController::InputState characterInputFromKeys(const FrameInput &input);
bool viewToggleRequested(const FrameInput &input);
bool resetRequested(const FrameInput &input);
bool quitRequested(const FrameInput &input);

}  // namespace InputMapping

#endif  // GAME_INPUTMAPPING_H

#include "InputMapping.h"

namespace InputMapping
{

CharacterController::InputState characterInputFromKeys(const FrameInput &input)
{
    CharacterController::InputState state;

    state.forward = input.isDown(Key::W) || input.isDown(Key::Up);
    state.backward = input.isDown(Key::S) || input.isDown(Key::Down);
    state.left = input.isDown(Key::A) || input.isDown(Key::Left);
    state.right = input.isDown(Key::D) || input.isDown(Key::Right);

    // No physics yet: the intent is wired up now so the controller can act on it later
    state.jump = input.isDown(Key::Space) || input.isDown(Key::E);
    state.sprint = input.isDown(Key::Shift);

    return state;
}

bool viewToggleRequested(const FrameInput &input)
{
    return input.wasPressed(Key::F5);
}

bool resetRequested(const FrameInput &input)
{
    return input.wasPressed(Key::R);
}

bool quitRequested(const FrameInput &input)
{
    return input.wasPressed(Key::Escape) || input.close_requested;
}

}  // namespace InputMapping

#ifndef CORE_PLATFORM_KEY_H
#define CORE_PLATFORM_KEY_H

// Platform-independent key identifiers.
//
// The platform layer (Win32 today) is the only place that knows about virtual key codes and
// scancodes; everything above it speaks this enum. Adding a key means adding one enumerator here and
// one case in Win32Window - no OS header ever reaches core or engine.

enum class Key
{
    W,
    A,
    S,
    D,
    Q,
    E,
    C,
    R,
    F5,
    Space,
    Shift,       // either Shift key
    Left,
    Right,
    Up,
    Down,
    Escape,

    Count        // keep last: it sizes the lookup tables
};

constexpr int kKeyCount = static_cast<int>(Key::Count);

// Index into a per-key table (bool array, ...)
constexpr int keyIndex(Key key)
{
    return static_cast<int>(key);
}

#endif  // CORE_PLATFORM_KEY_H

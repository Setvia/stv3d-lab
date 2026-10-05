#include "Win32Window.h"

#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM (signed extraction from LPARAM)

#include "core/log/LogManager.h"

namespace
{

const char *kWindowClassName = "stv3d_lab_window";

// The integer spelling in the header must match the Win32 types exactly
static_assert(sizeof(WPARAM) == sizeof(unsigned long long), "WPARAM is UINT_PTR; the header assumes 64-bit");
static_assert(sizeof(LPARAM) == sizeof(long long), "LPARAM is LONG_PTR; the header assumes 64-bit");
static_assert(sizeof(LRESULT) == sizeof(long long), "LRESULT is LONG_PTR; the header assumes 64-bit");

// Virtual key code -> Key. Unmapped keys return Key::Count, which callers treat as "ignore".
Key keyFromVirtualKey(WPARAM virtual_key)
{
    switch (virtual_key) {
        case 'W': return Key::W;
        case 'A': return Key::A;
        case 'S': return Key::S;
        case 'D': return Key::D;
        case 'Q': return Key::Q;
        case 'E': return Key::E;
        case 'C': return Key::C;
        case 'R': return Key::R;
        case VK_SPACE: return Key::Space;
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return Key::Shift;
        case VK_LEFT: return Key::Left;
        case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up;
        case VK_DOWN: return Key::Down;
        case VK_F5: return Key::F5;
        case VK_ESCAPE: return Key::Escape;
        default: return Key::Count;
    }
}

// Without this the process would be DPI virtualized by Windows (a 125% display would then hand us a
// scaled, blurry window and, worse, a client size that does not match the pixels we render).
void makeProcessDpiAware()
{
    static bool done = false;
    if (!done) {
        SetProcessDPIAware();
        done = true;
    }
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    Win32Window *self = reinterpret_cast<Win32Window *>(GetWindowLongPtrA(window, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcA(window, message, wparam, lparam);
    }
    return static_cast<LRESULT>(self->handleMessage(window, message,
                                                   static_cast<unsigned long long>(wparam),
                                                   static_cast<long long>(lparam)));
}

}  // namespace

Win32Window::~Win32Window()
{
    destroy();
}

bool Win32Window::create(const Config &config)
{
    if (handle.isValid()) {
        return true;
    }

    makeProcessDpiAware();

    HINSTANCE instance = GetModuleHandleA(nullptr);
    const char *class_name = kWindowClassName;

    WNDCLASSA window_class = {};
    window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;  // CS_OWNDC: a stable DC for GL/WGL
    window_class.lpfnWndProc = windowProcedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    window_class.lpszClassName = class_name;
    RegisterClassA(&window_class);  // a second registration returns 0 with ERROR_CLASS_ALREADY_EXISTS

    // Ask for a client area of exactly width x height, so the window is not smaller than requested
    RECT rect = {0, 0, config.width, config.height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND window = CreateWindowExA(0, class_name, config.title, WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT,
                                  rect.right - rect.left, rect.bottom - rect.top,
                                  nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        LOG_ERROR() << "CreateWindowEx failed, GetLastError = " << logHex(GetLastError(), 8);
        return false;
    }

    handle.window = window;
    handle.instance = instance;
    SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    RECT client = {};
    GetClientRect(window, &client);
    frame_input.client_width = static_cast<int>(client.right);
    frame_input.client_height = static_cast<int>(client.bottom);

    closed = false;
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    SetFocus(window);  // keyboard input goes to the focused window; without this it stays silent
    return true;
}

void Win32Window::destroy()
{
    if (!handle.isValid()) {
        return;
    }

    HWND window = static_cast<HWND>(handle.window);
    SetWindowLongPtrA(window, GWLP_USERDATA, 0);  // no more callbacks into this object
    DestroyWindow(window);
    UnregisterClassA(kWindowClassName, static_cast<HINSTANCE>(handle.instance));

    handle = NativeWindowHandle{};
    frame_input = FrameInput{};
    closed = true;
}

bool Win32Window::pumpMessages()
{
    if (!handle.isValid()) {
        return false;
    }

    MSG message = {};
    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        if (message.message == WM_QUIT) {
            closed = true;
            break;
        }
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return !closed;
}

void Win32Window::requestClose()
{
    if (handle.isValid()) {
        PostMessageA(static_cast<HWND>(handle.window), WM_CLOSE, 0, 0);
    }
}

bool Win32Window::processKey(bool is_down, unsigned long long virtual_key)
{
    const Key key = keyFromVirtualKey(static_cast<WPARAM>(virtual_key));
    if (key == Key::Count) {
        return false;  // not a key the game cares about: let DefWindowProc deal with it
    }

    const int index = keyIndex(key);
    if (is_down) {
        // Windows repeats WM_KEYDOWN while a key is held: only the first one is a "pressed" edge
        if (!frame_input.key_down[index]) {
            frame_input.key_pressed[index] = true;
        }
        frame_input.key_down[index] = true;
    } else {
        frame_input.key_down[index] = false;
    }
    return true;
}

long long Win32Window::handleMessage(void *window_handle, unsigned int message,
                                     unsigned long long wparam, long long lparam)
{
    HWND window = static_cast<HWND>(window_handle);

    switch (message) {
        case WM_CLOSE:
            // The app decides when to stop; it sees this flag in the input of the current frame
            frame_input.close_requested = true;
            closed = true;
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SIZE: {
            const int width = LOWORD(lparam);
            const int height = HIWORD(lparam);
            if (width != frame_input.client_width || height != frame_input.client_height) {
                frame_input.client_width = width;
                frame_input.client_height = height;
                frame_input.resized = true;
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;  // never let GDI paint the background: the frame comes from the render backend

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (processKey(true, wparam)) {
                return 0;
            }
            break;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (processKey(false, wparam)) {
                return 0;
            }
            break;

        case WM_KILLFOCUS:
            // Losing focus swallows the matching WM_KEYUP, which would leave keys "stuck" down
            for (int i = 0; i < kKeyCount; ++i) {
                frame_input.key_down[i] = false;
            }
            frame_input.mouse_left_down = false;
            return 0;

        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            frame_input.mouse_delta_x += x - frame_input.mouse_x;
            frame_input.mouse_delta_y += y - frame_input.mouse_y;
            frame_input.mouse_x = x;
            frame_input.mouse_y = y;
            return 0;
        }

        case WM_LBUTTONDOWN:
            frame_input.mouse_left_down = true;
            SetCapture(window);  // keep receiving mouse moves when the cursor leaves the window
            return 0;

        case WM_LBUTTONUP:
            frame_input.mouse_left_down = false;
            ReleaseCapture();
            return 0;

        case WM_MOUSEWHEEL:
            // One notch is WHEEL_DELTA (120); accumulate so a fast scroll is not lost
            frame_input.wheel_steps += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
            return 0;

        default:
            break;
    }

    return DefWindowProcA(window, message, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
}

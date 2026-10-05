#ifndef GAME_LOOP_H
#define GAME_LOOP_H

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

// Game loop state bits (a one-byte bit field describing the loop's current state)
using GameLoopState = std::uint8_t;
namespace GameLoopFlags
{
    using T = GameLoopState;
    constexpr T EVENT_ACTIVE = 1U << 0;  // event active
    constexpr T TIME_ACTIVE  = 1U << 1;  // time active
    constexpr T INITIALIZED  = 1U << 2;  // initialization complete
    constexpr T OVERLOAD     = 1U << 3;  // resource loading complete
    constexpr T _UNDEF_4     = 1U << 4;  // reserved (was 0U << 4, always 0 -- a pointless placeholder)
    constexpr T _UNDEF_5     = 1U << 5;  // reserved
    constexpr T _UNDEF_6     = 1U << 6;  // reserved
    constexpr T _UNDEF_7     = 1U << 7;  // reserved
}  // namespace GameLoopFlags

using Task = std::function<void()>;

// Task scheduler: three task kinds, triggered by "logic tick / frame / real time" respectively
class TaskScheduler
{
public:
    // Tick tasks: called at the fixed timestep (logic updates)
    void onTick(std::uint64_t tickCount);

    // Frame tasks: called per frame (render interpolation, etc.)
    void onFrame(float dt);

    // Real-time tasks: each frame checks whether they are due (timers, heartbeats)
    void onRealTime(double nowSeconds);

    void addTickTask(Task task);
    void addFrameTask(Task task);
    void addRealTimeTask(Task task);

    void clear();

private:
    std::vector<Task> frame_tasks;
    std::vector<Task> tick_tasks;
    std::vector<Task> real_time_tasks;
};

// Game main loop: fixed-step logic ticks plus one frame step.
//
// No Qt and no window: this class owns neither a timer nor an event loop. The platform layer (Qt
// today, the Win32 message pump from step A3 on) owns those and calls advance() once per turn of its
// pump. Splitting the time source out that way also makes the loop testable: advanceBy() takes the
// elapsed time as an argument, so the accumulator can be exercised with exact numbers instead of
// sleeping in a test.
//
// Note: there is deliberately no blocking `while (state) { ... }` loop. The event pump must keep
// running, otherwise the window stops responding; the loop is advanced from the pump instead.
class GameLoop
{
public:
    using TickCallback = std::function<void(std::uint64_t tickCount)>;
    using FrameCallback = std::function<void(float dt)>;

    GameLoop() = default;
    GameLoop(const GameLoop &) = delete;             // a service object: one owner, no copies
    GameLoop &operator=(const GameLoop &) = delete;

    void init();   // initialize the state bits and the clock
    void start();  // start (runs init first if needed); also re-arms a paused loop and resets the clock
    void pause();  // pause time advance (the state is preserved; frames and their callbacks keep running)
    void exit();   // stop everything and clear all tasks

    GameLoopState getState() const { return loop_state; }
    bool isActive() const { return (loop_state & GameLoopFlags::EVENT_ACTIVE) != 0; }
    std::uint64_t getTickCount() const { return tick_count; }

    TaskScheduler &getScheduler() { return scheduler; }

    // Callbacks replace the Qt signals the loop used to emit
    void setTickCallback(TickCallback callback) { tick_callback = std::move(callback); }
    void setFrameCallback(FrameCallback callback) { frame_callback = std::move(callback); }

    // Frame interval in milliseconds, default 16ms ~ 60fps.
    // This is the interval the platform pump should run at; the loop itself never waits.
    void setFrameInterval(int milliseconds);
    int getFrameInterval() const { return frame_interval_ms; }

    // Fixed timestep of one logic tick (seconds), default 1/60. Physics/character/animation should
    // all step by this to stay frame-rate independent
    double getFixedTickSeconds() const { return fixed_tick_seconds; }
    void setFixedTickSeconds(double seconds);

    // Leftover time that did not add up to a whole tick yet (exposed for tests of the accumulator)
    double getTickAccumulator() const { return tick_accumulator; }

    // Pending queue: executed in order at the start of the next step
    void enqueue(Task task);

    // Advance the loop by one pump turn: sample the clock, then run advanceBy(elapsed)
    void advance();

    // Advance the loop by an explicit amount of time (seconds).
    // This is the deterministic core: advance() only supplies the number.
    void advanceBy(double elapsed_seconds);

private:
    // One step: drain the pending queue, run 0..N logic ticks, then the frame tasks
    void mainLoop(double elapsed_seconds);

    TickCallback tick_callback;
    FrameCallback frame_callback;

    GameLoopState loop_state = 0;
    std::deque<Task> task_queue;
    TaskScheduler scheduler;
    std::chrono::steady_clock::time_point last_time{};

    int frame_interval_ms = 16;
    std::uint64_t tick_count = 0;
    double tick_accumulator = 0.0;
    double fixed_tick_seconds = 1.0 / 60.0;
};

#endif  // GAME_LOOP_H

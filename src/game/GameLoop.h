#ifndef GAME_LOOP_H
#define GAME_LOOP_H

#include "game/LoadQueue.h"

#include <chrono>
#include <cstddef>
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
    constexpr T OVERLOAD     = 1U << 3;  // resource loading complete (derived: the load queue is idle)
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

// Game main loop: fixed-step logic ticks, a slice of streaming loads, plus one frame step.
//
// The platform layer owns the timer and the event loop (the Win32 message pump) and calls advance()
// once per turn of its pump. Splitting the time source out that way also makes the loop testable:
// advanceBy() takes the elapsed time as an argument, so the accumulator can be exercised with exact
// numbers instead of sleeping in a test.
//
// One step is: pending queue -> 0..N logic ticks -> streaming slice -> frame step. See mainLoop().
//
// Note: there is no blocking `while (state) { ... }` loop - the event pump must keep running,
// otherwise the window stops responding; the loop is advanced from the pump instead.
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
    void exit();   // stop everything and clear all tasks and pending loads

    // The live state. OVERLOAD is not stored: it is reported from the load queue, so it is true
    // exactly while nothing is left to load.
    GameLoopState getState() const
    {
        return static_cast<GameLoopState>(loop_state | (load_queue.isIdle() ? GameLoopFlags::OVERLOAD : 0));
    }
    bool isActive() const { return (loop_state & GameLoopFlags::EVENT_ACTIVE) != 0; }
    std::uint64_t getTickCount() const { return tick_count; }

    TaskScheduler &getScheduler() { return scheduler; }

    // The load queue this loop drives. Requests may be made before init()/start(): registering the
    // whole plan up front is what lets the app size what the plan needs (buffer capacity, draw slots)
    // once, instead of growing it while frames may still be reading it. Nothing runs before start().
    LoadQueue &getLoadQueue() { return load_queue; }
    const LoadQueue &getLoadQueue() const { return load_queue; }

    // How many streaming items one turn of the pump may run (default 1). 0 suspends streaming without
    // stopping the loop (a level transition can hold it there). Preload items ignore this: runPreload()
    // is explicit and blocking, because the caller chooses that moment.
    std::size_t getStreamingItemsPerTurn() const { return streaming_items_per_turn; }
    void setStreamingItemsPerTurn(std::size_t itemsPerTurn) { streaming_items_per_turn = itemsPerTurn; }

    // Invoked by the loop: once per logic tick, and once per frame
    void setTickCallback(TickCallback callback) { tick_callback = std::move(callback); }
    void setFrameCallback(FrameCallback callback) { frame_callback = std::move(callback); }

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
    // One step: drain the pending queue, run 0..N logic ticks, stream a slice, then the frame tasks
    void mainLoop(double elapsed_seconds);

    TickCallback tick_callback;
    FrameCallback frame_callback;

    GameLoopState loop_state = 0;
    std::deque<Task> task_queue;
    TaskScheduler scheduler;
    LoadQueue load_queue;
    std::chrono::steady_clock::time_point last_time{};

    std::uint64_t tick_count = 0;
    double tick_accumulator = 0.0;
    double fixed_tick_seconds = 1.0 / 60.0;
    std::size_t streaming_items_per_turn = 1;
};

#endif  // GAME_LOOP_H

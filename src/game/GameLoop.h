#ifndef GAME_LOOP_H
#define GAME_LOOP_H

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

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

// Game main loop.
//
// Note: this **no longer** uses a blocking `while (state) { ... }` loop: Qt's event loop (app.exec())
// must keep running to process window/input events, and blocking the UI thread would freeze it, so
// mainLoop() advances one frame, driven by the internal QTimer with a fixed-timestep accumulator.
class GameLoop : public QObject
{
    Q_OBJECT  // required: without it signals/slots are unusable and connect() fails to compile

public:
    explicit GameLoop(QObject *parent = nullptr);

    void init();   // initialize the state bits and the clock
    void start();  // start (runs init first if needed)
    void pause();  // pause time advance (state is preserved)
    void exit();   // stop the timer and clear all tasks

    GameLoopState getState() const { return loop_state; }
    bool isActive() const { return (loop_state & GameLoopFlags::EVENT_ACTIVE) != 0; }
    std::uint64_t getTickCount() const { return tick_count; }

    TaskScheduler &getScheduler() { return scheduler; }

    // Frame interval in milliseconds, default 16ms ≈ 60fps
    void setFrameInterval(int milliseconds);
    int getFrameInterval() const { return timer.interval(); }

    // Fixed timestep of one logic tick (seconds), default 1/60. Physics/character/animation should all step by this to stay frame-rate independent
    double getFixedTickSeconds() const { return fixed_tick_seconds; }
    void setFixedTickSeconds(double seconds);

    // Pending queue: executed in order at the start of the current frame
    void enqueue(Task task);

signals:
    void ticked(std::uint64_t tickCount);  // every logic tick
    void frameStepped(float dt);           // every frame

private:
    void mainLoop();  // advance one frame (private: driven by the internal timer)

    GameLoopState loop_state = 0;
    std::deque<Task> task_queue;
    TaskScheduler scheduler;
    QTimer timer{this};  // value member + Qt parent/child: no more raw-pointer new (that used to leak)
    QElapsedTimer clock;
    std::uint64_t tick_count = 0;
    double tick_accumulator = 0.0;
    double fixed_tick_seconds = 1.0 / 60.0;
};

#endif  // GAME_LOOP_H

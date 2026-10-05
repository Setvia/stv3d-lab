#include "GameLoop.h"

namespace
{
constexpr int kDefaultFrameIntervalMs = 16;  // default frame interval 16ms ~ 60fps

// A frame may catch up on several logic ticks, but the catch-up count is capped so that a slow
// frame cannot trigger an ever-growing backlog (the classic "death spiral")
constexpr int kMaxSubSteps = 5;
}  // namespace

// ---------------- TaskScheduler ----------------

void TaskScheduler::addTickTask(Task task) { tick_tasks.push_back(std::move(task)); }
void TaskScheduler::addFrameTask(Task task) { frame_tasks.push_back(std::move(task)); }
void TaskScheduler::addRealTimeTask(Task task) { real_time_tasks.push_back(std::move(task)); }

void TaskScheduler::onTick(std::uint64_t tickCount)
{
    (void)tickCount;  // tasks do not care about the tick number yet; the parameter is kept for future use
    for (const Task &task : tick_tasks) {
        if (task) {
            task();
        }
    }
}

void TaskScheduler::onFrame(float dt)
{
    (void)dt;
    for (const Task &task : frame_tasks) {
        if (task) {
            task();
        }
    }
}

void TaskScheduler::onRealTime(double nowSeconds)
{
    (void)nowSeconds;
    for (const Task &task : real_time_tasks) {
        if (task) {
            task();
        }
    }
}

void TaskScheduler::clear()
{
    tick_tasks.clear();
    frame_tasks.clear();
    real_time_tasks.clear();
}

// ---------------- GameLoop ----------------

void GameLoop::init()
{
    loop_state |= GameLoopFlags::INITIALIZED;
    loop_state |= GameLoopFlags::OVERLOAD;  // this project has no asynchronously loaded resources yet
    tick_count = 0;
    tick_accumulator = 0.0;
    last_time = std::chrono::steady_clock::now();
}

void GameLoop::start()
{
    if ((loop_state & GameLoopFlags::INITIALIZED) == 0) {
        init();
    }

    loop_state |= GameLoopFlags::EVENT_ACTIVE;
    loop_state |= GameLoopFlags::TIME_ACTIVE;

    // Reset the clock, so the time spent paused (or before start) does not arrive as one huge delta
    last_time = std::chrono::steady_clock::now();
}

void GameLoop::pause()
{
    loop_state &= static_cast<GameLoopState>(~GameLoopFlags::TIME_ACTIVE);
}

void GameLoop::exit()
{
    task_queue.clear();
    scheduler.clear();
    loop_state = 0;
}

void GameLoop::setFrameInterval(int milliseconds)
{
    frame_interval_ms = milliseconds > 0 ? milliseconds : kDefaultFrameIntervalMs;
}

void GameLoop::setFixedTickSeconds(double seconds)
{
    if (seconds > 0.0) {
        fixed_tick_seconds = seconds;
    }
}

void GameLoop::enqueue(Task task)
{
    if (task) {
        task_queue.push_back(std::move(task));
    }
}

void GameLoop::advance()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double elapsed_seconds = std::chrono::duration<double>(now - last_time).count();
    last_time = now;

    // steady_clock cannot go backwards, but a negative delta must never reach the accumulator
    mainLoop(elapsed_seconds > 0.0 ? elapsed_seconds : 0.0);
}

void GameLoop::advanceBy(double elapsed_seconds)
{
    mainLoop(elapsed_seconds > 0.0 ? elapsed_seconds : 0.0);
}

void GameLoop::mainLoop(double elapsed_seconds)
{
    // (1) pending queue: run it all before this step starts
    while (!task_queue.empty()) {
        Task task = std::move(task_queue.front());
        task_queue.pop_front();
        if (task) {
            task();
        }
    }

    if (!isActive()) {
        return;
    }

    // (2) fixed-step logic ticks; when time is not active only the frame step below runs
    if ((loop_state & GameLoopFlags::TIME_ACTIVE) != 0) {
        tick_accumulator += elapsed_seconds;

        int steps = 0;
        while (tick_accumulator >= fixed_tick_seconds && steps < kMaxSubSteps) {
            tick_accumulator -= fixed_tick_seconds;
            ++tick_count;
            scheduler.onTick(tick_count);
            if (tick_callback) {
                tick_callback(tick_count);
            }
            ++steps;
        }
        if (steps == kMaxSubSteps) {
            tick_accumulator = 0.0;  // too far behind: drop the backlog so it cannot fall further behind
        }
    }

    // (3) per-frame step
    const float dt = static_cast<float>(elapsed_seconds);
    scheduler.onFrame(dt);
    scheduler.onRealTime(elapsed_seconds);
    if (frame_callback) {
        frame_callback(dt);
    }
}

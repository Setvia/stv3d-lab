#include "GameLoop.h"

namespace {
constexpr int kDefaultFrameIntervalMs = 16;  // default frame interval 16ms ≈ 60fps
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

GameLoop::GameLoop(QObject *parent) : QObject(parent)
{
    timer.setInterval(kDefaultFrameIntervalMs);
    connect(&timer, &QTimer::timeout, this, &GameLoop::mainLoop);
}

void GameLoop::init()
{
    loop_state |= GameLoopFlags::INITIALIZED;
    loop_state |= GameLoopFlags::OVERLOAD;  // this project has no asynchronously loaded resources yet
    tick_count = 0;
    tick_accumulator = 0.0;
    clock.start();
}

void GameLoop::start()
{
    if ((loop_state & GameLoopFlags::INITIALIZED) == 0) {
        init();
    }

    loop_state |= GameLoopFlags::EVENT_ACTIVE;
    loop_state |= GameLoopFlags::TIME_ACTIVE;

    clock.restart();
    timer.start();
}

void GameLoop::pause()
{
    loop_state &= static_cast<GameLoopState>(~GameLoopFlags::TIME_ACTIVE);
}

void GameLoop::exit()
{
    timer.stop();
    task_queue.clear();
    scheduler.clear();
    loop_state = 0;
}

void GameLoop::setFrameInterval(int milliseconds)
{
    timer.setInterval(milliseconds > 0 ? milliseconds : kDefaultFrameIntervalMs);
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

void GameLoop::mainLoop()
{
    // (1) pending queue: run it all before this frame starts
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

    // (2) real elapsed time (seconds)
    const double elapsed_seconds = static_cast<double>(clock.restart()) / 1000.0;

    // (3) when time is not active only frame tasks run (e.g. logic paused but still rendering)
    if ((loop_state & GameLoopFlags::TIME_ACTIVE) != 0) {
        tick_accumulator += elapsed_seconds;

        // Advance logic at the fixed timestep: a frame may catch up on several logic ticks, and the catch-up count is capped to prevent a "death spiral"
        constexpr int kMaxSubSteps = 5;
        int steps = 0;
        while (tick_accumulator >= fixed_tick_seconds && steps < kMaxSubSteps) {
            tick_accumulator -= fixed_tick_seconds;
            ++tick_count;
            scheduler.onTick(tick_count);
            emit ticked(tick_count);
            ++steps;
        }
        if (steps == kMaxSubSteps) {
            tick_accumulator = 0.0;  // too far behind: drop the backlog so it cannot fall further and further behind
        }
    }

    // (4) per-frame tasks
    const float dt = static_cast<float>(elapsed_seconds);
    scheduler.onFrame(dt);
    scheduler.onRealTime(elapsed_seconds);
    emit frameStepped(dt);
}

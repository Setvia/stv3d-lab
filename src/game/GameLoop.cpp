#include "GameLoop.h"

namespace {
constexpr int kDefaultFrameIntervalMs = 16;  // default frame interval 16ms ≈ 60fps
}  // namespace

// ---------------- TaskScheduler ----------------

void TaskScheduler::addTickTask(Task task) { m_tick_tasks.push_back(std::move(task)); }
void TaskScheduler::addFrameTask(Task task) { m_frame_tasks.push_back(std::move(task)); }
void TaskScheduler::addRealTimeTask(Task task) { m_real_time_tasks.push_back(std::move(task)); }

void TaskScheduler::onTick(std::uint64_t tickCount)
{
    (void)tickCount;  // tasks do not care about the tick number yet; the parameter is kept for future use
    for (const Task &task : m_tick_tasks) {
        if (task) {
            task();
        }
    }
}

void TaskScheduler::onFrame(float dt)
{
    (void)dt;
    for (const Task &task : m_frame_tasks) {
        if (task) {
            task();
        }
    }
}

void TaskScheduler::onRealTime(double nowSeconds)
{
    (void)nowSeconds;
    for (const Task &task : m_real_time_tasks) {
        if (task) {
            task();
        }
    }
}

void TaskScheduler::clear()
{
    m_tick_tasks.clear();
    m_frame_tasks.clear();
    m_real_time_tasks.clear();
}

// ---------------- GameLoop ----------------

GameLoop::GameLoop(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(kDefaultFrameIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &GameLoop::mainLoop);
}

void GameLoop::init()
{
    m_loop_state |= GameLoopFlags::INITIALIZED;
    m_loop_state |= GameLoopFlags::OVERLOAD;  // this project has no asynchronously loaded resources yet
    m_tick_count = 0;
    m_tick_accumulator = 0.0;
    m_clock.start();
}

void GameLoop::start()
{
    if ((m_loop_state & GameLoopFlags::INITIALIZED) == 0) {
        init();
    }

    m_loop_state |= GameLoopFlags::EVENT_ACTIVE;
    m_loop_state |= GameLoopFlags::TIME_ACTIVE;

    m_clock.restart();
    m_timer.start();
}

void GameLoop::pause()
{
    m_loop_state &= static_cast<GameLoopState>(~GameLoopFlags::TIME_ACTIVE);
}

void GameLoop::exit()
{
    m_timer.stop();
    m_task_queue.clear();
    m_scheduler.clear();
    m_loop_state = 0;
}

void GameLoop::setFrameInterval(int milliseconds)
{
    m_timer.setInterval(milliseconds > 0 ? milliseconds : kDefaultFrameIntervalMs);
}

void GameLoop::setFixedTickSeconds(double seconds)
{
    if (seconds > 0.0) {
        m_fixed_tick_seconds = seconds;
    }
}

void GameLoop::enqueue(Task task)
{
    if (task) {
        m_task_queue.push_back(std::move(task));
    }
}

void GameLoop::mainLoop()
{
    // (1) pending queue: run it all before this frame starts
    while (!m_task_queue.empty()) {
        Task task = std::move(m_task_queue.front());
        m_task_queue.pop_front();
        if (task) {
            task();
        }
    }

    if (!isActive()) {
        return;
    }

    // (2) real elapsed time (seconds)
    const double elapsed_seconds = static_cast<double>(m_clock.restart()) / 1000.0;

    // (3) when time is not active only frame tasks run (e.g. logic paused but still rendering)
    if ((m_loop_state & GameLoopFlags::TIME_ACTIVE) != 0) {
        m_tick_accumulator += elapsed_seconds;

        // Advance logic at the fixed timestep: a frame may catch up on several logic ticks, and the catch-up count is capped to prevent a "death spiral"
        constexpr int kMaxSubSteps = 5;
        int steps = 0;
        while (m_tick_accumulator >= m_fixed_tick_seconds && steps < kMaxSubSteps) {
            m_tick_accumulator -= m_fixed_tick_seconds;
            ++m_tick_count;
            m_scheduler.onTick(m_tick_count);
            emit ticked(m_tick_count);
            ++steps;
        }
        if (steps == kMaxSubSteps) {
            m_tick_accumulator = 0.0;  // too far behind: drop the backlog so it cannot fall further and further behind
        }
    }

    // (4) per-frame tasks
    const float dt = static_cast<float>(elapsed_seconds);
    m_scheduler.onFrame(dt);
    m_scheduler.onRealTime(elapsed_seconds);
    emit frameStepped(dt);
}

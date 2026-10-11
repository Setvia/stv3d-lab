#ifndef GAME_LOAD_QUEUE_H
#define GAME_LOAD_QUEUE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// Handle to one queued load. 0 is never handed out, so it doubles as "no load".
using LoadHandle = std::uint32_t;

constexpr LoadHandle kNoLoad = 0;

// When a request is allowed to run.
//
//   Preload   - has to be finished before the loop starts: what the first logic tick reads, or what
//               the per-tick computations are defined against (terrain, skybox, anything physics or
//               lighting is evaluated on). The app runs these with runPreload() and aborts startup if
//               one of them fails.
//   Streaming - not needed by any per-tick computation, so it may arrive after the loop is already
//               running: NPC models, props, audio. The loop drains these with step(), a bounded number
//               of items per turn, so the cost of one item delays a frame by its share and never by
//               its whole amount.
enum class LoadPriority : std::uint8_t
{
    Preload,
    Streaming,
};

// A load queue: the list of things to load, plus the rule for when each of them runs.
//
// It owns no thread and no timer. The loop decides when work happens - runPreload() for the blocking
// part, step(budget) once per turn for the streaming part - which keeps loading deterministic and
// testable, and keeps every device call on the thread that owns the device.
//
// One load is two functions, run in this order:
//
//   produce()  builds the payload: CPU work only, no device calls. This is the half that could move to
//              a worker thread later without the queue changing shape (the payload just has to end up
//              somewhere the other half can reach).
//   apply()    runs with that payload available, on the caller's thread: upload through
//              IRenderDevice, build the Model, publish it. Skipped when produce() failed.
//
// Either one may return false to fail the item. An empty function means "nothing to do in this phase"
// and counts as success, so a load that needs only one phase can pass only that one.
class LoadQueue
{
public:
    using Job = std::function<bool()>;

    LoadQueue() = default;
    LoadQueue(const LoadQueue &) = delete;  // a service object: one owner, no copies
    LoadQueue &operator=(const LoadQueue &) = delete;

    // Queue one load and get a handle back (for cancel()). Items run in request order within their
    // priority, so the order requests are made in is the order they complete in.
    LoadHandle request(LoadPriority priority, Job produce, Job apply);

    // Abandon a queued item: it will never run, not even its produce phase.
    // Returns false when the handle is unknown or the item already finished.
    bool cancel(LoadHandle handle);

    // Run every Preload item to completion, in request order, right now.
    // Returns true when all of them succeeded; a failure does not stop the remaining ones, so the log
    // shows every broken asset at once instead of one per restart.
    bool runPreload();

    // Run at most maxItems Streaming items (produce + apply each) and return how many ran.
    // GameLoop::mainLoop calls this once per pump turn.
    std::size_t step(std::size_t maxItems);

    // Nothing left to run: every item is done, failed or cancelled. This is what the loop's
    // OVERLOAD ("resource loading complete") bit reports.
    bool isIdle() const { return pendingCount() == 0 && !run_in_progress; }

    std::size_t pendingCount() const;  // queued and not started yet (the running item is not counted)
    std::size_t doneCount() const { return done_count; }
    std::size_t failedCount() const { return failed_count; }

    // Drop every item without running it (used when the loop tears down).
    void clear();

private:
    enum class State : std::uint8_t
    {
        Queued,
        Running,
        Done,
        Failed,
        Cancelled,
    };

    struct Item
    {
        LoadPriority priority = LoadPriority::Streaming;
        Job produce;
        Job apply;
        State state = State::Queued;
    };

    // Runs one item's phases, records the outcome and releases the jobs (an item is one-shot)
    bool runOne(std::size_t index);

    std::vector<Item> items;  // request order; the handle of items[i] is i + 1

    std::size_t done_count = 0;
    std::size_t failed_count = 0;
    bool run_in_progress = false;  // set while an item's phases are on the stack
};

#endif  // GAME_LOAD_QUEUE_H

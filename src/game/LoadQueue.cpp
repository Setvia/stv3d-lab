#include "game/LoadQueue.h"

LoadHandle LoadQueue::request(LoadPriority priority, Job produce, Job apply)
{
    Item item;
    item.priority = priority;
    item.produce = std::move(produce);
    item.apply = std::move(apply);
    items.push_back(std::move(item));

    return static_cast<LoadHandle>(items.size());  // handles start at 1, so kNoLoad stays free
}

bool LoadQueue::cancel(LoadHandle handle)
{
    const std::size_t index = static_cast<std::size_t>(handle) - 1;
    if (handle == kNoLoad || index >= items.size()) {
        return false;
    }

    Item &item = items[index];
    if (item.state != State::Queued) {
        return false;  // already ran, is running, failed or was cancelled before
    }

    item.state = State::Cancelled;
    item.produce = nullptr;  // the scripts are dropped as well, so whatever they captured is released
    item.apply = nullptr;
    return true;
}

bool LoadQueue::runPreload()
{
    bool all_succeeded = true;

    for (std::size_t index = 0; index < items.size(); ++index) {
        const Item &item = items[index];
        if (item.priority == LoadPriority::Preload && item.state == State::Queued) {
            if (!runOne(index)) {
                all_succeeded = false;
            }
        }
    }

    return all_succeeded;
}

std::size_t LoadQueue::step(std::size_t maxItems)
{
    std::size_t ran = 0;

    for (std::size_t index = 0; index < items.size() && ran < maxItems; ++index) {
        const Item &item = items[index];
        if (item.priority == LoadPriority::Streaming && item.state == State::Queued) {
            runOne(index);
            ++ran;
        }
    }

    return ran;
}

std::size_t LoadQueue::pendingCount() const
{
    std::size_t pending = 0;
    for (const Item &item : items) {
        if (item.state == State::Queued) {
            ++pending;
        }
    }
    return pending;
}

void LoadQueue::clear()
{
    items.clear();
    done_count = 0;
    failed_count = 0;
}

bool LoadQueue::runOne(std::size_t index)
{
    Item &item = items[index];

    // Marked as running first, so a query made from inside a phase (a progress log, for instance) does
    // not report this item as still pending
    item.state = State::Running;
    run_in_progress = true;

    const bool produced = !item.produce || item.produce();
    const bool applied = produced && (!item.apply || item.apply());

    run_in_progress = false;
    item.produce = nullptr;  // one-shot: this is where the payload the jobs captured is released
    item.apply = nullptr;
    item.state = applied ? State::Done : State::Failed;

    if (applied) {
        ++done_count;
    } else {
        ++failed_count;
    }

    return applied;
}

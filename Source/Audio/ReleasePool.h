#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace sb
{
// Lets the audio thread drop shared_ptrs without freeing memory itself:
// it parks them here and the message thread empties the pool on a timer.
template <typename T>
class ReleasePool
{
public:
    ReleasePool() { items.reserve (256); }

    void release (std::shared_ptr<T>&& p) // audio thread
    {
        if (p == nullptr) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        if (items.size() < items.capacity()) items.push_back (std::move (p));
        // pool full: keep the reference alive in the overflow slot rather than allocating
        else overflow = std::move (p);
    }

    void clear() // message thread
    {
        std::vector<std::shared_ptr<T>> old;
        old.reserve (256);
        std::shared_ptr<T> of;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            std::swap (old, items);
            std::swap (of, overflow);
        }
        // old (and of) are freed here, on the message thread
    }

private:
    juce::SpinLock lock;
    std::vector<std::shared_ptr<T>> items;
    std::shared_ptr<T> overflow;
};

// Hands a shared_ptr from the message thread to the audio thread.
template <typename T>
class Handoff
{
public:
    void post (std::shared_ptr<T> p) // message thread
    {
        std::shared_ptr<T> old;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            old = std::move (pending);
            pending = std::move (p);
            hasPending = true;
        }
    }
    // audio thread: returns true and fills out if something new arrived
    bool take (std::shared_ptr<T>& out)
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (! sl.isLocked() || ! hasPending) return false;
        out = std::move (pending);
        hasPending = false;
        return true;
    }
private:
    juce::SpinLock lock;
    std::shared_ptr<T> pending;
    bool hasPending = false;
};
}

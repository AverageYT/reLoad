#pragma once

#include "dsp/Wavetable.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>

namespace reload::dsp
{
// Hands wavetables from the message thread to the audio thread without locks,
// and without the audio thread ever freeing memory.
//
//  - publish() (message thread) parks a new table in an atomic slot.
//  - acquire() (audio thread) swaps it in at the start of a block and pushes
//    the table it replaced onto a lock-free retire queue.
//  - collectGarbage() (message thread) frees retired tables.
//
// publish() and collectGarbage() may be called from any non-audio thread
// (hosts sometimes restore state off the message thread); they serialise on a
// mutex the audio thread never touches.
class WavetableExchange
{
public:
    WavetableExchange() = default;
    ~WavetableExchange()
    {
        delete pending.exchange (nullptr);
        delete current;
        collectGarbageLocked();
    }

    // Message thread.
    void publish (std::shared_ptr<const Wavetable> table)
    {
        const std::lock_guard<std::mutex> guard (nonAudioLock);
        auto* node = new Node { std::move (table) };
        // An unconsumed pending node was never seen by the audio thread, so it
        // is safe to delete here.
        delete pending.exchange (node);
        collectGarbageLocked();
    }

    // Message thread.
    void collectGarbage()
    {
        const std::lock_guard<std::mutex> guard (nonAudioLock);
        collectGarbageLocked();
    }

    // Audio thread. Returns the table to use for this block (may be null
    // before the first publish).
    const Wavetable* acquire() noexcept
    {
        // Only swap if the replaced table can be retired; otherwise keep
        // playing the current one and try again next block.
        if (pending.load (std::memory_order_relaxed) != nullptr && retireFifo.getFreeSpace() > 0)
        {
            if (auto* incoming = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (current != nullptr)
                {
                    auto scope = retireFifo.write (1);
                    scope.forEach ([this] (int index) { retired[static_cast<size_t> (index)] = current; });
                }
                current = incoming;
            }
        }
        return current != nullptr ? current->table.get() : nullptr;
    }

private:
    void collectGarbageLocked()
    {
        auto scope = retireFifo.read (retireFifo.getNumReady());
        scope.forEach ([this] (int index) {
            delete retired[static_cast<size_t> (index)];
            retired[static_cast<size_t> (index)] = nullptr;
        });
    }

    struct Node
    {
        std::shared_ptr<const Wavetable> table;
    };

    static constexpr int retireCapacity = 32;

    std::atomic<Node*> pending { nullptr };
    Node* current = nullptr; // audio thread only (and destructor)
    juce::AbstractFifo retireFifo { retireCapacity };
    std::array<Node*, retireCapacity> retired {};
    std::mutex nonAudioLock;

    JUCE_DECLARE_NON_COPYABLE (WavetableExchange)
};
} // namespace reload::dsp

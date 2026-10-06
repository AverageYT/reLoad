#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace reload
{
// Single-producer (message thread) / single-consumer (audio thread) queue for
// short MIDI messages, e.g. from the on-screen keyboard. Lock-free and
// allocation-free, unlike juce::MidiKeyboardState::processNextMidiBuffer().
class MidiInjectionFifo
{
public:
    struct Event
    {
        std::array<std::uint8_t, 3> bytes {};
        int size = 0;
    };

    // Message thread. Returns false if the message is too long or the queue is full.
    bool push (const juce::MidiMessage& m)
    {
        const int size = m.getRawDataSize();
        if (size < 1 || size > 3)
            return false;

        auto scope = fifo.write (1);
        if (scope.blockSize1 + scope.blockSize2 < 1)
            return false;

        scope.forEach ([&] (int index) {
            auto& e = events[static_cast<size_t> (index)];
            std::copy_n (m.getRawData(), size, e.bytes.begin());
            e.size = size;
        });
        return true;
    }

    // Audio thread.
    template <typename Callback>
    void drain (Callback&& callback) noexcept
    {
        auto scope = fifo.read (fifo.getNumReady());
        scope.forEach ([&] (int index) { callback (events[static_cast<size_t> (index)]); });
    }

private:
    static constexpr int capacity = 512;
    juce::AbstractFifo fifo { capacity };
    std::array<Event, capacity> events {};
};
} // namespace reload

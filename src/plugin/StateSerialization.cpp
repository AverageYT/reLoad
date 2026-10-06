#include "plugin/StateSerialization.h"

namespace reload::state
{
namespace
{
    constexpr char magic[4] = { 'R', 'L', 'D', 'S' };
    const juce::Identifier wavetableType ("Wavetable");
} // namespace

juce::ValueTree wavetableToValueTree (const dsp::Wavetable& table)
{
    const auto& info = table.getInfo();
    juce::ValueTree v (wavetableType);
    v.setProperty ("numFrames", table.getNumFrames(), nullptr);
    v.setProperty ("frameSize", dsp::Wavetable::frameSize, nullptr);
    v.setProperty ("rootFrequency", info.rootFrequency, nullptr);
    v.setProperty ("rootNote", info.rootNote, nullptr);
    v.setProperty ("name", info.name, nullptr);
    v.setProperty ("mode", info.mode, nullptr);
    v.setProperty ("reason", info.reason, nullptr);

    // Explicit little-endian float32 so the blob is portable between platforms.
    juce::MemoryOutputStream raw;
    for (auto sample : table.getFrames())
        raw.writeFloat (sample);

    juce::MemoryBlock compressed;
    {
        juce::MemoryOutputStream out (compressed, false);
        juce::GZIPCompressorOutputStream gzip (out, 6);
        gzip.write (raw.getData(), raw.getDataSize());
    }
    v.setProperty ("data", compressed, nullptr);
    return v;
}

std::shared_ptr<const dsp::Wavetable> wavetableFromValueTree (const juce::ValueTree& v)
{
    if (! v.hasType (wavetableType) || static_cast<int> (v.getProperty ("frameSize", 0)) != dsp::Wavetable::frameSize)
        return nullptr;

    const int numFrames = v.getProperty ("numFrames", 0);
    const auto* compressed = v.getProperty ("data").getBinaryData();
    if (numFrames < 1 || numFrames > dsp::Wavetable::maxFrames || compressed == nullptr)
        return nullptr;

    juce::MemoryInputStream compressedIn (*compressed, false);
    juce::GZIPDecompressorInputStream gzip (compressedIn);

    const auto numSamples = static_cast<size_t> (numFrames) * dsp::Wavetable::frameSize;
    std::vector<float> frames (numSamples);
    for (auto& sample : frames)
    {
        if (gzip.isExhausted())
            return nullptr;
        sample = gzip.readFloat();
        if (! std::isfinite (sample))
            sample = 0.0f;
    }

    dsp::WavetableInfo info;
    info.rootFrequency = v.getProperty ("rootFrequency", 440.0);
    info.rootNote = juce::jlimit (0, 127, static_cast<int> (v.getProperty ("rootNote", 69)));
    info.name = v.getProperty ("name", "").toString();
    info.mode = v.getProperty ("mode", "").toString();
    info.reason = v.getProperty ("reason", "").toString();
    if (! (info.rootFrequency > 0.0 && info.rootFrequency < 20000.0))
        info.rootFrequency = 440.0 * std::exp2 ((info.rootNote - 69) / 12.0);

    return dsp::Wavetable::create (std::move (frames), std::move (info));
}

void writeState (const juce::ValueTree& state, juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream out (dest, false);
    out.write (magic, sizeof (magic));
    state.writeToStream (out);
}

juce::ValueTree readState (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= static_cast<int> (sizeof (magic))
        || std::memcmp (data, magic, sizeof (magic)) != 0)
        return {};

    return juce::ValueTree::readFromData (static_cast<const char*> (data) + sizeof (magic),
                                          static_cast<size_t> (sizeInBytes) - sizeof (magic));
}
} // namespace reload::state

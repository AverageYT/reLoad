#include "plugin/AudioFileLoader.h"

namespace reload
{
std::shared_ptr<const analysis::SourceAudio> loadAudioFile (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return nullptr;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Unsupported or unreadable audio file: " + file.getFileName();
        return nullptr;
    }

    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
    {
        error = "The file contains no audio: " + file.getFileName();
        return nullptr;
    }

    const auto maxSamples = static_cast<juce::int64> (maxSourceSeconds * reader->sampleRate);
    const int numSamples = static_cast<int> (std::min (reader->lengthInSamples, maxSamples));
    const int numChannels = static_cast<int> (std::max (1u, reader->numChannels));

    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    if (! reader->read (&buffer, 0, numSamples, 0, true, true))
    {
        error = "Couldn't decode " + file.getFileName();
        return nullptr;
    }

    auto source = std::make_shared<analysis::SourceAudio>();
    source->sampleRate = reader->sampleRate;
    source->name = file.getFileName();
    source->path = file.getFullPathName();
    source->samples.assign (static_cast<size_t> (numSamples), 0.0f);

    const float gain = 1.0f / static_cast<float> (numChannels);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* in = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            source->samples[static_cast<size_t> (i)] += in[i] * gain;
    }
    return source;
}
} // namespace reload

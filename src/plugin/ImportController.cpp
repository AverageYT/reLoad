#include "plugin/ImportController.h"

#include "plugin/AudioFileLoader.h"

namespace reload
{
ImportController::ImportController (PublishFn publishWavetable)
    : publish (std::move (publishWavetable))
{
    selfRef = this;
}

ImportController::~ImportController()
{
    {
        const juce::ScopedLock sl (lock);
        if (cancelCurrent != nullptr)
            cancelCurrent->store (true);
    }
    pool.removeAllJobs (true, 10000);
}

void ImportController::loadFile (const juce::File& file)
{
    startJob (file, nullptr, true);
}

void ImportController::setSettings (const analysis::ImportSettings& newSettings)
{
    std::shared_ptr<const analysis::SourceAudio> current;
    {
        const juce::ScopedLock sl (lock);
        if (newSettings == settings)
            return;
        settings = newSettings;
        current = source;
        if (current == nullptr)
            status = sourcePath.isNotEmpty() ? "Source file not found. Load it again to re-analyse." : "";
    }

    if (current != nullptr)
        startJob ({}, current, true);
    else
        sendChangeMessage();
}

analysis::ImportSettings ImportController::getSettings() const
{
    const juce::ScopedLock sl (lock);
    return settings;
}

analysis::AnalysisReport ImportController::getReport() const
{
    const juce::ScopedLock sl (lock);
    return report;
}

std::shared_ptr<const analysis::SourceAudio> ImportController::getSource() const
{
    const juce::ScopedLock sl (lock);
    return source;
}

juce::String ImportController::getSourcePath() const
{
    const juce::ScopedLock sl (lock);
    return sourcePath;
}

juce::String ImportController::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

void ImportController::startJob (const juce::File& file, std::shared_ptr<const analysis::SourceAudio> existing, bool analyseAfterLoad)
{
    analysis::ImportSettings jobSettings;
    int jobGeneration = 0;
    auto cancel = std::make_shared<std::atomic<bool>> (false);
    {
        const juce::ScopedLock sl (lock);
        if (cancelCurrent != nullptr)
            cancelCurrent->store (true);
        cancelCurrent = cancel;
        jobGeneration = ++generation;
        jobSettings = settings;
        status = existing == nullptr ? "Loading " + file.getFileName() + "..." : "Analysing...";
    }
    busy = true;
    sendChangeMessage();

    auto weak = selfRef;
    pool.addJob ([weak, file, existing, analyseAfterLoad, jobSettings, cancel, jobGeneration] {
        auto src = existing;
        juce::String loadError;
        if (src == nullptr)
            src = loadAudioFile (file, loadError);

        analysis::AnalysisResult result;
        const bool analysed = src != nullptr && analyseAfterLoad;
        if (analysed)
            result = analysis::analyse (*src, jobSettings, cancel.get());

        if (cancel->load())
            return;

        juce::MessageManager::callAsync ([weak, finished = std::move (result), src, analysed, loadError, jobGeneration]() mutable {
            if (auto* self = weak.get())
                self->finishJob (jobGeneration, std::move (finished), std::move (src), analysed, loadError);
        });
    });
}

void ImportController::finishJob (int jobGeneration, analysis::AnalysisResult result,
                                  std::shared_ptr<const analysis::SourceAudio> loaded, bool analysed, const juce::String& loadError)
{
    {
        const juce::ScopedLock sl (lock);
        if (jobGeneration != generation)
            return; // superseded by a newer request
    }
    busy = false;

    if (loaded == nullptr)
    {
        // Load failed: keep the previous source and table.
        const juce::ScopedLock sl (lock);
        status = loadError;
    }
    else if (! analysed)
    {
        // Display-only reload after restoring a project.
        const juce::ScopedLock sl (lock);
        source = loaded;
        status = {};
    }
    else
    {
        applyResult (result, loaded);
        return; // applyResult sends the change message
    }
    sendChangeMessage();
}

void ImportController::applyResult (const analysis::AnalysisResult& result, std::shared_ptr<const analysis::SourceAudio> loaded)
{
    {
        const juce::ScopedLock sl (lock);
        if (loaded != nullptr)
        {
            source = loaded;
            sourcePath = loaded->path;
        }
        if (result.report.ok)
        {
            report = result.report;
            status = {};
        }
        else
        {
            // Keep the previous table; show why the new analysis failed.
            status = result.report.error;
        }
    }

    if (result.report.ok && result.wavetable != nullptr)
        publish (result.wavetable);

    sendChangeMessage();
}

juce::ValueTree ImportController::toValueTree() const
{
    const juce::ScopedLock sl (lock);
    juce::ValueTree v ("Import");
    v.setProperty ("sourcePath", sourcePath, nullptr);
    v.appendChild (settings.toValueTree(), nullptr);
    v.appendChild (report.toValueTree(), nullptr);
    return v;
}

void ImportController::restoreFromValueTree (const juce::ValueTree& v)
{
    juce::String path;
    {
        const juce::ScopedLock sl (lock);
        if (cancelCurrent != nullptr)
            cancelCurrent->store (true);
        ++generation;
        busy = false;

        settings = analysis::ImportSettings::fromValueTree (v.getChildWithName ("ImportSettings"));
        report = analysis::AnalysisReport::fromValueTree (v.getChildWithName ("AnalysisReport"));
        sourcePath = v.getProperty ("sourcePath", "").toString();
        source = nullptr;
        status = {};
        path = sourcePath;
    }

    const juce::File file (juce::File::isAbsolutePath (path) ? path : juce::String());
    if (path.isNotEmpty() && file.existsAsFile())
        startJob (file, nullptr, false);
    else
        sendChangeMessage();
}
} // namespace reload

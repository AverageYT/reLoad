#pragma once

#include "analysis/Analyzer.h"

#include <juce_events/juce_events.h>

#include <functional>

namespace reload
{
// Owns the import workflow: decoding the dropped file, running analysis on a
// background thread, and handing the finished wavetable to the processor.
// Public methods are for the message thread unless noted. Listeners get a
// change message whenever the source, report, status or busy flag changes.
class ImportController : public juce::ChangeBroadcaster
{
public:
    using PublishFn = std::function<void (std::shared_ptr<const dsp::Wavetable>)>;

    explicit ImportController (PublishFn publishWavetable);
    ~ImportController() override;

    void loadFile (const juce::File& file);
    void setSettings (const analysis::ImportSettings& newSettings);

    analysis::ImportSettings getSettings() const;
    analysis::AnalysisReport getReport() const;
    std::shared_ptr<const analysis::SourceAudio> getSource() const;
    juce::String getSourcePath() const;
    juce::String getStatus() const;  // progress or error text; empty when idle and OK
    bool isBusy() const noexcept { return busy.load(); }

    // Applies a finished analysis: publishes the table and updates the report.
    void applyResult (const analysis::AnalysisResult& result, std::shared_ptr<const analysis::SourceAudio> source);

    // State. Thread-safe. Restoring does not re-analyse (the saved wavetable
    // is authoritative); it only reloads the source file for display if it
    // still exists.
    juce::ValueTree toValueTree() const;
    void restoreFromValueTree (const juce::ValueTree& tree);

private:
    struct Job;
    void startJob (const juce::File& file, std::shared_ptr<const analysis::SourceAudio> source, bool analyseAfterLoad);
    void finishJob (int generation, analysis::AnalysisResult result,
                    std::shared_ptr<const analysis::SourceAudio> source, bool analysed, const juce::String& loadError);

    PublishFn publish;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("reLoad analysis").withNumberOfThreads (1) };

    mutable juce::CriticalSection lock;
    analysis::ImportSettings settings;
    analysis::AnalysisReport report;
    std::shared_ptr<const analysis::SourceAudio> source;
    juce::String sourcePath;
    juce::String status;

    std::atomic<bool> busy { false };
    int generation = 0;
    std::shared_ptr<std::atomic<bool>> cancelCurrent;
    // Created once in the constructor so jobs can be started from any thread
    // (lazily creating a weak-reference master isn't thread-safe).
    juce::WeakReference<ImportController> selfRef;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ImportController)
    JUCE_DECLARE_NON_COPYABLE (ImportController)
};
} // namespace reload

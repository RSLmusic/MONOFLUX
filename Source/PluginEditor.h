#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class MonoFluxEditor : public juce::AudioProcessorEditor,
                       private juce::AudioProcessorValueTreeState::Listener,
                       private juce::Timer
{
public:
    explicit MonoFluxEditor (MonoFluxProcessor&);
    ~MonoFluxEditor() override;

    void resized() override;

private:
    // ── Inner WebView ─────────────────────────────────────────────────────────
    class UIWebView : public juce::WebBrowserComponent
    {
    public:
        explicit UIWebView (MonoFluxEditor& owner)
            : juce::WebBrowserComponent (
                  juce::WebBrowserComponent::Options{}
                      .withKeepPageLoadedWhenBrowserIsHidden()),
              owner (owner)
        {}

        // Intercepts juce://param?id=<paramId>&v=<0-1> from JavaScript
        bool pageAboutToLoad (const juce::String& url) override;

        // Initialises the UI with all current parameter values after page load
        void pageFinishedLoading (const juce::String& url) override;

    private:
        MonoFluxEditor& owner;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UIWebView)
    };

    // ── Helpers ───────────────────────────────────────────────────────────────
    void deployHtml();
    juce::String buildParamJson() const;

    // AudioProcessorValueTreeState::Listener — updates WebView on automation
    void parameterChanged (const juce::String& id, float value) override;

    // Timer — pushes meter levels to WebView at ~30 fps
    void timerCallback() override;

    // ── Members ───────────────────────────────────────────────────────────────
    MonoFluxProcessor& proc;
    UIWebView          webView;
    juce::File         htmlTempFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonoFluxEditor)
};

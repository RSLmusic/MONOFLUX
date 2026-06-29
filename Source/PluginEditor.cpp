#include "PluginEditor.h"
#include <BinaryData.h>

// ── Constructor / Destructor ─────────────────────────────────────────────────

MonoFluxEditor::MonoFluxEditor(MonoFluxProcessor& p)
    : AudioProcessorEditor(p), proc(p), webView(*this)
{
    setSize(1200, 720);
    setResizable(false, false);

    // Write embedded HTML to a per-session temp file and load it
    deployHtml();

    addAndMakeVisible(webView);
    webView.goToURL(juce::URL(htmlTempFile).toString(false));

    // Subscribe to every APVTS parameter so automation updates the WebView
    for (auto* param : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
            proc.apvts.addParameterListener(rp->getParameterID(), this);

    startTimerHz(30); // meter refresh
}

MonoFluxEditor::~MonoFluxEditor()
{
    stopTimer();
    for (auto* param : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param))
            proc.apvts.removeParameterListener(rp->getParameterID(), this);
}

// ── Layout ───────────────────────────────────────────────────────────────────

void MonoFluxEditor::resized()
{
    webView.setBounds(getLocalBounds());
}

// ── HTML deployment ───────────────────────────────────────────────────────────

void MonoFluxEditor::deployHtml()
{
    const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("MonoFlux");
    tempDir.createDirectory();

    htmlTempFile = tempDir.getChildFile("index.html");
    htmlTempFile.replaceWithData(BinaryData::index_html,
                                 (size_t) BinaryData::index_htmlSize);
}

// ── WebView callbacks ─────────────────────────────────────────────────────────

bool MonoFluxEditor::UIWebView::pageAboutToLoad(const juce::String& url)
{
    // JS sends:  window.location = 'juce://param?id=<id>&v=<0..1>'
    // We parse it, update APVTS, and return false to cancel the navigation.
    if (url.startsWith("juce://param?"))
    {
        const auto query = url.fromFirstOccurrenceOf("?", false, false);
        juce::StringPairArray kv;
        for (const auto& token : juce::StringArray::fromTokens(query, "&", ""))
        {
            const auto k = token.upToFirstOccurrenceOf("=", false, false);
            const auto v = juce::URL::removeEscapeChars(
                token.fromFirstOccurrenceOf("=", false, false));
            kv.set(k, v);
        }

        const auto paramId = kv["id"];
        const float value  = kv["v"].getFloatValue();

        if (paramId.isNotEmpty())
            if (auto* p = owner.proc.apvts.getParameter(paramId))
                p->setValueNotifyingHost(value);

        return false; // Do not navigate — keeps the page intact
    }
    return true;
}

void MonoFluxEditor::UIWebView::pageFinishedLoading(const juce::String&)
{
    // Push all current values to the UI after the page is ready
    evaluateJavascript("window.monofluxSetAllParams(" + owner.buildParamJson() + ")");
}

// ── Parameter → WebView sync ─────────────────────────────────────────────────

juce::String MonoFluxEditor::buildParamJson() const
{
    // Only the float params whose IDs match canvas data-param attributes,
    // plus flux_value which the slider uses.
    static const char* ids[] = {
        "inputTrim", "outputTrim", "flux_value",
        "chorus_rate",   "chorus_depth",  "chorus_width",  "chorus_mix",
        "tremolo_rate",  "tremolo_depth", "tremolo_shape", "tremolo_mix",
        "sat_drive",     "sat_tone",      "sat_character", "sat_mix",
        "dist_gain",     "dist_bite",     "dist_texture",  "dist_mix",
        "contrast",      "intensity",     "focus",         "influence",
        nullptr
    };

    auto obj = std::make_unique<juce::DynamicObject>();
    for (int i = 0; ids[i] != nullptr; ++i)
        if (auto* raw = proc.apvts.getRawParameterValue(ids[i]))
            obj->setProperty(ids[i], (double) raw->load());

    return juce::JSON::toString(juce::var(obj.release()), true);
}

void MonoFluxEditor::parameterChanged(const juce::String& id, float value)
{
    // APVTS listener fires on any thread — dispatch to message thread before
    // calling evaluateJavascript which requires the message thread.
    juce::MessageManager::callAsync([this, id, value]()
    {
        webView.evaluateJavascript(
            "window.monofluxSetParam('" + id + "'," +
            juce::String(value, 8) + ")");
    });
}

// ── Meter timer ───────────────────────────────────────────────────────────────

void MonoFluxEditor::timerCallback()
{
    auto toDb = [](float v) -> float
    {
        return v > 1e-6f ? 20.0f * std::log10(v) : -96.0f;
    };

    const float inL  = toDb(proc.meterInL .load(std::memory_order_relaxed));
    const float inR  = toDb(proc.meterInR .load(std::memory_order_relaxed));
    const float outL = toDb(proc.meterOutL.load(std::memory_order_relaxed));
    const float outR = toDb(proc.meterOutR.load(std::memory_order_relaxed));

    webView.evaluateJavascript(
        "window.monofluxSetMeters(" +
        juce::String(inL,  1) + "," +
        juce::String(inR,  1) + "," +
        juce::String(outL, 1) + "," +
        juce::String(outR, 1) + ")");
}

#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── Parameter layout ─────────────────────────────────────────────────────────

juce::AudioProcessorValueTreeState::ParameterLayout MonoFluxProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto addFloat = [&](const juce::String& id, const juce::String& name, float def)
    {
        p.push_back(std::make_unique<juce::AudioParameterFloat>(
            id, name, juce::NormalisableRange<float>(0.0f, 1.0f), def));
    };
    auto addBool = [&](const juce::String& id, const juce::String& name, bool def)
    {
        p.push_back(std::make_unique<juce::AudioParameterBool>(id, name, def));
    };
    auto addChoice = [&](const juce::String& id, const juce::String& name,
                         juce::StringArray choices, int def)
    {
        p.push_back(std::make_unique<juce::AudioParameterChoice>(id, name, choices, def));
    };

    // UI knob params (match data-param attributes exactly)
    addFloat("inputTrim",     "Input Trim",      0.5f);
    addFloat("outputTrim",    "Output Trim",     0.5f);
    addFloat("flux_value",    "Flux",            0.0f);
    addFloat("chorus_rate",   "Chorus Rate",     0.5f);
    addFloat("chorus_depth",  "Chorus Depth",    0.5f);
    addFloat("chorus_width",  "Chorus Width",    0.5f);
    addFloat("chorus_mix",    "Chorus Mix",      0.5f);
    addFloat("tremolo_rate",  "Tremolo Rate",    0.5f);
    addFloat("tremolo_depth", "Tremolo Depth",   0.5f);
    addFloat("tremolo_shape", "Tremolo Shape",   0.0f);
    addFloat("tremolo_mix",   "Tremolo Mix",     0.5f);
    addFloat("sat_drive",     "Sat Drive",       0.3f);
    addFloat("sat_tone",      "Sat Tone",        0.5f);
    addFloat("sat_character", "Sat Character",   0.5f);
    addFloat("sat_mix",       "Sat Mix",         0.5f);
    addFloat("dist_gain",     "Dist Gain",       0.3f);
    addFloat("dist_bite",     "Dist Bite",       0.5f);
    addFloat("dist_texture",  "Dist Texture",    0.5f);
    addFloat("dist_mix",      "Dist Mix",        0.5f);
    addFloat("contrast",      "Contrast",        0.5f);
    addFloat("intensity",     "Intensity",       0.5f);
    addFloat("focus",         "Focus",           0.5f);
    addFloat("influence",     "Influence",       0.5f);

    // Engage / toggle booleans
    addBool("movement_engage",  "Movement Engage",  false);
    addBool("tremolo_engage",   "Tremolo Engage",   false);
    addBool("harmonics_engage", "Harmonics Engage", false);
    addBool("dw_lock",          "D/W Lock",         false);
    addBool("phase_flip",       "Phase Flip",       false);
    addBool("polarity",         "Polarity",         false);
    addBool("hq_mode",          "HQ Mode",          false);
    addBool("analog_mode",      "Analog Mode",      true);

    // Segmented choices
    addChoice("stereo_mode", "Stereo Mode",
              juce::StringArray{"Stereo", "M/S", "Mono"}, 0);
    addChoice("oversample",  "Oversample",
              juce::StringArray{"1x", "2x", "4x", "8x"}, 0);
    addChoice("sat_mode",    "Sat Mode",
              juce::StringArray{"Smooth", "Warm", "Tape", "Tube", "Vintage"}, 0);
    addChoice("dist_mode",   "Dist Mode",
              juce::StringArray{"Soft", "Crunch", "Aggr", "Fuzz", "Digital"}, 0);

    return { p.begin(), p.end() };
}

// ── Constructor / Destructor ─────────────────────────────────────────────────

MonoFluxProcessor::MonoFluxProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MonoFluxState", createParameterLayout())
{
}

MonoFluxProcessor::~MonoFluxProcessor() {}

// ── Bus layout ───────────────────────────────────────────────────────────────

bool MonoFluxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

// ── Prepare / Release ────────────────────────────────────────────────────────

void MonoFluxProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    tremoloPhase      = 0.0;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = (juce::uint32) getTotalNumOutputChannels();

    inputGain.prepare(spec);
    inputGain.setRampDurationSeconds(0.005);

    chorus.prepare(spec);
    chorus.setCentreDelay(7.0f);
    chorus.setFeedback(0.05f);

    outputGain.prepare(spec);
    outputGain.setRampDurationSeconds(0.005);
}

void MonoFluxProcessor::releaseResources() {}

// ── Process block ────────────────────────────────────────────────────────────

void MonoFluxProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numCh  = buffer.getNumChannels();
    const auto numSmp = buffer.getNumSamples();

    // Input trim: 0 → -12 dB, 0.5 → 0 dB, 1 → +12 dB
    const float inTrimN = *apvts.getRawParameterValue("inputTrim");
    inputGain.setGainDecibels((inTrimN - 0.5f) * 24.0f);
    {
        auto block = juce::dsp::AudioBlock<float>(buffer);
        inputGain.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    measureLevels(buffer, meterInL, meterInR);

    // Movement (chorus + optional tremolo)
    if (*apvts.getRawParameterValue("movement_engage") >= 0.5f)
        applyMovement(buffer);

    // Harmonics (saturation + distortion)
    if (*apvts.getRawParameterValue("harmonics_engage") >= 0.5f)
        applyHarmonics(buffer);

    // Output trim
    const float outTrimN = *apvts.getRawParameterValue("outputTrim");
    outputGain.setGainDecibels((outTrimN - 0.5f) * 24.0f);
    {
        auto block = juce::dsp::AudioBlock<float>(buffer);
        outputGain.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    // Phase flip (invert all channels)
    if (*apvts.getRawParameterValue("phase_flip") >= 0.5f)
        for (int ch = 0; ch < numCh; ++ch)
            juce::FloatVectorOperations::negate(buffer.getWritePointer(ch),
                                                buffer.getReadPointer(ch), numSmp);

    // Polarity (invert R channel only)
    if (*apvts.getRawParameterValue("polarity") >= 0.5f && numCh >= 2)
        juce::FloatVectorOperations::negate(buffer.getWritePointer(1),
                                            buffer.getReadPointer(1), numSmp);

    measureLevels(buffer, meterOutL, meterOutR);
}

// ── Movement section ─────────────────────────────────────────────────────────

void MonoFluxProcessor::applyMovement(juce::AudioBuffer<float>& buffer)
{
    const float chorusMix   = *apvts.getRawParameterValue("chorus_mix");
    const float chorusRate  = *apvts.getRawParameterValue("chorus_rate")  * 4.9f + 0.1f;
    const float chorusDepth = *apvts.getRawParameterValue("chorus_depth") * 0.94f + 0.01f;
    const float chorusWidth = *apvts.getRawParameterValue("chorus_width");

    chorus.setRate(chorusRate);
    chorus.setDepth(chorusDepth);
    chorus.setMix(chorusMix);

    {
        auto block = juce::dsp::AudioBlock<float>(buffer);
        chorus.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    // Post-chorus stereo width via M/S matrix
    const int numCh  = buffer.getNumChannels();
    const int numSmp = buffer.getNumSamples();
    if (numCh >= 2 && std::abs(chorusWidth - 0.5f) > 0.01f)
    {
        const float w = chorusWidth * 2.0f; // 0 = mono, 1.0 = unity, 2.0 = max wide
        auto* L = buffer.getWritePointer(0);
        auto* R = buffer.getWritePointer(1);
        for (int i = 0; i < numSmp; ++i)
        {
            const float m =  (L[i] + R[i]) * 0.5f;
            const float s =  (L[i] - R[i]) * 0.5f;
            L[i] = m + s * w;
            R[i] = m - s * w;
        }
    }

    if (*apvts.getRawParameterValue("tremolo_engage") >= 0.5f)
        applyTremolo(buffer);
}

void MonoFluxProcessor::applyTremolo(juce::AudioBuffer<float>& buffer)
{
    const float rate  = *apvts.getRawParameterValue("tremolo_rate")  * 9.9f + 0.1f;
    const float depth = *apvts.getRawParameterValue("tremolo_depth");
    const float shape = *apvts.getRawParameterValue("tremolo_shape");
    const float mix   = *apvts.getRawParameterValue("tremolo_mix");

    const int    numCh  = buffer.getNumChannels();
    const int    numSmp = buffer.getNumSamples();
    const double inc    = juce::MathConstants<double>::twoPi * rate / currentSampleRate;

    for (int i = 0; i < numSmp; ++i)
    {
        const float sine = (float) std::sin(tremoloPhase);
        float lfo;

        if (shape < 0.5f)
        {
            // Blend sine → triangle
            const float t   = (float)(tremoloPhase / juce::MathConstants<double>::twoPi);
            const float tri = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
            lfo = sine + (tri - sine) * (shape * 2.0f);
        }
        else
        {
            // Blend triangle → square
            const float t   = (float)(tremoloPhase / juce::MathConstants<double>::twoPi);
            const float tri = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
            const float sq  = tremoloPhase < juce::MathConstants<double>::pi ? 1.0f : -1.0f;
            lfo = tri + (sq - tri) * ((shape - 0.5f) * 2.0f);
        }

        // Amplitude: (1 - depth) to 1.0
        const float amp     = 1.0f - depth * 0.5f * (1.0f - lfo);
        const float wetGain = mix * amp + (1.0f - mix);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer(ch)[i] *= wetGain;

        tremoloPhase += inc;
        if (tremoloPhase >= juce::MathConstants<double>::twoPi)
            tremoloPhase -= juce::MathConstants<double>::twoPi;
    }
}

// ── Harmonics section ────────────────────────────────────────────────────────

void MonoFluxProcessor::applyHarmonics(juce::AudioBuffer<float>& buffer)
{
    applySaturation(buffer);
    applyDistortion(buffer);
}

void MonoFluxProcessor::applySaturation(juce::AudioBuffer<float>& buffer)
{
    const float drive  = *apvts.getRawParameterValue("sat_drive");
    const float mix    = *apvts.getRawParameterValue("sat_mix");
    const int   mode   = (int) *apvts.getRawParameterValue("sat_mode");
    const int   numCh  = buffer.getNumChannels();
    const int   numSmp = buffer.getNumSamples();

    if (mix < 0.001f) return;

    juce::AudioBuffer<float> dry(numCh, numSmp);
    for (int ch = 0; ch < numCh; ++ch)
        dry.copyFrom(ch, 0, buffer.getReadPointer(ch), numSmp);

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < numSmp; ++i)
            data[i] = satWaveshape(data[i], mode, drive);
    }

    applyWetDryMix(buffer, dry, mix);
}

void MonoFluxProcessor::applyDistortion(juce::AudioBuffer<float>& buffer)
{
    const float gain   = *apvts.getRawParameterValue("dist_gain");
    const float mix    = *apvts.getRawParameterValue("dist_mix");
    const int   mode   = (int) *apvts.getRawParameterValue("dist_mode");
    const int   numCh  = buffer.getNumChannels();
    const int   numSmp = buffer.getNumSamples();

    if (mix < 0.001f) return;

    juce::AudioBuffer<float> dry(numCh, numSmp);
    for (int ch = 0; ch < numCh; ++ch)
        dry.copyFrom(ch, 0, buffer.getReadPointer(ch), numSmp);

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < numSmp; ++i)
            data[i] = distWaveshape(data[i], mode, gain);
    }

    applyWetDryMix(buffer, dry, mix);
}

// ── Waveshapers ───────────────────────────────────────────────────────────────

float MonoFluxProcessor::satWaveshape(float x, int mode, float drive) noexcept
{
    const float d  = 1.0f + drive * 15.0f; // 1–16x
    const float xd = x * d;

    switch (mode)
    {
        case 0: // Smooth — normalised tanh
        {
            const float norm = std::tanh(d);
            return norm > 1e-4f ? std::tanh(xd) / norm : x;
        }
        case 1: // Warm — algebraic soft clip
            return xd / (1.0f + std::abs(xd));

        case 2: // Tape — sine-domain fold
        {
            const float half_pi = juce::MathConstants<float>::pi * 0.5f;
            return std::sin(juce::jlimit(-half_pi, half_pi, xd * half_pi));
        }
        case 3: // Tube — asymmetric tanh
        {
            if (xd >= 0.0f)
                return std::tanh(xd * 1.3f) / std::tanh(d * 1.3f);
            return std::tanh(xd * 0.7f) / std::tanh(d * 0.7f);
        }
        case 4: // Vintage — exponential curve
        {
            const float sign  = xd >= 0.0f ? 1.0f : -1.0f;
            const float abXd  = std::abs(xd);
            const float denom = 1.0f - std::exp(-d);
            return denom > 1e-4f ? sign * (1.0f - std::exp(-abXd)) / denom : x;
        }
        default: return x;
    }
}

float MonoFluxProcessor::distWaveshape(float x, int mode, float gain) noexcept
{
    const float d  = 1.0f + gain * 30.0f; // 1–31x
    const float xd = x * d;

    switch (mode)
    {
        case 0: // Soft
            return std::tanh(xd) * 0.95f;

        case 1: // Crunch — tanh + 2nd harmonic
        {
            const float t = std::tanh(xd * 0.8f);
            return t + 0.2f * t * std::abs(t);
        }
        case 2: // Aggr — hard clip
            return juce::jlimit(-0.95f, 0.95f, xd);

        case 3: // Fuzz — full-wave rectify with sign restore
        {
            const float r = std::abs(std::tanh(xd));
            return (x >= 0.0f ? r : -r) * 0.9f;
        }
        case 4: // Digital — quantised staircase
        {
            const float t = juce::jlimit(-1.0f, 1.0f, xd * 0.5f);
            return std::round(t * 12.0f) / 12.0f;
        }
        default: return x;
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void MonoFluxProcessor::applyWetDryMix(juce::AudioBuffer<float>& wet,
                                        const juce::AudioBuffer<float>& dry,
                                        float mix)
{
    const int numCh  = wet.getNumChannels();
    const int numSmp = wet.getNumSamples();
    const float dryGain = 1.0f - mix;

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto*       w = wet.getWritePointer(ch);
        const auto* d = dry.getReadPointer(ch);
        for (int i = 0; i < numSmp; ++i)
            w[i] = w[i] * mix + d[i] * dryGain;
    }
}

void MonoFluxProcessor::measureLevels(const juce::AudioBuffer<float>& buf,
                                       std::atomic<float>& l,
                                       std::atomic<float>& r) noexcept
{
    if (buf.getNumChannels() >= 1)
        l.store(buf.getMagnitude(0, 0, buf.getNumSamples()),
                std::memory_order_relaxed);
    if (buf.getNumChannels() >= 2)
        r.store(buf.getMagnitude(1, 0, buf.getNumSamples()),
                std::memory_order_relaxed);
}

// ── State persistence ────────────────────────────────────────────────────────

void MonoFluxProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void MonoFluxProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// ── Editor factory ───────────────────────────────────────────────────────────

juce::AudioProcessorEditor* MonoFluxProcessor::createEditor()
{
    return new MonoFluxEditor(*this);
}

// ── Plugin entry point ───────────────────────────────────────────────────────

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MonoFluxProcessor();
}

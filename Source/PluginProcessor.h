#pragma once
#include <JuceHeader.h>

class MonoFluxProcessor : public juce::AudioProcessor
{
public:
    MonoFluxProcessor();
    ~MonoFluxProcessor() override;

    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName()     const override { return "MonoFlux"; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Thread-safe peak levels — written by audio thread, read by UI timer
    std::atomic<float> meterInL  { 0.0f };
    std::atomic<float> meterInR  { 0.0f };
    std::atomic<float> meterOutL { 0.0f };
    std::atomic<float> meterOutR { 0.0f };

private:
    double currentSampleRate { 44100.0 };

    juce::dsp::Gain<float>   inputGain;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Gain<float>   outputGain;

    double tremoloPhase { 0.0 };

    static float satWaveshape  (float x, int mode, float drive) noexcept;
    static float distWaveshape (float x, int mode, float gain)  noexcept;

    void applyMovement   (juce::AudioBuffer<float>&);
    void applyTremolo    (juce::AudioBuffer<float>&);
    void applyHarmonics  (juce::AudioBuffer<float>&);
    void applySaturation (juce::AudioBuffer<float>&);
    void applyDistortion (juce::AudioBuffer<float>&);
    void applyWetDryMix  (juce::AudioBuffer<float>& wet,
                          const juce::AudioBuffer<float>& dry, float mix);
    void measureLevels   (const juce::AudioBuffer<float>&,
                          std::atomic<float>& l, std::atomic<float>& r) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonoFluxProcessor)
};

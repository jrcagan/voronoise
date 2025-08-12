#pragma once

#include <JuceHeader.h>
#include "synth/WavetableSynth.h"
#include "DSP/Fifo.h"

//==============================================================================
class VoronoiseAudioProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    VoronoiseAudioProcessor();
    ~VoronoiseAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock);
    void prepareToPlay (double sampleRate);
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    juce::ValueTree getValueTree();
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    enum class DSP_Options {
        Distortion,
        Chorus,
        Reverb,
        Flanger,
        Phaser,
        Comb,
        Filter,
        Waveshaper,
        END
    };

    enum class Filter_Options {
        Highpass,
        Bandpass,
        Lowpass
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts {*this, nullptr, "Settings", createParameterLayout()};

    using DSP_Order = std::array<DSP_Options, static_cast<size_t>(DSP_Options::END)>;
    SimpleMBComp::Fifo<DSP_Order> dspOrderFifo;
private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoronoiseAudioProcessor)

    DSP_Order dspOrder;

    template<typename DSP>
    struct DSP_Choice : juce::dsp::ProcessorBase {
        void prepare(const juce::dsp::ProcessSpec& spec) override {
            dsp.prepare(spec);
        }

        void process(const juce::dsp::ProcessContextReplacing<float>& context) override {
            dsp.process(context);
        }

        void reset() {
            dsp.reset();
        }

        DSP dsp;
    };

    WavetableSynth synth;
    DSP_Choice<juce::dsp::Phaser<float>> phaser;
    DSP_Choice<juce::dsp::Reverb> reverb;
    DSP_Choice<juce::dsp::LadderFilter<float>> filter;
    DSP_Choice<juce::dsp::WaveShaper<float>> waveshaper;

    using DSP_Pointers = std::array<juce::dsp::ProcessorBase*, static_cast<size_t>(DSP_Options::END)>;

};

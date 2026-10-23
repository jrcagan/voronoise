#pragma once

#include <JuceHeader.h>
#include "synth/WavetableSynth.h"
#include "synth/HitSynth.h"
#include "synth/Scale.h"
#include "DSP/Fifo.h"
#include "primitives/Ball.h"
#include "Voronoise/Params.h"

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

    // note-ons get passed from the audio thread to the editor through this,
    // the editor pulls them on a timer so the audio thread never touches the ui
    struct NoteEvent {
        int noteNumber = 0;
        int velocity = 0;
    };
    SimpleMBComp::Fifo<NoteEvent, 128> noteEventFifo;

    // ball hits go the other way, editor -> audio thread
    SimpleMBComp::Fifo<HitSynth::Hit, 256> hitFifo;

    // call from the message thread, returns false if the file couldn't be read
    bool loadSample(const juce::File& file);
    juce::String getSampleName() const;
    juce::String getSampleWildcard();
    SampleData::Ptr getCurrentSample() const; // message thread only

    // current settings pane values, safe to call from any thread
    float getParamValue(juce::StringRef id) const;
    Scale::Settings getNoteSettings() const;
    SampleSlicer::Settings getSliceSettings() const;
    HitSynth::Settings getSoundSettings() const;
private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoronoiseAudioProcessor)

    DSP_Order dspOrder {};

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
    HitSynth hitSynth;

    juce::AudioFormatManager formatManager;
    SimpleMBComp::Fifo<HitSynth::SampleData::Ptr, 8> sampleFifo;
    // keeps every loaded sample alive until the audio thread is done with it,
    // so nothing ever gets freed on the audio thread
    juce::ReferenceCountedArray<HitSynth::SampleData> loadedSamples;
    DSP_Choice<juce::dsp::Phaser<float>> phaser;
    DSP_Choice<juce::dsp::Reverb> reverb;
    DSP_Choice<juce::dsp::LadderFilter<float>> filter;
    DSP_Choice<juce::dsp::WaveShaper<float>> waveshaper;

    using DSP_Pointers = std::array<juce::dsp::ProcessorBase*, static_cast<size_t>(DSP_Options::END)>;

};

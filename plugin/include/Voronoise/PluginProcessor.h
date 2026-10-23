#pragma once

#include <JuceHeader.h>
#include "synth/WavetableSynth.h"
#include "synth/HitSynth.h"
#include "synth/Scale.h"
#include "synth/Lfo.h"
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

    // notes clicked on the on-screen keyboard, merged into the midi every block
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }

    // call from the message thread, returns false if the file couldn't be read
    bool loadSample(const juce::File& file);
    juce::String getSampleName() const;
    juce::String getSampleWildcard();
    SampleData::Ptr getCurrentSample() const; // message thread only

    // current value of a setting with the lfos added on, safe to call from any thread
    float getParamValue(juce::StringRef id) const;
    float getBaseValue(juce::StringRef id) const;               // what the knob is set to, no lfos
    float getModulatedNormalised(juce::StringRef id) const;     // 0 - 1 with lfos, for drawing
    Scale::Settings getNoteSettings() const;
    SampleSlicer::Settings getSliceSettings() const;
    HitSynth::Settings getSoundSettings() const;

    //==============================================================================
    // lfos and what they're routed to. all of these are message thread only except
    // the getLfo* ones, which read atomics the audio thread writes
    struct Modulation {
        int lfo = 0;
        juce::String paramID;
        float depth = 0.5f; // -1 - 1, how far past the knob the lfo can push it (in 0 - 1 units)
    };

    std::vector<Modulation> getModulations() const;
    std::vector<Modulation> getModulationsFor(const juce::String& paramID) const;
    void addModulation(int lfo, const juce::String& paramID, float depth = 0.5f);
    void setModulationDepth(int lfo, const juce::String& paramID, float depth);
    void removeModulation(int lfo, const juce::String& paramID);
    int getModulationVersion() const { return modulationVersion.load(); } // goes up on every change

    const LfoShape& getLfoShape(int lfo) const;
    void setLfoShape(int lfo, const LfoShape& shape);
    float getLfoOutput(int lfo) const; // 0 - 1
    float getLfoPhase(int lfo) const;  // 0 - 1, where the playhead is on the shape

    static constexpr int MAX_MODULATIONS = 96;
private:
    void rebuildRoutes();
    void loadLfoShapesFromState();
    void updateLfos(int numSamples, bool noteStarted);

    struct Route {
        int lfo = 0;
        int paramIndex = 0;
        float depth = 0.f;
    };
    std::vector<Route> routes;       // guarded by routeLock, the audio thread only ever tries the lock
    juce::SpinLock routeLock;
    std::vector<float> offsetScratch; // audio thread only, sized once
    std::unique_ptr<std::atomic<float>[]> modOffsets; // normalised offset per parameter index
    std::vector<bool> isDiscrete;     // choice/int/bool params get rounded after modulating
    int numParams = 0;
    std::atomic<int> modulationVersion { 0 };

    struct LfoIDs {
        juce::String rate, sync, division, mode, phase, smooth;
    };
    struct LfoRuntime {
        double phase = 0.0;
        float smoothed = 0.f;
    };
    std::array<LfoIDs, Params::NUM_LFOS> lfoIDs;
    std::array<LfoShape, Params::NUM_LFOS> lfoShapes;
    std::array<LfoTable, Params::NUM_LFOS> lfoTables;
    std::array<LfoRuntime, Params::NUM_LFOS> lfoRuntime;
    std::array<std::atomic<float>, Params::NUM_LFOS> lfoOutputs {};
    std::array<std::atomic<float>, Params::NUM_LFOS> lfoPhases {};
    double currentSampleRate = 44100.0;

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
    juce::MidiKeyboardState keyboardState;

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

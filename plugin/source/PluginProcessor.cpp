#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"

//==============================================================================
VoronoiseAudioProcessor::VoronoiseAudioProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
{
    // for use in storing sites a user adds in the grid
    if (! apvts.state.getChildWithName("Sites").isValid())
    apvts.state.addChild({ "Sites", {}, {} }, -1, nullptr);

    formatManager.registerBasicFormats();
}

VoronoiseAudioProcessor::~VoronoiseAudioProcessor()
{
}

//==============================================================================
const juce::String VoronoiseAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool VoronoiseAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool VoronoiseAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool VoronoiseAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double VoronoiseAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int VoronoiseAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int VoronoiseAudioProcessor::getCurrentProgram()
{
    return 0;
}

void VoronoiseAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String VoronoiseAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void VoronoiseAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void VoronoiseAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    synth.prepareToPlay(sampleRate);
    hitSynth.prepareToPlay(sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    // waveshaper's function is a null pointer by default, calling it would crash
    waveshaper.dsp.functionToUse = [](float x) { return std::tanh(x); };

    phaser.prepare(spec);
    reverb.prepare(spec);
    filter.prepare(spec);
    waveshaper.prepare(spec);
}

void VoronoiseAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool VoronoiseAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

juce::AudioProcessorValueTreeState::ParameterLayout VoronoiseAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    using namespace juce;
    auto addFloat = [&layout](const char* id, const String& name, NormalisableRange<float> range, float defaultValue, const String& unit = {})
    {
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, range, defaultValue,
                                                         AudioParameterFloatAttributes().withLabel(unit)));
    };
    auto addInt = [&layout](const String& id, const String& name, int min, int max, int defaultValue,
                            std::function<String(int, int)> toText = nullptr)
    {
        layout.add(std::make_unique<AudioParameterInt>(ParameterID { id, 1 }, name, min, max, defaultValue,
                                                       AudioParameterIntAttributes().withStringFromValueFunction(toText)));
    };
    auto addBool = [&layout](const char* id, const String& name, bool defaultValue)
    {
        layout.add(std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, name, defaultValue));
    };
    auto addChoice = [&layout](const char* id, const String& name, const StringArray& choices, int defaultIndex)
    {
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, choices, defaultIndex));
    };
    auto noteName = [](int note, int) { return Scale::getNoteName(note); };

    // view
    addChoice(Params::LINES, "Lines", Params::getLinesNames(), 0);

    // balls
    addFloat(Params::GRAVITY, "Gravity", { 0.f, 3000.f, 1.f }, Ball::GRAVITY, "px/s2");
    addFloat(Params::BOUNCINESS, "Bounciness", { 0.f, 1.f, 0.01f }, Ball::RESTITUTION);
    addFloat(Params::DRAG, "Air drag", { 0.f, 3.f, 0.01f }, 0.f);
    addFloat(Params::BALL_SIZE, "Size", { 2.f, 30.f, 0.5f }, 6.f, "px");
    addFloat(Params::SIZE_VARIATION, "Size variation", { 0.f, 1.f, 0.01f }, 0.f);
    addFloat(Params::LIFETIME, "Lifetime", { 1.f, 30.f, 0.1f }, 8.f, "s");
    addFloat(Params::LAUNCH_SPEED, "Launch speed", { 0.f, 1500.f, 1.f }, 100.f, "px/s");
    addInt(Params::MAX_BALLS, "Max balls", 1, 500, 200);
    addFloat(Params::PASS_THROUGH, "Pass through chance", { 0.f, 1.f, 0.01f }, 0.f);
    addBool(Params::PASS_THROUGH_PLAYS, "Pass throughs play", true);

    // notes
    addBool(Params::SCALE_LOCK, "Lock to scale", true);
    addChoice(Params::SCALE_ROOT, "Key", Scale::getNoteNames(), 0);
    addChoice(Params::SCALE_TYPE, "Scale", Scale::getTypeNames(), static_cast<int>(Scale::Type::MinorPentatonic));
    addInt(Params::LOWEST_NOTE, "Lowest note", 0, 127, 48, noteName);
    addInt(Params::HIGHEST_NOTE, "Highest note", 0, 127, 83, noteName);
    addInt(Params::OCTAVE_SHIFT, "Octave shift", -4, 4, 0);
    addChoice(Params::PITCH_SOURCE, "Dropped ball pitch", { "By height", "Random" }, 0);
    addBool(Params::SNAP_MIDI, "Snap MIDI balls to scale", false);
    for (int pc = 0; pc < 12; pc++)
    {
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID { Params::noteWeight(pc), 1 },
                                                         Scale::getNoteNames()[pc] + " probability",
                                                         NormalisableRange<float> { 0.f, 1.f, 0.01f }, 1.f));
    }

    // sound
    addFloat(Params::PULSE_WIDTH, "Pulse width", { 0.05f, 0.95f, 0.01f }, 0.5f);
    addFloat(Params::SAMPLE_START, "Start", { 0.f, 1.f, 0.001f }, 0.f);
    addFloat(Params::SAMPLE_END, "End", { 0.f, 1.f, 0.001f }, 1.f);
    addChoice(Params::SLICE_MODE, "Slice by", SampleSlicer::getModeNames(), 0);
    addInt(Params::SLICE_COUNT, "Slices", 1, 64, 1);
    addFloat(Params::SENSITIVITY, "Transient sensitivity", { 0.f, 1.f, 0.01f }, 0.5f);
    addChoice(Params::SLICE_PICK, "Slice played", SampleSlicer::getPickNames(), 0);
    addBool(Params::REVERSE, "Reverse", false);
    addInt(Params::TRANSPOSE, "Transpose", -24, 24, 0, [](int v, int) { return (v > 0 ? "+" : "") + String(v) + " st"; });
    addFloat(Params::FINE_TUNE, "Fine tune", { -100.f, 100.f, 1.f }, 0.f, "ct");
    addInt(Params::ROOT_NOTE, "Root note", 0, 127, HitSynth::SAMPLE_ROOT_NOTE, noteName);
    addBool(Params::TRACK_PITCH, "Follow ball pitch", true);
    addFloat(Params::SAMPLE_GAIN, "Gain", { -24.f, 12.f, 0.1f }, 0.f, "dB");
    addFloat(Params::FADE_IN, "Fade in", { 0.f, 200.f, 0.1f, 0.5f }, 2.f, "ms");
    addFloat(Params::FADE_OUT, "Fade out", { 0.f, 1000.f, 0.1f, 0.5f }, 5.f, "ms");

    return layout;
}

float VoronoiseAudioProcessor::getParamValue(juce::StringRef id) const
{
    auto* value = apvts.getRawParameterValue(id);
    jassert(value != nullptr); // typo in a param id
    return value != nullptr ? value->load() : 0.f;
}

Scale::Settings VoronoiseAudioProcessor::getNoteSettings() const
{
    Scale::Settings s;
    s.lockToScale = getParamValue(Params::SCALE_LOCK) > 0.5f;
    s.root = static_cast<int>(getParamValue(Params::SCALE_ROOT));
    s.type = static_cast<Scale::Type>(static_cast<int>(getParamValue(Params::SCALE_TYPE)));
    s.lowestNote = static_cast<int>(getParamValue(Params::LOWEST_NOTE));
    s.highestNote = static_cast<int>(getParamValue(Params::HIGHEST_NOTE));
    s.octaveShift = static_cast<int>(getParamValue(Params::OCTAVE_SHIFT));
    for (int pc = 0; pc < 12; pc++)
        s.weights[static_cast<size_t>(pc)] = getParamValue(Params::noteWeight(pc));
    return s;
}

SampleSlicer::Settings VoronoiseAudioProcessor::getSliceSettings() const
{
    SampleSlicer::Settings s;
    s.start = getParamValue(Params::SAMPLE_START);
    s.end = getParamValue(Params::SAMPLE_END);
    s.sliceCount = static_cast<int>(getParamValue(Params::SLICE_COUNT));
    s.mode = static_cast<SampleSlicer::Mode>(static_cast<int>(getParamValue(Params::SLICE_MODE)));
    s.sensitivity = getParamValue(Params::SENSITIVITY);
    s.pick = static_cast<SampleSlicer::Pick>(static_cast<int>(getParamValue(Params::SLICE_PICK)));
    return s;
}

HitSynth::Settings VoronoiseAudioProcessor::getSoundSettings() const
{
    HitSynth::Settings s;
    s.pulseWidth = getParamValue(Params::PULSE_WIDTH);
    s.slicing = getSliceSettings();
    s.reverse = getParamValue(Params::REVERSE) > 0.5f;
    s.transpose = static_cast<int>(getParamValue(Params::TRANSPOSE));
    s.fineTune = getParamValue(Params::FINE_TUNE);
    s.rootNote = static_cast<int>(getParamValue(Params::ROOT_NOTE));
    s.trackPitch = getParamValue(Params::TRACK_PITCH) > 0.5f;
    s.sampleGain = juce::Decibels::decibelsToGain(getParamValue(Params::SAMPLE_GAIN));
    s.fadeInMs = getParamValue(Params::FADE_IN);
    s.fadeOutMs = getParamValue(Params::FADE_OUT);
    return s;
}

juce::ValueTree VoronoiseAudioProcessor::getValueTree() {
    return apvts.state;
}

void VoronoiseAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;

    buffer.clear();

    synth.processBlock(buffer,midiMessages);

    // handles note-ons for the editor, which picks them up on its timer
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            noteEventFifo.push({ msg.getNoteNumber(), msg.getVelocity() });
        }
    }

    // reading the params is just a few atomic loads, so it's fine to do every block
    hitSynth.setSettings(getSoundSettings());

    // ball hits from the editor
    HitSynth::SampleData::Ptr newSample;
    while (sampleFifo.pull(newSample)) {
        hitSynth.setSample(newSample);
    }

    HitSynth::Hit hit;
    while (hitFifo.pull(hit)) {
        hitSynth.trigger(hit);
    }

    hitSynth.render(buffer, 0, buffer.getNumSamples());

    // for future dsp implementations
    auto newDSPOrder = DSP_Order();

    while(dspOrderFifo.pull(newDSPOrder)) {

    }

    if (newDSPOrder != DSP_Order()) {
        dspOrder = newDSPOrder;
    }
    
    // has to start as all nullptr, options without a case below stay empty
    DSP_Pointers dspPointers {};

    for (size_t i = 0; i < dspPointers.size(); i++) {
        switch(dspOrder[i]) {
            case DSP_Options::Phaser:
                dspPointers[i] = &phaser;
                break;
            case DSP_Options::Reverb:
                dspPointers[i] = &reverb;
                break;
            case DSP_Options::Filter:
                dspPointers[i] = &filter;
                break;
            case DSP_Options::Waveshaper:
                dspPointers[i] = &waveshaper;
                break;
            default:
                break;
        }
    }

    auto block = juce::dsp::AudioBlock<float>(buffer);
    auto context = juce::dsp::ProcessContextReplacing<float>(block);

    for (size_t i = 0; i < dspPointers.size(); i++) {
        if(dspPointers[i] != nullptr) {
            dspPointers[i]->process(context);
        }
    }
}

bool VoronoiseAudioProcessor::loadSample(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    // cap at 30 seconds so a huge file doesn't eat all the memory
    const auto maxLength = static_cast<juce::int64>(reader->sampleRate * 30.0);
    const auto length = static_cast<int>(juce::jmin(reader->lengthInSamples, maxLength));

    HitSynth::SampleData::Ptr data = new HitSynth::SampleData();
    data->buffer.setSize(static_cast<int>(juce::jmin(reader->numChannels, 2u)), length);
    reader->read(&data->buffer, 0, length, 0, true, true);
    data->sampleRate = reader->sampleRate;
    data->name = file.getFileNameWithoutExtension();
    data->file = file;
    data->analyse(); // transients + waveform for the sample view

    // remembered so the sample comes back when the project is reopened
    apvts.state.setProperty("samplePath", file.getFullPathName(), nullptr);

    // free old samples the audio thread isn't holding anymore (only this array has them)
    for (int i = loadedSamples.size(); --i >= 0;)
    {
        if (loadedSamples.getObjectPointerUnchecked(i)->getReferenceCount() == 1)
            loadedSamples.remove(i);
    }

    loadedSamples.add(data);
    return sampleFifo.push(data);
}

juce::String VoronoiseAudioProcessor::getSampleName() const
{
    if (loadedSamples.isEmpty())
        return {};

    return loadedSamples.getLast()->name;
}

SampleData::Ptr VoronoiseAudioProcessor::getCurrentSample() const
{
    return loadedSamples.isEmpty() ? nullptr : loadedSamples.getLast();
}

juce::String VoronoiseAudioProcessor::getSampleWildcard()
{
    return formatManager.getWildcardForAllFormats();
}

//==============================================================================
bool VoronoiseAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* VoronoiseAudioProcessor::createEditor()
{
    return new VoronoiseAudioProcessorEditor (*this);
}

//==============================================================================
void VoronoiseAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // settings, sites and the sample path all live in the apvts state
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void VoronoiseAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType()))
        return;

    // the editor notices the state object changed and picks up the new sites
    apvts.replaceState(juce::ValueTree::fromXml(*xml));

    if (! apvts.state.getChildWithName("Sites").isValid())
        apvts.state.addChild({ "Sites", {}, {} }, -1, nullptr);

    const juce::File sampleFile (apvts.state.getProperty("samplePath").toString());
    if (sampleFile.existsAsFile())
        loadSample(sampleFile);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoronoiseAudioProcessor();
}

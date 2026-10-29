#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"
#include "geometry/Metrics.h"

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

    for (int pc = 0; pc < 12; pc++)
        noteWeightIDs[static_cast<size_t>(pc)] = Params::noteWeight(pc);

    // per parameter modulation slots, indexed the same as getParameters()
    const auto& params = getParameters();
    numParams = params.size();
    modOffsets = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numParams));
    offsetScratch.resize(static_cast<size_t>(numParams), 0.f);
    for (auto* param : params)
    {
        modOffsets[static_cast<size_t>(param->getParameterIndex())].store(0.f);
        isDiscrete.push_back(dynamic_cast<juce::AudioParameterFloat*>(param) == nullptr);
    }

    for (int lfo = 0; lfo < Params::NUM_LFOS; lfo++)
    {
        lfoIDs[static_cast<size_t>(lfo)] = { Params::lfoRate(lfo), Params::lfoSync(lfo), Params::lfoDivision(lfo),
                                             Params::lfoMode(lfo), Params::lfoPhase(lfo), Params::lfoSmooth(lfo) };
    }

    loadLfoShapesFromState();
    rebuildRoutes();

    setWavetable(Wavetable::createPreset(Wavetable::Preset::BasicShapes));

    // sites changing rebuilds the lines right away, the timer catches lfos on the lines setting
    apvts.state.addListener(this);
    world.rebuildGeometry();
    startTimerHz(30);
}

VoronoiseAudioProcessor::~VoronoiseAudioProcessor()
{
    stopTimer();
    apvts.state.removeListener(this);
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
    currentSampleRate = sampleRate;
    world.prepare(sampleRate);
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
    addChoice(Params::METRIC, "Metric", Metrics::getNames(), 0);
    addChoice(Params::BALL_SOUND, "Ball sound", getWaveformNames(), 0);
    addFloat(Params::WT_POSITION, "WT position", { 0.f, 1.f, 0.001f }, 0.f);

    // hit random
    addFloat(Params::RND_POSITION, "Random WT pos", { 0.f, 1.f, 0.01f }, 0.f);
    addInt(Params::RND_PITCH, "Random pitch", 0, 24, 0, [](int v, int) { return v == 0 ? juce::String("off") : "+/-" + juce::String(v) + " st"; });
    addFloat(Params::RND_LEVEL, "Random level", { 0.f, 1.f, 0.01f }, 0.f);
    addFloat(Params::RND_PAN, "Random pan", { 0.f, 1.f, 0.01f }, 0.f);
    addFloat(Params::HIT_DYNAMICS, "Hit dynamics", { 0.f, 1.f, 0.01f }, 0.25f);

    // lfos
    for (int lfo = 0; lfo < Params::NUM_LFOS; lfo++)
    {
        const auto prefix = "LFO " + String(lfo + 1) + " ";
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID { Params::lfoRate(lfo), 1 }, prefix + "rate",
                                                         NormalisableRange<float> { 0.01f, 20.f, 0.001f, 0.3f }, 1.f,
                                                         AudioParameterFloatAttributes().withLabel("Hz")));
        layout.add(std::make_unique<AudioParameterBool>(ParameterID { Params::lfoSync(lfo), 1 }, prefix + "sync", false));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID { Params::lfoDivision(lfo), 1 }, prefix + "division",
                                                          Params::getLfoDivisionNames(), 4));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID { Params::lfoMode(lfo), 1 }, prefix + "mode",
                                                          Params::getLfoModeNames(), 0));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID { Params::lfoPhase(lfo), 1 }, prefix + "phase",
                                                         NormalisableRange<float> { 0.f, 1.f, 0.001f }, 0.f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID { Params::lfoSmooth(lfo), 1 }, prefix + "smooth",
                                                         NormalisableRange<float> { 0.f, 1.f, 0.001f }, 0.f));
    }

    // balls
    addFloat(Params::GRAVITY, "Gravity", { 0.f, 3000.f, 1.f }, Ball::GRAVITY, "px/s2");
    addFloat(Params::BOUNCINESS, "Bounciness", { 0.f, 1.f, 0.01f }, Ball::RESTITUTION);
    addFloat(Params::MIN_BOUNCE, "Min bounce", { 0.f, 800.f, 1.f, 0.6f }, 250.f, "px/s");
    addFloat(Params::DRAG, "Air drag", { 0.f, 3.f, 0.01f }, 0.f);
    addFloat(Params::BALL_SIZE, "Size", { 2.f, 30.f, 0.5f }, 6.f, "px");
    addFloat(Params::SIZE_VARIATION, "Size variation", { 0.f, 1.f, 0.01f }, 0.f);
    addFloat(Params::LIFETIME, "Lifetime", { 1.f, 30.f, 0.1f }, 8.f, "s");
    addFloat(Params::LAUNCH_SPEED, "Launch speed", { 0.f, 1500.f, 1.f }, 100.f, "px/s");
    addInt(Params::MAX_BALLS, "Max balls", 1, 500, 200);
    addFloat(Params::PASS_THROUGH, "Pass through chance", { 0.f, 1.f, 0.01f }, 0.f);
    addBool(Params::PASS_THROUGH_PLAYS, "Pass throughs play", true);
    addFloat(Params::MIDI_DROP_CHANCE, "MIDI drop chance", { 0.f, 1.f, 0.01f }, 0.5f);

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

    // amp envelope, skewed so the short times get most of the knob
    addFloat(Params::AMP_ATTACK, "Attack", { 0.f, 20000.f, 0.1f, 0.3f }, 5.f, "ms");
    addFloat(Params::AMP_DECAY, "Decay", { 1.f, 5000.f, 0.1f, 0.3f }, 200.f, "ms");
    addFloat(Params::AMP_SUSTAIN, "Sustain", { 0.f, 1.f, 0.01f }, 0.8f);
    addFloat(Params::AMP_RELEASE, "Release", { 1.f, 10000.f, 0.1f, 0.3f }, 300.f, "ms");
    addFloat(Params::KEYS_LEVEL, "Keys level", { -60.f, 6.f, 0.1f, 2.f }, -6.f, "dB");
    addFloat(Params::HIT_LENGTH, "Hit length", { 1.f, 5000.f, 0.1f, 0.3f }, 200.f, "ms");

    // sound
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
    auto* param = apvts.getParameter(id);
    jassert(param != nullptr); // typo in a param id
    if (param == nullptr)
        return 0.f;

    const auto index = static_cast<size_t>(param->getParameterIndex());
    const float value = param->convertFrom0to1(getModulatedNormalised(id));
    return isDiscrete[index] ? std::round(value) : value;
}

float VoronoiseAudioProcessor::getBaseValue(juce::StringRef id) const
{
    auto* value = apvts.getRawParameterValue(id);
    jassert(value != nullptr); // typo in a param id
    return value != nullptr ? value->load() : 0.f;
}

float VoronoiseAudioProcessor::getModulatedNormalised(juce::StringRef id) const
{
    auto* param = apvts.getParameter(id);
    if (param == nullptr)
        return 0.f;

    const auto index = static_cast<size_t>(param->getParameterIndex());
    return juce::jlimit(0.f, 1.f, param->getValue() + modOffsets[index].load(std::memory_order_relaxed));
}

//==============================================================================
std::vector<VoronoiseAudioProcessor::Modulation> VoronoiseAudioProcessor::getModulations() const
{
    std::vector<Modulation> mods;
    const auto tree = apvts.state.getChildWithName("Modulations");
    for (int i = 0; i < tree.getNumChildren(); i++)
    {
        const auto mod = tree.getChild(i);
        mods.push_back({ static_cast<int>(mod["lfo"]), mod["param"].toString(), static_cast<float>(mod["depth"]) });
    }
    return mods;
}

std::vector<VoronoiseAudioProcessor::Modulation> VoronoiseAudioProcessor::getModulationsFor(const juce::String& paramID) const
{
    std::vector<Modulation> mods;
    for (auto& mod : getModulations())
        if (mod.paramID == paramID)
            mods.push_back(mod);
    return mods;
}

void VoronoiseAudioProcessor::addModulation(int lfo, const juce::String& paramID, float depth)
{
    if (lfo < 0 || lfo >= Params::NUM_LFOS || apvts.getParameter(paramID) == nullptr)
        return;

    auto tree = apvts.state.getOrCreateChildWithName("Modulations", nullptr);
    if (tree.getNumChildren() >= MAX_MODULATIONS)
        return;

    // dropping the same lfo on the same knob twice doesn't stack
    for (int i = 0; i < tree.getNumChildren(); i++)
        if (static_cast<int>(tree.getChild(i)["lfo"]) == lfo && tree.getChild(i)["param"].toString() == paramID)
            return;

    tree.appendChild(juce::ValueTree("Mod")
                         .setProperty("lfo", lfo, nullptr)
                         .setProperty("param", paramID, nullptr)
                         .setProperty("depth", juce::jlimit(-1.f, 1.f, depth), nullptr),
                     nullptr);
    rebuildRoutes();
}

void VoronoiseAudioProcessor::setModulationDepth(int lfo, const juce::String& paramID, float depth)
{
    auto tree = apvts.state.getChildWithName("Modulations");
    for (int i = 0; i < tree.getNumChildren(); i++)
    {
        auto mod = tree.getChild(i);
        if (static_cast<int>(mod["lfo"]) == lfo && mod["param"].toString() == paramID)
            mod.setProperty("depth", juce::jlimit(-1.f, 1.f, depth), nullptr);
    }
    rebuildRoutes();
}

void VoronoiseAudioProcessor::removeModulation(int lfo, const juce::String& paramID)
{
    auto tree = apvts.state.getChildWithName("Modulations");
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        auto mod = tree.getChild(i);
        if (static_cast<int>(mod["lfo"]) == lfo && mod["param"].toString() == paramID)
            tree.removeChild(i, nullptr);
    }
    rebuildRoutes();
}

void VoronoiseAudioProcessor::rebuildRoutes()
{
    std::vector<Route> newRoutes;
    for (const auto& mod : getModulations())
    {
        if (auto* param = apvts.getParameter(mod.paramID))
            newRoutes.push_back({ juce::jlimit(0, Params::NUM_LFOS - 1, mod.lfo), param->getParameterIndex(), mod.depth });
    }

    {
        const juce::SpinLock::ScopedLockType lock(routeLock);
        routes.swap(newRoutes);
    }

    // with no audio running nothing would clear these, so zero anything that lost its route
    std::vector<bool> routed(static_cast<size_t>(numParams), false);
    for (const auto& route : routes)
        routed[static_cast<size_t>(route.paramIndex)] = true;
    for (int i = 0; i < numParams; i++)
        if (! routed[static_cast<size_t>(i)])
            modOffsets[static_cast<size_t>(i)].store(0.f);

    modulationVersion++;
}

const LfoShape& VoronoiseAudioProcessor::getLfoShape(int lfo) const
{
    return lfoShapes[static_cast<size_t>(juce::jlimit(0, Params::NUM_LFOS - 1, lfo))];
}

void VoronoiseAudioProcessor::setLfoShape(int lfo, const LfoShape& shape)
{
    if (lfo < 0 || lfo >= Params::NUM_LFOS)
        return;

    const auto index = static_cast<size_t>(lfo);
    lfoShapes[index] = shape;
    lfoTables[index].fill(shape);

    // saved with the project
    auto shapes = apvts.state.getOrCreateChildWithName("LfoShapes", nullptr);
    while (shapes.getNumChildren() < Params::NUM_LFOS)
        shapes.appendChild(LfoShape().toValueTree(), nullptr);
    shapes.removeChild(lfo, nullptr);
    shapes.addChild(shape.toValueTree(), lfo, nullptr);
}

void VoronoiseAudioProcessor::loadLfoShapesFromState()
{
    const auto shapes = apvts.state.getChildWithName("LfoShapes");

    // different starting shapes so the three are easy to tell apart
    constexpr LfoShape::Preset DEFAULTS[] = { LfoShape::Preset::Sine, LfoShape::Preset::Triangle, LfoShape::Preset::SawDown };

    for (int lfo = 0; lfo < Params::NUM_LFOS; lfo++)
    {
        LfoShape shape;
        shape.setPreset(DEFAULTS[lfo]);
        if (lfo < shapes.getNumChildren())
            shape.fromValueTree(shapes.getChild(lfo));
        setLfoShape(lfo, shape);
    }
}

float VoronoiseAudioProcessor::getLfoOutput(int lfo) const
{
    return lfoOutputs[static_cast<size_t>(juce::jlimit(0, Params::NUM_LFOS - 1, lfo))].load(std::memory_order_relaxed);
}

float VoronoiseAudioProcessor::getLfoPhase(int lfo) const
{
    return lfoPhases[static_cast<size_t>(juce::jlimit(0, Params::NUM_LFOS - 1, lfo))].load(std::memory_order_relaxed);
}

void VoronoiseAudioProcessor::updateLfos(int numSamples, bool noteStarted)
{
    const double blockSeconds = numSamples / currentSampleRate;

    double bpm = 120.0;
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto hostBpm = position->getBpm())
                bpm = *hostBpm;

    for (size_t lfo = 0; lfo < static_cast<size_t>(Params::NUM_LFOS); lfo++)
    {
        const auto& ids = lfoIDs[lfo];
        auto& runtime = lfoRuntime[lfo];

        const auto mode = static_cast<Params::LfoMode>(static_cast<int>(getParamValue(ids.mode)));
        if (noteStarted && mode != Params::LfoMode::Free)
            runtime.phase = 0.0;

        const bool synced = getParamValue(ids.sync) > 0.5f;
        const double frequency = synced ? (bpm / 60.0) / Params::getLfoDivisionBeats(static_cast<int>(getParamValue(ids.division)))
                                        : static_cast<double>(getParamValue(ids.rate));

        const double offset = getParamValue(ids.phase);
        double shownPhase;

        if (mode == Params::LfoMode::Envelope)
        {
            // runs from the phase offset to the end once, then holds
            runtime.phase = juce::jmin(1.0, runtime.phase + frequency * blockSeconds);
            shownPhase = juce::jmin(1.0, offset + runtime.phase * (1.0 - offset));
        }
        else
        {
            runtime.phase = std::fmod(runtime.phase + frequency * blockSeconds, 1.0);
            shownPhase = std::fmod(runtime.phase + offset, 1.0);
        }

        const float target = lfoTables[lfo].lookup(static_cast<float>(shownPhase));

        // smooth 1 = a quarter second to catch up
        const double smoothTime = getParamValue(ids.smooth) * 0.25;
        if (smoothTime < 1e-4)
            runtime.smoothed = target;
        else
            runtime.smoothed += (target - runtime.smoothed) * static_cast<float>(1.0 - std::exp(-blockSeconds / smoothTime));

        lfoOutputs[lfo].store(runtime.smoothed, std::memory_order_relaxed);
        lfoPhases[lfo].store(static_cast<float>(shownPhase), std::memory_order_relaxed);
    }

    // if the message thread is busy changing routes, keep last block's offsets
    const juce::SpinLock::ScopedTryLockType lock(routeLock);
    if (! lock.isLocked())
        return;

    std::fill(offsetScratch.begin(), offsetScratch.end(), 0.f);
    for (const auto& route : routes)
        offsetScratch[static_cast<size_t>(route.paramIndex)] += route.depth * lfoOutputs[static_cast<size_t>(route.lfo)].load(std::memory_order_relaxed);

    for (const auto& route : routes)
        modOffsets[static_cast<size_t>(route.paramIndex)].store(offsetScratch[static_cast<size_t>(route.paramIndex)], std::memory_order_relaxed);
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
        s.weights[static_cast<size_t>(pc)] = getParamValue(noteWeightIDs[static_cast<size_t>(pc)]);
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
    s.position = getParamValue(Params::WT_POSITION);
    s.synthLevel = juce::Decibels::decibelsToGain(getParamValue(Params::KEYS_LEVEL), -59.9f);
    s.randomPosition = getParamValue(Params::RND_POSITION);
    s.randomPitch = static_cast<int>(getParamValue(Params::RND_PITCH));
    s.randomLevel = getParamValue(Params::RND_LEVEL);
    s.randomPan = getParamValue(Params::RND_PAN);
    s.slicing = getSliceSettings();
    s.reverse = getParamValue(Params::REVERSE) > 0.5f;
    s.transpose = static_cast<int>(getParamValue(Params::TRANSPOSE));
    s.fineTune = getParamValue(Params::FINE_TUNE);
    s.rootNote = static_cast<int>(getParamValue(Params::ROOT_NOTE));
    s.trackPitch = getParamValue(Params::TRACK_PITCH) > 0.5f;
    s.sampleGain = juce::Decibels::decibelsToGain(getParamValue(Params::SAMPLE_GAIN));
    s.fadeInMs = getParamValue(Params::FADE_IN);
    s.fadeOutMs = getParamValue(Params::FADE_OUT);
    s.envelope = { getParamValue(Params::AMP_ATTACK) * 0.001f, getParamValue(Params::AMP_DECAY) * 0.001f,
                   getParamValue(Params::AMP_SUSTAIN), getParamValue(Params::AMP_RELEASE) * 0.001f };
    s.hitLengthMs = getParamValue(Params::HIT_LENGTH);
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

    // on-screen keyboard notes join the incoming midi so they play and drop balls the same way
    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

    // last block's lfo values, the envelope is set before this block's lfos run
    synth.setEnvelope({ getParamValue(Params::AMP_ATTACK) * 0.001f, getParamValue(Params::AMP_DECAY) * 0.001f,
                        getParamValue(Params::AMP_SUSTAIN), getParamValue(Params::AMP_RELEASE) * 0.001f });
    synth.setLevel(juce::Decibels::decibelsToGain(getParamValue(Params::KEYS_LEVEL), -59.9f));
    synth.setPosition(getParamValue(Params::WT_POSITION));

    Wavetable::Ptr newWavetable;
    while (wavetableFifo.pull(newWavetable)) {
        synth.setWavetable(newWavetable);
        hitSynth.setWavetable(newWavetable);
    }

    synth.processBlock(buffer,midiMessages);

    // note-ons can drop balls, and retrigger the lfos
    world.beginBlock();
    bool noteStarted = false;
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            world.handleNoteOn(msg.getNoteNumber(), msg.getVelocity(), metadata.samplePosition);
            noteStarted = true;
        }
        else if (msg.isNoteOff())
        {
            world.handleNoteOff(msg.getNoteNumber(), metadata.samplePosition);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (int note = 0; note < 128; note++)
                world.handleNoteOff(note, metadata.samplePosition);
        }
    }

    // lfos first so everything below reads this block's modulated values
    updateLfos(buffer.getNumSamples(), noteStarted);

    // reading the params is just a few atomic loads, so it's fine to do every block
    hitSynth.setSettings(getSoundSettings());

    // ball hits from the editor
    HitSynth::SampleData::Ptr newSample;
    while (sampleFifo.pull(newSample)) {
        hitSynth.setSample(newSample);
    }

    renderHits(buffer);

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

void VoronoiseAudioProcessor::renderHits(juce::AudioBuffer<float>& buffer)
{
    HitSynth::Hit hit;
    while (hitFifo.pull(hit))
        hitSynth.trigger(hit);

    // step the balls through this block, then play their hits on the samples they happened
    const int numSamples = buffer.getNumSamples();
    const int numHits = world.process(numSamples, blockHits);

    int rendered = 0;
    for (int i = 0; i < numHits; i++)
    {
        const auto& event = blockHits[static_cast<size_t>(i)];
        if (event.sampleOffset > rendered)
        {
            hitSynth.render(buffer, rendered, event.sampleOffset - rendered);
            rendered = event.sampleOffset;
        }
        hitSynth.trigger(event.hit);
    }
    hitSynth.render(buffer, rendered, numSamples - rendered);
}

void VoronoiseAudioProcessor::valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType("Sites"))
        world.rebuildGeometry();
}

void VoronoiseAudioProcessor::valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType("Sites"))
        world.rebuildGeometry();
}

void VoronoiseAudioProcessor::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&)
{
    // a site being dragged around, built in the background so the drag stays smooth
    if (tree.hasType("Site"))
        world.requestRebuild();
}

void VoronoiseAudioProcessor::timerCallback()
{
    world.syncGeometry();
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

void VoronoiseAudioProcessor::setWavetable(Wavetable::Ptr newWavetable)
{
    if (newWavetable == nullptr)
        return;

    // free old tables the audio thread let go of (only this array still has them)
    for (int i = loadedWavetables.size(); --i >= 0;)
    {
        if (loadedWavetables.getObjectPointerUnchecked(i)->getReferenceCount() == 1)
            loadedWavetables.remove(i);
    }

    wavetable = newWavetable;
    loadedWavetables.add(newWavetable);
    wavetableFifo.push(newWavetable);
}

bool VoronoiseAudioProcessor::loadWavetable(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 1)
        return false;

    // more than 64 frames' worth is never used, so don't read a huge file all the way
    const auto length = static_cast<int>(juce::jmin(reader->lengthInSamples, static_cast<juce::int64>(reader->sampleRate * 60.0)));
    juce::AudioBuffer<float> audio(static_cast<int>(juce::jmin(reader->numChannels, 2u)), length);
    reader->read(&audio, 0, length, 0, true, true);

    auto table = Wavetable::fromAudio(audio, file.getFileNameWithoutExtension());
    if (table == nullptr)
        return false;

    setWavetable(table);
    return true;
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

    // the wavetable's frames go in as one blob, on the copy so saving never touches the live state
    // (hosts can save from any thread)
    const Wavetable::Ptr table = wavetable;
    state.setProperty("wavetable", table->toMemoryBlock().toBase64Encoding(), nullptr);
    state.setProperty("wavetableName", table->getName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void VoronoiseAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType()))
        return;

    // swapping the state means listening to the new one
    apvts.state.removeListener(this);
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    apvts.state.addListener(this);

    if (! apvts.state.getChildWithName("Sites").isValid())
        apvts.state.addChild({ "Sites", {}, {} }, -1, nullptr);
    world.rebuildGeometry();

    loadLfoShapesFromState();
    rebuildRoutes();

    juce::MemoryBlock tableData;
    if (tableData.fromBase64Encoding(apvts.state.getProperty("wavetable").toString()))
        if (auto table = Wavetable::fromMemoryBlock(tableData, apvts.state.getProperty("wavetableName").toString()))
            setWavetable(table);

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

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
    samplesPerBlock; // currently here just to get rid of warning, will do smth with this later prob
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
    return layout;
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

    auto newDSPOrder = DSP_Order();

    while(dspOrderFifo.pull(newDSPOrder)) {

    }

    if (newDSPOrder != DSP_Order()) {
        dspOrder = newDSPOrder;
    }
    
    DSP_Pointers dspPointers;   

    for (int i = 0; i < dspPointers.size(); i++) {
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
        }
    }

    auto block = juce::dsp::AudioBlock<float>(buffer);
    auto context = juce::dsp::ProcessContextReplacing<float>(block);

    for (int i = 0; i < dspPointers.size(); i++) {
        if(dspPointers[i] != nullptr) {
            dspPointers[i]->process(context);
        }
    }
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
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    juce::ignoreUnused (destData);
}

void VoronoiseAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    juce::ignoreUnused (data, sizeInBytes);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoronoiseAudioProcessor();
}

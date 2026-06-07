// ============================================================================
//  src/plugin/JucePlugin.cpp
// ============================================================================
#include "JucePlugin.h"
#include "JuceEditorComponent.h"
#include "common.h"

namespace odenise::plugin {

// ----------------------------------------------------------------------------
JucePlugin::JucePlugin()
    : juce::AudioProcessor(
        BusesProperties()
            .withInput ("Input",  juce::AudioChannelSet::mono(), true)
            .withOutput("Output", juce::AudioChannelSet::mono(), true))
{
    // Scan initial des peripheriques disponibles.
    // L'editeur pourra relancer un scan via layer_.scanDevices().
    layer_.scanDevices();
}

// ----------------------------------------------------------------------------
void JucePlugin::prepareToPlay(double sample_rate, int max_block_size)
{
    layer_.processor()->prepare(
        static_cast<int>(sample_rate),
        max_block_size);
}

// ----------------------------------------------------------------------------
void JucePlugin::releaseResources()
{
    layer_.processor()->release();
}

// ----------------------------------------------------------------------------
//  [RT] Traitement audio.
//  Stub : recopie l'entree sur la sortie (passthrough) tant que la chaine
//  STFT n'est pas couplee. Le backend reel sera branche ici en phase 3b+.
// ----------------------------------------------------------------------------
void JucePlugin::processBlock(juce::AudioBuffer<float>& buffer,
                               juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals no_denormals;

    const int total_channels = getTotalNumInputChannels();
    const int num_samples    = buffer.getNumSamples();

    // Canaux de sortie superflus -> silence
    for (int ch = getTotalNumInputChannels();
         ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, num_samples);

    // TODO phase 3b+ : construire TrackIO depuis buffer et appeler
    //   layer_.processor()->engine()->... ou backend directement.
    (void)total_channels;
}

// ----------------------------------------------------------------------------
juce::AudioProcessorEditor* JucePlugin::createEditor()
{
    // Cubase et tous les hotes VST3 conformes appellent createEditor() depuis
    // le message thread. Si cette assertion echoue, c'est un bug hote.
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    return new JuceEditorComponent(*this);
}

} // namespace odenise::plugin

// ----------------------------------------------------------------------------
//  Point d'entree JUCE : fabrique le juce::AudioProcessor pour l'hote.
// ----------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new odenise::plugin::JucePlugin();
}

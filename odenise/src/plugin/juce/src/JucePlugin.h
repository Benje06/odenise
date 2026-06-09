// ============================================================================
//  src/plugin/JucePlugin.h  --  Point d'entree VST3/CLAP odenise.
//
//  Herite de juce::AudioProcessor (requis par JUCE pour VST3/CLAP).
//  Possede JuceAudioLayer (DeviceManager + odenise engine + editor logique).
//
//  Cycle de vie plugin :
//    prepareToPlay()  -> layer_.processor()->prepare()
//    processBlock()   -> stub (traitement reel en phase 3b+)
//    releaseResources()-> layer_.processor()->release()
//    createEditor()   -> JuceEditorComponent
// ============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "logger.h"
#include "JuceAudioLayer.h"

namespace odenise::plugin {

class JucePlugin : public juce::AudioProcessor {
public:
    JucePlugin();
    ~JucePlugin() override = default;

    // -----------------------------------------------------------------------
    //  Cycle de vie audio (appeles par l'hote VST3/CLAP)
    // -----------------------------------------------------------------------
    void prepareToPlay(double sample_rate, int max_block_size) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi) override;

    // -----------------------------------------------------------------------
    //  Editeur UI
    // -----------------------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // -----------------------------------------------------------------------
    //  Metadonnees plugin (requises par l'hote)
    // -----------------------------------------------------------------------
    const juce::String getName() const override { return "odenise"; }

    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()                        override { return 1; }
    int  getCurrentProgram()                     override { return 0; }
    void setCurrentProgram(int)                  override {}
    const juce::String getProgramName(int)       override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int)   override {}

    // -----------------------------------------------------------------------
    //  Acces interne (pour JuceEditorComponent)
    // -----------------------------------------------------------------------
    JuceAudioLayer* layer() noexcept { return &layer_; }

private:
    JuceAudioLayer layer_;
};

} // namespace odenise::plugin

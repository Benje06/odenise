// ============================================================================
//  src/plugin/juce/ui/JuceEditorComponent.h  --  UI plugin odenise (VST3/CLAP).
//
//  Herite de juce::AudioProcessorEditor (requis par l'hote VST3/CLAP)
//  et de juce::Timer (poll a 100 ms pour rafraichir les stats engine).
//
//  Compatibilite hotes :
//    - Taille figee (setResizeLimits fixe) : Cubase exige des contraintes coherentes.
//    - JUCE_MODAL_LOOPS_PERMITTED=0 defini dans le CMakeLists (requis Cubase).
//    - createEditor() verifie le message thread via jassert.
//
//  Widgets :
//    combo_interfaces_ : liste des interfaces audio detectees
//    label_info_       : caracteristiques de l'interface selectionnee
//                        (sample rates, bits, canaux in/out)
//    combo_inputs_     : liste des canaux d'entree de l'interface selectionnee
//    combo_outputs_    : liste des canaux de sortie de l'interface selectionnee
//    vu_meter_         : vu-metre stereo (peak + RMS) mis a jour par timerCallback
// ============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace odenise::plugin {

class JucePlugin;

// ---------------------------------------------------------------------------
//  VuMeter -- composant vu-metre vertical, peak + RMS, deux canaux.
//  Mis a jour via setPeak(ch, val) et setRms(ch, val) depuis timerCallback.
//  val en lineaire [0.0, 1.0].
// ---------------------------------------------------------------------------
class VuMeter : public juce::Component {
public:
    // Nombre de canaux fixes a 2 (in + out) pour cet affichage.
    static constexpr int kChannels = 2;

    VuMeter();

    // Ecrit depuis le thread UI (timerCallback) -- pas de RT.
    void setPeak(int channel, float value) noexcept;
    void setRms (int channel, float value) noexcept;

    // juce::Component
    void paint(juce::Graphics& g) override;

private:
    // peak_[ch], rms_[ch] en lineaire [0,1]
    float peak_[kChannels] = {};
    float rms_ [kChannels] = {};

    // Decroissance du peak hold (pixels par repaint)
    float peak_hold_[kChannels] = {};
    static constexpr float kDecay = 0.02f;
};

// ---------------------------------------------------------------------------
//  JuceEditorComponent
// ---------------------------------------------------------------------------
class JuceEditorComponent
    : public juce::AudioProcessorEditor
    , private juce::Timer
    , private juce::ComboBox::Listener
{
public:
    explicit JuceEditorComponent(JucePlugin& plugin);
    ~JuceEditorComponent() override;

    // juce::Component
    void paint(juce::Graphics& g) override;
    void resized()                override;

private:
    // juce::Timer
    void timerCallback() override;

    // juce::ComboBox::Listener
    void comboBoxChanged(juce::ComboBox* combo) override;

    // Peuple combo_interfaces_ depuis editor_->audioInterfaces().
    void populateInterfaceCombo();

    // Met a jour label_info_, combo_inputs_ et combo_outputs_.
    void updateInterfaceInfo(int interface_id);

    // -----------------------------------------------------------------------
    //  Membres
    // -----------------------------------------------------------------------
    JucePlugin& plugin_;   // non-owning

    // -- Interface --
    juce::Label    label_interfaces_;
    juce::ComboBox combo_interfaces_;

    // -- Caracteristiques --
    juce::Label    label_info_;

    // -- Canaux entree --
    juce::Label    label_inputs_;
    juce::ComboBox combo_inputs_;

    // -- Canaux sortie --
    juce::Label    label_outputs_;
    juce::ComboBox combo_outputs_;

    // -- Vu-metre --
    juce::Label    label_vu_;
    VuMeter        vu_meter_;

    static constexpr int kWidth  = 520;
    static constexpr int kHeight = 380;
};

} // namespace odenise::plugin

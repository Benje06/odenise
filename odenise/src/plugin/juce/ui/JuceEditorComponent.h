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
//  Routage :
//    Section entree : combo interface d'entree + combo canal d'entree + VuMeter
//    Section sortie : combo interface de sortie + combo canal de sortie + VuMeter
//    Les deux sections sont independantes : on peut router l'entree depuis
//    l'ID24 et la sortie vers le Realtek par exemple.
// ============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace odenise::plugin {

class JucePlugin;

// ---------------------------------------------------------------------------
//  VuMeter -- composant vu-metre vertical, N canaux (defaut 1).
//  Mis a jour via setPeak(ch, val) et setRms(ch, val) depuis timerCallback.
//  val en lineaire [0.0, 1.0].
// ---------------------------------------------------------------------------
class VuMeter : public juce::Component {
public:
    static constexpr int kMaxChannels = 8;

    explicit VuMeter(int num_channels = 1);

    void setNumChannels(int n) noexcept;

    // Ecrit depuis le thread UI (timerCallback) -- jamais depuis le thread RT.
    void setPeak(int channel, float value) noexcept;
    void setRms (int channel, float value) noexcept;

    // juce::Component
    void paint(juce::Graphics& g) override;

private:
    int   num_channels_              = 1;
    float peak_    [kMaxChannels]    = {};
    float rms_     [kMaxChannels]    = {};
    float peak_hold_[kMaxChannels]   = {};

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

    void paint(juce::Graphics& g) override;
    void resized()                override;

private:
    void timerCallback()                       override;
    void comboBoxChanged(juce::ComboBox* cb)   override;

    // Peuple les combos d'interfaces depuis audioInputs() / audioOutputs().
    void populateCombos();

    // Met a jour label + combo canaux + VuMeter pour la section entree.
    void updateInputInfo(int interface_id);

    // Met a jour label + combo canaux + VuMeter pour la section sortie.
    void updateOutputInfo(int interface_id);

    // -----------------------------------------------------------------------
    JucePlugin& plugin_;

    // -- Section entree --
    juce::Label    label_in_iface_;
    juce::ComboBox combo_in_iface_;    // interfaces d'entree
    juce::Label    label_in_info_;     // caracteristiques
    juce::Label    label_in_ch_;
    juce::ComboBox combo_in_ch_;       // canaux d'entree
    VuMeter        vu_in_;             // vu-metre entree

    // -- Section sortie --
    juce::Label    label_out_iface_;
    juce::ComboBox combo_out_iface_;   // interfaces de sortie
    juce::Label    label_out_info_;    // caracteristiques
    juce::Label    label_out_ch_;
    juce::ComboBox combo_out_ch_;      // canaux de sortie
    VuMeter        vu_out_;            // vu-metre sortie

    static constexpr int kWidth   = 560;
    static constexpr int kHeight  = 480;
    static constexpr int kVuW     = 60;   // largeur colonne vu-metre
    static constexpr int kLabelW  = 130;  // largeur colonne labels
    static constexpr int kRowH    = 28;
    static constexpr int kInfoH   = 90;
    static constexpr int kGap     = 8;
    static constexpr int kSepH    = 16;  // separateur entre sections
};

} // namespace odenise::plugin

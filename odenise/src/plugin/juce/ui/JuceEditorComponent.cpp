// ============================================================================
//  src/plugin/juce/ui/JuceEditorComponent.cpp
// ============================================================================
#include "JuceEditorComponent.h"
#include "JucePlugin.h"
#include "common.h"

namespace odenise::plugin {

// ============================================================================
//  VuMeter
// ============================================================================

VuMeter::VuMeter()
{
    for (int ch = 0; ch < kChannels; ++ch) {
        peak_[ch]      = 0.0f;
        rms_ [ch]      = 0.0f;
        peak_hold_[ch] = 0.0f;
    }
}

void VuMeter::setPeak(int channel, float value) noexcept
{
    if (channel < 0 || channel >= kChannels) return;
    peak_[channel] = juce::jlimit(0.0f, 1.0f, value);
    if (peak_[channel] > peak_hold_[channel])
        peak_hold_[channel] = peak_[channel];
}

void VuMeter::setRms(int channel, float value) noexcept
{
    if (channel < 0 || channel >= kChannels) return;
    rms_[channel] = juce::jlimit(0.0f, 1.0f, value);
}

void VuMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xff1a1a1a));

    const int bar_w    = (bounds.getWidth() - (kChannels + 1) * 4) / kChannels;
    const int bar_h    = bounds.getHeight() - 8;
    const int bar_y    = 4;

    for (int ch = 0; ch < kChannels; ++ch)
    {
        const int bar_x = 4 + ch * (bar_w + 4);

        // Fond de barre
        g.setColour(juce::Colour(0xff2d2d2d));
        g.fillRect(bar_x, bar_y, bar_w, bar_h);

        // RMS -- vert -> jaune -> rouge selon niveau
        const int rms_h = static_cast<int>(rms_[ch] * bar_h);
        const int rms_y = bar_y + bar_h - rms_h;

        if (rms_[ch] < 0.7f)
            g.setColour(juce::Colour(0xff44bb44));
        else if (rms_[ch] < 0.9f)
            g.setColour(juce::Colour(0xffddbb00));
        else
            g.setColour(juce::Colour(0xffdd2222));

        g.fillRect(bar_x, rms_y, bar_w, rms_h);

        // Peak hold -- ligne blanche
        const int ph_y = bar_y + bar_h - static_cast<int>(peak_hold_[ch] * bar_h);
        g.setColour(juce::Colours::white);
        g.fillRect(bar_x, ph_y, bar_w, 2);

        // Decroissance peak hold
        peak_hold_[ch] = std::max(0.0f, peak_hold_[ch] - kDecay);

        // Label canal
        g.setColour(juce::Colours::lightgrey);
        g.setFont(10.0f);
        g.drawText(ch == 0 ? "In" : "Out",
                   bar_x, bar_y + bar_h + 2, bar_w, 12,
                   juce::Justification::centred, false);
    }
}

// ============================================================================
//  JuceEditorComponent
// ============================================================================

JuceEditorComponent::JuceEditorComponent(JucePlugin& plugin)
    : juce::AudioProcessorEditor(plugin)
    , plugin_(plugin)
{
    setSize(kWidth, kHeight);
    // Taille figee : Cubase exige des contraintes explicites et coherentes.
    setResizeLimits(kWidth, kHeight, kWidth, kHeight);

    // -- Interface --
    label_interfaces_.setText("Interface audio", juce::dontSendNotification);
    addAndMakeVisible(label_interfaces_);

    combo_interfaces_.setTextWhenNothingSelected("-- choisir une interface --");
    combo_interfaces_.addListener(this);
    addAndMakeVisible(combo_interfaces_);

    // -- Info --
    label_info_.setJustificationType(juce::Justification::topLeft);
    label_info_.setText("", juce::dontSendNotification);
    label_info_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(label_info_);

    // -- Entrees --
    label_inputs_.setText("Canal entree", juce::dontSendNotification);
    addAndMakeVisible(label_inputs_);

    combo_inputs_.setTextWhenNothingSelected("-- entree --");
    combo_inputs_.addListener(this);
    addAndMakeVisible(combo_inputs_);

    // -- Sorties --
    label_outputs_.setText("Canal sortie", juce::dontSendNotification);
    addAndMakeVisible(label_outputs_);

    combo_outputs_.setTextWhenNothingSelected("-- sortie --");
    combo_outputs_.addListener(this);
    addAndMakeVisible(combo_outputs_);

    // -- Vu-metre --
    label_vu_.setText("Niveau", juce::dontSendNotification);
    label_vu_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label_vu_);
    addAndMakeVisible(vu_meter_);

    populateInterfaceCombo();

    startTimerHz(10);
}

// ----------------------------------------------------------------------------
JuceEditorComponent::~JuceEditorComponent()
{
    stopTimer();
    combo_interfaces_.removeListener(this);
    combo_inputs_.removeListener(this);
    combo_outputs_.removeListener(this);
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(
        juce::ResizableWindow::backgroundColourId));
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::resized()
{
    auto area = getLocalBounds().reduced(12);
    const int row_h   = 28;
    const int label_w = 120;
    const int gap     = 8;
    const int vu_w    = 80;

    // Colonne gauche (controles) / droite (vu-metre)
    auto vu_col  = area.removeFromRight(vu_w);
    area.removeFromRight(gap);

    // Ligne 1 : interface
    auto row1 = area.removeFromTop(row_h);
    label_interfaces_.setBounds(row1.removeFromLeft(label_w));
    combo_interfaces_.setBounds(row1);
    area.removeFromTop(gap);

    // Zone info
    label_info_.setBounds(area.removeFromTop(90));
    area.removeFromTop(gap);

    // Ligne 2 : canal entree
    auto row2 = area.removeFromTop(row_h);
    label_inputs_.setBounds(row2.removeFromLeft(label_w));
    combo_inputs_.setBounds(row2);
    area.removeFromTop(gap);

    // Ligne 3 : canal sortie
    auto row3 = area.removeFromTop(row_h);
    label_outputs_.setBounds(row3.removeFromLeft(label_w));
    combo_outputs_.setBounds(row3);

    // Vu-metre (colonne droite)
    label_vu_.setBounds(vu_col.removeFromTop(20));
    vu_meter_.setBounds(vu_col);
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::timerCallback()
{
    auto* editor = plugin_.layer()->editor();
    if (!editor) return;

    editor->poll();

    // Stub vu-metre : simule decroissance tant que le backend RT
    // ne publie pas encore de niveaux reels.
    // Phase 3b+ : remplacer par lecture depuis engine->spectrum() ou
    // un double-buffer atomique alimente par processBlock().
    vu_meter_.setPeak(0, 0.0f);
    vu_meter_.setRms (0, 0.0f);
    vu_meter_.setPeak(1, 0.0f);
    vu_meter_.setRms (1, 0.0f);
    vu_meter_.repaint();
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::comboBoxChanged(juce::ComboBox* combo)
{
    auto* editor = plugin_.layer()->editor();
    if (!editor) return;

    if (combo == &combo_interfaces_)
    {
        const int juce_id = combo_interfaces_.getSelectedId();
        if (juce_id <= 0) return;

        const auto& ifaces = editor->audioInterfaces();
        const int   idx    = juce_id - 1;
        if (idx < 0 || idx >= static_cast<int>(ifaces.size())) return;

        const int iface_id = ifaces[static_cast<size_t>(idx)].id;
        editor->selectAudioInterface(iface_id);
        updateInterfaceInfo(iface_id);
    }
    else if (combo == &combo_inputs_)
    {
        const int ch = combo_inputs_.getSelectedId() - 1;
        std::string msg = _("JuceEditorComponent: selected input channel ");
        msg += std::to_string(ch);
        LOG(msg);
    }
    else if (combo == &combo_outputs_)
    {
        const int ch = combo_outputs_.getSelectedId() - 1;
        std::string msg = _("JuceEditorComponent: selected output channel ");
        msg += std::to_string(ch);
        LOG(msg);
    }
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::populateInterfaceCombo()
{
    combo_interfaces_.clear(juce::dontSendNotification);

    auto* editor = plugin_.layer()->editor();
    if (!editor) return;

    const auto& ifaces = editor->audioInterfaces();
    int juce_id = 1;
    for (const auto& iface : ifaces)
        combo_interfaces_.addItem(iface.name, juce_id++);
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::updateInterfaceInfo(int interface_id)
{
    auto* editor = plugin_.layer()->editor();
    if (!editor) return;

    const auto& ifaces = editor->audioInterfaces();

    const odenise::audio::AudioInterfaceInfo* found = nullptr;
    for (const auto& iface : ifaces) {
        if (iface.id == interface_id) { found = &iface; break; }
    }

    if (!found) {
        label_info_.setText("", juce::dontSendNotification);
        combo_inputs_.clear(juce::dontSendNotification);
        combo_outputs_.clear(juce::dontSendNotification);
        return;
    }

    // -- Caracteristiques --
    std::string info;

    info += "Entrees : ";
    info += std::to_string(found->max_input_channels);
    info += "   Sorties : ";
    info += std::to_string(found->max_output_channels);
    info += "\n";

    info += "Sample rates :";
    for (int sr : found->supported_sample_rates) {
        info += " ";
        info += std::to_string(sr);
    }
    info += " Hz\n";

    if (!found->supported_buffer_sizes.empty()) {
        info += "Buffer sizes :";
        for (int bs : found->supported_buffer_sizes) {
            info += " ";
            info += std::to_string(bs);
        }
        info += "\n";
    }

    info += (found->current_bit_depth > 0)
        ? "Bits : " + std::to_string(found->current_bit_depth)
        : "Bits : N/A";

    label_info_.setText(info, juce::dontSendNotification);

    // -- Canaux entree --
    combo_inputs_.clear(juce::dontSendNotification);
    for (int ch = 0; ch < found->max_input_channels; ++ch) {
        std::string name = "In ";
        name += std::to_string(ch + 1);
        combo_inputs_.addItem(name, ch + 1);
    }

    // -- Canaux sortie --
    combo_outputs_.clear(juce::dontSendNotification);
    for (int ch = 0; ch < found->max_output_channels; ++ch) {
        std::string name = "Out ";
        name += std::to_string(ch + 1);
        combo_outputs_.addItem(name, ch + 1);
    }
}

} // namespace odenise::plugin

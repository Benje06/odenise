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

VuMeter::VuMeter(int num_channels)
    : num_channels_(juce::jlimit(1, kMaxChannels, num_channels))
{}

void VuMeter::setNumChannels(int n) noexcept
{
    num_channels_ = juce::jlimit(1, kMaxChannels, n);
}

void VuMeter::setPeak(int ch, float value) noexcept
{
    if (ch < 0 || ch >= num_channels_) return;
    peak_[ch] = juce::jlimit(0.0f, 1.0f, value);
    if (peak_[ch] > peak_hold_[ch])
        peak_hold_[ch] = peak_[ch];
}

void VuMeter::setRms(int ch, float value) noexcept
{
    if (ch < 0 || ch >= num_channels_) return;
    rms_[ch] = juce::jlimit(0.0f, 1.0f, value);
}

void VuMeter::paint(juce::Graphics& g)
{
    const auto b     = getLocalBounds();
    const int  pad   = 3;
    const int  bar_h = b.getHeight() - pad * 2;
    const int  bar_y = pad;
    const int  total_w = b.getWidth() - pad * (num_channels_ + 1);
    const int  bar_w = (num_channels_ > 0)
                       ? total_w / num_channels_
                       : total_w;

    g.fillAll(juce::Colour(0xff1a1a1a));

    for (int ch = 0; ch < num_channels_; ++ch)
    {
        const int bx = pad + ch * (bar_w + pad);

        // Fond
        g.setColour(juce::Colour(0xff2d2d2d));
        g.fillRect(bx, bar_y, bar_w, bar_h);

        // RMS colore
        const int rh = static_cast<int>(rms_[ch] * bar_h);
        g.setColour(rms_[ch] < 0.7f ? juce::Colour(0xff44bb44)
                  : rms_[ch] < 0.9f ? juce::Colour(0xffddbb00)
                                     : juce::Colour(0xffdd2222));
        g.fillRect(bx, bar_y + bar_h - rh, bar_w, rh);

        // Peak hold
        const int ph = bar_y + bar_h - static_cast<int>(peak_hold_[ch] * bar_h);
        g.setColour(juce::Colours::white);
        g.fillRect(bx, ph, bar_w, 2);

        peak_hold_[ch] = std::max(0.0f, peak_hold_[ch] - kDecay);
    }
}

// ============================================================================
//  Helpers locaux
// ============================================================================

// Construit la chaine de caracteristiques d'une AudioInterfaceInfo.
// Format :
//   Sample rate: 44.1KHz  24bits 512 sample 10.7 ms 2 canaux
//   Rates   : 44100  [48000]  96000  192000
//   Buffers : 64  128  256  [512]  1024  2048
static std::string buildInfoString(const odenise::audio::AudioInterfaceInfo& iface,
                                   bool show_inputs)
{
    std::string s;

    // -- Bloc "Actuel" -------------------------------------------------------
    const int   sr     = iface.current_sample_rate;
    const int   buf    = iface.current_buffer_size;
    const float lat_ms = (sr > 0 && buf > 0)
                         ? (static_cast<float>(buf) / sr) * 1000.0f
                         : 0.0f;
    if (sr > 0) {
        char tmp[16];
        const float sr_khz = sr / 1000.0f;
        std::snprintf(tmp, sizeof(tmp),
            floorf(sr_khz) == sr_khz ? "%.0fKHz " : "%.1fKHz ",
            sr_khz);
        s += tmp;
    } else {
        s += "Sample rate: N/A ";
    }
    s += (iface.current_bit_depth > 0) ? std::to_string(iface.current_bit_depth) : "N/A";
    s += "bits ";
    //s += "Buffer Size: ";
    if (buf > 0) { 
        s += std::to_string(buf);
        s += " samples "; 
    }else{
        s += " N/A ";
    }           
    if (lat_ms > 0.0f) {
        // lat_ms avec 1 decimale
        char lat_buf[16];
        std::snprintf(lat_buf, sizeof(lat_buf), "%.1f", static_cast<double>(lat_ms));
        //s += "\nLatence: ";
        s += lat_buf;
        s += " ms  ";
    }
 
    // -- Canaux --------------------------------------------------------------
    const int ch = show_inputs ? iface.max_input_channels : iface.max_output_channels;
    s += std::to_string(ch);
    if (ch > 1){
        s += " Canaux";
    }else{
        s += " Canal";
    }


    // -- Sample rates disponibles (actuel entre [ ]) -------------------------
    if (!iface.supported_sample_rates.empty()) {
        s += "\nRates: ";
        for (int r : iface.supported_sample_rates) {
            s += (r == sr) ? " [" + std::to_string(r) + "]"
                           : "  " + std::to_string(r);
        }
        s += " Hz";
    }

    // -- Buffer sizes disponibles (actuel entre [ ]) -------------------------
    if (!iface.supported_buffer_sizes.empty()) {
        s += "\nBuffers: ";
        for (int b : iface.supported_buffer_sizes) {
            s += (b == buf) ? " [" + std::to_string(b) + "]"
                            : "  " + std::to_string(b);
        }
        s += " smp";
    }

    return s;
}

// ============================================================================
//  JuceEditorComponent
// ============================================================================

JuceEditorComponent::JuceEditorComponent(JucePlugin& plugin)
    : juce::AudioProcessorEditor(plugin)
    , plugin_(plugin)
    , vu_in_ (1)
    , vu_out_(1)
{
    setSize(kWidth, kHeight);
    setResizeLimits(kWidth, kHeight, kWidth, kHeight);

    addAndMakeVisible(vu_in_);
    addAndMakeVisible(vu_out_);

    // Audio driver slection
    label_driver_.setText("Driver", juce::dontSendNotification);
    addAndMakeVisible(label_driver_);

    combo_driver_.setTextWhenNothingSelected("-- Pilote Audio --");
    combo_driver_.addListener(this);
    addAndMakeVisible(combo_driver_);

    // -- Section entree --
    label_in_iface_.setText("In", juce::dontSendNotification);
    addAndMakeVisible(label_in_iface_);

    combo_in_iface_.setTextWhenNothingSelected("-- interface entree --");
    combo_in_iface_.addListener(this);
    addAndMakeVisible(combo_in_iface_);

    label_in_ch_.setText("Ch", juce::dontSendNotification);
    addAndMakeVisible(label_in_ch_);

    combo_in_ch_.setTextWhenNothingSelected("-- canal --");
    combo_in_ch_.addListener(this);
    addAndMakeVisible(combo_in_ch_);

    label_in_info_.setJustificationType(juce::Justification::topLeft);
    label_in_info_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(label_in_info_);
 
    // -- Section sortie --
    label_out_iface_.setText("Out", juce::dontSendNotification);
    addAndMakeVisible(label_out_iface_);

    combo_out_iface_.setTextWhenNothingSelected("-- Sortie --");
    combo_out_iface_.addListener(this);
    addAndMakeVisible(combo_out_iface_);

    label_out_info_.setJustificationType(juce::Justification::topLeft);
    label_out_info_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(label_out_info_);

    label_out_ch_.setText("Ch", juce::dontSendNotification);
    addAndMakeVisible(label_out_ch_);

    combo_out_ch_.setTextWhenNothingSelected("-- canal --");
    combo_out_ch_.addListener(this);
    addAndMakeVisible(combo_out_ch_);

    populateCombos();
    startTimerHz(10);
}

// ----------------------------------------------------------------------------
JuceEditorComponent::~JuceEditorComponent()
{
    stopTimer();
    combo_driver_  .removeListener(this);
    combo_in_iface_.removeListener(this);
    combo_in_ch_   .removeListener(this);
    combo_out_iface_.removeListener(this);
    combo_out_ch_  .removeListener(this);
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(
        juce::ResizableWindow::backgroundColourId));

    // Separateur visuel entre les deux sections
    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    const int sep_y = kGap + kRowH + kGap + kInfoH + kGap + kRowH + kGap / 2;
    g.drawHorizontalLine(sep_y,
        static_cast<float>(kGap),
        static_cast<float>(kWidth - kGap));
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::resized()
{
    // Layout :
    //   [label_w] [combo / info               ] [vu_w]
    //
    // Section entree (haut), separateur, section sortie (bas).

    const int content_w = kWidth  - kGap * 2;
    const int content_end_w = content_w - kVuW;
    int y = kGap;
    int x = kGap;
    int in_pos;
    int out_pos;
    // ---- Section Audio driver ------------------------------------------------

    // Audio Driver
    label_driver_.setBounds( x, y, kLabelW,  kRowH);
    x += kLabelW;
    combo_driver_.setBounds( x, y, kComboInterfaceW,  kRowH);
    x += kComboInterfaceW ;
    y += kRowH;

    // ---- Section interfaces ------------------------------------------------
    // interface entree
    x = kGap;
    in_pos=x;
    label_in_iface_.setBounds( x, y, kLabelW,  kRowH);
    x += kLabelW;
    combo_in_iface_.setBounds( x, y, kComboInterfaceW,  kRowH);
    x += kComboInterfaceW ;

    // canal entree
    label_in_ch_.setBounds( x , y, kLabelW, kRowH);
    x += kLabelW;
    combo_in_ch_.setBounds( x , y, kComboChannelW, kRowH);
    x += kComboChannelW;

    // interface sortie
    out_pos=x;
    label_out_iface_.setBounds( x , y, kLabelW, kRowH);
    x += kLabelW;
    combo_out_iface_.setBounds( x , y, kComboInterfaceW, kRowH);
    x += kComboInterfaceW ;

    // canal sortie
    label_out_ch_.setBounds( x , y, kLabelW, kRowH);
    x += kLabelW;
    combo_out_ch_.setBounds( x , y, kComboChannelW, kRowH);
    x += kComboInterfaceW;

    x = kGap;
    y += kRowH;
    // Info entree
    label_in_info_.setBounds(  in_pos, y, content_w - out_pos, kInfoH);
    // Info sortie
    label_out_info_.setBounds( out_pos, y, content_w - out_pos, kInfoH);
    

    // ---- Separateur ----------------------------------------------------
    y += kSepH;

    // ---- chaine de traitement ------------------------------------------------
    const int vu_h = kHeight - y;
    const int vu_top = y;
    
    vu_in_.setBounds( x, vu_top, kVuW, vu_h);
    vu_out_.setBounds( content_end_w, vu_top, kVuW, vu_h);

    



}

// ----------------------------------------------------------------------------
void JuceEditorComponent::timerCallback()
{
    auto* editor = plugin_.layer()->editor();
    if (!editor) return;

    editor->poll();

    // Stub : niveaux a zero tant que processBlock ne publie pas de mesures.
    // Phase 3b+ : lire depuis un double-buffer atomique alimente par processBlock.
    vu_in_ .setPeak(0, 0.0f); vu_in_ .setRms(0, 0.0f);
    vu_out_.setPeak(0, 0.0f); vu_out_.setRms(0, 0.0f);
    vu_in_ .repaint();
    vu_out_.repaint();
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::comboBoxChanged(juce::ComboBox* cb)
{
    auto* editor = plugin_.layer()->editor();
    if (!editor) return;

    if (cb == &combo_driver_)
    {
        const int idx = cb->getSelectedId() - 1;
        if (idx < 0) return;
        const auto& list = editor->audioDrivers();
        if (idx >= static_cast<int>(list.size())) return;
        const int id = list[static_cast<size_t>(idx)].id;
        editor->selectDriver(id);
        plugin_.layer()->scanDevices(id);
        populateInterfaceCombos();
    }
    else if (cb == &combo_in_iface_)
    {
        const int idx = cb->getSelectedId() - 1;
        if (idx < 0) return;
        const auto& list = editor->audioInputs();
        if (idx >= static_cast<int>(list.size())) return;
        const int id = list[static_cast<size_t>(idx)].id;
        editor->selectInputInterface(id);
        updateInputInfo(id);
    }
    else if (cb == &combo_in_ch_)
    {
        editor->selectInputChannel(cb->getSelectedId() - 1);
    }
    else if (cb == &combo_out_iface_)
    {
        const int idx = cb->getSelectedId() - 1;
        if (idx < 0) return;
        const auto& list = editor->audioOutputs();
        if (idx >= static_cast<int>(list.size())) return;
        const int id = list[static_cast<size_t>(idx)].id;
        editor->selectOutputInterface(id);
        updateOutputInfo(id);
    }
    else if (cb == &combo_out_ch_)
    {
        editor->selectOutputChannel(cb->getSelectedId() - 1);
    }
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::populateCombos()
{
    auto* editor = plugin_.layer()->editor();
    if (!editor) return;

    combo_driver_.clear(juce::dontSendNotification);
    int jid = 1;
    for (const auto& drv : editor->audioDrivers())
        combo_driver_.addItem(drv.name, jid++);
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::populateInterfaceCombos()
{
    auto* editor = plugin_.layer()->editor();
    if (!editor) return;

    combo_in_iface_.clear(juce::dontSendNotification);
    combo_in_ch_   .clear(juce::dontSendNotification);
    label_in_info_ .setText("", juce::dontSendNotification);
    int jid = 1;
    for (const auto& iface : editor->audioInputs())
        combo_in_iface_.addItem(iface.name, jid++);

    combo_out_iface_.clear(juce::dontSendNotification);
    combo_out_ch_   .clear(juce::dontSendNotification);
    label_out_info_ .setText("", juce::dontSendNotification);
    jid = 1;
    for (const auto& iface : editor->audioOutputs())
        combo_out_iface_.addItem(iface.name, jid++);
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::updateInputInfo(int interface_id)
{
    auto* editor = plugin_.layer()->editor();
    if (!editor) return;

    for (const auto& iface : editor->audioInputs()) {
        if (iface.id != interface_id) continue;

        label_in_info_.setText(buildInfoString(iface, true),
                               juce::dontSendNotification);

        combo_in_ch_.clear(juce::dontSendNotification);
        for (int ch = 0; ch < iface.max_input_channels; ++ch) {
            const std::string label =
                (ch < static_cast<int>(iface.channel_names.size()) && !iface.channel_names[ch].empty())
                ? iface.channel_names[ch]
                : "In " + std::to_string(ch + 1);
            combo_in_ch_.addItem(label, ch + 1);
        }

        vu_in_.setNumChannels(juce::jlimit(1, VuMeter::kMaxChannels,
                                           iface.max_input_channels));
        return;
    }

    label_in_info_.setText("", juce::dontSendNotification);
    combo_in_ch_.clear(juce::dontSendNotification);
}

// ----------------------------------------------------------------------------
void JuceEditorComponent::updateOutputInfo(int interface_id)
{
    auto* editor = plugin_.layer()->editor();
    if (!editor) return;

    for (const auto& iface : editor->audioOutputs()) {
        if (iface.id != interface_id) continue;

        label_out_info_.setText(buildInfoString(iface, false),
                                juce::dontSendNotification);

        combo_out_ch_.clear(juce::dontSendNotification);
        for (int ch = 0; ch < iface.max_output_channels; ++ch) {
            const std::string label =
                (ch < static_cast<int>(iface.channel_names.size()) && !iface.channel_names[ch].empty())
                ? iface.channel_names[ch]
                : "Out " + std::to_string(ch + 1);
            combo_out_ch_.addItem(label, ch + 1);
        }

        vu_out_.setNumChannels(juce::jlimit(1, VuMeter::kMaxChannels,
                                            iface.max_output_channels));
        return;
    }

    label_out_info_.setText("", juce::dontSendNotification);
    combo_out_ch_.clear(juce::dontSendNotification);
}

} // namespace odenise::plugin

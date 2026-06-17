// ============================================================================
//  src/plugin/juce/ui/JuceEditorComponent.cpp
// ============================================================================
#include "common.h"
#include "JuceEditorComponent.h"
#include "JucePlugin.h"

namespace odenise::plugin {

    // ============================================================================
    //  VuMeter
    // ============================================================================

    VuMeter::VuMeter(int num_channels)
        : num_channels_(juce::jlimit(1, kMaxChannels, num_channels))
    {}

    void VuMeter::setNumChannels(int n) noexcept{
        num_channels_ = juce::jlimit(1, kMaxChannels, n);
    }

    void VuMeter::setPeak(int ch, float value) noexcept{
        if (ch < 0 || ch >= num_channels_) return;
        peak_[ch] = juce::jlimit(0.0f, 1.0f, value);
        if (peak_[ch] > peak_hold_[ch])
            peak_hold_[ch] = peak_[ch];
    }

    void VuMeter::setRms(int ch, float value) noexcept{
        if (ch < 0 || ch >= num_channels_) return;
        rms_[ch] = juce::jlimit(0.0f, 1.0f, value);
    }

    void VuMeter::paint(juce::Graphics& g){
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
                                    bool show_inputs){
        std::string s;

        // -- Bloc "Actuel" -------------------------------------------------------
        // -- Canaux --------------------------------------------------------------
        const int ch = show_inputs ? iface.max_input_channels : iface.max_output_channels;
        s += std::to_string(ch);
        s += "Ch ";

        // Sample Rate
        const int sr = iface.current_sample_rate;
        float sr_khz = sr / 1000.0f;
        if (sr > 0) {
            char tmp[16];  
            std::snprintf(tmp, sizeof(tmp),
                floorf(sr_khz) == sr_khz ? "%.0fKHz " : "%.1fKHz ",
                sr_khz);
            s += tmp;
        } else {
            s += "Sample rate: N/A ";
        }
        /* DEBUG
        s += "sr: ";
        s += std::to_string(sr);
        */

        // Transport bits depth
        s += (iface.current_bit_depth > 0) ? std::to_string(iface.current_bit_depth) : "N/A";
        s += "bits ";

        // Sample Buffer
        const int   buf    = (iface.current_buffer_size > 0)
                            ? iface.current_buffer_size
                            : iface.default_buffer_size;
        //s += "Buffer Size: ";
        if (buf > 0) { 
            s += std::to_string(buf);
            s += "smp "; 
        }else{
            s += " N/A ";
        }
        /* DEBUG
        const int   cur_buf = iface.current_buffer_size;
        const int   def_buf = iface.default_buffer_size;
        s += "\nbuf: ";
        s += std::to_string(buf);
        s += "\ncur_buf: ";
        s += std::to_string(cur_buf);
        s += "\ndef_buf: ";
        s += std::to_string(def_buf);
        */

        // Latencies
        const int lat_smpl = show_inputs 
                            ? iface.input_latency_samples 
                            : iface.output_latency_samples; 

        const float lat_ms_eff = (sr > 0 && buf > 0)
                                ? (static_cast<float>(lat_smpl) / static_cast<float>(sr)) * 1000.0f
                                : 0.0f;
        if (lat_ms_eff > 0.0f) {
            char lat_buf[16];
            std::snprintf(lat_buf, sizeof(lat_buf), "%.3f", static_cast<double>(lat_ms_eff));
            s += lat_buf;
            s += "ms";
        }else{
            s += "lat: N/A";
        }
        /* DEBUG
        s += "\nlat_smpl: ";
        s += std::to_string(lat_smpl);
        s += "\niface.input_latency_samples: ";
        s += std::to_string(iface.input_latency_samples);
        s += "\niface.output_latency_samples: ";
        s += std::to_string(iface.output_latency_samples);
        const float lat_ms_th = (sr > 0 && buf > 0)
                            ? (static_cast<float>(buf) / static_cast<float>(sr)) * 1000.0f
                            : 0.0f;
        s += "\nlat_ms_th: ";
        s += std::to_string(lat_ms_th);
        s += "\nlat_ms_eff: ";
        s += std::to_string(lat_ms_eff);
        s += "\n";
        */
        /*
        // -- Sample rates disponibles (actuel entre [ ]) -------------------------
        if (!iface.supported_sample_rates.empty()) {
            s += "\nRates:";
            for (int r : iface.supported_sample_rates) {
                const float r_khz = r / 1000.0f;
                char tmp[16];
                std::snprintf(tmp, sizeof(tmp),
                floorf(r_khz) == r_khz ? "%.0fKHz " : "%.1fKHz ",
                r_khz);
                
                if (r_khz == sr_khz) {
                    s += " [";
                    s += tmp;
                    s += "]";
                }else{
                    s += "  ";
                    s += tmp;
                }                          
            }
        }

        // -- Buffer sizes disponibles (actuel entre [ ]) -------------------------
        if (!iface.supported_buffer_sizes.empty()) {
            s += "\nBuffers:";
            for (int b : iface.supported_buffer_sizes) {
                s += (b == buf) ? " [" + std::to_string(b) + "]"
                                : "  " + std::to_string(b);
            }
        }
        */
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
        setResizable(true,true);
        setSize(kWidth, kHeight);
        //setResizeLimits(kWidth, kHeight, kWidth, kHeight);

        addAndMakeVisible(vu_in_);
        addAndMakeVisible(vu_out_);

        // Audio driver slection
        label_drv_.setText("Driver:", juce::dontSendNotification);
        label_drv_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(label_drv_);

        combo_drv_.setTextWhenNothingSelected("-- Pilote --");
        combo_drv_.getProperties().set("minimumHorizontalScale", 1.0f);
        combo_drv_.addListener(this);
        addAndMakeVisible(combo_drv_);

        // Compute BackEnd selection
        combo_bcknd_.setTextWhenNothingSelected("-- BackEnd --");
        combo_bcknd_.getProperties().set("minimumHorizontalScale", 1.0f);
        combo_bcknd_.addListener(this);
        addAndMakeVisible(combo_bcknd_);

        // round-trip computation
        label_rnd_trp_.setText("lat_in_ms + process_ms + lat_out_ms = out_ms", juce::dontSendNotification);
        label_rnd_trp_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(label_rnd_trp_);

        // -- Section entree --
        label_in_iface_.setText("In:", juce::dontSendNotification);
        label_in_iface_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(label_in_iface_);

        combo_in_iface_.setTextWhenNothingSelected("-- Entree --");
        combo_in_iface_.getProperties().set("minimumHorizontalScale", 1.0f);
        combo_in_iface_.addListener(this);
        addAndMakeVisible(combo_in_iface_);
        /*
        label_in_ch_.setText("Ch:", juce::dontSendNotification);
        label_in_ch_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(label_in_ch_);
        */
        combo_in_ch_.setTextWhenNothingSelected("-- Canal --");
        combo_in_ch_.getProperties().set("minimumHorizontalScale", 1.0f);
        combo_in_ch_.addListener(this);
        addAndMakeVisible(combo_in_ch_);

    
        // -- Section sortie --
        label_out_iface_.setText("Out:", juce::dontSendNotification);
        label_out_iface_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(label_out_iface_);

        combo_out_iface_.setTextWhenNothingSelected("-- Sortie --");
        combo_out_iface_.getProperties().set("minimumHorizontalScale", 1.0f);
        combo_out_iface_.addListener(this);
        addAndMakeVisible(combo_out_iface_);

        /*
        label_out_ch_.setText("Ch:", juce::dontSendNotification);
        label_out_ch_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(label_out_ch_);
        */
        combo_out_ch_.setTextWhenNothingSelected("-- Canal --");
        combo_out_ch_.getProperties().set("minimumHorizontalScale", 1.0f);
        combo_out_ch_.addListener(this);
        addAndMakeVisible(combo_out_ch_);

        // -- Interface info --
        // Entree
        label_in_info_.setJustificationType(juce::Justification::topLeft);
        label_in_info_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(label_in_info_);
        // Sortie
        label_out_info_.setJustificationType(juce::Justification::topLeft);
        label_out_info_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(label_out_info_);

        // -- Modules --
        // list of modules
        combo_mods_.setTextWhenNothingSelected("-- Modules --");
        combo_mods_.getProperties().set("minimumHorizontalScale", 1.0f);
        combo_mods_.addListener(this);
        addAndMakeVisible(combo_mods_);
        // info of selected module
        label_module_info_.setJustificationType(juce::Justification::topLeft);
        label_module_info_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(label_module_info_);
        
        // -- Vue chaine --
        chain_view_.refresh(); // vide au demarrage, se peuple apres selectBackend
        addAndMakeVisible(chain_view_);
    
        btn_layout_toggle_.setButtonText("H/V");
        btn_layout_toggle_.onClick = [this] {
            chain_view_.toggleLayout();
        };
        addAndMakeVisible(btn_layout_toggle_);

        populateCombosDriver();
        populateComboBackends();
        startTimerHz(10);
    }

    // ----------------------------------------------------------------------------
    JuceEditorComponent::~JuceEditorComponent(){
        stopTimer();
        combo_drv_.removeListener(this);
        combo_bcknd_.removeListener(this);
        combo_mods_.removeListener(this);
        combo_in_iface_.removeListener(this);
        combo_in_ch_.removeListener(this);
        combo_out_iface_.removeListener(this);
        combo_out_ch_.removeListener(this);
    }

    // ----------------------------------------------------------------------------
    void JuceEditorComponent::paint(juce::Graphics& g){
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
    void JuceEditorComponent::resized(){
        // Layout :
        //   [combo_drv / driver]           [combo_bcknd / bcknd_cmpt]           [label_rnd_trp / lat_in + lat_prcssng + lat_out]
        //   [label_in] [combo_in / interface] [combo_in / channel]     [label_out] [combo_out / interface] [combo_out / channel]
        //   [label_in / info]                                                                                 [label_out / info]
        //   [vu_in]                                                                                                     [vu_out]
        // Selection Driver, selection backend, info latence round-trip
        // Section entree (haut),  section sortie (bas).
        // separateur,
        // ajoute et chainnage des modules.

        const int content_w = kWidth  - (kGap * 2);
        const int content_end_w = content_w - kVuW;
        int y = kGap;
        int x = kGap;
        int in_pos;
        int out_pos;
        // ---- Section Audio driver ------------------------------------------------

        // Audio Driver

        /*label_drv_.setBounds( x, y, kLabeldrvW,  kRowH); x += kLabeldrvW;*/
        combo_drv_.setBounds( x, y, kComboDrvW,  kRowH);
        // BackEnd
        x += kComboDrvW + dGap ;
        combo_bcknd_.setBounds( x, y, kComboDrvW,  kRowH);
        // round-trip 
        x += kComboDrvW + dGap;
        label_rnd_trp_.setBounds( x, y, kLabelRndTrpW,  kRowH);
        y += kRowH + kGap;

        // ---- Section interfaces ------------------------------------------------
        // interface entree
        // x + kLabelW + kComboIfaceW + kComboChnlW - kGap = 30 + 110 + 100 - 4 = 236;
        x = 0;
        in_pos=x;
        label_in_iface_.setBounds( x, y, kLabelW,  kRowH);
        x += kLabelW;
        combo_in_iface_.setBounds( x - kGap, y, kComboIfaceW,  kRowH);
        x += kComboIfaceW - kGap;

        // canal entree
        /*label_in_ch_.setBounds( x , y, kLabelW + kGap, kRowH);
        x += kLabelW;*/
        combo_in_ch_.setBounds( x , y, kComboChnlW, kRowH);
        x += kComboChnlW;

        // interface sortie
        // x + 10 + kLabelW + 6 + kComboIfaceW + kComboChnlW = 4 + 10 + 30 + 6 + 110 + 100 = 246;
        x= kWidth - kLabelW - 6 - kGap - kComboIfaceW - kComboChnlW;
        out_pos=x;
        label_out_iface_.setBounds( x , y, kLabelW + (2*kGap), kRowH);
        x += kLabelW + 6;
        combo_out_iface_.setBounds( x , y, kComboIfaceW, kRowH);
        x += kComboIfaceW;

        // canal sortie
        /*label_out_ch_.setBounds( x , y, kLabelW + kGap, kRowH);
        x += kLabelW;*/
        combo_out_ch_.setBounds( x , y, kComboChnlW, kRowH);

        x = kGap;
        y += kRowH;
        // Info entree
        label_in_info_.setBounds(  in_pos, y, content_w - out_pos, kInfoH);
        // Info sortie
        label_out_info_.setBounds( out_pos, y, content_w - out_pos, kInfoH);
        

        // ---- Separateur ----------------------------------------------------
        y += kSepH;

        // ---- chaine de traitement ------------------------------------------
        x = kVuW + kGap;
        combo_mods_.setBounds( x , y, kComboChnlW, kRowH);
        x += kComboChnlW; 
        label_module_info_.setBounds( x , y, (2*kComboChnlW), (2*kComboChnlW));

        
        // --- Vue graphique de la chaine -------------------------------------
        // Positionnee sous la ligne combo_mods_ + label_module_info_.
        // Le bouton H/V est aligné a droite de la zone.
        y += kRowH + kGap;
        const int btn_w = 36;
        btn_layout_toggle_.setBounds(kWidth - btn_w - kGap, y, btn_w, kRowH);
        y += kRowH + kGap / 2;
        chain_view_.setBounds(kGap, y, kWidth - (2 * kGap), kChainViewH);

        // ---- Vu metres ------------------------------------------------
        const int vu_h = kHeight - y;
        const int vu_top = y;
        x = 0;
        vu_in_.setBounds( x, vu_top, kVuW, vu_h);
        vu_out_.setBounds( content_end_w +kGap, vu_top, kVuW, vu_h);
    }

    // ----------------------------------------------------------------------------
    void JuceEditorComponent::timerCallback(){
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
    void JuceEditorComponent::comboBoxChanged(juce::ComboBox* cb){
        auto* editor = plugin_.layer()->editor();
        if (!editor) return;
        
        if (cb == &combo_mods_){
            /*const int idx = cb->getSelectedId() - 1;
            if (idx < 0) return;
            const auto& list = editor->modules();
            if (idx >= static_cast<int>(list.size())) return;
            const int id = list[static_cast<size_t>(idx)].id;
            editor->selectModule(id);*/
            std::string infos = editor->get_module_info(cb->getSelectedId() - 1);
            label_module_info_.setText(infos, juce::dontSendNotification);
            // Reconstruit le graphe UI apres ajout d'un module.
            editor->rebuildGraph();
            chain_view_.refresh();
            // TODO: afficher le modules etc...
        }
        else if (cb == &combo_bcknd_){
            const int idx = cb->getSelectedId() - 1;
            if (idx < 0) return;
            const auto& list = editor->backends();
            if (idx >= static_cast<int>(list.size())) return;
            const size_t id = list[static_cast<size_t>(idx)].id;
            editor->selectBackend(id);
            editor->get_modules();
            populateComboModules();
        }
        else if (cb == &combo_in_ch_){
            editor->selectInputChannel(cb->getSelectedId() - 1);
        }
        else if (cb == &combo_out_ch_){
            editor->selectOutputChannel(cb->getSelectedId() - 1);
        }
        else if (cb == &combo_in_iface_){
            const int idx = cb->getSelectedId() - 1;
            if (idx < 0) return;
            const auto& list = editor->audioInputs();
            if (idx >= static_cast<int>(list.size())) return;
            const int id = list[static_cast<size_t>(idx)].id;
            const std::string name = list[static_cast<size_t>(idx)].name;
            // probeDevice avant selectInputInterface pour que les capacites
            // soient disponibles quand updateInputInfo lit l'AudioInterfaceInfo.
            const int driver_id = editor->selectedDriverId();
            plugin_.layer()->probeDevice(driver_id, id, name, true);
            editor->selectInputInterface(id);
            updateInputInfo(id);
        }
        else if (cb == &combo_out_iface_){
            const int idx = cb->getSelectedId() - 1;
            if (idx < 0) return;
            const auto& list = editor->audioOutputs();
            if (idx >= static_cast<int>(list.size())) return;
            const int id = list[static_cast<size_t>(idx)].id;
            const std::string name = list[static_cast<size_t>(idx)].name;
            // probeDevice avant selectOutputInterface pour que les capacites
            // soient disponibles quand updateOutputInfo lit l'AudioInterfaceInfo.
            const int driver_id = editor->selectedDriverId();
            plugin_.layer()->probeDevice(driver_id, id, name, false);
            editor->selectOutputInterface(id);
            updateOutputInfo(id);
        }
        else if (cb == &combo_drv_){
            const int idx = cb->getSelectedId() - 1;
            if (idx < 0) return;
            const auto& list = editor->audioDrivers();
            if (idx >= static_cast<int>(list.size())) return;
            const int id = list[static_cast<size_t>(idx)].id;
            editor->selectDriver(id);
            plugin_.layer()->scanDevices(id);
            populateCombosInterface();
        }
    }

    // ----------------------------------------------------------------------------
    void JuceEditorComponent::populateCombosDriver(){
        auto* editor = plugin_.layer()->editor();
        if (!editor) return;

        combo_drv_.clear(juce::dontSendNotification);
        int jid = 1;
        for (const auto& drv : editor->audioDrivers())
            combo_drv_.addItem(drv.name, jid++);
    }

    // ----------------------------------------------------------------------------
    void JuceEditorComponent::populateComboBackends(){
        auto* editor = plugin_.layer()->editor();
        if (!editor) return;
        editor->get_backends();
        combo_bcknd_.clear(juce::dontSendNotification);
        int jid = 1;
        for (const auto& bcknd : editor->backends()){
            combo_bcknd_.addItem(bcknd.name, jid++);
        }
    }
    
    // ----------------------------------------------------------------------------
    void JuceEditorComponent::populateComboModules(){
        auto* editor = plugin_.layer()->editor();
        if (!editor) return;

        combo_mods_.clear(juce::dontSendNotification);
        int jid = 1;
        for (const auto& mod : editor->modules())
            combo_mods_.addItem(mod.name, jid++);
    }

    // ----------------------------------------------------------------------------
    void JuceEditorComponent::populateCombosInterface(){
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
    void JuceEditorComponent::updateOutputInfo(int interface_id){
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
    
}  
// namespace odenise::plugin

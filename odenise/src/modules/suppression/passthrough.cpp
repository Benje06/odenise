// ============================================================================
//  passthrough.cpp -- Module de suppression neutre (sortie = entree).
//
//  Phase 3 : implemente ModuleBase (chemin C++).
//
//  PassthroughModule :
//    - info_c()      : metadonnees POD (frontiere ABI-safe).
//    - self_test_c() : self-test POD (frontiere ABI-safe).
//    - latency_samples() = 0 (pas de latence algorithmique).
//    - install(ctx)  : alloue output_buf_ via ctx->scratch_buf().
//    - set_input(src): stocke input_.
//    - process(n)    : memcpy(output_buf_, input_, n * sizeof(float)).
// ============================================================================
#include "passthrough.h"

// -----------------------------------------------------------------------
//  info_c -- metadonnees POD (frontiere inter-compilateurs).
// -----------------------------------------------------------------------
const OdeniseModuleInfoC* PassthroughModule::info_c() const noexcept {
    static const PortDef s_ports[] = {
        { 0, kPortAudio, PortDir::In,  1, "audio_in"  },
        { 1, kPortAudio, PortDir::Out, 1, "audio_out" },
    };
    static const OdeniseModuleInfoC s_info = {
        /* abi_version    */ odenise::kAbiVersion,
        /* id             */ 1,
        /* kind           */ static_cast<int>(odenise::ModuleKind::Suppression),
        /* name           */ "passthrough",
        /* description    */ "Module neutre : sortie = entree (validation de la chaine)",
        /* needs_gpu      */ 0,
        /* backend_type_id*/ odenise::kBackendAny,
        /* ports          */ s_ports,
        /* port_count     */ 2
    };
    return &s_info;
}

static const OdeniseModuleInfoC s_info = {
    odenise::kAbiVersion, 1,
    static_cast<int>(odenise::ModuleKind::Suppression),
    "passthrough",
    "Module neutre : sortie = entree (validation de la chaine)",
    0, odenise::kBackendAny,
    
};
// -----------------------------------------------------------------------
//  self_test_c -- self-test POD (frontiere inter-compilateurs).
//  Instancie un module de test, effectue un process de 4 echantillons,
//  verifie que in == out. Utilise un buffer local comme output_buf_.
// -----------------------------------------------------------------------
const OdeniseTestResultC* PassthroughModule::self_test_c() const noexcept {
    static OdeniseTestResultC s_result = { 0, nullptr };

    auto* mod = new (std::nothrow) PassthroughModule;
    if (!mod) {
        s_result = { 0, "echec allocation PassthroughModule" };
        return &s_result;
    }

    // inject_output_for_test() permet au self-test de fournir un buffer
    // sans passer par BackendContext (ctx == nullptr en self-test).
    float in_buf[4]  = { 0.1f, -0.5f, 0.0f, 1.0f };
    float out_buf[4] = {};
    mod->inject_output_for_test(out_buf);
    mod->set_input(in_buf);
    mod->process(4);

    bool ok = true;
    for (int i = 0; i < 4; ++i)
        if (in_buf[i] != out_buf[i]) { ok = false; break; }

    delete mod;
    s_result = ok
        ? OdeniseTestResultC{ 1, "passthrough OK : 4 echantillons in == out" }
        : OdeniseTestResultC{ 0, "in != out apres passthrough" };
    return &s_result;
}

// -----------------------------------------------------------------------
//  ModuleBase -- interface de traitement.
// -----------------------------------------------------------------------
int PassthroughModule::latency_samples() const noexcept { return -1; }
int PassthroughModule::latency_samples_rt() const noexcept { return -1; }

// [CTRL] Installation : recupere le buffer de sortie depuis le scratch.
// ctx peut etre nullptr (cas du self-test interne).
bool PassthroughModule::install(odenise::BackendContext* ctx) {
    if (ctx)
        output_buf_ = ctx->scratch_buf(kMaxFrames * sizeof(float));
    // Si ctx == nullptr (self-test interne), output_buf_ reste tel quel.
    return true;
}

void PassthroughModule::uninstall(odenise::BackendContext* /*ctx*/) noexcept {
    // Le scratch appartient au backend -- pas de liberation ici.
    output_buf_ = nullptr;
    input_      = nullptr;
}

void PassthroughModule::set_param(odenise::ParamId /*id*/, float /*value*/) noexcept {
    
}

// [CTRL] Reconfiguration structurelle. Le passthrough n'a rien a
// reconfigurer : pas de buffers propres, pas de parametres DSP.
odenise::Status PassthroughModule::reconfigure(const odenise::BackendCaps& b_caps, const odenise::RuntimeConfig& cfg,
                                    odenise::ApplyResult& how) {
    return odenise::Status::Ok;
}

// [RT] Buffer de sortie du module.
void* PassthroughModule::output_buf() noexcept { return output_buf_; }

// [CTRL] Cablage : pointeur vers le buffer d'entree du module precedent.
void PassthroughModule::set_input(const void* src) noexcept {
    input_ = static_cast<const float*>(src);
}

// [RT] Passthrough : copie entree vers sortie.
void PassthroughModule::process(size_t num_frames) noexcept {
    if (input_ && output_buf_ && num_frames > 0)
        std::memcpy(output_buf_, input_,
                    num_frames * sizeof(float));
}

// [CTRL] Ports statiques du PassthroughModule.
//   audio_in  : entree PCM mono
//   audio_out : sortie PCM mono (= entree, latence 0)
const PortDef* PassthroughModule::ports(int& count) const noexcept {
    static const PortDef s_ports[] = {
        { 0, kPortAudio, PortDir::In,  1, "audio_in"  },
        { 1, kPortAudio, PortDir::Out, 1, "audio_out" },
    };
    count = 2;
    return s_ports;
}

void PassthroughModule::setAudioIO(odenise::TrackIO io) {

};

bool PassthroughModule::Run() {return true;};
bool PassthroughModule::Run2() {return true;};

void PassthroughModule::inject_output_for_test(void* buf) noexcept { 
    output_buf_ = buf; 
}
// ============================================================================
//  Point d'entree du module.
// ============================================================================
extern "C" ODENISE_EXPORT odenise::ModuleBase* odenise_module_entry(odenise::EngineCaps /*e_caps*/) {
    return new (std::nothrow) PassthroughModule;
}

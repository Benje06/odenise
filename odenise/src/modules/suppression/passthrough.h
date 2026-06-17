// ============================================================================
//  passthrough.h -- Module de suppression neutre (sortie = entree).
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
#pragma once
#include "engine.h"
#include <cstring>   // std::memcpy
#include <new>       // std::nothrow

class PassthroughModule final : public odenise::ModuleBase {
public:
    PassthroughModule() = default;
    ~PassthroughModule() override = default;

    // -----------------------------------------------------------------------
    //  info_c -- metadonnees POD (frontiere inter-compilateurs).
    // -----------------------------------------------------------------------
    const OdeniseModuleInfoC* info_c() const noexcept override;

    // -----------------------------------------------------------------------
    //  self_test_c -- self-test POD (frontiere inter-compilateurs).
    //  Instancie un module de test, effectue un process de 4 echantillons,
    //  verifie que in == out. Utilise un buffer local comme output_buf_.
    // -----------------------------------------------------------------------
    const OdeniseTestResultC* self_test_c() const noexcept override;

    // -----------------------------------------------------------------------
    //  ModuleBase -- interface de traitement.
    // -----------------------------------------------------------------------
    int latency_samples() const noexcept override;
    int latency_samples_rt() const noexcept override;

    // [CTRL] Installation : recupere le buffer de sortie depuis le scratch.
    // ctx peut etre nullptr (cas du self-test interne).
    bool install(odenise::BackendContext* ctx) override;

    void uninstall(odenise::BackendContext* /*ctx*/) noexcept override;

    void set_param(odenise::ParamId /*id*/, float /*value*/) noexcept override;

    // [CTRL] Reconfiguration structurelle. Le passthrough n'a rien a
    // reconfigurer : pas de buffers propres, pas de parametres DSP.
    odenise::Status reconfigure(const odenise::BackendCaps& b_caps,
                                const odenise::RuntimeConfig& cfg,
                                odenise::ApplyResult& how) override;

    // [RT] Buffer de sortie du module.
    void* output_buf() noexcept override ;

    // [CTRL] Cablage : pointeur vers le buffer d'entree du module precedent.
    void set_input(const void* src) noexcept override ;

    // [RT] Passthrough : copie entree vers sortie.
    void process(size_t num_frames) noexcept override;

    // [CTRL] Description statique des ports du module.
    const PortDef* ports(int& count) const noexcept override;
 
    // [CTRL] Injection du buffer de sortie pour le self-test interne.
    // Permet de fournir un buffer local sans passer par BackendContext.
    // Ne jamais appeler depuis le chemin RT.
    void inject_output_for_test(void* buf) noexcept;

    void setAudioIO(odenise::TrackIO io) override;

private:

    bool Run() override;
    bool Run2() override;
    // Taille maximale de bloc acceptee (pre-allouee par le backend a n_max).
    // Valeur par defaut coherente avec EngineCaps::n_max.
    static constexpr int kMaxFrames = 4096;

    const float* input_      = nullptr;  // cablage : buffer entree (module precedent)
    void*        output_buf_ = nullptr;  // scratch buffer fourni par BackendContext
};

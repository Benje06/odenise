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
    const OdeniseModuleInfoC* info_c() const noexcept override {
        static const OdeniseModuleInfoC s_info = {
            /* abi_version    */ odenise::kAbiVersion,
            /* id             */ 1,
            /* kind           */ static_cast<int>(odenise::ModuleKind::Suppression),
            /* name           */ "passthrough",
            /* description    */ "Module neutre : sortie = entree (validation de la chaine)",
            /* needs_gpu      */ 0,
            /* backend_type_id*/ odenise::kBackendAny
        };
        return &s_info;
    }

    // -----------------------------------------------------------------------
    //  self_test_c -- self-test POD (frontiere inter-compilateurs).
    //  Instancie un module de test, effectue un process de 4 echantillons,
    //  verifie que in == out. Utilise un buffer local comme output_buf_.
    // -----------------------------------------------------------------------
    const OdeniseTestResultC* self_test_c() const noexcept override {
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
    int latency_samples() const noexcept override { return -1; }
    int latency_samples_rt() const noexcept override { return -1; }

    // [CTRL] Installation : recupere le buffer de sortie depuis le scratch.
    // ctx peut etre nullptr (cas du self-test interne).
    bool install(odenise::BackendContext* ctx) override {
        if (ctx)
            output_buf_ = ctx->scratch_buf(kMaxFrames * sizeof(float));
        // Si ctx == nullptr (self-test interne), output_buf_ reste tel quel.
        return true;
    }

    void uninstall(odenise::BackendContext* /*ctx*/) noexcept override {
        // Le scratch appartient au backend -- pas de liberation ici.
        output_buf_ = nullptr;
        input_      = nullptr;
    }

    void set_param(odenise::ParamId /*id*/, float /*value*/) noexcept override {}

    // [CTRL] Reconfiguration structurelle. Le passthrough n'a rien a
    // reconfigurer : pas de buffers propres, pas de parametres DSP.
    odenise::Status reconfigure(const odenise::BackendCaps& b_caps, const odenise::RuntimeConfig& cfg,
                                      odenise::ApplyResult& how) override {
        return odenise::Status::Ok;
    }

    // [RT] Buffer de sortie du module.
    void* output_buf() noexcept override { return output_buf_; }

    // [CTRL] Cablage : pointeur vers le buffer d'entree du module precedent.
    void set_input(const void* src) noexcept override {
        input_ = static_cast<const float*>(src);
    }

    // [RT] Passthrough : copie entree vers sortie.
    void process(size_t num_frames) noexcept override {
        if (input_ && output_buf_ && num_frames > 0)
            std::memcpy(output_buf_, input_,
                        num_frames * sizeof(float));
    }

    void setAudioIO(odenise::TrackIO io) override {};

private:

    bool Run() override {return true;};
    bool Run2() override {return true;};
    // Taille maximale de bloc acceptee (pre-allouee par le backend a n_max).
    // Valeur par defaut coherente avec EngineCaps::n_max.
    static constexpr int kMaxFrames = 4096;

    // [CTRL] Injection du buffer de sortie pour le self-test interne.
    // Permet de fournir un buffer local sans passer par BackendContext.
    // Ne jamais appeler depuis le chemin RT.
    void inject_output_for_test(void* buf) noexcept { output_buf_ = buf; }

    const float* input_      = nullptr;  // cablage : buffer entree (module precedent)
    void*        output_buf_ = nullptr;  // scratch buffer fourni par BackendContext
};

// ============================================================================
//  Point d'entree du module.
// ============================================================================
extern "C" ODENISE_EXPORT odenise::ModuleBase* odenise_module_entry(odenise::EngineCaps /*e_caps*/) {
    return new (std::nothrow) PassthroughModule;
}

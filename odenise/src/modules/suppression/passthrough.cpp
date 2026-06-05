// ============================================================================
//  passthrough.cpp -- Module de suppression neutre (sortie = entree).
//
//  Phase 3b : implemente ModuleBase (chemin C++) + expose create_module.
//  Valide la chaine dlopen + AudioChain + BackendContext de bout en bout.
//  Derriere la frontiere C (vtable), l'implementation est du C++ normal.
// ============================================================================
#include "ns_engine.h"    // OdeniseModuleVTable, ModuleBase, etc.
#include <cstring>        // std::memcpy
#include <new>            // std::nothrow

// ============================================================================
//  PassthroughModule -- implementation de ModuleBase.
//  Latence nulle. Copie memcpy de l'entree vers le buffer de sortie.
//  Le buffer de sortie est alloue dans le scratch du backend a l'install.
// ============================================================================
class PassthroughModule final : public ns::ModuleBase {
public:
    PassthroughModule(int /*sample_rate*/, int n_max)
        : n_max_(n_max) {}

    ~PassthroughModule() override = default;

    // [CTRL] Latence algorithmique : zero (copie directe).
    int latency_samples() const noexcept override { return 0; }

    // [CTRL] Installation sur le contexte backend.
    // Alloue le buffer de sortie dans le scratch pre-alloue par le backend.
    // Retourne false si ctx est nul ou si le scratch est insuffisant.
    bool install(ns::BackendContext* ctx) override {
        if (!ctx) return false;
        const std::size_t bytes = static_cast<std::size_t>(n_max_) * sizeof(float);
        output_buf_ = static_cast<float*>(ctx->scratch_buf(bytes));
        return (output_buf_ != nullptr);
    }

    // [CTRL] Liberation : le scratch appartient au backend, on ne le libere pas.
    void uninstall(ns::BackendContext* /*ctx*/) noexcept override {
        output_buf_ = nullptr;
        input_      = nullptr;
    }

    // [RT] Parametre a chaud : pas de parametre pour le passthrough.
    void set_param(ns::ParamId /*id*/, float /*value*/) noexcept override {}

    // [CTRL] Buffer de sortie (pointe dans le scratch du backend).
    void* output_buf() noexcept override { return output_buf_; }

    // [CTRL] Cablage : le backend indique l'entree au cablage.
    void set_input(const void* src) noexcept override {
        input_ = static_cast<const float*>(src);
    }

    // [RT] Traitement : memcpy entree -> sortie. Zero allocation, zero verrou.
    void process(int num_frames) noexcept override {
        if (!input_ || !output_buf_ || num_frames <= 0) return;
        std::memcpy(output_buf_, input_,
                    static_cast<std::size_t>(num_frames) * sizeof(float));
    }

private:
    int          n_max_      = 4096;
    const float* input_      = nullptr;   // entree cablee (pointeur externe)
    float*       output_buf_ = nullptr;   // dans le scratch du backend
};

// ============================================================================
//  self_test -- valide PassthroughModule sans moteur ni backend reel.
//  Utilise un BackendContext minimal en pile pour les tests autonomes.
// ============================================================================
namespace {

// BackendContext de test : scratch en pile, pas de stream.
class ScratchContext final : public ns::BackendContext {
public:
    explicit ScratchContext(float* buf) : buf_(buf) {}
    void* scratch_buf(std::size_t /*bytes*/) noexcept override { return buf_; }
    void* compute_stream() noexcept override { return nullptr; }
    int   backend_type()   const noexcept override { return ns::kBackendCPU; }
private:
    float* buf_;
};

} // namespace

static OdeniseTestResultC pt_self_test() {
    constexpr int N = 4;
    float in_buf[N]  = { 0.1f, -0.5f, 0.0f, 1.0f };
    float out_buf[N] = {};

    ScratchContext ctx(out_buf);
    PassthroughModule mod(48000, N);

    if (!mod.install(&ctx))
        return { 0, "install a echoue" };

    mod.set_input(in_buf);
    mod.process(N);
    mod.uninstall(&ctx);

    for (int i = 0; i < N; ++i) {
        if (in_buf[i] != out_buf[i])
            return { 0, "in != out apres passthrough" };
    }
    return { 1, "passthrough OK : 4 echantillons in == out (chemin C++)" };
}

// ============================================================================
//  Extension C++ (phase 3b) -- create_module.
//  Retourne un PassthroughModule* vu comme ns::ModuleBase*.
//  Gere par le module : cree ici, detruit par l'engine via destructeur virtuel.
// ============================================================================
static ns::ModuleBase* pt_create_module(int sample_rate, int n_max) {
    return new (std::nothrow) PassthroughModule(sample_rate, n_max);
}

// ============================================================================
//  Vtable statique + point d'entree.
// ============================================================================
static const OdeniseModuleVTable s_vtable = {
    /* abi_version   */ ns::kAbiVersion,
    /* info          */ { /* id          */ 1,
                          /* kind        */ static_cast<int>(ns::ModuleKind::Suppression),
                          /* name        */ "passthrough",
                          /* description */ "Module neutre : sortie = entree (validation de la chaine)",
                          /* needs_gpu   */ 0,
                          /* backend_type_id */ ns::kBackendAny },
    /* create        */ nullptr,
    /* destroy       */ nullptr,
    /* set_param     */ nullptr,
    /* process       */ nullptr,
    /* self_test     */ pt_self_test,
    /* create_module */ pt_create_module,
    /* create_backend*/ nullptr
};

extern "C" ODENISE_EXPORT const OdeniseModuleVTable* odenise_module_entry() {
    return &s_vtable;
}

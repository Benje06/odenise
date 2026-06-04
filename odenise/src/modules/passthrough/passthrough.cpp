// ============================================================================
//  passthrough.cpp -- Module de suppression neutre (sortie = entree).
//
//  Premier module dynamique : valide la chaine dlopen de bout en bout.
//  Derriere la frontiere C (vtable), l'implementation est du C++ normal.
// ============================================================================
#include "ns_engine.h"    // OdeniseModuleVTable, OdeniseProcessCtx, etc.
#include <cstring>        // std::memcpy
#include <algorithm>      // std::min

// --- Instance C++ (opaque cote coeur, vue comme void*) ---
struct PassthroughInstance {
    int sample_rate = 48000;
    int n_max       = 4096;
};

// --- Fonctions de la vtable (extern "C" noexcept) ---
static OdeniseModuleInstance pt_create(int sample_rate, int n_max) {
    auto* inst = new (std::nothrow) PassthroughInstance;
    if (!inst) return nullptr;
    inst->sample_rate = sample_rate;
    inst->n_max       = n_max;
    return static_cast<OdeniseModuleInstance>(inst);
}

static void pt_destroy(OdeniseModuleInstance self) {
    delete static_cast<PassthroughInstance*>(self);
}

static int pt_set_param(OdeniseModuleInstance /*self*/, int /*param_id*/, float /*value*/) {
    return static_cast<int>(ns::Status::Ok);   // pas de parametre
}

static int pt_process(OdeniseModuleInstance /*self*/, OdeniseProcessCtx* ctx) {
    if (!ctx || !ctx->in || !ctx->out || ctx->in_channels < 1)
        return static_cast<int>(ns::Status::InvalidArg);
    // Copie le premier canal d'entree vers la sortie (passthrough mono).
    std::memcpy(ctx->out, ctx->in[0],
                static_cast<std::size_t>(ctx->num_frames) * sizeof(float));
    return static_cast<int>(ns::Status::Ok);
}

static OdeniseTestResultC pt_self_test() {
    // Test minimal : creer une instance, traiter 4 echantillons, verifier in==out.
    auto* inst = static_cast<PassthroughInstance*>(pt_create(48000, 1024));
    if (!inst)
        return { 0, "echec allocation instance" };

    float in_buf[4]  = { 0.1f, -0.5f, 0.0f, 1.0f };
    float out_buf[4] = {};
    const float* in_ptr = in_buf;
    OdeniseProcessCtx ctx;
    ctx.in           = &in_ptr;
    ctx.out          = out_buf;
    ctx.in_channels  = 1;
    ctx.num_frames   = 4;

    int rc = pt_process(inst, &ctx);
    pt_destroy(inst);

    if (rc != 0)
        return { 0, "process a renvoye une erreur" };

    for (int i = 0; i < 4; ++i) {
        if (in_buf[i] != out_buf[i])
            return { 0, "in != out apres passthrough" };
    }
    return { 1, "passthrough OK : 4 echantillons in == out" };
}

// --- Vtable statique + point d'entree ---
static const OdeniseModuleVTable s_vtable = {
    /* abi_version */ ns::kAbiVersion,
    /* info */        { /* id */ 1,
                        /* kind */ static_cast<int>(ns::ModuleKind::Suppression),
                        /* name */ "passthrough",
                        /* description */ "Module neutre : sortie = entree (validation de la chaine)",
                        /* needs_gpu */ 0 },
    /* create */      pt_create,
    /* destroy */     pt_destroy,
    /* set_param */   pt_set_param,
    /* process */     pt_process,
    /* self_test */   pt_self_test
};

extern "C" ODENISE_EXPORT const OdeniseModuleVTable* odenise_module_entry() {
    return &s_vtable;
}

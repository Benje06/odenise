// ============================================================================
//  backend_cpu.cpp -- Backend de calcul CPU (repli / fallback).
//
//  Module dynamique de la famille ComputeBackend. A cette etape : plomberie
//  uniquement (create/destroy/self-test), aucun calcul. Les primitives DSP
//  reelles seront ajoutees a l'etape 3. Derriere la frontiere C (vtable),
//  l'implementation est du C++ normal.
// ============================================================================
#include "ns_engine.h"    // OdeniseModuleVTable, OdeniseProcessCtx, etc.
#include <new>            // std::nothrow

// --- Instance C++ (opaque cote coeur, vue comme void*) ---
struct CpuBackendInstance {
    int sample_rate = 48000;
    int n_max       = 4096;
};

// --- Fonctions de la vtable (extern "C" noexcept) ---
static OdeniseModuleInstance cpu_create(int sample_rate, int n_max) {
    auto* inst = new (std::nothrow) CpuBackendInstance;
    if (!inst) return nullptr;
    inst->sample_rate = sample_rate;
    inst->n_max       = n_max;
    return static_cast<OdeniseModuleInstance>(inst);
}

static void cpu_destroy(OdeniseModuleInstance self) {
    delete static_cast<CpuBackendInstance*>(self);
}

static int cpu_set_param(OdeniseModuleInstance /*self*/, int /*param_id*/, float /*value*/) {
    return static_cast<int>(ns::Status::Ok);   // pas de parametre a cette etape
}

static int cpu_process(OdeniseModuleInstance /*self*/, OdeniseProcessCtx* /*ctx*/) {
    // Backend de calcul : ne route pas l'audio comme un module de suppression.
    // Les primitives de calcul seront branchees a l'etape 3 (STFT reel).
    return static_cast<int>(ns::Status::Unsupported);
}

static OdeniseTestResultC cpu_self_test() {
    // Test minimal : creer une instance, verifier l'allocation, detruire.
    auto* inst = static_cast<CpuBackendInstance*>(cpu_create(48000, 1024));
    if (!inst)
        return { 0, "echec allocation instance" };
    cpu_destroy(inst);
    return { 1, "backend CPU OK : instanciation/destruction" };
}

// --- Vtable statique + point d'entree ---
static const OdeniseModuleVTable s_vtable = {
    /* abi_version */ ns::kAbiVersion,
    /* info */        { /* id */ 0,
                        /* kind */ static_cast<int>(ns::ModuleKind::ComputeBackend),
                        /* name */ "cpu",
                        /* description */ "Backend de calcul CPU (repli, sans GPU)",
                        /* needs_gpu */ 0 },
    /* create */      cpu_create,
    /* destroy */     cpu_destroy,
    /* set_param */   cpu_set_param,
    /* process */     cpu_process,
    /* self_test */   cpu_self_test
};

extern "C" ODENISE_EXPORT const OdeniseModuleVTable* odenise_module_entry() {
    return &s_vtable;
}

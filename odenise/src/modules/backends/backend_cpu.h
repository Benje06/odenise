// ============================================================================
//  backend_cpu.h -- Backend de calcul CPU (repli / fallback).
//
//  Declarations de CpuBackendContext et CpuBackendImpl.
// ============================================================================
#pragma once

#include "engine.h"
#include "audio_chain.h"

#include <algorithm>    // std::min, std::max
#include <chrono>       // std::chrono::steady_clock
#include <cstring>      // std::memcpy
#include <new>          // std::nothrow
#include <random>       // std::mt19937, std::uniform_real_distribution
#include <thread>       // std::this_thread::yield
#include <cstddef>      // std::size_t, std::byte
#include <vector>       // std::vector
#include <atomic>

// ============================================================================
//  CpuBackendContext -- contexte de ressource CPU.
//  Fourni par CpuBackendImpl a chaque module lors de l'installation.
//  Possede le scratch buffer pre-alloue a ring_size_max * sizeof(float).
// ============================================================================
class CpuBackendContext final : public odenise::BackendContext {
public:
    explicit CpuBackendContext(size_t ring_size_max);

    // [CTRL] Retourne le debut du scratch buffer.
    void* scratch_buf(std::size_t bytes) noexcept override;

    // [RT/CTRL] Pas de stream CPU.
    void* compute_stream() noexcept override;

    // [CTRL] Type de backend : CPU.
    int   backend_type() const noexcept override;

    // [CTRL] Redimensionne le scratch buffer (appele par reconfigure).
    void resize(size_t ring_size);

private:
    std::vector<std::byte> scratch_;
};

// ============================================================================
//  CpuBackendImpl -- implementation complete de BackendBase pour le CPU.
//  Herite aussi de ModuleBase pour satisfaire la signature de l'entry point
//  (OdeniseModuleEntryFn retourne ModuleBase*). Le loader caste vers
//  BackendBase* via dynamic_cast apres avoir verifie le kind.
// ============================================================================
class CpuBackendImpl final : public odenise::BackendBase {
public:
    explicit CpuBackendImpl(odenise::EngineCaps e_caps);
    ~CpuBackendImpl() override;

    // -----------------------------------------------------------------------
    //  Thread RT -- Run() / Run2()
    // -----------------------------------------------------------------------
    bool Run()  override;
    bool Run2() override;

    // -----------------------------------------------------------------------
    //  info_c / caps_c / self_test_c -- metadonnees POD.
    // -----------------------------------------------------------------------
    const OdeniseModuleInfoC*   info_c()       const noexcept override;
    const OdeniseBackendCapsC*  caps_c()       const noexcept override;
    const OdeniseTestResultC*   self_test_c()  const noexcept override;

    // -----------------------------------------------------------------------
    //  reconfigure -- suspend threads, redimensionne les ressources, reprend.
    // -----------------------------------------------------------------------
    // called by engine to configure the backend
    odenise::Status reconfigure( const odenise::EngineCaps&    e_caps,
                                 const odenise::RuntimeConfig& cfg) override;
    // Module inherit function
    odenise::Status reconfigure( const odenise::BackendCaps& b_caps,
                                 const odenise::RuntimeConfig& cfg,
                                 odenise::ApplyResult& how) override;

    // -----------------------------------------------------------------------
    //  install_module / uninstall_module
    // -----------------------------------------------------------------------
    std::vector<odenise::ModuleInfo> get_chain() const noexcept;

    bool install_module(odenise::ModuleBase* mod,
                        odenise::ModuleKind  kind,
                        size_t position,
                        size_t loaded_id = 0) override;

    void uninstall_module(size_t position) noexcept override;

    // -----------------------------------------------------------------------
    //  process -- [RT] guard.
    // -----------------------------------------------------------------------
    odenise::Status process(const float* const* in,
                            float*              out,
                            size_t                 num_frames) noexcept override;

    // -----------------------------------------------------------------------
    //  setAudioIO -- set les entrees/sorties audio.
    // -----------------------------------------------------------------------
    void setAudioIO(odenise::TrackIO io) override;

    // -----------------------------------------------------------------------
    //  measure -- mesure de latence et de charge CPU hors RT.
    // -----------------------------------------------------------------------
    void measure(int num_blocks) override;

    // -----------------------------------------------------------------------
    //  Methodes ModuleBase requises par l'heritage (non utilisees).
    // -----------------------------------------------------------------------
    int             latency_samples()    const noexcept override;
    int             latency_samples_rt() const noexcept override;
    bool            install(odenise::BackendContext*)    override;
    void            uninstall(odenise::BackendContext*) noexcept override;
    void            set_param(odenise::ParamId, float)  noexcept override;
    void*           output_buf()   noexcept override;
    void            set_input(const void*) noexcept override;
    void            process(size_t)   noexcept override;

private:
    int                         sample_rate_;
    size_t                      ring_size_max_;
    CpuBackendContext           ctx_;
    odenise::EngineCaps         e_caps_;
    odenise::AudioChain         chain_;
    odenise::ModuleBase*        first_module_ = nullptr;
    odenise::ModuleBase*        last_module_  = nullptr;
    bool                        nodes_empty_  = true;
};

// ============================================================================
//  Point d'entree du module.
// ============================================================================
extern "C" ODENISE_EXPORT odenise::ModuleBase* odenise_module_entry(odenise::EngineCaps e_caps);

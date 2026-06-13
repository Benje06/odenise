// ============================================================================
//  tests/main.cc  --  Point d'entree des tests : valide la chaine de chargement
//  du programme (init NLS, LogManager, creation du moteur, scan des modules,
//  chargement + self-test du passthrough).
//  Enrobe tout dans un try/catch generalise (hors chemin temps reel).
//
//  Convention de log : on construit la chaine dans une variable locale (msg
//  pour LOG, msg_err pour LOG_ERR) AVANT l'appel, plutot que de l'imbriquer.
//  Le premier argument de error() est __func__ (portable GCC/MSVC, identique
//  en sortie : le nom de la fonction courante).
//
//  Limites connues (stubs phase 3) :
//    - run_process_test : verifie uniquement le guard Ok/Unsupported.
//      Le backend ne cable pas encore les buffers in/out dans Run() (TODO
//      explicite dans backend_cpu.cpp). La verification sample-by-sample
//      n'est pas applicable tant que le thread RT n'est pas branche.
//    - run_latency_test : cached_stats_ reste a zero sans appel a measure().
//      measure() n'est pas expose par l'interface Engine ; les valeurs sont
//      loggees a titre informatif (pas d'assertion de non-nullite).
//    - run_set_audio_io_test : verifie uniquement que setAudioIO() ne plante
//      pas et propage jusqu'au backend. La suspension interne du backend et
//      le cablage effectif des buffers RT sont valides en phase 3b.
// ============================================================================

#include "engine.h"
#include "AudioProcessor.h"

#include <memory>
#include <string>    // std::string (construction des messages de log)

namespace {

    constexpr int kTestFrames = 128;
    std::string msg;
    std::string msg_err;
/*
    int run_process_test(std::unique_ptr<odenise::Engine>& engine) {
        msg = _("=== test: process passthrough ===");
        LOG(msg);

        // Avant selection : aucun module => process() doit renvoyer Unsupported.
        // Note : on cree un nouvel engine sans module de suppression lie
        // (suppression_id=0 par defaut) pour tester l'etat initial proprement.
        {
            odenise::EngineCaps    probe_caps;
            odenise::RuntimeConfig probe_cfg;   // suppression_id = 0
            odenise::Status        probe_st;
            auto probe_engine = odenise::createEngine(probe_caps, probe_cfg, &probe_st);
            if (!probe_engine) {
                msg_err = error(__func__, _("createEngine for probe"), _("returned nullptr"));
                LOG_ERR(msg_err);
                return 1;
            }

            float dummy_in  = 0.0f;
            float dummy_out = 0.0f;
            const float* dummy_ptr = &dummy_in;
            odenise::TrackIO probe{ &dummy_ptr, &dummy_out, 1 };
            std::span<const odenise::TrackIO> probe_tracks(&probe, 1);
            if (probe_engine->process(probe_tracks, 1) != odenise::Status::Unsupported) {
                msg_err = error(__func__, _("process before module selection"),
                                _("expected Unsupported"));
                LOG_ERR(msg_err);
                return 1;
            }
            msg = _("  -> no module bound: process() returns Unsupported (OK)");
            LOG(msg);
        }

        // Demande de chargement du passthrough (id=1) via reconfigure.
        odenise::RuntimeConfig cfg;
        cfg.suppression_id = 1;
        odenise::ApplyResult how;
        if (engine->reconfigure(cfg, how) != odenise::Status::Ok) {
            msg_err = error(__func__, _("reconfigure to passthrough"), _("failed"));
            LOG_ERR(msg_err);
            return 1;
        }

        // Traitement d'un bloc : process() doit retourner Ok.
        // La verification sample-by-sample n'est pas applicable ici :
        // le thread RT (Run()) ne cable pas encore les buffers in/out (TODO
        // dans backend_cpu.cpp). On valide uniquement le code de retour.
        float in_buf[kTestFrames];
        float out_buf[kTestFrames];
        for (int i = 0; i < kTestFrames; ++i) {
            in_buf[i]  = static_cast<float>(i) / static_cast<float>(kTestFrames);
            out_buf[i] = -1.0f;
        }

        const float* in_ptr = in_buf;
        odenise::TrackIO track;
        track.in          = &in_ptr;
        track.out         = out_buf;
        track.in_channels = 1;

        std::span<const odenise::TrackIO> tracks(&track, 1);
        odenise::Status rc = engine->process(tracks, kTestFrames);
        if (rc != odenise::Status::Ok) {
            msg_err = error(__func__, _("process returned error"),
                            std::to_string(static_cast<int>(rc)));
            LOG_ERR(msg_err);
            return 1;
        }
        msg = _("  -> process() with passthrough bound returns Ok (OK)");
        LOG(msg);

        msg = _("=== process passthrough test passed (");
        msg += std::to_string(kTestFrames);
        msg += _(" frames, guard only) ===");
        LOG(msg);
        return 0;
    }
*/
/*
    int run_backend_test(odenise::audio::AudioProcessor& processor) {
        msg = _("=== test: Compute backend module ===");
        LOG(msg);

        // Au moins un ComputeBackend doit etre charge (le repli CPU).
        const auto backends = processor.engine()->modules(odenise::ModuleKind::ComputeBackend);
        msg = _("compute backends: ");
        msg += std::to_string(backends.size());
        LOG(msg);
        if (backends.empty()) {
            msg_err = error(__func__, _("compute backends"), _("none loaded (expected CPU fallback)"));
            LOG_ERR(msg_err);
            return 1;
        }
        // Self-test de chaque backend trouve.
        for (const auto& b : backends) {
            const std::string label = std::string("[") + odenise::kindName(b.kind) + "] '" + b.name + "'";
            msg = _("  -> self-test ");
            msg += label;
            msg += "...";
            LOG(msg);
            auto result = processor.engine()->selfTest(b.id);
            if (result.passed) {
                msg = _("     PASS: ");
                msg += result.detail;
                LOG(msg);
            } else {
                msg_err = error(__func__, _("self-test FAILED for ") + label, result.detail);
                LOG_ERR(msg_err);
                return 1;
            }
        }
        msg = _("=== compute backend test passed ===");
        LOG(msg);
        return 0;
    }

    int run_suppression_test(odenise::audio::AudioProcessor& processor) {
        msg = _("=== test: Suppression module ===");
        LOG(msg);

        const auto suppressions = processor.engine()->modules(odenise::ModuleKind::Suppression);
        msg = _("suppression modules: ");
        msg += std::to_string(suppressions.size());
        LOG(msg);
        if (suppressions.empty()) {
            msg_err = error(__func__, _("suppression modules"), _("none loaded (expected passthrough)"));
            LOG_ERR(msg_err);
            return 1;
        }
        for (const auto& b : suppressions) {
            const std::string label = std::string("[") + odenise::kindName(b.kind) + "] '" + b.name + "'";
            msg = _("  -> self-test ");
            msg += label;
            msg += "...";
            LOG(msg);
            auto result = processor.engine()->selfTest(b.id);
            if (result.passed) {
                msg = _("     PASS: ");
                msg += result.detail;
                LOG(msg);
            } else {
                msg_err = error(__func__, _("self-test FAILED for ") + label, result.detail);
                LOG_ERR(msg_err);
                return 1;
            }
        }
        msg = _("=== suppression module test passed ===");
        LOG(msg);
        return 0;
    }

    int run_latency_test(odenise::audio::AudioProcessor& processor) {
        msg = _("=== test: latency info ===");
        LOG(msg);

        // Latence declaree : sommee au cablage depuis les modules installes.
        // Vaut 0 si aucun module C++ (ModuleBase) n'est encore installe.
        const odenise::LatencyInfo li = processor.engine()->latencyInfo();
        msg = _("  -> declared latency: ");
        msg += std::to_string(li.declared_samples);
        msg += _(" samples (");
        msg += std::to_string(li.declared_ms);
        msg += _(" ms)");
        LOG(msg);

        // Stats de traitement : loggues a titre informatif.
        // cached_stats_ reste a zero tant que BackendBase::measure() n'a pas
        // ete appele. measure() n'est pas expose par l'interface Engine ;
        // il sera declenche depuis l'UI/timer dans les phases suivantes.
        const odenise::ProcessingStats ps = processor.engine()->processingStats();
        msg = _("  -> processing stats: min=");
        msg += std::to_string(ps.min_ms);
        msg += _(" max=");
        msg += std::to_string(ps.max_ms);
        msg += _(" mean=");
        msg += std::to_string(ps.mean_ms);
        msg += _(" ms, load=");
        msg += std::to_string(ps.load_pct);
        msg += _("%");
        LOG(msg);

        // backendCaps : valide meme sans backend C++ actif (retourne struct vide).
        const odenise::BackendCaps bc = processor.engine()->backendCaps();
        msg = _("  -> backend caps: name='");
        msg += bc.backend_name.empty() ? _("(none)") : bc.backend_name;
        msg += _("' gpu=");
        msg += bc.is_gpu ? _("yes") : _("no");
        LOG(msg);

        msg = _("=== latency info test passed ===");
        LOG(msg);
        return 0;
    }

    int run_set_audio_io_test(odenise::audio::AudioProcessor& processor) {
        msg = _("=== test: setAudioIO ===");
        LOG(msg);

        // Buffers minimaux : 1 canal entree, 1 canal sortie, kTestFrames samples.
        // L'objectif est de valider que l'appel traverse AudioProcessor ->
        // Engine -> BackendBase sans crash. Le cablage effectif des buffers RT
        // est valide en phase 3b.
        float in_buf[kTestFrames]  = {};
        float out_buf[kTestFrames] = {};
        const float* in_ptr = in_buf;
        odenise::TrackIO io{ &in_ptr, out_buf, 1 };

        processor.setAudioIO(io);

        msg = _("  -> setAudioIO() propagated to backend (OK)");
        LOG(msg);
        msg = _("=== setAudioIO test passed ===");
        LOG(msg);
        return 0;
    }
*/
    int run_build_processor_test(odenise::audio::AudioProcessor& processor, odenise::RuntimeConfig& cfg) {
        msg = _("=== test: Build AudioProcessor ===");
        LOG(msg);

        // AudioProcessor construit avec caps et cfg par defaut.
        // Verifie que l'engine interne a ete cree correctement.
        if (!processor.engine()) {
            msg_err = error(__func__, _("AudioProcessor::engine()"), _("returned nullptr"));
            LOG_ERR(msg_err);
            return 1;
        }
        msg = _("engine created via AudioProcessor, latency = ");
        msg += std::to_string(processor.engine()->latency_samples());
        msg += _(" samples");
        LOG(msg);
        msg = _("=== AudioProcessor test passed ===");
        LOG(msg);
        return 0;
    }

    int run_load_chain_test(odenise::audio::AudioProcessor& processor, odenise::RuntimeConfig& cfg) {
        msg = _("=== test: load chain ===");
        LOG(msg);
        int ok;
        // Modules de suppression (dont passthrough)
        const auto backends = processor.get_available_backends();
        const auto modules = processor.get_available_modules();
        odenise::ModuleInfo backend_;
        if(backends.size() != 0){
            for( auto mod: backends){
                if(mod.kind == odenise::ModuleKind::ComputeBackend){
                    ok = processor.bindBackend(mod.id,cfg);
                    backend_ = mod;
                    msg = _("Backend compute loaded.");
                    LOG(msg);
                    break;
                }
            }
        }else{
            msg = _("=== load chain Failed ===");
            msg += _("=== No Backend ===");
            LOG(msg);
            return 1;
        }
        if(!ok){
        msg = _("=== load chain Failed ===");
            msg += _("=== Fail to load bakcend ===");
            LOG(msg);
            return 1; 
        }
        if(modules.size() != 0){
            for(auto mod : modules){
                if(odenise::ModuleKind::Suppression == mod.kind){
                    ok = processor.insertModule(mod.id, 0, cfg);
                    msg = _("Module filter loaded.");
                    LOG(msg);
                    break;
                }
            }
        }else{
            msg = _("=== load chain Failed ===");
            msg += _("=== No module to load ===");
            LOG(msg);
            return 1;
        }
        if(!ok){
         msg = _("=== load chain Failed ===");
            msg += _("=== Fail to load module ===");
            LOG(msg);
            return 1; 
        }
        msg = _("=== load chain test passed ===");
        LOG(msg);
        return 1;
    }
} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    init_nls();

    // Handler de log : niveau 2 (fichier + console) vers odenise_test.log.
    LogManager::instance().add_handler(
        std::make_shared<Logger>("odenise_test.log", 2));

    // AudioProcessor possede l'engine. Tous les tests passent par lui.
    odenise::EngineCaps    caps;
    odenise::RuntimeConfig cfg;   // suppression_id = 0
    odenise::Status        st;
    //cfg.suppression_id = 1;
    odenise::ApplyResult how;

    odenise::audio::AudioProcessor processor;


    try {
        int r = run_build_processor_test(processor,cfg);
        if (r != 0) return r;

        r = run_load_chain_test(processor,cfg);
        if (r != 0) return r;
/*
        r = run_backend_test(processor);
        if (r != 0) return r;

        r = run_suppression_test(processor);
        if (r != 0) return r;

        r = run_set_audio_io_test(processor);
        if (r != 0) return r;

        /*r = run_process_test(processor);
        if (r != 0) return r;*/
        /*
        r = run_latency_test(processor);
        return r;
    */
    } catch (const std::exception& e) {
        std::string msg_err = error(__func__, _("unhandled exception"), e.what());
        LOG_ERR(msg_err);
        return 2;
    } catch (...) {
        std::string msg_err = error(__func__, _("unhandled exception"), _("unknown"));
        LOG_ERR(msg_err);
        return 2;
    }
}

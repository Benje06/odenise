// ============================================================================
//  tests/main.cc  --  Point d'entree des tests : valide la chaine de chargement
//  du programme (init NLS, LogManager, creation du moteur, scan des modules,
//  chargement + self-test du passthrough).
//  Enrobe tout dans un try/catch generalise (hors chemin temps reel).
// ============================================================================
#include "ns_engine.h"
#include "tools/logger.h"

#include <memory>
#include <cmath>     // std::fabs

namespace {
constexpr int kTestFrames = 128;

int run_load_chain_test() {
    LOG(_("=== test: load chain ==="));

    ns::EngineCaps    caps;
    ns::RuntimeConfig cfg;
    ns::Status        st;

    auto engine = ns::createEngine(caps, cfg, &st);
    if (!engine) {
        LOG_ERR(error("test", _("createEngine"), _("returned nullptr")));
        return 1;
    }
    LOG(_("engine created, latency = ") + std::to_string(engine->latencySamples()) + _(" samples"));

    // Backends dynamiques (ComputeBackend)
    const auto backends = ns::availableBackends();
    LOG(_("dynamic backends found: ") + std::to_string(backends.size()));

    // Modules de suppression (dont passthrough)
    const auto sup = engine->modules(ns::ModuleKind::Suppression);
    LOG(_("suppression modules: ") + std::to_string(sup.size()));

    // Self-test de chaque module de suppression trouve
    for (const auto& m : sup) {
        LOG(_("  -> self-test '") + m.name + "'...");
        auto result = engine->selfTest(ns::ModuleKind::Suppression, m.id);
        if (result.passed) {
            LOG(_("     PASS: ") + result.detail);
        } else {
            LOG_ERR(error("test", _("self-test FAILED for '") + m.name + "'", result.detail));
            return 1;
        }
    }

    LOG(_("=== load chain test passed ==="));
    return 0;
}

int run_process_test() {
    LOG(_("=== test: process passthrough ==="));

    // Demarrage en config par defaut : suppression_id = 0 => aucun module lie.
    ns::EngineCaps    caps;
    ns::RuntimeConfig cfg;
    ns::Status        st;

    auto engine = ns::createEngine(caps, cfg, &st);
    if (!engine) {
        LOG_ERR(error("test", _("createEngine (process)"), _("returned nullptr")));
        return 1;
    }

    // Avant selection : aucun module => process() doit renvoyer Unsupported.
    {
        float dummy_in  = 0.0f;
        float dummy_out = 0.0f;
        const float* dummy_ptr = &dummy_in;
        ns::TrackIO probe{ &dummy_ptr, &dummy_out, 1 };
        std::span<const ns::TrackIO> probe_tracks(&probe, 1);
        if (engine->process(probe_tracks, 1) != ns::Status::Unsupported) {
            LOG_ERR(error("test", _("process before module selection"),
                          _("expected Unsupported")));
            return 1;
        }
        LOG(_("  -> no module bound: process() returns Unsupported (OK)"));
    }

    // Demande de chargement du passthrough (id=1) via reconfigure.
    cfg.suppression_id = 1;
    ns::ApplyResult how;
    if (engine->reconfigure(cfg, how) != ns::Status::Ok) {
        LOG_ERR(error("test", _("reconfigure to passthrough"), _("failed")));
        return 1;
    }

    // Rampe mono : 0, 1/N, 2/N, ... (N-1)/N
    float in_buf[kTestFrames];
    float out_buf[kTestFrames];
    for (int i = 0; i < kTestFrames; ++i) {
        in_buf[i]  = static_cast<float>(i) / static_cast<float>(kTestFrames);
        out_buf[i] = -1.0f;   // valeur sentinelle
    }

    const float* in_ptr = in_buf;
    ns::TrackIO track;
    track.in          = &in_ptr;
    track.out         = out_buf;
    track.in_channels = 1;

    std::span<const ns::TrackIO> tracks(&track, 1);
    ns::Status rc = engine->process(tracks, kTestFrames);
    if (rc != ns::Status::Ok) {
        LOG_ERR(error("test", _("process returned error"),
                       std::to_string(static_cast<int>(rc))));
        return 1;
    }

    // Verification sample par sample.
    for (int i = 0; i < kTestFrames; ++i) {
        if (std::fabs(in_buf[i] - out_buf[i]) > 1e-9f) {
            LOG_ERR(error("test", _("process mismatch at sample ") + std::to_string(i),
                          _("in=") + std::to_string(in_buf[i])
                          + _(" out=") + std::to_string(out_buf[i])));
            return 1;
        }
    }

    LOG(_("=== process passthrough test passed (") + std::to_string(kTestFrames) + _(" frames) ==="));
    return 0;
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    init_nls();

    LogManager::instance().add_handler(
        std::make_shared<Logger>("odenise_test.log", 2));

    try {
        int r = run_load_chain_test();
        if (r != 0) return r;

        r = run_process_test();
        return r;
    } catch (const std::exception& e) {
        LOG_ERR(error("main", _("unhandled exception"), e.what()));
        return 2;
    } catch (...) {
        LOG_ERR(error("main", _("unhandled exception"), _("unknown")));
        return 2;
    }
}

// ============================================================================
//  tests/main.cc  --  Point d'entree des tests : valide la chaine de chargement
//  du programme (init NLS, LogManager, creation du moteur, scan des modules).
//  Enrobe tout dans un try/catch generalise (hors chemin temps reel).
// ============================================================================
#include "ns_engine.h"
#include "tools/logger.h"

#include <memory>

namespace {

// Etape 1 : la chaine de chargement repond-elle ?
int run_load_chain_test() {
    LOG(_("=== test: load chain ==="));

    ns::EngineCaps   caps;     // valeurs par defaut
    ns::RuntimeConfig cfg;
    ns::Status       st;

    auto engine = ns::createEngine(caps, cfg, &st);
    if (!engine) {
        LOG_ERR(error("test", _("createEngine"), _("returned nullptr")));
        return 1;
    }
    LOG(_("engine created, latency = ") + std::to_string(engine->latencySamples()) + _(" samples"));

    const auto backends = ns::availableBackends();
    LOG(_("dynamic backends found: ") + std::to_string(backends.size()));

    const auto sup = engine->modules(ns::ModuleKind::Suppression);
    LOG(_("suppression modules: ") + std::to_string(sup.size()));

    LOG(_("=== load chain test passed ==="));
    return 0;
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    init_nls();

    // Handler de log : niveau 2 (fichier + console) vers odenise_test.log.
    LogManager::instance().add_handler(
        std::make_shared<Logger>("odenise_test.log", 2));

    try {
        return run_load_chain_test();
    } catch (const std::exception& e) {
        LOG_ERR(error("main", _("unhandled exception"), e.what()));
        return 2;
    } catch (...) {
        LOG_ERR(error("main", _("unhandled exception"), _("unknown")));
        return 2;
    }
}

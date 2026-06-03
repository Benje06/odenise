// test_core.cpp -- test fumee : la lib s'instancie et repond.
#include "ns_engine.h"
#include <cstdio>

int main() {
    ns::EngineCaps caps;          // valeurs par defaut
    ns::RuntimeConfig cfg;
    ns::Status st;

    auto engine = ns::createEngine(caps, cfg, &st);
    if (!engine) {
        std::printf("ECHEC: createEngine a renvoye nullptr\n");
        return 1;
    }
    std::printf("OK: moteur cree, latence = %d samples\n", engine->latencySamples());

    auto backends = ns::availableBackends();
    std::printf("OK: %zu backend(s) dynamique(s) trouve(s)\n", backends.size());

    auto sup = engine->modules(ns::ModuleKind::Suppression);
    std::printf("OK: %zu module(s) de suppression\n", sup.size());

    std::printf("=== etape 1 : smoke test passe ===\n");
    return 0;
}

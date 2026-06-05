// ============================================================================
//  module_registry.h  --  Registre interne des modules dynamiques (PRIVE).
//
//  Header INTERNE au coeur : non installe, non expose dans l'API publique.
//  Possede les poignees de bibliotheque chargees et les tables de fonctions.
//  Le moteur (engine.cpp) interroge ce registre pour instancier et router les
//  modules, et pour peupler Engine::modules() / Engine::selfTest().
// ============================================================================
#pragma once

#include "ns_engine.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ns::detail {

// Un module charge : poignee de lib (gardee ouverte), fonction d'entree
// pour instancier les objets avec les bonnes caps, et metadonnees converties
// en types C++ (copiees, donc independantes du cycle de vie des const char*).
// Les objets C++ (base, backend) sont crees par l'engine via make() avec les
// vraies caps (sample_rate, n_max) -- pas par le loader qui ne les connait pas.
struct LoadedModule {
    void*                  handle    = nullptr;  // LibHandle opaque (HMODULE/void*)
    OdeniseModuleEntryFn   entry_fn  = nullptr;  // fabrique : entry(sr, n_max)
    ModuleInfo             info;                 // converti depuis OdeniseModuleInfoC
    std::string            path;                 // chemin du .so/.dll

    // Instancie un objet avec les caps reelles. Retourne nullptr si echec.
    // Pour ComputeBackend, l'objet implemente aussi BackendBase.
    ns::ModuleBase* make(int sample_rate, int n_max) const {
        return entry_fn ? entry_fn(sample_rate, n_max) : nullptr;
    }
};

// ---------------------------------------------------------------------------
//  Registre : scanne un dossier, charge les modules valides, les enumere.
//  Les bibliotheques restent ouvertes tant que le registre vit. C'est au
//  moteur de garantir qu'aucune instance ne survit a la destruction du
//  registre (regle d'ownership de la frontiere dynamique).
// ---------------------------------------------------------------------------
class ModuleRegistry {
public:
    ModuleRegistry() = default;
    ~ModuleRegistry();                              // ferme toutes les libs
    ModuleRegistry(const ModuleRegistry&)            = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    // Charge tous les modules valides d'un dossier. Renvoie le nombre charge.
    // Les fichiers invalides (mauvaise ABI, symbole absent...) sont ignores
    // avec un diagnostic, sans interrompre le scan.
    int scanDirectory(const std::filesystem::path& dir);

    // Enumere les modules d'une famille (pour l'UI).
    std::vector<ModuleInfo> list(ModuleKind kind) const;

    // Retourne le ModuleBase* d'un module (kind + id), instancie avec les
    // caps fournies. L'appelant prend possession de l'objet (delete requis).
    // Retourne nullptr si absent ou echec d'instanciation.
    ns::ModuleBase*  make(ModuleKind kind, int id,
                          int sample_rate, int n_max) const;

    // Retourne le BackendBase* d'un backend (ComputeBackend + id), instancie
    // avec les caps fournies. L'appelant prend possession de l'objet.
    // Retourne nullptr si absent ou si ce n'est pas un ComputeBackend.
    ns::BackendBase* make_backend(int id,
                                  int sample_rate, int n_max) const;

    // Execute le self-test embarque d'un module.
    TestResult selfTest(ModuleKind kind, int id) const;

private:
    bool tryLoad(const std::filesystem::path& file);   // charge un fichier
    std::vector<LoadedModule> modules_;
};

} // namespace ns::detail

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

// Un module charge : poignee de lib (gardee ouverte), table de fonctions,
// et metadonnees converties en types C++ (copiees, donc independantes du
// cycle de vie des const char* du module).
struct LoadedModule {
    void*                      handle = nullptr;   // LibHandle opaque (HMODULE/void*)
    const OdeniseModuleVTable* vtable = nullptr;   // possede par le module
    ModuleInfo                 info;               // converti depuis OdeniseModuleInfoC
    std::string                path;               // chemin du .so/.dll
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

    // Table de fonctions d'un module (kind + id), ou nullptr si absent.
    const OdeniseModuleVTable* find(ModuleKind kind, int id) const;

    // Execute le self-test embarque d'un module.
    TestResult selfTest(ModuleKind kind, int id) const;

private:
    bool tryLoad(const std::filesystem::path& file);   // charge un fichier
    std::vector<LoadedModule> modules_;
};

} // namespace ns::detail

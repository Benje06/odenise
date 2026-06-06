// ============================================================================
//  module_registry.h  --  Registre interne des modules dynamiques (PRIVE).
//
//  Header INTERNE au coeur : non installe, non expose dans l'API publique.
//
//  Deux etats pour un module :
//    Disponible : metadonnees connues (scan), handle ferme, pas d'objet.
//    Charge     : handle ouvert, objet instancie, pret a l'emploi.
//
//  Ownership : le registry cree et detruit les objets des modules charges
//  (principe : celui qui cree detruit). L'engine et le backend obtiennent
//  des pointeurs via find_module()/find_backend() sans jamais en prendre
//  la propriete (pas de delete).
// ============================================================================
#pragma once

#include "engine.h"

#include <system_error>
#include <filesystem>
#include <string>
#include <vector>

namespace odenise::detail {

// ---------------------------------------------------------------------------
//  AvailableModule -- metadonnees d'un module decouvert au scan.
//  Handle ferme, pas d'objet instancie.
//  Sert a peupler l'UI (list_available) et a guider load_module().
// ---------------------------------------------------------------------------
struct AvailableModule {
    ModuleInfo  info;   // id, kind, name, description, needs_gpu, backend_type_id
    std::string path;   // chemin absolu du .so/.dll
};

// ---------------------------------------------------------------------------
//  LoadedModule -- module charge : handle ouvert + objet instancie.
//  Possede par le registry (cree et detruit par lui).
//  Pour ComputeBackend, module et backend pointent sur le meme objet C++
//  vu sous deux interfaces distinctes.
// ---------------------------------------------------------------------------
struct LoadedModule {
    void*          handle  = nullptr;  // LibHandle opaque (HMODULE/void*)
    ModuleBase*    module  = nullptr;  // objet instancie, possede par le registry
    BackendBase*   backend = nullptr;  // non nul si kind == ComputeBackend
    ModuleInfo     info;               // copie des metadonnees
    std::string    path;               // chemin du .so/.dll
};

// ---------------------------------------------------------------------------
//  ModuleRegistry -- scan, chargement, acces aux modules.
//
//  Cycle de vie :
//    1. scan_modules(dir)        -- decouvre les modules disponibles
//    2. load_module(kind, id)    -- charge un module selectionne
//    3. find_module/find_backend -- acces aux objets charges (non-owning)
//    4. unload_module(kind, id)  -- decharge un module
//    5. ~ModuleRegistry()        -- decharge tout
// ---------------------------------------------------------------------------
class ModuleRegistry {
public:
    ModuleRegistry() = default;
    ~ModuleRegistry();
    ModuleRegistry(const ModuleRegistry&)            = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    // -----------------------------------------------------------------------
    //  Decouverte -- peuple available_, ne charge rien.
    //  Renvoie le nombre de modules decouverts.
    // -----------------------------------------------------------------------
    int scan_modules(const std::filesystem::path& dir);

    // -----------------------------------------------------------------------
    //  Chargement / dechargement
    // -----------------------------------------------------------------------

    // Charge un module disponible (kind + id) : ouvre la lib, instancie
    // l'objet avec (0,0), verifie le cast BackendBase si necessaire.
    // Retourne true si charge avec succes (ou deja charge).
    bool load_module(ModuleKind kind, int id);

    // Decharge un module : detruit l'objet et ferme le handle.
    // Sans effet si le module n'est pas charge.
    void unload_module(ModuleKind kind, int id) noexcept;

    // -----------------------------------------------------------------------
    //  Acces aux modules charges (pointeurs non-owning).
    //  Retournent nullptr si le module n'est pas charge.
    // -----------------------------------------------------------------------
    ModuleBase*  find_module(ModuleKind kind, int id) const;
    BackendBase* find_backend(int id) const;

    // -----------------------------------------------------------------------
    //  Listes
    // -----------------------------------------------------------------------

    // Modules decouverts (pour l'UI : propose ce qui est disponible).
    std::vector<ModuleInfo> list_available(ModuleKind kind) const;

    // Modules actuellement charges (pour l'engine : ce qui est actif).
    std::vector<ModuleInfo> list_loaded(ModuleKind kind) const;

    // Retourne l'id du premier module disponible d'un kind, sans allocation.
    // Retourne -1 si aucun module de ce kind n'est disponible.
    int first_available_id(ModuleKind kind) const noexcept;

    // -----------------------------------------------------------------------
    //  Self-test d'un module disponible.
    //  Charge temporairement si pas deja charge, execute le test, decharge.
    // -----------------------------------------------------------------------
    TestResult self_test(ModuleKind kind, int id);

private:
    // Probe d'un fichier : lit les metadonnees via un objet temporaire (0,0).
    // Retourne false si le fichier n'est pas un module odenise valide.
    bool probe_file(const std::filesystem::path& file);

    // Trouve un AvailableModule par kind+id. Retourne nullptr si absent.
    const AvailableModule* find_available(ModuleKind kind, int id) const;

    // Trouve un LoadedModule par kind+id. Retourne nullptr si absent.
    LoadedModule* find_loaded(ModuleKind kind, int id);
    const LoadedModule* find_loaded(ModuleKind kind, int id) const;

    std::vector<AvailableModule> available_;  // modules decouverts, non charges
    std::vector<LoadedModule>    loaded_;     // modules charges, possedes par le registry
};

} // namespace odenise::detail

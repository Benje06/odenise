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
    size_t         id;                 // position dans le vector loaded_
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

    // Charge un module disponible (available_id) : ouvre la lib, instancie
    // l'objet avec (0,0), verifie le cast BackendBase si necessaire.
    // Retourne true si charge avec succes (ou deja charge).
    bool load_module(size_t available_id);

    // Decharge un module : detruit l'objet et ferme le handle.
    // Sans effet si le module n'est pas charge.
    void unload_module(size_t loaded_id) noexcept;

    // -----------------------------------------------------------------------
    //  Acces aux modules charges (pointeurs non-owning).
    //  Retournent nullptr si le module n'est pas charge.
    // -----------------------------------------------------------------------
    ModuleBase*  find_module(size_t loaded_id) const;
    BackendBase* find_backend() const;

    // -----------------------------------------------------------------------
    //  Listes
    // -----------------------------------------------------------------------

    // Modules decouverts pour un type ou tous (pour l'UI : propose ce qui est disponible).
    std::vector<ModuleInfo> list_available(ModuleKind kind) const;
    std::vector<ModuleInfo> list_available() const;

    // Modules actuellement charges pour un type ou tous (pour l'engine : ce qui est actif).
    std::vector<LoadedModule> list_loaded(ModuleKind kind) const;
    std::vector<LoadedModule> list_loaded() const;

    // Retourne l'id du premier module disponible d'un kind, sans allocation.
    // Retourne -1 si aucun module de ce kind n'est disponible.
    size_t first_available_id(ModuleKind kind) const noexcept;

    // -----------------------------------------------------------------------
    //  Self-test d'un module disponible.
    //  Charge temporairement si pas deja charge, execute le test, decharge.
    // -----------------------------------------------------------------------
    TestResult self_test(size_t loaded_id);

private:
    // Probe d'un fichier : lit les metadonnees via un objet temporaire (0,0).
    // Retourne false si le fichier n'est pas un module odenise valide.
    bool probe_file(const std::filesystem::path& file);

    // Trouve un AvailableModule par kind+id. Retourne nullptr si absent.
    const AvailableModule* find_available(ModuleKind kind, size_t available_id) const;
    const AvailableModule* find_available(size_t available_id) const;

    // Trouve un LoadedModule par loaded_id. Retourne nullptr si absent.
    const LoadedModule* find_loaded(size_t loaded_id) const;
    const LoadedModule* find_loaded(ModuleKind kind) const;

    std::vector<AvailableModule> available_;  // modules decouverts, non charges
    std::vector<LoadedModule>    loaded_;     // modules charges, possedes par le registry
};

} // namespace odenise::detail

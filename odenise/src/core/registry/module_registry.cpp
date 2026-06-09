// ============================================================================
//  module_registry.cpp  --  Chargement dynamique portable des modules + registre.
//
//  Implemente odenise::ModuleRegistry.
//
//  Deux phases distinctes :
//    1. scan_modules() : decouverte via probe temporaire (open/probe/close).
//       Lit info_c() sur un objet (0,0) immediatement detruit, stocke les
//       metadonnees dans available_. Aucun objet definitif cree.
//    2. load_module() : chargement a la demande. Rouvre la lib, instancie
//       l'objet definitif avec (0,0), stocke dans loaded_. L'engine appelle
//       ensuite backend->reconfigure() pour adapter aux caps reelles.
//
//  Ownership : le registry cree et detruit les objets (principe RAII).
//  L'engine obtient des pointeurs non-owning via find_module/find_backend.
//
//  Diagnostics via le LogManager (LOG / LOG_ERR), format error(from,what,why).
// ============================================================================
#include "module_registry.h"

// ---------------------------------------------------------------------------
//  Shim plateforme
// ---------------------------------------------------------------------------
#if defined(_WIN32)
  #include <windows.h>
  namespace odenise {
    void* dl_open(const char* path)          { return (void*)LoadLibraryA(path); }
    void* dl_sym (void* h, const char* name) { return (void*)GetProcAddress((HMODULE)h, name); }
    void  dl_close(void* h)                  { if (h) FreeLibrary((HMODULE)h); }
    std::string dl_error()                   { return "LoadLibrary/GetProcAddress error"; }
    constexpr const char* kModuleExt = ".dll";
#else
  #include <dlfcn.h>
  namespace odenise {
    void* dl_open(const char* path)          { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
    void* dl_sym (void* h, const char* name) { return dlsym(h, name); }
    void  dl_close(void* h)                  { if (h) dlclose(h); }
    std::string dl_error()                   { const char* e = dlerror(); return e ? e : "unknown error"; }
    #if defined(__APPLE__)
      constexpr const char* kModuleExt = ".dylib";
    #else
      constexpr const char* kModuleExt = ".so";
    #endif
#endif


bool kindFromInt(int k, ModuleKind& out) {
    switch (k) {
        case static_cast<int>(ModuleKind::ComputeBackend): out = ModuleKind::ComputeBackend; return true;
        case static_cast<int>(ModuleKind::Suppression):    out = ModuleKind::Suppression;    return true;
        case static_cast<int>(ModuleKind::Window):         out = ModuleKind::Window;         return true;
        case static_cast<int>(ModuleKind::DualMic):        out = ModuleKind::DualMic;        return true;
        case static_cast<int>(ModuleKind::Inference):      out = ModuleKind::Inference;      return true;
        default: return false;
    }
}

ModuleInfo toModuleInfo(const OdeniseModuleInfoC& c, ModuleKind kind) {
    ModuleInfo m;
    m.id              = c.id;
    m.kind            = kind;
    m.name            = c.name        ? c.name        : "";
    m.description     = c.description ? c.description : "";
    m.needs_gpu       = (c.needs_gpu != 0);
    m.backend_type_id = c.backend_type_id;
    return m;
}

// ---------------------------------------------------------------------------
//  Destructeur : decharge tous les modules charges dans l'ordre inverse.
// ---------------------------------------------------------------------------
ModuleRegistry::~ModuleRegistry() {
    for (auto it = loaded_.rbegin(); it != loaded_.rend(); ++it) {
        // Pour ComputeBackend, backend et module pointent sur le meme objet.
        // Un seul delete suffit (via module, qui a le destructeur virtuel).
        delete it->module;
        it->module  = nullptr;
        it->backend = nullptr;
        dl_close(it->handle);
        it->handle = nullptr;
    }
    loaded_.clear();
}

// ---------------------------------------------------------------------------
//  probe_file -- phase 1 : lit les metadonnees d'un fichier.
//  Ouvre la lib, instancie un objet temporaire (0,0), lit info_c(),
//  detruit l'objet, ferme la lib. Stocke dans available_ si valide.
// ---------------------------------------------------------------------------
bool ModuleRegistry::probe_file(const std::filesystem::path& file) {
    const std::string path   = file.string();
    const std::string fname  = file.filename().string();
    const std::string subdir = file.parent_path().filename().string();
    std::string msg_err;

    void* handle = dl_open(path.c_str());
    if (!handle) {
        msg_err = error(__func__,
            _("Loader cannot open '") + subdir + _("' module '") + fname + "'",
            dl_error());
        LOG_ERR(msg_err);
        return false;
    }

    auto entry = reinterpret_cast<OdeniseModuleEntryFn>(
        dl_sym(handle, ODENISE_MODULE_ENTRY_SYMBOL));
    if (!entry) {
        msg_err = error(__func__,
            _("Loader cannot resolve entry symbol of '") + subdir + _("' module '") + fname + "'",
            std::string(_("missing entry symbol ")) + ODENISE_MODULE_ENTRY_SYMBOL);
        LOG_ERR(msg_err);
        dl_close(handle);
        return false;
    }

    odenise::EngineCaps e_caps;
    e_caps.ring_size_max=0;
    // Objet temporaire pour lire info_c() uniquement.
    ModuleBase* probe = entry(e_caps);
    if (!probe) {
        msg_err = error(__func__,
            _("Loader cannot probe '") + subdir + _("' module '") + fname + "'",
            _("odenise_module_entry(0,0) returned null"));
        LOG_ERR(msg_err);
        dl_close(handle);
        return false;
    }

    const OdeniseModuleInfoC* info = probe->info_c();
    if (!info) {
        msg_err = error(__func__,
            _("Loader cannot read info of '") + subdir + _("' module '") + fname + "'",
            _("info_c() returned null"));
        LOG_ERR(msg_err);
        delete probe;
        dl_close(handle);
        return false;
    }

    if (info->abi_version != kAbiVersion) {
        std::string why = std::string(_("incompatible ABI ")) + std::to_string(info->abi_version)
            + _(" (expected ") + std::to_string(kAbiVersion) + ")";
        msg_err = error(__func__,
            _("Loader cannot accept ABI of '") + subdir + _("' module '") + fname + "'",
            why);
        LOG_ERR(msg_err);
        delete probe;
        dl_close(handle);
        return false;
    }

    ModuleKind kind;
    if (!kindFromInt(info->kind, kind)) {
        msg_err = error(__func__,
            _("Loader cannot identify kind of '") + subdir + _("' module '") + fname + "'",
            _("unknown module kind ") + std::to_string(info->kind));
        LOG_ERR(msg_err);
        delete probe;
        dl_close(handle);
        return false;
    }

    // Pour ComputeBackend : valide que l'objet implemente bien BackendBase.
    if (kind == ModuleKind::ComputeBackend && !dynamic_cast<BackendBase*>(probe)) {
        msg_err = error(__func__,
            _("Loader cannot cast backend of '") + subdir + _("' module '") + fname + "'",
            _("ComputeBackend does not implement BackendBase"));
        LOG_ERR(msg_err);
        delete probe;
        dl_close(handle);
        return false;
    }

    // Metadonnees lues -- destruction du probe et fermeture de la lib.
    AvailableModule am;
    am.info = toModuleInfo(*info, kind);
    am.path = path;
    delete probe;
    dl_close(handle);

    available_.push_back(std::move(am));

    std::string msg = _("loader: available [");
    msg += kindName(kind);
    msg += _("] '");
    msg += available_.back().info.name;
    msg += "' (";
    msg += path;
    msg += ")";
    LOG(msg);
    return true;
}

// ---------------------------------------------------------------------------
//  scan_modules -- scan recursif, peuple available_.
// ---------------------------------------------------------------------------
int ModuleRegistry::scan_modules(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        std::string msg = _("loader: module directory not found: ");
        msg += dir.string();
        LOG(msg);
        return 0;
    }

    int found = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != kModuleExt) continue;
        if (probe_file(e.path())) ++found;
    }
    return found;
}

// ---------------------------------------------------------------------------
//  find_available -- cherche dans available_ par kind + id. ou par id
// ---------------------------------------------------------------------------
const AvailableModule* ModuleRegistry::find_available(ModuleKind kind, size_t module_id) const {
    for (const auto& a : available_)
        if (a.info.kind == kind && a.info.id == module_id)
            return &a;
    return nullptr;
}
const AvailableModule* ModuleRegistry::find_available(size_t module_id) const {
    for (const auto& a : available_)
        if (a.info.id == module_id)
            return &a;
    return nullptr;
}

// ---------------------------------------------------------------------------
//  find_loaded -- cherche dans loaded_ par kind + id. ou par id 
// ---------------------------------------------------------------------------
const LoadedModule* ModuleRegistry::find_loaded(size_t loaded_id) const {
    for (auto& l : loaded_)
        if (l.id == loaded_id)
            return &l;
    return nullptr;
}
const LoadedModule* ModuleRegistry::find_loaded(ModuleKind kind) const {
    for (auto& l : loaded_)
        if (l.info.kind == kind)
            return &l;
    return nullptr;
}


// ---------------------------------------------------------------------------
//  load_module -- phase 2 : charge un module disponible.
//  Rouvre la lib, instancie l'objet definitif avec (0,0), stocke dans loaded_.
//  Sans effet (retourne true) si deja charge.
// ---------------------------------------------------------------------------
bool ModuleRegistry::load_module(size_t available_id) {

    const AvailableModule* am = find_available(available_id);
    if (!am) {
        std::string msg_err = error(__func__,
            _("load_module: module not available"),
            std::string(" id=") + std::to_string(available_id));
        LOG_ERR(msg_err);
        return false;
    }

    void* handle = dl_open(am->path.c_str());
    if (!handle) {
        std::string msg_err = error(__func__,
            _("load_module: cannot open '") + am->info.name + "'",
            dl_error());
        LOG_ERR(msg_err);
        return false;
    }

    auto entry = reinterpret_cast<OdeniseModuleEntryFn>(
        dl_sym(handle, ODENISE_MODULE_ENTRY_SYMBOL));
    if (!entry) {
        std::string msg_err = error(__func__,
            _("load_module: cannot resolve entry of '") + am->info.name + "'",
            std::string(_("missing symbol ")) + ODENISE_MODULE_ENTRY_SYMBOL);
        LOG_ERR(msg_err);
        dl_close(handle);
        return false;
    }

    odenise::EngineCaps e_caps;
    e_caps.ring_size_max = 0;

    ModuleBase* module = entry(e_caps);
    if (!module) {
        std::string msg_err = error(__func__,
            _("load_module: entry returned null for '") + am->info.name + "'",
            _("odenise_module_entry(e_caps) returned null"));
        LOG_ERR(msg_err);
        dl_close(handle);
        return false;
    }

    // Pour ComputeBackend : cast vers BackendBase*.
    BackendBase* backend = nullptr;
    if (am->info.kind == ModuleKind::ComputeBackend) {
        backend = dynamic_cast<BackendBase*>(module);
        if (!backend) {
            std::string msg_err = error(__func__,
                _("load_module: cast failed for backend '") + am->info.name + "'",
                _("ComputeBackend does not implement BackendBase"));
            LOG_ERR(msg_err);
            delete module;
            dl_close(handle);
            return false;
        }
    }

    LoadedModule lm;
    // TODO lm.info_id or lm_id must differt from am_info_id
    lm.handle  = handle;
    lm.module  = module;
    lm.backend = backend;
    lm.info    = am->info;
    lm.path    = am->path;
    lm.id      = loaded_.size();
    loaded_.push_back(std::move(lm));

    std::string msg = _("loader: loaded [");
    msg += kindName(am->info.kind);
    msg += _("] '");
    msg += am->info.name;
    msg += "'";
    LOG(msg);
    return true;
}

// ---------------------------------------------------------------------------
//  unload_module -- decharge un module : detruit l'objet, ferme le handle.
// ---------------------------------------------------------------------------
void ModuleRegistry::unload_module(size_t loaded_id) noexcept {
    for (auto lm = loaded_.begin(); lm != loaded_.end(); ++lm) {
        if (lm->id == loaded_id) {
            auto kind = lm->info.kind;
            auto id = lm->id;
            delete lm->module;
            lm->module  = nullptr;
            lm->backend = nullptr;
            dl_close(lm->handle);
            lm->handle = nullptr;
            lm = loaded_.erase(lm);
            for ( auto lmrest = lm; lmrest != loaded_.end(); ++lmrest) {
                lmrest->id = lmrest->id - 1;
            }
            std::string msg = _("loader: unloaded [");
            msg += kindName(kind);
            msg += _("] id=");
            msg += std::to_string(id);
            LOG(msg);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
//  find_module / find_backend -- acces non-owning aux modules charges.
// ---------------------------------------------------------------------------
ModuleBase* ModuleRegistry::find_module(size_t loaded_id) const {
    const LoadedModule* lm = find_loaded(loaded_id);
    return lm ? lm->module : nullptr;
}

BackendBase* ModuleRegistry::find_backend() const {
    const LoadedModule* lm = find_loaded(ModuleKind::ComputeBackend);
    return lm ? lm->backend : nullptr;
}

// ---------------------------------------------------------------------------
//  first_available_id -- retourne l'id du premier module disponible d'un kind.
//  Sans allocation. Retourne 0 si aucun module de ce kind n'est disponible.
// ---------------------------------------------------------------------------
size_t ModuleRegistry::first_available_id(ModuleKind kind) const noexcept {
    for (const auto& a : available_)
        if (a.info.kind == kind)
            return a.info.id;
    return 65535;
}

// ---------------------------------------------------------------------------
//  list_available / list_loaded
// ---------------------------------------------------------------------------
std::vector<ModuleInfo> ModuleRegistry::list_available(ModuleKind kind) const {
    std::vector<ModuleInfo> out;
    for (const auto& a : available_)
        if (a.info.kind == kind)
            out.push_back(a.info);
    return out;
}
std::vector<ModuleInfo> ModuleRegistry::list_available() const {
    std::vector<ModuleInfo> out;
    for (const auto& a : available_)
        out.push_back(a.info);
    return out;
}
ModuleInfo ModuleRegistry::get_available_module_info(size_t available_id) const {
    for (const auto& a : available_)
        if (a.info.id == available_id)
            return a.info;
    return {};
}

std::vector<LoadedModule> ModuleRegistry::list_loaded(ModuleKind kind) const {
    std::vector<LoadedModule> out;
    for (const auto& l : loaded_)
        if (l.info.kind == kind)
            out.push_back(l);
    return out;
}
std::vector<LoadedModule> ModuleRegistry::list_loaded() const {
    std::vector<LoadedModule> out;
    for (const auto& l : loaded_)
        out.push_back(l);
    return out;
}

// ---------------------------------------------------------------------------
//  self_test -- charge temporairement si necessaire, execute, decharge.
// ---------------------------------------------------------------------------
TestResult ModuleRegistry::self_test(size_t available_id) {
    if (!load_module(available_id))
        return TestResult{ false, _("self_test: cannot load module") };
    

    ModuleBase* module = find_module(loaded_.size() - 1);
    if (!module)
        return TestResult{ false, _("self_test: module not found after load") };

    ModuleInfo module_info = get_available_module_info(available_id);
    if ( module_info.description != module->info_c()->description 
      || module_info.name != module->info_c()->name){
        return TestResult{ false, _("self_test: module test will bnot be requested module") };
    }
    const OdeniseTestResultC* r = module->self_test_c();
    TestResult out;
    if (!r) {
        out = TestResult{ false, _("self_test_c() returned null") };
    } else {
        out.passed = (r->passed != 0);
        out.detail = r->detail ? r->detail : "";
    }

    unload_module(loaded_.size() - 1);

    return out;
}

} // namespace odenise

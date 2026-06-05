// ============================================================================
//  loader.cpp  --  Chargement dynamique portable des modules + registre.
//
//  Implemente ns::detail::ModuleRegistry. Couche fine au-dessus de
//  dlopen/LoadLibrary : ouvre les .so/.dll d'un dossier, resout le symbole
//  d'entree, verifie l'ABI via un objet temporaire, stocke entry_fn.
//
//  Separation des responsabilites :
//   - Le loader lit les metadonnees (info_c()) via un objet temporaire
//     instancie avec (0, 0), puis le detruit immediatement.
//   - L'engine cree les objets definitifs via make(sample_rate, n_max)
//     au moment du bind, avec les vraies caps moteur.
//   - Le registre ne possede que les handles de bibliotheque.
//
//  Diagnostics via le LogManager (LOG / LOG_ERR), au format error(from,what,why).
// ============================================================================
#include "module_registry.h"
#include "tools/logger.h"

#include <system_error>

// ---------------------------------------------------------------------------
//  Shim plateforme : ouverture / resolution de symbole / fermeture.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
  #include <windows.h>
  namespace {
    void* dl_open(const char* path)          { return (void*)LoadLibraryA(path); }
    void* dl_sym (void* h, const char* name) { return (void*)GetProcAddress((HMODULE)h, name); }
    void  dl_close(void* h)                  { if (h) FreeLibrary((HMODULE)h); }
    std::string dl_error()                   { return "LoadLibrary/GetProcAddress error"; }
    constexpr const char* kModuleExt = ".dll";
  }
#else
  #include <dlfcn.h>
  namespace {
    void* dl_open(const char* path)          { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
    void* dl_sym (void* h, const char* name) { return dlsym(h, name); }
    void  dl_close(void* h)                  { if (h) dlclose(h); }
    std::string dl_error()                   { const char* e = dlerror(); return e ? e : "unknown error"; }
    #if defined(__APPLE__)
      constexpr const char* kModuleExt = ".dylib";
    #else
      constexpr const char* kModuleExt = ".so";
    #endif
  }
#endif

namespace ns::detail {

namespace {

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

} // namespace

// ---------------------------------------------------------------------------
//  Destructeur : seul le handle de bibliotheque est possede par le registre.
//  Les objets C++ (ModuleBase*, BackendBase*) sont possedes par l'engine.
// ---------------------------------------------------------------------------
ModuleRegistry::~ModuleRegistry() {
    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it)
        dl_close(it->handle);
    modules_.clear();
}

// ---------------------------------------------------------------------------
//  tryLoad -- charge un fichier, lit les metadonnees, stocke entry_fn.
//  Instancie un objet temporaire avec (0,0) uniquement pour lire info_c(),
//  puis le detruit. L'objet definitif est cree par make() via l'engine.
// ---------------------------------------------------------------------------
bool ModuleRegistry::tryLoad(const std::filesystem::path& file) {
    const std::string path   = file.string();
    const std::string fname  = file.filename().string();
    const std::string subdir = file.parent_path().filename().string();
    std::string msg;
    std::string msg_err;

    void* handle = dl_open(path.c_str());
    if (!handle) {
        msg_err = error(__func__,
            _("Loader cannot open '") + subdir + _("' module '") + fname + "'",
            dl_error());
        LOG_ERR(msg_err);
        return false;
    }

    // Resolution du symbole d'entree.
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

    // Instanciation temporaire pour lire info_c() uniquement.
    // L'objet est detruit immediatement apres la lecture des metadonnees.
    // Les objets definitifs sont crees par make() avec les vraies caps.
    ns::ModuleBase* probe = entry(0, 0);
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

    // Verification ABI.
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

    // Validation du kind.
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

    // Validation du cast BackendBase pour ComputeBackend.
    if (kind == ModuleKind::ComputeBackend) {
        if (!dynamic_cast<ns::BackendBase*>(probe)) {
            msg_err = error(__func__,
                _("Loader cannot cast backend of '") + subdir + _("' module '") + fname + "'",
                _("ComputeBackend module does not implement BackendBase"));
            LOG_ERR(msg_err);
            delete probe;
            dl_close(handle);
            return false;
        }
    }

    // Metadonnees lues -- destruction de l'objet temporaire.
    ModuleInfo mi = toModuleInfo(*info, kind);
    delete probe;

    LoadedModule lm;
    lm.handle   = handle;
    lm.entry_fn = entry;
    lm.info     = std::move(mi);
    lm.path     = path;

    msg = _("loader: loaded [");
    msg += kindName(kind);
    msg += _("] module '");
    msg += lm.info.name;
    msg += "' (";
    msg += path;
    msg += ")";
    LOG(msg);
    modules_.push_back(std::move(lm));
    return true;
}

// ---------------------------------------------------------------------------
//  scanDirectory -- scan recursif du dossier de modules.
// ---------------------------------------------------------------------------
int ModuleRegistry::scanDirectory(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        std::string msg = _("loader: module directory not found: ");
        msg += dir.string();
        LOG(msg);
        return 0;
    }

    int loaded = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != kModuleExt) continue;
        if (tryLoad(e.path())) ++loaded;
    }
    return loaded;
}

// ---------------------------------------------------------------------------
//  list -- enumere les modules d'une famille.
// ---------------------------------------------------------------------------
std::vector<ModuleInfo> ModuleRegistry::list(ModuleKind kind) const {
    std::vector<ModuleInfo> out;
    for (const auto& m : modules_)
        if (m.info.kind == kind)
            out.push_back(m.info);
    return out;
}

// ---------------------------------------------------------------------------
//  make -- instancie un ModuleBase* avec les caps reelles.
//  L'appelant prend possession de l'objet (delete requis).
// ---------------------------------------------------------------------------
ns::ModuleBase* ModuleRegistry::make(ModuleKind kind, int id,
                                     int sample_rate, int n_max) const {
    for (const auto& m : modules_) {
        if (m.info.kind == kind && m.info.id == id)
            return m.entry_fn ? m.entry_fn(sample_rate, n_max) : nullptr;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  make_backend -- instancie un BackendBase* avec les caps reelles.
//  L'appelant prend possession de l'objet (delete requis).
// ---------------------------------------------------------------------------
ns::BackendBase* ModuleRegistry::make_backend(int id,
                                               int sample_rate, int n_max) const {
    for (const auto& m : modules_) {
        if (m.info.kind == ModuleKind::ComputeBackend && m.info.id == id) {
            ns::ModuleBase* base = m.entry_fn ? m.entry_fn(sample_rate, n_max) : nullptr;
            if (!base) return nullptr;
            ns::BackendBase* backend = dynamic_cast<ns::BackendBase*>(base);
            if (!backend) { delete base; return nullptr; }
            return backend;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  selfTest -- instancie un objet temporaire et execute son self-test.
// ---------------------------------------------------------------------------
TestResult ModuleRegistry::selfTest(ModuleKind kind, int id) const {
    // Instanciation temporaire avec (0,0) : le self-test ne depend pas des caps.
    ns::ModuleBase* base = make(kind, id, 0, 0);
    if (!base)
        return TestResult{ false, _("module not found or instantiation failed") };

    const OdeniseTestResultC* r = base->self_test_c();
    TestResult out;
    if (!r) {
        out = TestResult{ false, _("self_test_c() returned null") };
    } else {
        out.passed = (r->passed != 0);
        out.detail = r->detail ? r->detail : "";
    }
    delete base;
    return out;
}

} // namespace ns::detail

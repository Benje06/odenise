// ============================================================================
//  loader.cpp  --  Chargement dynamique portable des modules + registre.
//
//  Implemente ns::detail::ModuleRegistry. Couche fine au-dessus de
//  dlopen/LoadLibrary : ouvre les .so/.dll d'un dossier, resout le symbole
//  d'entree, verifie l'ABI, et conserve la table de fonctions du module.
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
    m.id          = c.id;
    m.kind        = kind;
    m.name        = c.name        ? c.name        : "";
    m.description = c.description ? c.description : "";
    m.needs_gpu   = (c.needs_gpu != 0);
    return m;
}

} // namespace

ModuleRegistry::~ModuleRegistry() {
    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it)
        dl_close(it->handle);
    modules_.clear();
}

bool ModuleRegistry::tryLoad(const std::filesystem::path& file) {
    const std::string path = file.string();

    // Identification du module pour les diagnostics : nom de fichier + sous-dossier
    // scanne (= la famille presumee : "suppression", "backends", ...). Avant la
    // validation du kind (plus bas), on ne dispose que de ces indices de chemin.
    const std::string fname  = file.filename().string();
    const std::string subdir = file.parent_path().filename().string();
    std::string msg;
    std::string msg_err;

    void* handle = dl_open(path.c_str());
    if (!handle) {
        msg_err = error(__func__,
            _("Loader cannot open of '") + subdir + _("' module '") + fname + "'",
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

    const OdeniseModuleVTable* vt = entry();
    if (!vt) {
        msg_err = error(__func__,
            _("Loader cannot read vtable of '") + subdir + _("' module '") + fname + "'",
            _("null vtable returned"));
        LOG_ERR(msg_err);
        dl_close(handle);
        return false;
    }

    if (vt->abi_version != kAbiVersion) {
        std::string why = std::string(_("incompatible ABI ")) + std::to_string(vt->abi_version)
            + _(" (expected ") + std::to_string(kAbiVersion) + ")";
        msg_err = error(__func__,
            _("Loader cannot accept ABI of '") + subdir + _("' module '") + fname + "'",
            why);
        LOG_ERR(msg_err);
        dl_close(handle);
        return false;
    }

    // Un module est valide s'il expose le chemin C++ (create_module ou
    // create_backend) OU le chemin legacy complet (create+destroy+process).
    // Les deux peuvent coexister ; au moins l'un doit etre present.
    const bool has_cpp    = (vt->create_module != nullptr)
                         || (vt->create_backend != nullptr);
    const bool has_legacy = (vt->create != nullptr)
                         && (vt->destroy != nullptr)
                         && (vt->process != nullptr);
    if (!has_cpp && !has_legacy) {
        msg_err = error(__func__,
            _("Loader cannot use vtable of '") + subdir + _("' module '") + fname + "'",
            _("incomplete vtable : neither C++ (create_module/create_backend)"
              " nor legacy (create/destroy/process) is present"));
        LOG_ERR(msg_err);
        dl_close(handle);
        return false;
    }

    ModuleKind kind;
    if (!kindFromInt(vt->info.kind, kind)) {
        msg_err = error(__func__,
            _("Loader cannot identify kind of '") + subdir + _("' module '") + fname + "'",
            _("unknown module kind ") + std::to_string(vt->info.kind));
        LOG_ERR(msg_err);
        dl_close(handle);
        return false;
    }

    LoadedModule lm;
    lm.handle = handle;
    lm.vtable = vt;
    lm.info   = toModuleInfo(vt->info, kind);
    lm.path   = path;
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

int ModuleRegistry::scanDirectory(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        std::string msg = _("loader: module directory not found: ");
        msg += dir.string();
        LOG(msg);
        return 0;
    }

    // Scan recursif : les modules sont ranges par sous-dossier de kind
    // (modules/suppression/, modules/backends/, ...), miroir de l'arbre source.
    // On descend donc dans toute l'arborescence sous 'dir'.
    int loaded = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != kModuleExt) continue;
        if (tryLoad(e.path())) ++loaded;
    }
    return loaded;
}

std::vector<ModuleInfo> ModuleRegistry::list(ModuleKind kind) const {
    std::vector<ModuleInfo> out;
    for (const auto& m : modules_)
        if (m.info.kind == kind)
            out.push_back(m.info);
    return out;
}

const OdeniseModuleVTable* ModuleRegistry::find(ModuleKind kind, int id) const {
    for (const auto& m : modules_)
        if (m.info.kind == kind && m.info.id == id)
            return m.vtable;
    return nullptr;
}

TestResult ModuleRegistry::selfTest(ModuleKind kind, int id) const {
    const OdeniseModuleVTable* vt = find(kind, id);
    if (!vt || !vt->self_test)
        return TestResult{ false, _("module or self-test missing") };

    OdeniseTestResultC r = vt->self_test();
    TestResult out;
    out.passed = (r.passed != 0);
    out.detail = r.detail ? r.detail : "";
    return out;
}

} // namespace ns::detail

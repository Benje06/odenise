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

    void* handle = dl_open(path.c_str());
    if (!handle) {
        LOG_ERR(error("loader", _("cannot open module: ") + path, dl_error()));
        return false;
    }

    auto entry = reinterpret_cast<OdeniseModuleEntryFn>(
        dl_sym(handle, ODENISE_MODULE_ENTRY_SYMBOL));
    if (!entry) {
        LOG_ERR(error("loader", path, std::string(_("missing entry symbol ")) + ODENISE_MODULE_ENTRY_SYMBOL));
        dl_close(handle);
        return false;
    }

    const OdeniseModuleVTable* vt = entry();
    if (!vt) {
        LOG_ERR(error("loader", path, _("null vtable returned")));
        dl_close(handle);
        return false;
    }

    if (vt->abi_version != kAbiVersion) {
        LOG_ERR(error("loader", path,
            std::string(_("incompatible ABI ")) + std::to_string(vt->abi_version)
            + _(" (expected ") + std::to_string(kAbiVersion) + ")"));
        dl_close(handle);
        return false;
    }

    if (!vt->create || !vt->destroy || !vt->process) {
        LOG_ERR(error("loader", path, _("incomplete vtable (create/destroy/process)")));
        dl_close(handle);
        return false;
    }

    ModuleKind kind;
    if (!kindFromInt(vt->info.kind, kind)) {
        LOG_ERR(error("loader", path, _("unknown module kind ") + std::to_string(vt->info.kind)));
        dl_close(handle);
        return false;
    }

    LoadedModule lm;
    lm.handle = handle;
    lm.vtable = vt;
    lm.info   = toModuleInfo(vt->info, kind);
    lm.path   = path;
    LOG(_("loader: loaded module '") + lm.info.name + "' (" + path + ")");
    modules_.push_back(std::move(lm));
    return true;
}

int ModuleRegistry::scanDirectory(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        LOG(_("loader: module directory not found: ") + dir.string());
        return 0;
    }

    int loaded = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
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

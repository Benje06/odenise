// ============================================================================
//  loader.cpp  --  Chargement dynamique portable des modules + registre.
//
//  Implemente ns::detail::ModuleRegistry. Couche fine au-dessus de
//  dlopen/LoadLibrary : ouvre les .so/.dll d'un dossier, resout le symbole
//  d'entree, verifie l'ABI, et conserve la table de fonctions du module.
//
//  Diagnostics de chargement ecrits sur stderr (load-time uniquement, hors
//  chemin temps reel). Un logger injectable pourra remplacer fprintf plus tard.
// ============================================================================
#include "module_registry.h"

#include <cstdio>
#include <system_error>

// ---------------------------------------------------------------------------
//  Shim plateforme : ouverture / resolution de symbole / fermeture.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
  #include <windows.h>
  namespace {
    void* dl_open(const char* path)            { return (void*)LoadLibraryA(path); }
    void* dl_sym (void* h, const char* name)   { return (void*)GetProcAddress((HMODULE)h, name); }
    void  dl_close(void* h)                    { if (h) FreeLibrary((HMODULE)h); }
    const char* dl_error()                     { return "LoadLibrary/GetProcAddress a echoue"; }
    constexpr const char* kModuleExt = ".dll";
  }
#else
  #include <dlfcn.h>
  namespace {
    void* dl_open(const char* path)            { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
    void* dl_sym (void* h, const char* name)   { return dlsym(h, name); }
    void  dl_close(void* h)                    { if (h) dlclose(h); }
    const char* dl_error()                     { const char* e = dlerror(); return e ? e : "erreur inconnue"; }
    #if defined(__APPLE__)
      constexpr const char* kModuleExt = ".dylib";
    #else
      constexpr const char* kModuleExt = ".so";
    #endif
  }
#endif

namespace ns::detail {

namespace {

// Convertit l'entier 'kind' de la frontiere C en enum, en validant la plage.
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

// Copie les metadonnees C du module dans un ModuleInfo C++ (chaines copiees).
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
    // Ferme les bibliotheques dans l'ordre inverse du chargement.
    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it)
        dl_close(it->handle);
    modules_.clear();
}

bool ModuleRegistry::tryLoad(const std::filesystem::path& file) {
    const std::string path = file.string();

    void* handle = dl_open(path.c_str());
    if (!handle) {
        std::fprintf(stderr, "[odenise] echec ouverture '%s' : %s\n",
                     path.c_str(), dl_error());
        return false;
    }

    auto entry = reinterpret_cast<OdeniseModuleEntryFn>(
        dl_sym(handle, ODENISE_MODULE_ENTRY_SYMBOL));
    if (!entry) {
        std::fprintf(stderr, "[odenise] '%s' : symbole '%s' absent (pas un module ?)\n",
                     path.c_str(), ODENISE_MODULE_ENTRY_SYMBOL);
        dl_close(handle);
        return false;
    }

    const OdeniseModuleVTable* vt = entry();
    if (!vt) {
        std::fprintf(stderr, "[odenise] '%s' : table de fonctions nulle\n", path.c_str());
        dl_close(handle);
        return false;
    }

    // Verification d'ABI : non negociable.
    if (vt->abi_version != kAbiVersion) {
        std::fprintf(stderr,
                     "[odenise] '%s' : ABI %d incompatible (attendu %d) -- ignore\n",
                     path.c_str(), vt->abi_version, kAbiVersion);
        dl_close(handle);
        return false;
    }

    // Validation minimale des pointeurs indispensables.
    if (!vt->create || !vt->destroy || !vt->process) {
        std::fprintf(stderr, "[odenise] '%s' : table incomplete (create/destroy/process)\n",
                     path.c_str());
        dl_close(handle);
        return false;
    }

    ModuleKind kind;
    if (!kindFromInt(vt->info.kind, kind)) {
        std::fprintf(stderr, "[odenise] '%s' : famille de module inconnue (%d)\n",
                     path.c_str(), vt->info.kind);
        dl_close(handle);
        return false;
    }

    LoadedModule lm;
    lm.handle = handle;
    lm.vtable = vt;
    lm.info   = toModuleInfo(vt->info, kind);
    lm.path   = path;
    modules_.push_back(std::move(lm));
    return true;
}

int ModuleRegistry::scanDirectory(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        std::fprintf(stderr, "[odenise] dossier de modules introuvable : '%s'\n",
                     dir.string().c_str());
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
        return TestResult{ false, "module ou self-test absent" };

    OdeniseTestResultC r = vt->self_test();
    TestResult out;
    out.passed = (r.passed != 0);
    out.detail = r.detail ? r.detail : "";
    return out;
}

} // namespace ns::detail

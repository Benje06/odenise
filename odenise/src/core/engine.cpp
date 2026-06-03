// engine.cpp -- orchestration du moteur (STUB minimal, etape 1).
#include "ns_engine.h"
#include "module_registry.h"

#include <cstdlib>
#include <filesystem>

namespace ns {

namespace {
// Dossier de modules : env var > a cote du binaire > chemin d'install.
std::filesystem::path moduleDir() {
    if (const char* e = std::getenv("ODENISE_MODULE_PATH"))
        return e;
    return "modules";   // relatif au repertoire courant pour l'instant
}
} // namespace

// Implementation neutre : satisfait l'interface, ne traite rien encore.
class EngineImpl final : public Engine {
public:
    explicit EngineImpl(const EngineCaps& caps, const RuntimeConfig& cfg)
        : caps_(caps), cfg_(cfg) {
        registry_.scanDirectory(moduleDir());
    }

    int latencySamples() const noexcept override { return cfg_.n; }

    Status reconfigure(const RuntimeConfig& cfg, ApplyResult& how) override {
        cfg_ = cfg;
        how  = ApplyResult::Hot;
        return Status::Ok;
    }

    BackendCaps backendCaps() const override { return {}; }

    Status process(std::span<const TrackIO>, int) noexcept override {
        return Status::Unsupported;   // pas de DSP a l'etape 1
    }

    Status setParam(ParamId, float) noexcept override { return Status::Ok; }
    float  getParam(ParamId) const noexcept override { return 0.0f; }
    Status setGminCurve(std::span<const float>) noexcept override { return Status::Ok; }
    Status setBandLayout(std::span<const float>) override { return Status::Ok; }

    Status captureBegin(ProfileLevel, float) override { return Status::Unsupported; }
    bool   captureActive() const noexcept override { return false; }
    std::vector<std::byte> saveProfile(ProfileLevel) const override { return {}; }
    Status loadProfile(ProfileLevel, std::span<const std::byte>) override { return Status::Unsupported; }

    std::vector<ModuleInfo> modules(ModuleKind kind) const override {
        return registry_.list(kind);
    }
    TestResult selfTest(ModuleKind kind, int id) const override {
        return registry_.selfTest(kind, id);
    }

    Metrics  metrics()  const override { return {}; }
    Spectrum spectrum() const override { return {}; }

private:
    EngineCaps           caps_;
    RuntimeConfig        cfg_;
    detail::ModuleRegistry registry_;
};

std::unique_ptr<Engine> createEngine(const EngineCaps& caps,
                                     const RuntimeConfig& cfg,
                                     Status* status) {
    if (status) *status = Status::Ok;
    return std::make_unique<EngineImpl>(caps, cfg);
}

std::vector<ModuleInfo> availableBackends() {
    detail::ModuleRegistry reg;
    reg.scanDirectory(moduleDir());
    return reg.list(ModuleKind::ComputeBackend);
}

} // namespace ns

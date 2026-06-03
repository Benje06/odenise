// engine.cpp -- orchestration du moteur (STUB minimal, etape 1).
#include "ns_engine.h"
#include "module_registry.h"
#include "tools/logger.h"

#include <cstdlib>
#include <filesystem>

namespace ns {

namespace {
// Dossier de modules : env var > "modules" relatif. Lecture portable de
// l'environnement (evite getenv deprecie sous MSVC : C4996).
std::filesystem::path moduleDir() {
#if defined(_WIN32)
    char*  buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, "ODENISE_MODULE_PATH") == 0 && buf) {
        std::filesystem::path p = buf;
        std::free(buf);
        return p;
    }
#else
    if (const char* e = std::getenv("ODENISE_MODULE_PATH"))
        return e;
#endif
    return "modules";
}
} // namespace

class EngineImpl final : public Engine {
public:
    EngineImpl(const EngineCaps& caps, const RuntimeConfig& cfg)
        : caps_(caps), cfg_(cfg) {
        const auto dir = moduleDir();
        const int n = registry_.scanDirectory(dir);
        LOG(_("engine: created (n=") + std::to_string(cfg_.n)
            + _(", modules loaded: ") + std::to_string(n) + ")");
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
    EngineCaps             caps_;
    RuntimeConfig          cfg_;
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

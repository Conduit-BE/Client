#pragma once
namespace conduit {
struct DiagnosticsSettings {
    float scale = 1.0f;
    bool factoriesEnabled = true;
    bool emptyFactories = true;
    bool showFactory(unsigned controls, unsigned roots) const noexcept {
        return factoriesEnabled && (emptyFactories || controls != 0 || roots != 0);
    }
};
}

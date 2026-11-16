#pragma once
namespace conduit {
struct DiagnosticsSettings {
    float uiScale = 1.0f;
    float uiAlpha = 0.5f;
    bool factoriesEnabled = true;
    bool emptyFactories = true;
    bool showFactory(unsigned controls, unsigned roots) const noexcept {
        return factoriesEnabled && (emptyFactories || controls != 0 || roots != 0);
    }
};
}

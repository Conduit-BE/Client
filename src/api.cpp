#include "conduit/api.h"
#include "client.hpp"
#include "platform/windows/auto_start.hpp"

struct ConduitClient {
    explicit ConduitClient(const ConduitConfig& config) : implementation(config) {}
    conduit::Client implementation;
};

extern "C" {
uint32_t ConduitGetApiVersion() {
    return CONDUIT_API_VERSION;
}

ConduitStatus ConduitCreate(const ConduitConfig* config, ConduitClient** output) {
    if (!output) {
        return CONDUIT_INVALID_ARGUMENT;
    }
    *output = nullptr;
    if (!config || config->size != sizeof(ConduitConfig)) {
        return CONDUIT_INVALID_ARGUMENT;
    }
    if (config->api_version != CONDUIT_API_VERSION) {
        return CONDUIT_VERSION_MISMATCH;
    }
    if (!config->watch_directory_utf8 || !*config->watch_directory_utf8
        || !config->log_file_utf8 || !*config->log_file_utf8
        || (config->hotkey_virtual_key > 254 && config->hotkey_virtual_key != CONDUIT_HOTKEY_DISABLED)
        || (config->poll_interval_ms != 0 && config->poll_interval_ms < 10)
        || config->poll_interval_ms > 60000 || config->debounce_ms > 60000) {
        return CONDUIT_INVALID_ARGUMENT;
    }
    try {
        *output = new ConduitClient(*config);
        return CONDUIT_OK;
    } catch (...) {
        return CONDUIT_INTERNAL_ERROR;
    }
}

ConduitStatus ConduitStart(ConduitClient* client) {
    if (!client) { return CONDUIT_INVALID_ARGUMENT; }
    try { return client->implementation.start(); }
    catch (...) { return CONDUIT_INTERNAL_ERROR; }
}

ConduitStatus ConduitRequestReload(ConduitClient* client) {
    if (!client) { return CONDUIT_INVALID_ARGUMENT; }
    try { return client->implementation.requestReload(); }
    catch (...) { return CONDUIT_INTERNAL_ERROR; }
}

ConduitStatus ConduitGetStats(ConduitClient* client, ConduitStats* stats) {
    if (!client || !stats || stats->size != sizeof(ConduitStats)) {
        return CONDUIT_INVALID_ARGUMENT;
    }
    try {
        client->implementation.getStats(*stats);
        return CONDUIT_OK;
    } catch (...) { return CONDUIT_INTERNAL_ERROR; }
}

ConduitStatus ConduitStop(ConduitClient* client) {
    if (!client) { return CONDUIT_INVALID_ARGUMENT; }
    try {
        client->implementation.stop();
        return CONDUIT_OK;
    } catch (...) { return CONDUIT_INTERNAL_ERROR; }
}

void ConduitDestroy(ConduitClient* client) {
    delete client; // Destruction joins the worker; null is accepted.
}

void ConduitAutoStop() {
    conduit::windows::stopAutoStart();
}
}

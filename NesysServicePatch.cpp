#include "NesysServicePatch.h"

#include "NesysServiceProcess.h"
#include "config.h"

#include <Windows.h>
#include <atomic>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

std::atomic_bool g_initialized{false};
HMODULE g_loader_module = nullptr;

} // namespace

void NesysServicePatchInit(HMODULE loader_module) {
    bool expected = false;
    if (!g_initialized.compare_exchange_strong(expected, true)) {
        return;
    }

    g_loader_module = loader_module;
    const auto role = DetectCurrentProcessRole();
    const bool enabled = ConfigManager::instance().GetEnableNesysServiceAdapterPatch();

    PLOG_INFO << "NesysServicePatch: init role=" << ProcessRoleName(role)
              << " enable_nesys_service_adapter_patch=" << enabled
              << " loader_module=" << reinterpret_cast<void*>(g_loader_module);

    if (!enabled) {
        PLOG_INFO << "NesysServicePatch: disabled by config";
        return;
    }

    if (role == ProcessRole::Service) {
        PLOG_INFO << "NesysServicePatch: service role recognized";
        return;
    }

    PLOG_INFO << "NesysServicePatch: game role recognized";
}

} // namespace gc::nesys_service

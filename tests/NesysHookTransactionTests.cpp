#include "NesysHookTransaction.h"

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

struct FakeState {
    int fail_create_call{-1};
    int fail_queue_call{-1};
    bool fail_apply{false};
    int create_calls{0};
    int queue_calls{0};
    int apply_calls{0};
    std::vector<LPVOID> created;
    std::vector<LPVOID> queued;
    std::vector<LPVOID> disabled;
    std::vector<LPVOID> removed;
};

FakeState* g_fake = nullptr;

MH_STATUS WINAPI fake_initialize() {
    return MH_OK;
}

MH_STATUS WINAPI fake_create(LPVOID target, LPVOID, LPVOID*) {
    const int call = g_fake->create_calls++;
    if (call == g_fake->fail_create_call) {
        return MH_ERROR_MEMORY_ALLOC;
    }
    g_fake->created.push_back(target);
    return MH_OK;
}

MH_STATUS WINAPI fake_queue(LPVOID target) {
    const int call = g_fake->queue_calls++;
    if (call == g_fake->fail_queue_call) {
        return MH_ERROR_MEMORY_PROTECT;
    }
    g_fake->queued.push_back(target);
    return MH_OK;
}

MH_STATUS WINAPI fake_apply() {
    ++g_fake->apply_calls;
    return g_fake->fail_apply ? MH_ERROR_MEMORY_PROTECT : MH_OK;
}

MH_STATUS WINAPI fake_disable(LPVOID target) {
    g_fake->disabled.push_back(target);
    return MH_OK;
}

MH_STATUS WINAPI fake_remove(LPVOID target) {
    g_fake->removed.push_back(target);
    return MH_OK;
}

gc::nesys_service::MinHookApi fake_api() {
    return {
        fake_initialize,
        fake_create,
        fake_queue,
        fake_apply,
        fake_disable,
        fake_remove,
    };
}

gc::nesys_service::ResolvedApiHook hook(LPVOID target) {
    return {
        {L"fake.dll", "Fake", reinterpret_cast<LPVOID>(0x2000), nullptr},
        target,
    };
}

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

bool contains(const std::vector<LPVOID>& values, LPVOID target) {
    return std::find(values.begin(), values.end(), target) != values.end();
}

} // namespace

int main() {
    using namespace gc::nesys_service;
    int failures = 0;
    const auto first = reinterpret_cast<LPVOID>(0x1000);
    const auto second = reinterpret_cast<LPVOID>(0x1100);
    const auto unrelated = reinterpret_cast<LPVOID>(0x9999);
    const std::vector<ResolvedApiHook> hooks{hook(first), hook(second)};

    FakeState success{};
    g_fake = &success;
    OwnedMinHookTransaction committed(fake_api());
    failures += expect(committed.Initialize(), "initialize success");
    failures += expect(committed.CreateAll(hooks), "create all");
    failures += expect(committed.Commit(), "commit all");
    failures += expect(
        success.created == std::vector<LPVOID>{first, second},
        "create exact targets");
    failures += expect(
        success.queued == std::vector<LPVOID>{first, second},
        "queue exact targets");
    failures += expect(success.apply_calls == 1, "single queued apply");

    FakeState create_failure{};
    create_failure.fail_create_call = 1;
    g_fake = &create_failure;
    OwnedMinHookTransaction create_rollback(fake_api());
    failures += expect(create_rollback.Initialize(), "create failure init");
    failures += expect(!create_rollback.CreateAll(hooks), "create failure");
    failures += expect(
        contains(create_failure.removed, first),
        "created target removed");
    failures += expect(
        !contains(create_failure.removed, second),
        "failed target not removed");

    FakeState queue_failure{};
    queue_failure.fail_queue_call = 1;
    g_fake = &queue_failure;
    OwnedMinHookTransaction queue_rollback(fake_api());
    failures += expect(queue_rollback.Initialize(), "queue failure init");
    failures += expect(queue_rollback.CreateAll(hooks), "queue failure create");
    failures += expect(!queue_rollback.Commit(), "queue failure commit");
    failures += expect(
        contains(queue_failure.removed, first) &&
            contains(queue_failure.removed, second),
        "queue failure removes every owned target");

    FakeState apply_failure{};
    apply_failure.fail_apply = true;
    g_fake = &apply_failure;
    OwnedMinHookTransaction apply_rollback(fake_api());
    failures += expect(apply_rollback.Initialize(), "apply failure init");
    failures += expect(apply_rollback.CreateAll(hooks), "apply failure create");
    failures += expect(!apply_rollback.Commit(), "apply failure");
    failures += expect(
        contains(apply_failure.disabled, first) &&
            contains(apply_failure.disabled, second),
        "apply failure disables every owned target");

    const auto registry_open = reinterpret_cast<LPVOID>(0x3000);
    const auto registry_query = reinterpret_cast<LPVOID>(0x3100);
    const auto registry_close = reinterpret_cast<LPVOID>(0x3200);
    const auto network_hook = reinterpret_cast<LPVOID>(0x3300);
    const std::vector<ResolvedApiHook> combined_hooks{
        hook(registry_open),
        hook(network_hook),
        hook(registry_query),
        hook(registry_close),
    };

    FakeState combined_failure{};
    combined_failure.fail_queue_call = 2;
    g_fake = &combined_failure;
    OwnedMinHookTransaction combined_rollback(fake_api());
    failures += expect(
        combined_rollback.Initialize(),
        "combined failure init");
    failures += expect(
        combined_rollback.CreateAll(combined_hooks),
        "combined failure creates every owned hook");
    failures += expect(
        !combined_rollback.Commit(),
        "combined queue failure");
    failures += expect(
        contains(combined_failure.removed, registry_open) &&
            contains(combined_failure.removed, network_hook) &&
            contains(combined_failure.removed, registry_query) &&
            contains(combined_failure.removed, registry_close),
        "combined failure removes every network and registry target");

    failures += expect(
        !contains(create_failure.removed, unrelated) &&
            !contains(queue_failure.removed, unrelated) &&
            !contains(apply_failure.disabled, unrelated),
        "rollback never touches unrelated hook");

    return failures == 0 ? 0 : 1;
}

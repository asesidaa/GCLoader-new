#include "Audio/Asio/AsioForegroundState.h"

#include <cstdlib>
#include <iostream>
#include <semaphore>
#include <thread>

int main()
{
    gc::audio::AsioForegroundState state;
    if (state.Publish(true) !=
        gc::audio::AsioForegroundPublishResult::changed)
    {
        std::cerr << "FAIL: initial foreground publication\n";
        return EXIT_FAILURE;
    }

    std::binary_semaphore consumer_ready{0};
    std::binary_semaphore publications_complete{0};
    gc::audio::AsioForegroundSnapshot observed{};
    std::jthread consumer([&]
    {
        consumer_ready.release();
        publications_complete.acquire();
        observed = state.Read();
    });

    consumer_ready.acquire();
    const auto loss = state.Publish(false);
    const auto regain = state.Publish(true);
    publications_complete.release();
    consumer.join();

    if (loss != gc::audio::AsioForegroundPublishResult::changed ||
        regain != gc::audio::AsioForegroundPublishResult::changed ||
        !observed.is_foreground || observed.loss_generation != 1)
    {
        std::cerr << "FAIL: loss followed by regain was not preserved\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

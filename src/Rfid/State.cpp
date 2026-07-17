#include "Rfid/State.h"

namespace gc::rfid {

void State::ResetBus() noexcept
{
    assigned_address.reset();
    coins.fill(0);
}

} // namespace gc::rfid

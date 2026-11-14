// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef NBT_TEST6WB_TEMP_ENGINE_HPP
#define NBT_TEST6WB_TEMP_ENGINE_HPP
#include "cluster/hud_terminal.hpp"
namespace nbt { namespace test6wbtemp {
// Borrow the daemon's existing objects. Never opens/registers a DSI/MOST client.
bool start(hudterm::HudTerminal* terminal,most::Client& client);
void stop();
} }
#endif

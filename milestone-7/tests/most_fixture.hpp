// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// What the host tests of a MOST consumer share: bringing a most::Client up on the fake DsiClient. test_most_client spells this
// out itself, as it is what that test is about.
#ifndef MOST_FIXTURE_HPP
#define MOST_FIXTURE_HPP

#include "fake_dsi_client.hpp"
#include "most/client.hpp"

#include <assert.h>
#include <stdint.h>
#include <vector>

namespace most_fixture {

// The server's answer to the client's registration. The numbers are made up.
inline void registered(uint32_t client = 93, uint16_t device = 0x205D) {
    std::vector<uint8_t> resp;
    nbt::most::putLe32(resp, client);
    nbt::most::putLe16(resp, device);
    fake_dsi::data(nbt::most::kInfoClientResponse, &resp[0], (uint32_t)resp.size());
}

// Started, attached and registered: ready for messages.
inline void startClient(nbt::most::Client& c) {
    assert(c.start("NBTAAD_MOST"));
    fake_dsi::attach();
    registered();
}

} // namespace most_fixture

#endif

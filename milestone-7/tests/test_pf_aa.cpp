// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for tools/pf_aa.hpp (pf_aa): finding the Android Auto rules in pf.conf, appending them, NBT_KEEP_PF=1.

#include "../tools/pf_aa.hpp"
#include <assert.h>
#include <stdio.h>
#include <string>

using nbt::pfaa::Coverage;
using nbt::pfaa::EnvEdit;

// The end of the stock pf.conf (Supra T21424I = BMW Y24073J), with its CarPlay range that does not include 5277.
static const char* kStockTail =
    "# For CarPlay useCase\n"
    "pass in quick on uap0 proto tcp from 172.16.224/24 to 172.16.224.1 port {5000,5001,5010,6000,6030,6100,6200,7000,7001,7100,49512:65535}\n"
    "pass in quick on wfd0 proto tcp from <wfd> to 172.16.222.1 port 4099\n"
    "anchor filter_USB\n"
    "anchor route_USB\n";

int main() {
    // 1. Whole-word port match: 52770, 15277 and 5277x are not 5277.
    assert(nbt::pfaa::hasToken("port 5277 keep", "5277"));
    assert(nbt::pfaa::hasToken("port {5000,5277}", "5277"));
    assert(!nbt::pfaa::hasToken("port 52770", "5277"));
    assert(!nbt::pfaa::hasToken("port 15277", "5277"));
    assert(!nbt::pfaa::hasToken("NBT_5277", "5277"));

    // 2. Stock file: nothing covered; appending adds the comment and all four rules at the end, original untouched.
    std::string stock = kStockTail;
    Coverage c = nbt::pfaa::coverage(stock);
    assert(!c.uap0 && !c.wfd0);
    std::string added = nbt::pfaa::withRules(stock, c);
    assert(added.compare(0, stock.size(), stock) == 0);
    std::string tail = added.substr(stock.size());
    assert(tail == std::string(nbt::pfaa::kComment) + nbt::pfaa::kUapRules + nbt::pfaa::kWfdRules);
    Coverage after = nbt::pfaa::coverage(added);
    assert(after.uap0 && after.wfd0);
    assert(nbt::pfaa::withRules(added, after) == added);   // a second run adds nothing

    // 3. A file without a final newline gets one before the rules.
    std::string noNl = "anchor route_USB";
    std::string a2 = nbt::pfaa::withRules(noNl, nbt::pfaa::coverage(noNl));
    assert(a2.compare(0, 17, "anchor route_USB\n") == 0);

    // 4. Partly done by hand (uap0 only): only wfd0's rules are added.
    std::string half = stock + "pass quick on uap0 proto tcp from 172.16.224/24 to 172.16.224.1 port 5277 keep state\n";
    c = nbt::pfaa::coverage(half);
    assert(c.uap0 && !c.wfd0);
    std::string a3 = nbt::pfaa::withRules(half, c);
    assert(a3.find(nbt::pfaa::kWfdRules) != std::string::npos);
    assert(a3.find(nbt::pfaa::kUapRules) == std::string::npos);

    // 5. Comments do not count; a rule with no interface counts for both; a rule on another interface for neither;
    //    a block rule is not a pass rule.
    assert(!nbt::pfaa::coverage("# pass quick on uap0 port 5277\n").uap0);
    c = nbt::pfaa::coverage("pass quick proto tcp to port 5277 keep state\n");
    assert(c.uap0 && c.wfd0);
    c = nbt::pfaa::coverage("pass quick on en5 proto tcp to port 5277\n");
    assert(!c.uap0 && !c.wfd0);
    c = nbt::pfaa::coverage("block in quick on uap0 proto tcp to port 5277\n");
    assert(!c.uap0);

    // 6. nbt-aad.env: absent -> appended; =0 -> rewritten in place; =1 -> unchanged; position among settings.
    EnvEdit e = nbt::pfaa::setKeepPf("");
    assert(e.changed && e.text == "NBT_KEEP_PF=1\n" && e.position == 1 && e.settings == 1);
    e = nbt::pfaa::setKeepPf("# log\nNBT_LOG_FILE=/mnt/umass01200t7/nbt-aad.log\n\nNBT_VOL_OSD=0");
    assert(e.changed && e.position == 3 && e.settings == 3);
    assert(e.text == "# log\nNBT_LOG_FILE=/mnt/umass01200t7/nbt-aad.log\n\nNBT_VOL_OSD=0\nNBT_KEEP_PF=1\n");
    e = nbt::pfaa::setKeepPf("NBT_A=1\nNBT_KEEP_PF=0\nNBT_B=2\n");
    assert(e.changed && e.position == 2 && e.text == "NBT_A=1\nNBT_KEEP_PF=1\nNBT_B=2\n");
    e = nbt::pfaa::setKeepPf("NBT_KEEP_PF=1 \r\n# NBT_KEEP_PF=0\n");
    assert(!e.changed && e.position == 1 && e.settings == 1 && e.text == "NBT_KEEP_PF=1 \r\n# NBT_KEEP_PF=0\n");

    // 7. A 15-setting file puts the new line 16th: the caller warns (only the first 15 are used).
    std::string full;
    for (int i = 0; i < 15; ++i) { char b[32]; snprintf(b, sizeof b, "NBT_X%d=1\n", i); full += b; }
    e = nbt::pfaa::setKeepPf(full);
    assert(e.position == 16 && e.position > nbt::pfaa::kMaxEnvSettings);

    // 8. --uninstall: every NBT_KEEP_PF setting goes, comments and other settings stay; nothing to drop = unchanged.
    int removed = -1;
    std::string d = nbt::pfaa::dropKeepPf("# NBT_KEEP_PF=1 note\nNBT_A=1\nNBT_KEEP_PF=1\n  NBT_KEEP_PF=0\nNBT_B=2", &removed);
    assert(removed == 2 && d == "# NBT_KEEP_PF=1 note\nNBT_A=1\nNBT_B=2");
    d = nbt::pfaa::dropKeepPf("NBT_A=1\n", &removed);
    assert(removed == 0 && d == "NBT_A=1\n");
    // setKeepPf then dropKeepPf gives the file back.
    std::string env0 = "NBT_LOG_FILE=/mnt/umass01200t7/nbt-aad.log\n";
    assert(nbt::pfaa::dropKeepPf(nbt::pfaa::setKeepPf(env0).text, &removed) == env0 && removed == 1);

    // 9. /opt/aa/pf.bak: the rules (from pf_aa or hand-made variants) stripped again give the stock file back;
    //    the stock file itself is unchanged.
    assert(nbt::pfaa::stripAaRules(stock) == stock);
    assert(nbt::pfaa::stripAaRules(added) == stock);
    assert(nbt::pfaa::stripAaRules(half) == stock);
    std::string hand = stock + "# Android Auto wireless (NBT-AAD): TCP 5277 on the CarPlay AP, both directions\n"
                               "pass in quick on uap0 proto tcp to port {5277} \n";
    assert(nbt::pfaa::stripAaRules(hand) == stock);
    Coverage s = nbt::pfaa::coverage(nbt::pfaa::stripAaRules(hand));
    assert(!s.uap0 && !s.wfd0);
    // Not ours: the CarPlay range rule (49512:65535), a 52770 rule, an unrelated comment.
    std::string other = "# carplay\npass quick on uap0 proto tcp to port 52770\n";
    assert(nbt::pfaa::stripAaRules(other) == other);

    printf("test_pf_aa: all passed\n");
    return 0;
}

// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "../rhmiapp/home_setting.hpp"
#include <assert.h>
#include <stdlib.h>
#include <string>

int main() {
    char dir[] = "/tmp/nbt-home-setting-XXXXXX";
    assert(mkdtemp(dir));
    std::string choice = std::string(dir) + "/choice";
    std::string temp = choice + ".tmp";
    assert(!nbt::home::readShortcutSetting(choice.c_str()));
    assert(nbt::home::writeShortcutSetting(choice.c_str(), temp.c_str(), true));
    assert(nbt::home::readShortcutSetting(choice.c_str()));
    assert(access(temp.c_str(), F_OK) != 0);
    assert(nbt::home::writeShortcutSetting(choice.c_str(), temp.c_str(), false));
    assert(!nbt::home::readShortcutSetting(choice.c_str()));
    // A failed update must leave the previous value intact.
    assert(!nbt::home::writeShortcutSetting(choice.c_str(), "/missing/nbt-home/choice.tmp", true));
    assert(!nbt::home::readShortcutSetting(choice.c_str()));
    FILE* f = fopen(choice.c_str(), "w");
    assert(f);
    fputs("invalid\n", f);
    fclose(f);
    assert(!nbt::home::readShortcutSetting(choice.c_str()));
    unlink(choice.c_str());
    rmdir(dir);
    puts("test_home_setting OK");
}

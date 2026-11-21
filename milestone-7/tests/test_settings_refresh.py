#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Run the production Settings focus callback against a saved Favorites marker."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'rhmiapp/nbt_aad_app.cpp').read_text()


def function(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


show = function('void showFavouriteBar(')
focus = function('bool onSettingsFocus(')
# The old fake framework cannot record bool models. This focused stand-in runs the
# actual two functions, leaving the unrelated settings/model refreshes as no-ops.
other_refreshes = sorted(set(re.findall(r'\b(show\w+)\(g_app\)', focus)) - {'showFavouriteBar'})
stand = r'''
#include <assert.h>
#include <stdio.h>
#include <string>
#include <unistd.h>
namespace bmw {
struct PluginApp {
    int writes; bool value;
    PluginApp() : writes(0), value(false) {}
    void updateModel(unsigned id, bool v) { assert(id == 250); ++writes; value = v; }
};
struct Parcel {
    bool focused;
    explicit Parcel(bool f) : focused(f) {}
    void readBool(bool& v) const { v = focused; }
};
}
std::string marker;
namespace nbt {
std::string favouriteDisabledPath() { return marker; }
struct OverlayWatch { enum { kSettingsState = 14 }; };
}
bool fileExists(const char* p) { return access(p, F_OK) == 0; }
bool g_focusKnown = false, g_settingsFocused = false, g_colourFocused = false;
bmw::PluginApp* g_app;
const unsigned kSettingsState = 14;
void alog(const char*, ...) {}
void settingsPageEntered() {}
int nowMs() { return 0; }
struct Watch { void onOwnState(int, int) {} } g_ow;
'''
stand += '\n'.join('void %s(bmw::PluginApp*) {}' % name for name in other_refreshes)
stand += '\n' + show + '\n' + focus + r'''
int main(int argc, char** argv) {
    assert(argc == 2); marker = argv[1];
    bmw::PluginApp app; g_app = &app;
    FILE* f = fopen(marker.c_str(), "w"); assert(f); fclose(f);
    onSettingsFocus(0, bmw::Parcel(true));
    assert(app.writes == 1 && app.value);             // existing marker, no toggle needed
    assert(unlink(marker.c_str()) == 0);
    onSettingsFocus(0, bmw::Parcel(false));
    assert(app.writes == 1 && app.value);             // focus loss must not refresh
    onSettingsFocus(0, bmw::Parcel(true));
    assert(app.writes == 2 && !app.value);            // external file removal on re-entry
    f = fopen(marker.c_str(), "w"); assert(f); fclose(f);
    onSettingsFocus(0, bmw::Parcel(true));
    assert(app.writes == 3 && app.value);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='nbt-settings-refresh-') as tmp:
    tmp = Path(tmp)
    cpp, binary = tmp / 'test.cpp', tmp / 'test'
    cpp.write_text(stand)
    subprocess.run([os.environ.get('HOST_CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(tmp / 'favourite-bar.disabled')], check=True)
print('test_settings_refresh: all passed')

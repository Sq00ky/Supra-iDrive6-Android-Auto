// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "../tools/hudprobe_protocol.hpp"
#include "../tools/hudprobe_visual.hpp"
#include "../tools/hudprobe_text.hpp"
#include <assert.h>

using namespace nbt;
using namespace nbt::hudprobe;

int main() {
    const most::Msg cluster = sizeQuery(0x0101, 0, 5, kShort, 1);
    const most::Msg hud = sizeQuery(0x0102, 2, 0x1F, kWithSelector, 2);
    assert(cluster.dst == 0x0101 && cluster.fblock == 0x82 && cluster.inst == 0);
    assert(hud.dst == 0x0102 && hud.fblock == 0x82 && hud.inst == 2 && hud.hdrTag == 2);
    assert(cluster.fkt == 0xC04 && hud.fkt == 0xC04 && hud.opType == most::kOpStartResultAck);
    assert(cluster.data.size() == 2 && cluster.data[0] == 0 && cluster.data[1] == 5);
    assert(hud.data.size() == 3 && hud.data[0] == 0 && hud.data[1] == 0x1F && hud.data[2] == 1);

    unsigned handle;
    SizeForm form;
    Size size;
    uint8_t shortReply[] = {0, 5, 0, 199, 0, 129};
    uint8_t longReply[] = {0, 0x1F, 1, 0, 221, 0, 158};
    most::View m = most::View();
    m.fkt = 0xC04;
    m.op = most::kOpResultAck;
    m.data = shortReply;
    m.len = sizeof(shortReply);
    assert(sizeReply(m, handle, form, size));
    assert(handle == 5 && form == kShort && size.present() && size.width == 199 && size.height == 129);
    m.data = longReply;
    m.len = sizeof(longReply);
    assert(sizeReply(m, handle, form, size));
    assert(handle == 0x1F && form == kWithSelector && size.width == 221 && size.height == 158);
    m.op = most::kOpErrorAck;
    assert(!sizeReply(m, handle, form, size));
    m.op = most::kOpResultAck;
    longReply[2] = 2;
    assert(!sizeReply(m, handle, form, size));
    m.len = 3;
    assert(!sizeReply(m, handle, form, size));
    m.data = shortReply;
    m.len = sizeof(shortReply);
    shortReply[1] = 0x41;
    assert(!sizeReply(m, handle, form, size));
    shortReply[1] = 6;
    shortReply[3] = shortReply[5] = 0;
    assert(sizeReply(m, handle, form, size) && size.answered && !size.present());

    assert(strstr(layoutHint(true, false), "6WA"));
    assert(strstr(layoutHint(false, true), "6WB"));
    assert(strstr(layoutHint(true, true), "mixed"));
    assert(strcmp(layoutHint(false, false), "undetermined") == 0);
    VisualLayout layout;
    assert(parseVisualLayout("6wb 2 0x17 0x1F 0 0 220 157 4", layout));
    assert(layout.terminal == 2 && layout.panel == 0x17 && layout.buffer == 0x1F && layout.right == 220);
    assert(!parseVisualLayout("bad 1 1 6 0 0 199 129 4", layout));
    assert(!parseVisualLayout("bad 0 6 6 0 0 199 129 4", layout));
    assert(!parseVisualLayout("bad 0 1 6 100 0 110 129 4", layout));
    assert(!parseVisualLayout("bad 0 1 6 0 -1 199 129 4", layout));
    assert(!parseVisualLayout("bad 0 1 6 0 0 199 129 32", layout));

    const std::vector<hudterm::Call> draw = visualPattern(layout);
    assert(draw.front().fkt == hudterm::kRemoveAllItems && draw.front().handle() == layout.buffer);
    assert(draw.back().fkt == hudterm::kSetVisibility && draw.back().handle() == layout.panel);
    hudterm::HandleSet handles;
    handles.allow(layout.panel);
    handles.allow(layout.buffer);
    unsigned polygons = 0;
    for (size_t i = 0; i < draw.size(); ++i) {
        assert(hudterm::callOk(draw[i], handles));
        if (draw[i].fkt == hudterm::kAddPolygon) {
            assert(draw[i].handle() == layout.buffer);
            ++polygons;
        }
    }
    assert(polygons == 7);
    const std::vector<hudterm::Call> clear = clearVisualPattern(layout, true);
    assert(clear.size() == 2 && clear[0].handle() == layout.panel && clear[0].payload[2] == 0);
    assert(clear[1].fkt == hudterm::kRemoveAllItems && clear[1].handle() == layout.buffer);
    assert(clearVisualPattern(layout, false).size() == 1);
    assert(parseVisualLayout("f15-swap 0 1 5 35 20 136 110 4 4", layout));
    const std::vector<hudterm::Call> swap = visualPattern(layout);
    assert(swap[swap.size() - 2].fkt == hudterm::kSwitchVisibility);
    assert(swap[swap.size() - 2].payload == hudterm::switchVisibility(4, 5));
    assert(clearVisualPattern(layout, true).size() == 3);
    assert(!parseVisualLayout("bad 0 1 5 35 20 136 110 4 5", layout));
    assert(visualAnswer("full") && visualAnswer("none") && visualAnswer("rt") && visualAnswer("q"));
    assert(!visualAnswer("") && !visualAnswer("other"));
    most::Msg label;
    assert(textProbeMessage(0x0101, 0x0C, kUtf16, "22", 7, label));
    const uint8_t wideLabel[] = {0, 0x0C, 0, 0, '2', 0, '2', 0, 0};
    assert(label.data == std::vector<uint8_t>(wideLabel, wideLabel + sizeof(wideLabel)));
    assert(factoryTextKind(&label.data[0], label.data.size()) == kDistanceText);
    assert(label.inst == 0 && label.fkt == hudterm::kTextareaSetText && label.hdrTag == 7);
    assert(textProbeMessage(0x0101, 0x0D, kLatin9, "km", 8, label));
    const uint8_t narrowLabel[] = {0, 0x0D, 1, 'k', 'm', 0};
    assert(label.data == std::vector<uint8_t>(narrowLabel, narrowLabel + sizeof(narrowLabel)));
    assert(factoryTextKind(&label.data[0], label.data.size()) == kUnitText);
    assert(textProbeMessage(0x0101, 0x10, kUtf8, "MAIN RD", 9, label));
    assert(label.data[2] == 2 && factoryTextKind(&label.data[0], label.data.size()) == kStreetText);
    assert(textProbeMessage(0x0101, 0x10, kUtf8, "", 10, label));
    assert(label.data.size() == 4 && label.data[3] == 0 && factoryTextKind(&label.data[0], label.data.size()) == kUnknownText);
    assert(!textProbeMessage(0x2021, 0x10, kUtf16, "22", 11, label));
    assert(!textProbeMessage(0x0101, 0x10, kUtf8, "TOO LONG!", 12, label));
    return 0;
}

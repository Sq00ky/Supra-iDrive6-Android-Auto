// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the nav prompt focus logic of SpeechOut (audio/speech_focus.hpp), against a port that records every side
// effect in order.
#include "audio/speech_focus.hpp"
#include <stdio.h>
#include <string.h>
#include <string>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// The port calls since the last check, e.g. "feed- build busy@1000".
#define CHECK_CALLS(p, want) do { const std::string got_ = (p).take(); \
    if (got_ != (want)) { printf("FAIL %s:%d calls \"%s\", expected \"%s\"\n", __FILE__, __LINE__, got_.c_str(), \
                                 (want)); ++fails; } } while (0)

#define CHECK_TEXT(got, want) do { const char* g_ = (got); const char* w_ = (want); \
    if ((g_ == NULL) != (w_ == NULL) || (g_ && strcmp(g_, w_) != 0)) { printf("FAIL %s:%d text \"%s\", expected \"%s\"\n", \
        __FILE__, __LINE__, g_ ? g_ : "(null)", w_ ? w_ : "(null)"); ++fails; } } while (0)

namespace {

class FakePort : public SpeechFocusPort {
public:
    FakePort() : build_ok(true), remove_ok(true), queued(0) {}

    bool queueBuild() { add("build"); return build_ok; }
    bool queueRemove() { add("remove"); return remove_ok; }
    void cancelPending() { add("cancel"); }
    void setFeed(bool on) { add(on ? "feed+" : "feed-"); }
    void flush() { add("flush"); queued = 0; }
    size_t queuedBytes() const { return queued; }
    void setFocusBusy(bool busy, uint64_t now) {
        char b[32];
        snprintf(b, sizeof(b), "%s@%llu", busy ? "busy" : "free", (unsigned long long)now);
        add(b);
    }

    std::string take() { const std::string s = calls_; calls_.clear(); return s; }

    bool build_ok;
    bool remove_ok;
    size_t queued;   // the ring: frames write to it, the endpoint plays it out

private:
    void add(const char* c) { if (!calls_.empty()) calls_ += ' '; calls_ += c; }
    std::string calls_;
};

const char* const kRequesting = "requesting the nav prompt focus";

// A focus built at 1000 and granted at 1300, prompt still running.
void routed(SpeechFocus& f, FakePort& p) {
    CHECK_TEXT(f.promptStarted(1000), kRequesting);
    CHECK(f.granted(1300) == NavPromptPolicy::BUILDING);
    p.take();
}

// routed(), then the prompt ends and its audio has played out: released at 1801, remove queued.
void released(SpeechFocus& f, FakePort& p) {
    routed(f, p);
    f.promptStopped();
    p.queued = 0;
    CHECK(f.tick(1801).action == NavPromptPolicy::RELEASE);
    CHECK(f.state() == NavPromptPolicy::RELEASING);
    p.take();
}

// A build at 1000 that timed out at 3001 (backoff until 33001), its remove done at 3500.
void timedOut(SpeechFocus& f, FakePort& p) {
    CHECK_TEXT(f.promptStarted(1000), kRequesting);
    CHECK(f.tick(3001).action == NavPromptPolicy::GRANT_TIMEOUT);
    f.removeDone(3500, true);
    f.promptStopped();
    CHECK(f.state() == NavPromptPolicy::IDLE);
    p.take();
}

} // namespace

int main() {
    const NavPromptPolicy::Config defaults;

    // 1. Normal prompt: build, grant, hold 500 ms after the ring played out, remove.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        CHECK(f.state() == NavPromptPolicy::IDLE);
        CHECK(!f.playsFrames());
        CHECK(!f.active());
        CHECK(f.takesPrompt(1000));
        CHECK_TEXT(f.promptStarted(1000), kRequesting);
        CHECK_CALLS(p, "feed- build busy@1000");
        CHECK(f.state() == NavPromptPolicy::BUILDING);
        CHECK(f.playsFrames());   // into the ring, the feed waits for the grant
        CHECK(f.active());
        p.queued = 4000;

        SpeechFocus::Tick t = f.tick(1100);
        CHECK(t.action == NavPromptPolicy::NONE);
        CHECK(t.rebuild == NULL);
        CHECK_CALLS(p, "busy@1100");

        CHECK(f.granted(1300) == NavPromptPolicy::BUILDING);
        CHECK_CALLS(p, "feed+ free@1300");
        CHECK(f.state() == NavPromptPolicy::ROUTED);
        CHECK(f.grantAfterMs() == 300);
        CHECK(f.playsFrames());

        f.promptStopped();
        CHECK_CALLS(p, "");
        CHECK(f.active());   // its audio is still queued
        p.queued = 0;
        CHECK(!f.active());

        // The hold counts from the grant (the last busy time), not from the prompt stop.
        CHECK(f.tick(1800).action == NavPromptPolicy::NONE);
        CHECK_CALLS(p, "free@1800");
        t = f.tick(1801);
        CHECK(t.action == NavPromptPolicy::RELEASE);
        CHECK(t.idle_ms == 501);
        CHECK(f.grantAfterMs() == 300);
        CHECK_CALLS(p, "feed- remove busy@1801");
        CHECK(f.state() == NavPromptPolicy::RELEASING);
        CHECK(!f.playsFrames());
        CHECK(f.takesPrompt(1801));

        f.removeDone(1900, true);
        CHECK_CALLS(p, "free@1900");
        CHECK(f.state() == NavPromptPolicy::IDLE);

        // Nothing held any more: the session end only flushes.
        CHECK(!f.sessionEnded(2000));
        CHECK_CALLS(p, "flush free@2000");
    }

    // 1b. A second START while the build is pending, and hold_ms from the config.
    {
        FakePort p;
        SpeechFocus f(p);
        NavPromptPolicy::Config c;
        c.hold_ms = 1200;
        f.configure(c, true);
        CHECK_TEXT(f.promptStarted(1000), kRequesting);
        p.take();
        f.promptStopped();
        CHECK_TEXT(f.promptStarted(1100), "focus request still pending");
        CHECK_CALLS(p, "busy@1100");
        f.granted(1300);
        f.promptStopped();
        p.take();
        CHECK(f.tick(2500).action == NavPromptPolicy::NONE);
        CHECK(f.tick(2501).action == NavPromptPolicy::RELEASE);
        CHECK_CALLS(p, "free@2500 feed- remove busy@2501");
    }

    // 2. A new prompt within the hold: no second build, it plays at once on the held focus.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        routed(f, p);
        f.promptStopped();
        CHECK(f.tick(1600).action == NavPromptPolicy::NONE);
        p.take();
        CHECK_TEXT(f.promptStarted(1700), "focus still held");
        CHECK_CALLS(p, "free@1700");
        CHECK(f.playsFrames());
        f.promptStopped();
        CHECK(f.tick(2200).action == NavPromptPolicy::NONE);
        SpeechFocus::Tick t = f.tick(2201);
        CHECK(t.action == NavPromptPolicy::RELEASE);
        CHECK(t.idle_ms == 501);
        CHECK_CALLS(p, "free@2200 feed- remove busy@2201");
    }

    // 3. A new prompt while RELEASING: its audio waits in the ring, the next tick after the remove builds again.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        released(f, p);
        CHECK_TEXT(f.promptStarted(1850), "waiting for the focus release");
        CHECK_CALLS(p, "busy@1850");
        CHECK(f.playsFrames());
        CHECK(f.active());
        p.queued = 3000;

        CHECK(f.tick(1900).action == NavPromptPolicy::NONE);
        CHECK_CALLS(p, "busy@1900");

        f.removeDone(1950, true);
        CHECK_CALLS(p, "free@1950");   // no build in the completion itself
        CHECK(f.state() == NavPromptPolicy::IDLE);
        CHECK(f.playsFrames());

        SpeechFocus::Tick t = f.tick(2000);
        CHECK(t.action == NavPromptPolicy::NONE);
        CHECK_TEXT(t.rebuild, kRequesting);
        CHECK_CALLS(p, "feed- build busy@2000");
        CHECK(f.state() == NavPromptPolicy::BUILDING);

        CHECK(f.granted(2300) == NavPromptPolicy::BUILDING);
        CHECK(f.grantAfterMs() == 300);
        CHECK_CALLS(p, "feed+ free@2300");
    }

    // 3b. The waiting prompt is over and its ring empty when the release is done: dropped, no build.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        released(f, p);
        CHECK_TEXT(f.promptStarted(1850), "waiting for the focus release");
        f.promptStopped();
        f.removeDone(1950, true);
        p.take();
        SpeechFocus::Tick t = f.tick(2000);
        CHECK(t.rebuild == NULL);
        CHECK_CALLS(p, "flush free@2000");
        CHECK(f.state() == NavPromptPolicy::IDLE);
        CHECK(!f.playsFrames());
    }

    // 4. Grant timeout: drop the audio, remove, no focus requests for 30 s.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        CHECK_TEXT(f.promptStarted(1000), kRequesting);
        p.take();
        p.queued = 5000;
        SpeechFocus::Tick t = f.tick(3000);
        CHECK(t.action == NavPromptPolicy::NONE);
        CHECK_CALLS(p, "busy@3000");
        t = f.tick(3001);
        CHECK(t.action == NavPromptPolicy::GRANT_TIMEOUT);
        CHECK(t.waited_ms == 2001);
        CHECK_CALLS(p, "flush feed- remove busy@3001");
        CHECK(f.state() == NavPromptPolicy::RELEASING);
        CHECK(!f.playsFrames());
        CHECK(!f.takesPrompt(3001));
        CHECK(!f.takesPrompt(33000));
        CHECK(f.takesPrompt(33001));

        f.removeDone(3500, true);
        CHECK_CALLS(p, "free@3500");
        f.promptStopped();

        // BT mode starts every prompt; during the backoff it is dropped.
        CHECK_TEXT(f.promptStarted(4000), "focus backing off: dropped");
        CHECK_CALLS(p, "free@4000");
        CHECK(!f.playsFrames());
        CHECK(!f.active());
        f.promptStopped();
        CHECK_TEXT(f.promptStarted(33001), kRequesting);
        CHECK_CALLS(p, "feed- build busy@33001");
    }

    // 4b. A prompt that starts while the timed-out focus is RELEASING, inside the backoff: dropped.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        CHECK_TEXT(f.promptStarted(1000), kRequesting);
        f.tick(3001);
        f.promptStopped();
        p.take();
        CHECK_TEXT(f.promptStarted(3100), "focus backing off: dropped");
        CHECK_CALLS(p, "busy@3100");
        CHECK(!f.playsFrames());
    }

    // 5. A grant after the timeout: removed again at once.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        CHECK_TEXT(f.promptStarted(1000), kRequesting);
        f.tick(3001);
        p.take();
        CHECK(f.granted(3200) == NavPromptPolicy::RELEASING);
        CHECK_CALLS(p, "remove busy@3200");
        CHECK(!f.playsFrames());
        f.removeDone(3300, true);
        CHECK_CALLS(p, "busy@3300");   // the grant's remove is still out
        CHECK(f.state() == NavPromptPolicy::RELEASING);
        f.removeDone(3400, true);
        CHECK_CALLS(p, "free@3400");
        CHECK(f.state() == NavPromptPolicy::IDLE);
    }

    // 5b. A grant after the timed-out release is done.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        timedOut(f, p);
        CHECK(f.granted(3600) == NavPromptPolicy::IDLE);
        CHECK_CALLS(p, "remove busy@3600");
        CHECK(f.state() == NavPromptPolicy::IDLE);
        f.removeDone(3700, true);
        CHECK_CALLS(p, "free@3700");
    }

    // 6. A remove that did not run (cancelled, no slot): the session end removes again, once.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        released(f, p);
        f.removeDone(1900, false);
        CHECK_CALLS(p, "free@1900");
        CHECK(f.state() == NavPromptPolicy::IDLE);
        CHECK(f.sessionEnded(2000));
        CHECK_CALLS(p, "remove flush busy@2000");
        f.removeDone(2100, true);
        CHECK_CALLS(p, "free@2100");
        CHECK(!f.sessionEnded(3000));
        CHECK_CALLS(p, "flush free@3000");
    }

    // 7. Session end mid-prompt on the held focus.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        routed(f, p);
        p.queued = 2000;
        CHECK(f.sessionEnded(1500));
        CHECK_CALLS(p, "feed- remove flush busy@1500");
        CHECK(f.state() == NavPromptPolicy::RELEASING);
        CHECK(!f.active());
        CHECK(f.tick(1600).action == NavPromptPolicy::NONE);   // the prompt counts as stopped
        CHECK_CALLS(p, "busy@1600");
        f.removeDone(1700, true);
        CHECK_CALLS(p, "free@1700");
    }

    // 7b. Session end while BUILDING; the grant comes after it and is removed again.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        CHECK_TEXT(f.promptStarted(1000), kRequesting);
        p.take();
        CHECK(f.sessionEnded(1100));
        CHECK_CALLS(p, "feed- remove flush busy@1100");
        CHECK(f.granted(1300) == NavPromptPolicy::RELEASING);
        CHECK_CALLS(p, "remove busy@1300");
        f.removeDone(1400, true);
        f.removeDone(1500, true);
        CHECK_CALLS(p, "busy@1400 free@1500");
        CHECK(f.state() == NavPromptPolicy::IDLE);
    }

    // 7c. Session end while RELEASING with a waiting prompt: the release's remove may be cancelled by the route's
    // cancelPending() before it, so one more; the waiting audio is dropped, no build afterwards.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        released(f, p);
        CHECK_TEXT(f.promptStarted(1850), "waiting for the focus release");
        p.queued = 3000;
        p.take();
        CHECK(f.sessionEnded(1900));
        CHECK_CALLS(p, "remove flush busy@1900");
        f.removeDone(1950, false);
        CHECK(f.state() == NavPromptPolicy::RELEASING);
        f.removeDone(1960, true);
        CHECK(f.state() == NavPromptPolicy::IDLE);
        CHECK_CALLS(p, "busy@1950 free@1960");
        CHECK(f.tick(2000).rebuild == NULL);
        CHECK_CALLS(p, "free@2000");
        CHECK(!f.playsFrames());
    }

    // 7d. The session end clears the backoff.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        timedOut(f, p);
        CHECK(!f.takesPrompt(4000));
        CHECK(!f.sessionEnded(4000));
        CHECK_CALLS(p, "flush free@4000");
        CHECK(f.takesPrompt(4000));
    }

    // 8. Shutdown with nothing held: no remove, no wait.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        CHECK(!f.beginStop(1000));
        CHECK(!f.stopStep());
        const SpeechFocus::Stop s = f.endStop(1010);
        CHECK(!s.sent);
        CHECK(!s.held);
        CHECK_CALLS(p, "free@1010");
    }

    // 8b. Shutdown on the held focus: one remove, then it waits for it as long as it is out.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        routed(f, p);
        CHECK(!f.beginStop(1500));
        CHECK_CALLS(p, "feed- remove");
        for (int i = 0; i < 200; ++i) CHECK(f.stopStep());   // the 2 s limit is the caller's
        CHECK_CALLS(p, "");
        f.removeDone(1600, true);
        CHECK_CALLS(p, "free@1600");
        CHECK(!f.stopStep());
        const SpeechFocus::Stop s = f.endStop(1620);
        CHECK(s.sent);
        CHECK(!s.held);
        CHECK_CALLS(p, "free@1620");
    }

    // 8c. The caller's limit ran out with the remove still out.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        routed(f, p);
        f.beginStop(1500);
        CHECK(f.stopStep());
        p.take();
        const SpeechFocus::Stop s = f.endStop(3500);
        CHECK(s.sent);
        CHECK(s.held);
        CHECK_CALLS(p, "busy@3500");
    }

    // 8d. Shutdown while BUILDING: cancel the build, the release is the one remove.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        CHECK_TEXT(f.promptStarted(1000), kRequesting);
        p.take();
        CHECK(f.beginStop(1500));
        CHECK_CALLS(p, "cancel feed- remove");
        CHECK(f.stopStep());
        f.removeDone(1600, true);
        CHECK(!f.stopStep());
        CHECK_CALLS(p, "free@1600");
        const SpeechFocus::Stop s = f.endStop(1620);
        CHECK(s.sent);
        CHECK(!s.held);
    }

    // 8e. Shutdown with the timed-out build never granted (IDLE): cancel it, exactly one extra remove.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        timedOut(f, p);
        CHECK(f.beginStop(4000));
        CHECK_CALLS(p, "cancel");
        CHECK(f.stopStep());
        CHECK_CALLS(p, "remove");
        CHECK(f.stopStep());
        f.removeDone(4100, true);
        CHECK(!f.stopStep());
        CHECK_CALLS(p, "free@4100");
        const SpeechFocus::Stop s = f.endStop(4120);
        CHECK(s.sent);
        CHECK(!s.held);
        CHECK_CALLS(p, "free@4120");
    }

    // 8f. Shutdown with a held focus whose remove did not run: one more remove; if that one does not run either,
    // it reports the focus as possibly held.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        released(f, p);
        f.removeDone(1900, false);
        p.take();
        CHECK(!f.beginStop(2000));
        CHECK_CALLS(p, "");
        CHECK(f.stopStep());
        CHECK_CALLS(p, "remove");
        f.removeDone(2100, false);
        CHECK(!f.stopStep());
        CHECK_CALLS(p, "free@2100");
        const SpeechFocus::Stop s = f.endStop(2120);
        CHECK(s.sent);
        CHECK(s.held);
    }

    // 8g. Shutdown waits for the session end's remove, then needs none of its own.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        routed(f, p);
        f.sessionEnded(1500);
        p.take();
        CHECK(!f.beginStop(1600));
        CHECK_CALLS(p, "");
        CHECK(f.stopStep());
        f.removeDone(1700, true);
        CHECK(!f.stopStep());
        const SpeechFocus::Stop s = f.endStop(1720);
        CHECK(!s.sent);
        CHECK(!s.held);
        CHECK_CALLS(p, "free@1700 free@1720");
    }

    // 9. No focus in use (NBT_SPEECH_SRC=0 or audioconn not found): never takes a browser prompt, BT plays at once.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, false);
        CHECK(!f.takesPrompt(0));
        CHECK(!f.takesPrompt(100000));
        CHECK(f.playsFrames());
        CHECK(!f.active());
        CHECK_TEXT(f.promptStarted(1000), NULL);
        CHECK_CALLS(p, "feed+");
        CHECK(f.playsFrames());
        CHECK(f.active());
        f.promptStopped();
        p.queued = 100;
        CHECK(f.active());
        p.queued = 0;
        CHECK(!f.active());
        CHECK(f.tick(5000).action == NavPromptPolicy::NONE);
        CHECK_CALLS(p, "free@5000");
        CHECK(!f.sessionEnded(6000));
        CHECK_CALLS(p, "flush free@6000");
        CHECK(!f.beginStop(7000));
        CHECK(!f.stopStep());
        const SpeechFocus::Stop s = f.endStop(7000);
        CHECK(!s.sent);
        CHECK(!s.held);
        CHECK_CALLS(p, "free@7000");
    }

    // 10. The build could not be queued: dropped, no backoff.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        p.build_ok = false;
        CHECK_TEXT(f.promptStarted(1000), "could not queue the focus request: dropped");
        CHECK_CALLS(p, "feed- build flush free@1000");
        CHECK(f.state() == NavPromptPolicy::IDLE);
        CHECK(!f.playsFrames());
        CHECK(f.takesPrompt(1001));
        CHECK(!f.beginStop(2000));   // no build pending
        CHECK_CALLS(p, "");
    }

    // 11. The remove could not be queued: released at once; the focus counts as held, so the session end tries again.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        routed(f, p);
        f.promptStopped();
        p.remove_ok = false;
        CHECK(f.tick(1801).action == NavPromptPolicy::RELEASE);
        CHECK_CALLS(p, "feed- remove free@1801");
        CHECK(f.state() == NavPromptPolicy::IDLE);
        CHECK(!f.sessionEnded(2000));
        CHECK_CALLS(p, "remove flush free@2000");
    }

    // 12. Release timeout: going on without the remove, which stays counted, so it cannot end a newer release.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        released(f, p);
        CHECK(f.tick(6801).action == NavPromptPolicy::NONE);
        CHECK(f.tick(6802).action == NavPromptPolicy::RELEASE_TIMEOUT);
        CHECK_CALLS(p, "busy@6801 busy@6802");   // still in flight for the volume pill
        CHECK(f.state() == NavPromptPolicy::IDLE);

        CHECK_TEXT(f.promptStarted(7000), kRequesting);
        CHECK(f.granted(7100) == NavPromptPolicy::BUILDING);
        f.promptStopped();
        CHECK_CALLS(p, "feed- build busy@7000 feed+ busy@7100");
        CHECK(f.tick(7601).action == NavPromptPolicy::RELEASE);
        CHECK_CALLS(p, "feed- remove busy@7601");

        f.removeDone(7700, true);   // the old one
        CHECK(f.state() == NavPromptPolicy::RELEASING);
        CHECK_CALLS(p, "busy@7700");
        f.removeDone(7800, true);
        CHECK(f.state() == NavPromptPolicy::IDLE);
        CHECK_CALLS(p, "free@7800");
    }

    // 12b. Release timeout with a waiting prompt: the same tick goes IDLE and builds for it.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        released(f, p);
        CHECK_TEXT(f.promptStarted(1850), "waiting for the focus release");
        p.take();
        SpeechFocus::Tick t = f.tick(6801);
        CHECK(t.action == NavPromptPolicy::NONE);
        CHECK(t.rebuild == NULL);
        CHECK_CALLS(p, "busy@6801");
        t = f.tick(6802);
        CHECK(t.action == NavPromptPolicy::RELEASE_TIMEOUT);
        CHECK_TEXT(t.rebuild, kRequesting);
        CHECK_CALLS(p, "feed- build busy@6802");
        CHECK(f.state() == NavPromptPolicy::BUILDING);

        CHECK(f.granted(6900) == NavPromptPolicy::BUILDING);
        CHECK_CALLS(p, "feed+ busy@6900");   // the timed-out remove is still out
        f.removeDone(7000, true);
        CHECK(f.state() == NavPromptPolicy::ROUTED);
        CHECK_CALLS(p, "free@7000");
    }

    // 13. A grant while ROUTED: nothing queued.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        routed(f, p);
        CHECK(f.granted(1400) == NavPromptPolicy::ROUTED);
        CHECK_CALLS(p, "free@1400");
        CHECK(f.state() == NavPromptPolicy::ROUTED);
        CHECK(f.grantAfterMs() == 300);
    }

    // 14. Grant timeout whose remove cannot be queued: released at once, the backoff still applies.
    {
        FakePort p;
        SpeechFocus f(p);
        f.configure(defaults, true);
        CHECK_TEXT(f.promptStarted(1000), kRequesting);
        p.take();
        p.remove_ok = false;
        CHECK(f.tick(3001).action == NavPromptPolicy::GRANT_TIMEOUT);
        CHECK_CALLS(p, "flush feed- remove free@3001");
        CHECK(f.state() == NavPromptPolicy::IDLE);
        CHECK(!f.takesPrompt(33000));
        CHECK(f.takesPrompt(33001));
        f.promptStopped();
        CHECK_TEXT(f.promptStarted(4000), "focus backing off: dropped");
        CHECK_CALLS(p, "free@4000");
    }

    if (fails) { printf("test_speech_focus: %d failure(s)\n", fails); return 1; }
    printf("test_speech_focus OK\n");
    return 0;
}

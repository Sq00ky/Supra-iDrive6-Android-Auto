// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// libNbtAad.so - NBT-AAD as a native iDrive "PluginApp" (same framework as BMW's SecurityMenu / ActiveSeating).
//
// HmiMain3D's CAppManager loads every <ro|rw>/Plugins/<Name>/<Name>.manifest at boot (no coding whitelist; the
// only gate is CSpecialAppsHelper = coding A4A / A4A_REMOTE_APPS / REMOTE_HMI / TOUCH_COMMAND). The manifest names
// this library, the UI description (NbtAad.xml) and the text/image DBs. The XML declares one entry button; its
// placement comes from the XML (applicationType="Custom" + insertStateID/insertAfterFocusID, like ActiveSeating),
// so iDrive itself creates and places the menu row. Pressing it opens our own menu (hmiState 10, pure XML hmiAction):
//   Start Android Auto  (raAction 21) - launches /opt/aa/nbt-aad, or brings a running one to the front ("show");
//                                       the row reads "Connecting..." / "Open Android Auto" with the daemon state
//   Force Stop          (raAction 22) - "quit", then SIGTERM after 3 s, SIGKILL after 6 s
//   Restart             (raAction 23) - Force Stop, then Start
// When the phone connects after Start, the app sends iDrive back to the main menu (focusEvent 50 -> NativeStateID 1,
// the way BMW's ActiveSeating leaves its own screen), so stray knob presses under the Android Auto overlay land
// there and not on Force Stop / Restart. Those two rows are also ignored while the overlay is up.
// The old one-press entry (raAction 2 -> start) is still handled, for NbtAad_Direct.xml.
//
// ABI (from libSecurityMenu.so / libAppFramework.A.so, see plugin/plugin_abi.hpp notes):
//   pluginAppInfo = { u16 1, u16 0, name, vendor, u32 rhmiVersion 1, create, destroy }
//   create(IDispatcher&, const char* name, const Bundle& params, const Bundle& config) -> IPluginApp*
//   bmw::PluginApp(IDispatcher&, BasicString<char>, 1, 0, RHMIInterfaceVersion 1, params, config)
//   PluginApp vtable (34 slots, no pure virtuals): 21 onError, 22 onInit, 23 onFinalize, 25 onCheckAppAvailable
//   PluginApp::addHandler(actionId, Delegate<bool, const Parcel&>{obj, stub})
// Our app object is a bmw::PluginApp built with the framework's own constructor; it gets its own copy of the
// PluginApp vtable with onInit/onError/onFinalize replaced (what a C++ subclass would do - we have no BMW headers).
//
// Logs (rhmiapp-10): slog major 0x4e41 always. Nothing is kept in RAM: with the "Save logs to USB" checkbox on, app
// lines go to <stick>/nbt-aad-app.log and the daemon writes <stick>/nbt-aad.log itself (src/usb_log.hpp); the
// daemon's stdout from here is /dev/null.
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <new>
#include <sys/stat.h>
#include <spawn.h>
#include <sys/wait.h>
#include <dlfcn.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void*)-2)   // QNX value
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "../src/usb_log.hpp"
#include "../src/reconnect_policy.hpp"
#include "cds_parse.hpp"
#ifdef __QNX__
#include <sys/slog.h>
#include <sys/slogcodes.h>
#endif

#define NBT_AAD_APP_VERSION "rhmiapp-18-warm-reconnect5 (" __DATE__ " " __TIME__ ")"

extern char** environ;

// ---- minimal declarations of BMW's RHMI app framework (libAppFramework.A.so / libCoreFramework.A.so) ----------
// Only names and parameter types matter (they produce the exported, mangled symbols); nothing here is defined.
namespace bmw {
class IDispatcher;
class Bundle;
class Runnable;
class Parcel {
public:
    explicit Parcel(bool value);             // _ZN3bmw6ParcelC1Eb (libCoreFramework), 0x14 bytes, polymorphic
    ~Parcel();                               // _ZN3bmw6ParcelD1Ev
    void readBool(bool& value) const;        // _ZNK3bmw6Parcel8readBoolERb (SecurityMenu_Base::onFocus)
private:
    unsigned char m_storage[32];             // >= sizeof(bmw::Parcel) (0x14)
};
enum RHMIInterfaceVersion { RHMI_INTERFACE_VERSION_1 = 1 };

template <typename T> class BasicString {
public:
    BasicString(const T* s);                 // _ZN3bmw11BasicStringIcEC1EPKc
    BasicString(const BasicString& other);   // _ZN3bmw11BasicStringIcEC1ERKS1_
    ~BasicString();                          // _ZN3bmw11BasicStringIcED1Ev
    const T* raw() const { return m_data; }  // data pointer is the only member (see SecurityMenu)
private:
    T* m_data;
};

// Text-DB reference (libCoreFramework: ID::ID(id, 0x28) -> { u32 id, u32 type }, trivially destructible).
class TextID {
public:
    explicit TextID(unsigned id);            // _ZN3bmw6TextIDC1Ej
private:
    unsigned m_id;
    unsigned m_type;
};

// Image-DB reference ({u32 id, u32 type 0x2b}); updateModel(imageIdModel, ImageID(n)) re-points an icon.
class ImageID {
public:
    explicit ImageID(unsigned id);           // _ZN3bmw7ImageIDC1Ej
private:
    unsigned m_id;
    unsigned m_type;
};

// ---- list-model table (raListModel): libCoreFramework DataTableBase over a Map<unsigned, Variant> -----------------
// Layouts from libActiveSeating (startWidgetInit, Map::newAssoc, the ObjectContainer<TextID> vtable at 0x21408):
//   Variant          = one pointer to an IObjectContainer (deleted through vtable slot 1 by ~Variant)
//   IObjectContainer = vtable { ~D1, ~D0, type() const, clone() const }; ObjectContainer<TextID>: type() = 0x28
//   Map<unsigned,Variant> = { assoc** table, u32 hashSize 17, u32 count, assoc* free, MemoryBlock* blocks, u32 blockSize 10 }
//   DataTableBase(Map*, columns) = 8 bytes; cell (row, col) is map key row * columns + col
class Variant {
public:
    class IObjectContainer {
    public:
        virtual ~IObjectContainer() {}
        virtual int type() const = 0;
        virtual IObjectContainer* clone() const = 0;
    };
    template <typename T> class ObjectContainer : public IObjectContainer {
    public:
        explicit ObjectContainer(const T& v) : m_value(v) {}
        virtual int type() const;
        virtual IObjectContainer* clone() const { return new ObjectContainer<T>(m_value); }
    private:
        T m_value;
    };
    ~Variant();                              // _ZN3bmw7VariantD1Ev (deletes the container)
    IObjectContainer* m_obj;
};
template <> inline int Variant::ObjectContainer<TextID>::type() const { return 0x28; }

template <typename K, typename V> class Map {
public:
    Map() : m_table(0), m_hashSize(17), m_count(0), m_free(0), m_blocks(0), m_blockSize(10) {}
    ~Map();                                  // _ZN3bmw3MapIjNS_7VariantEED1Ev
    V& operator[](const K& key);             // _ZN3bmw3MapIjNS_7VariantEEixERKj
private:
    void* m_table;
    unsigned m_hashSize, m_count;
    void* m_free;
    void* m_blocks;
    unsigned m_blockSize;
};

class DataTableBase {
public:
    DataTableBase(Map<unsigned, Variant>* cells, unsigned columns); // _ZN3bmw13DataTableBaseC1EPNS_3MapIjNS_7VariantEEEj
private:
    unsigned char m_storage[16];             // 8 used (ActiveSeating keeps it in 8 bytes of stack)
};

template <typename R, typename A1, typename A2, typename A3, typename A4> struct Delegate {
    void* object;
    R (*stub)(void* object, A1 a1);
};
typedef Delegate<bool, const Parcel&, void, void, void> ActionDelegate;
typedef Delegate<bool, const BasicString<unsigned short>&, void, void, void> CdsDelegate;

const size_t kPluginAppStorage = 0x200;      // SecurityMenu's own members start at 0x160

class PluginApp {
public:
    PluginApp(IDispatcher& dispatcher, BasicString<char> name, unsigned major, unsigned minor,
              RHMIInterfaceVersion rhmi, const Bundle& params, const Bundle& config);
    void addHandler(unsigned actionId, const ActionDelegate& handler);
    // Notification handler for one of our hmiStates (type 1 = focus; the Parcel carries one bool "focused"):
    // SecurityMenu_Kommunikation::onInit registers addHandler(95 /*its hmiState*/, 1, ...).
    void addHandler(unsigned id, unsigned type, const ActionDelegate& handler);   // _ZN3bmw9PluginApp10addHandlerEjjRK...
    // Fires one of our XML <events>; a focusEvent whose targetModel is a NativeStateID raIntModel makes HmiMain3D
    // request that native state (ActiveSeating::onExitToSeatMenu: triggerEvent(23) -> NativeStateID 1789).
    void triggerEvent(unsigned eventId);                                          // _ZN3bmw9PluginApp12triggerEventEj
    void post(Runnable* runnable, unsigned delayMs);   // runs runnable->run() on the app thread after delayMs
    // Re-points a model at another text of our text DB. BMW's ActiveSeating sets its entry button text this way
    // (onInit: TextID(0x54) -> updateModel(2, ...)); we use it for the entry's "Connecting..." state.
    void updateModel(unsigned modelId, const TextID& text);   // _ZN3bmw9PluginApp11updateModelEjRKNS_6TextIDE
    void updateModel(unsigned modelId, const ImageID& image); // _ZN3bmw9PluginApp11updateModelEjRKNS_7ImageIDE
    void updateModel(unsigned modelId, bool value);           // _ZN3bmw9PluginApp11updateModelEjb (raBoolModel: checkbox)
    void updateModel(unsigned modelId, const DataTableBase& table); // _ZN3bmw9PluginApp11updateModelEjRKNS_13DataTableBaseE
    // Component property: 1 enabled, 2 selectable, 3 visible, 8 LABEL_WAITINGANIMATION (iDrive's busy ring while any
    // component of the state has it set; HmiMain3D component -> state -> activity waitingAnimation).
    void setProperty(unsigned componentId, unsigned propertyId, const Parcel& value);   // _ZN3bmw9PluginApp11setPropertyEjjRKNS_6ParcelE
    // Car data (same call SecurityMenu uses for "specialvehicle.menucontrol"); value arrives as UTF-16 JSON text.
    void cdsAddPropertyChangedEventHandler(BasicString<char>& property, unsigned id, const CdsDelegate& handler);
    bool onInit();                           // base implementations, called from our overrides
    void onFinalize();
private:
    unsigned char m_storage[kPluginAppStorage];
};
} // namespace bmw

namespace {

const char* const kDaemon     = "/opt/aa/nbt-aad";
const char* const kDisable    = "/opt/aa/app.disable";
const char* const kDaemonEnv  = "/opt/aa/nbt-aad.env";
const char* const kReconnectProfile = "/var/opt/aa/reconnect-profile";
const char* const kDaemonLog  = "/dev/null";               // the daemon opens its own USB log (usb_log.hpp)
const unsigned    kEntryAction = 2;          // <entryButton action="2"> / <raAction id="2"/> in NbtAad.xml
const char* const kGearProperty = "driving.gear";   // CDS property (libhmiremoting.so property table)
const char* const kGearFile    = "/dev/shmem/nbt-aad-gear";
// Park Distance Control / park assist state (libhmiremoting: CAN ParkingFunctions PDCStatus, published as
// {"PDCStatus":%d}). It changes when the driver presses the park-assist button as well as in reverse.
const char* const kPdcProperty = "sensors.PDCStatus";
// Car day/night for Android Auto's night mode (rhmiapp-15). libhmiremoting CCdsPropertyDayNightChange::onStatus
// @0x2171e0 formats '{"dayNightChangeOver":%u}' from the vehicle DayNight_ChangeOver signal. Raw value goes to the
// daemon ("daynight <n>", /dev/shmem/nbt-aad-daynight), which maps it like factory CarPlay does.
const char* const kDayNightProperty = "vehicle.dayNightChangeOver";
const char* const kDayNightFile     = "/dev/shmem/nbt-aad-daynight";
const char* const kPdcFile     = "/dev/shmem/nbt-aad-pdc";
const uint16_t    kDaemonCtlPort = 52790;    // nbt-aad control port (UDP 127.0.0.1)
const char* const kStateFile   = "/dev/shmem/nbt-aad-state";   // written by nbt-aad: "<idle|connecting|connected> <pid>"
const char* const kStatusOff   = "/opt/aa/status.disable";      // keep the entry text static
const unsigned    kEntryTextModel = 3;       // <entryButton model="3"> = textIdModel 3 in NbtAad.xml
const unsigned    kTextDefault    = 1;       // "Android Auto"           (NbtAad_ALL_Texts.zip, make_db.py)
const unsigned    kTextConnecting = 2;       // "Android Auto: Connecting..."
const unsigned    kStatusPollMs   = 1000;
// Android Auto menu (NbtAad.xml): hmiState 10 with three rows.
const unsigned    kMenuState      = 10;      // <hmiState id="10">
const unsigned    kActStart       = 21;      // <raAction id="21"/> row "Start Android Auto"
const unsigned    kActStop        = 22;      // <raAction id="22"/> row "Force Stop"
const unsigned    kActRestart     = 23;      // <raAction id="23"/> row "Restart"
const unsigned    kStartRowModel  = 31;      // textIdModel of the Start row
const unsigned    kTextStart      = 10;      // "Start Android Auto"
const unsigned    kTextRowConnecting = 13;   // "Connecting..."
const unsigned    kTextRowOpen    = 14;      // "Open Android Auto"
const unsigned    kEvGoHome       = 50;      // <focusEvent id="50" targetModel="40"/>: NativeStateID 1 = iDrive main menu
const unsigned    kEvGoMediaList  = 51;      // <focusEvent id="51" targetModel="41"/>: NativeStateID 20 = Media/Radio list
const int         kHomeWindowS    = 180;     // after Start, go home on "connected" only within this time
// Screen exclusivity (Fork F, rhmiapp-8x): while Android Auto is on screen, iDrive is held on our inert hmiState 13
// (focusEvent 52), the way iDrive holds its own ST_CarPlay state for native CarPlay. Knob turns / pushes / tilts and
// touches then land on that state (nothing selectable) instead of the iDrive menu underneath. iDrive still handles
// the hard keys itself (MENU / OPTION / MEDIA / COM / NAV / MAP leave the state; no PluginApp API can stop that), so
// leaving the state the way CarPlay is left = Android Auto steps aside ("hide" to the daemon). BACK only pops the
// state back to our menu (state 10): we put iDrive straight back on state 13. NBT_AA_HOLD=0 in nbt-aad.env turns it off
// (old behaviour: once connected, iDrive goes to its main menu, NBT_MENU_RETURN).
const unsigned    kAaState        = 13;      // <hmiState id="13">
const unsigned    kEvHoldAa       = 52;      // <focusEvent id="52" target="13"/>
const unsigned    kHandlerFocus   = 1;       // addHandler(stateId, kind): FOCUS (Parcel bool)
const unsigned    kHandlerVisible = 11;      // VISIBLE (Parcel bool), as ActiveSeating registers it
const int         kReverseGear    = 2;       // CDS driving.gear value for R (daemon default NBT_GEAR_REVERSE)
const int         kPdcActive      = 1;       // sensors.PDCStatus 1 = park assist on (daemon default, NBT_PDC_ACTIVE)
// "Connecting" animation: the Media/Radio entry row has no busy property (entry buttons only take 3/23/53), so its icon
// (imageIdModel 4) cycles through 8 spinner frames (NbtAad_ALL_Images.zip 55010..55017); our menu's Start row uses the
// HMI's own busy ring (property 8 on button 61).
const unsigned    kEntryImageModel = 4;
const unsigned    kImgEntryIcon    = 55001;
const unsigned    kImgSpinFirst    = 55010;
const unsigned    kSpinFrames      = 8;
const unsigned    kSpinMs          = 200;
const unsigned    kStartButton     = 61;      // <button id="61"> (Start row)
const unsigned    kPropWaiting     = 8;       // LABEL_WAITINGANIMATION
// "Start with the car" checkbox (rhmiapp-7): <checkbox action="24" model="34"> in hmiState 10. On = the flag file
// exists; then nbt-aad is launched after the profile's startup delay from CAppManager onInit, so a
// phone that connects on its own gets Android Auto without opening the menu. The About row (hmiAction 25 -> hmiState
// 12) is XML only.
// The flag lives on the persistence partition: /var/opt is /fs/sda1/opt (qnx6, mounted rw at boot by create_sdc.sh,
// survives reboots; HmiMain3D itself writes /var/opt/hmi/data/ID5/RHMI-Cache there). /opt/aa is usually on the
// read-only application partition, so it is only READ (a hand-made `touch /opt/aa/autostart` also counts).
const unsigned    kActAutostart    = 24;
const unsigned    kAutostartModel  = 34;
const char* const kAutostartDir    = "/var/opt/aa";
const char* const kAutostartFile   = "/var/opt/aa/autostart";
const char* const kAutostartManual = "/opt/aa/autostart";
// ReconnectTiming supplies the fast (3 s) / conservative (15 s) startup wait.
// About page (hmiState 12): a non-selectable <list> on a Richtext raListModel holding a 1x1 table whose cell is
// TextID 17. The HMI wraps that text to the screen width and honours "\n" in the text DB. Same pattern as BMW's
// ActiveSeating intro (libActiveSeating StateTutorial::startWidgetInit: DataTableBase(&map, 1), map[0] = TextID(0x58),
// updateModel(274, table); state property 24 = 3). Plain labels / Richtext labels are one row, cut at ~34 characters.
const unsigned    kAboutModel      = 37;
const unsigned    kAboutText       = 17;
// "Save logs to USB" checkbox (rhmiapp-10): <checkbox action="26" model="38"> in hmiState 10, flag
// /var/opt/aa/usblog (nbt::usblog). Off = no log files at all; on = both logs on the first /mnt/umass* stick.
const unsigned    kActUsbLog       = 26;
const unsigned    kUsbLogModel     = 38;
// "MENU key opens iDrive" checkbox (rhmiapp-14): <checkbox action="27" model="42"> in hmiState 10, flag file read by
// nbt-aad on every MENU press. Off (default) = MENU opens Android Auto's menu and iDrive is put back on the blank state.
const unsigned    kActMenuIdrive   = 27;
const unsigned    kMenuIdriveModel = 42;
const char* const kMenuIdriveFlag   = "/var/opt/aa/menu-idrive";
const char* const kMenuIdriveManual = "/opt/aa/menu-idrive";
const long        kUsbLogRecheckS  = 5;   // stick lookup cache: a stick plugged in later is found within 5 s

// PluginApp vtable slots (index from the address point)
enum { kSlotOnError = 21, kSlotOnInit = 22, kSlotOnFinalize = 23, kVtblSlots = 34 };

// App log file: <stick>/nbt-aad-app.log while "Save logs to USB" is on and a stick is mounted, else none. The
// lookup (flag + /mnt scan) is cached for kUsbLogRecheckS; usbLogRecheck() forces the next call to look again.
pthread_mutex_t g_logMutex = PTHREAD_MUTEX_INITIALIZER;
char   g_appLogPath[192] = "";
time_t g_appLogChecked = 0;
bool   g_appLogRotated = false;

void usbLogRecheck() {
    pthread_mutex_lock(&g_logMutex);
    g_appLogChecked = 0;
    pthread_mutex_unlock(&g_logMutex);
}

bool appLogPath(char* out, size_t n) {
    pthread_mutex_lock(&g_logMutex);
    time_t now = time(NULL);
    if (!g_appLogChecked || now - g_appLogChecked >= kUsbLogRecheckS || now < g_appLogChecked) {
        g_appLogChecked = now;
        char stick[128];
        if (nbt::usblog::enabled() && nbt::usblog::findStick(stick, sizeof(stick))) {
            if (!g_appLogRotated) {                       // size check once per HMI run
                nbt::usblog::logPath(g_appLogPath, sizeof(g_appLogPath), stick, nbt::usblog::kAppName);
                g_appLogRotated = true;
            } else {
                snprintf(g_appLogPath, sizeof(g_appLogPath), "%s/%s", stick, nbt::usblog::kAppName);
            }
        } else {
            g_appLogPath[0] = 0;
        }
    }
    bool ok = g_appLogPath[0] != 0;
    if (ok) snprintf(out, n, "%s", g_appLogPath);
    pthread_mutex_unlock(&g_logMutex);
    return ok;
}

void alog(const char* fmt, ...) {
    char msg[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    char line[448];
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(line, sizeof(line), "[%5ld.%03ld] [NbtAadApp] %s\n", (long)ts.tv_sec, (long)(ts.tv_nsec / 1000000), msg);
    if (n < 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
    char path[192];
    int fd = appLogPath(path, sizeof(path)) ? open(path, O_WRONLY | O_CREAT | O_APPEND, 0666) : -1;
#ifndef __QNX__
    if (fd < 0) fd = open("/tmp/nbt-aad-app.log", O_WRONLY | O_CREAT | O_APPEND, 0666);   // host tests
#endif
    if (fd >= 0) {
        ssize_t w = write(fd, line, (size_t)n);
        (void)w;
        close(fd);
    }
#ifdef __QNX__
    slogf(_SLOG_SETCODE(0x4e41, 1), _SLOG_INFO, "[NbtAadApp] %s", msg);
#endif
}

bool fileExists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

// ---- launching the daemon (same as plugin/nbt_aad_plugin.cpp) ---------------------------------------------------
time_t g_lastLaunch = 0;

void launchDaemon() {
    time_t now = time(NULL);
    if (g_lastLaunch && now - g_lastLaunch < 3) { alog("launch ignored (pressed again within 3 s)"); return; }
    g_lastLaunch = now;
    if (!fileExists(kDaemon)) { alog("launch: %s not found", kDaemon); return; }

    static char envbuf[16][160];
    char* envp[16 + 64];
    int ne = 0;
    strcpy(envbuf[0], "PATH=/bin:/usr/bin:/opt/os/bin:/opt/sys/bin:/opt/core/bin:/opt/car/bin:/fs/sda0/opt/mm/bin");
    envp[ne++] = envbuf[0];
    int nb = 1;
    FILE* f = fopen(kDaemonEnv, "r");
    if (f) {
        char line[160];
        while (nb < 16 && fgets(line, sizeof(line), f)) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
            if (!l || line[0] == '#' || !strchr(line, '=')) continue;
            strcpy(envbuf[nb], line);
            envp[ne++] = envbuf[nb++];
        }
        fclose(f);
    } else {
        strcpy(envbuf[1], "NBT_AUDIO_BT=1");
        envp[ne++] = envbuf[1];
        nb = 2;
    }
    for (char** e = environ; e && *e && ne < (int)(sizeof(envp) / sizeof(envp[0])) - 1; ++e) {
        if (strncmp(*e, "PATH=", 5) == 0) continue;
        envp[ne++] = *e;
    }
    envp[ne] = NULL;

    char* argv[] = { const_cast<char*>(kDaemon), NULL };
    int in  = open("/dev/null", O_RDONLY);
    int out = open(kDaemonLog, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (in < 0 || out < 0) {
        alog("launch: cannot open /dev/null or %s (errno %d)", kDaemonLog, errno);
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        return;
    }
#ifdef __QNX__
    int fdmap[3] = { in, out, out };
    struct inheritance inh;
    memset(&inh, 0, sizeof(inh));
    inh.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF | SPAWN_SETSIGMASK | SPAWN_NOZOMBIE;
    inh.pgroup = SPAWN_NEWPGROUP;
    sigfillset(&inh.sigdefault);
    sigemptyset(&inh.sigmask);
    pid_t pid = spawn(kDaemon, 3, fdmap, &inh, argv, envp);
#else
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in, 0);
    posix_spawn_file_actions_adddup2(&fa, out, 1);
    posix_spawn_file_actions_adddup2(&fa, out, 2);
    pid_t pid = -1;
    if (posix_spawn(&pid, kDaemon, &fa, NULL, argv, envp) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
#endif
    int e = errno;
    close(in);
    close(out);
    if (pid < 0) alog("launch: spawn %s failed (errno %d)", kDaemon, e);
    else alog("launch: started %s pid %d (%d env overrides, output -> %s)", kDaemon, (int)pid, nb, kDaemonLog);
}

// ---- car gear -> daemon ---------------------------------------------------------------------------------------
// The value is published as text (JSON-like, e.g. {"gear":2}); the first integer is the gear code. Every change is
// logged with the raw text so the code for reverse can be confirmed on the car, written to /dev/shmem/nbt-aad-gear
// (the daemon reads it at start) and sent to a running daemon as "gear <n>".
int g_lastGear = -1000;

void sendToDaemon(const char* msg) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(kDaemonCtlPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(fd, msg, strlen(msg), 0, (struct sockaddr*)&a, sizeof(a));
    close(fd);
}

bool onGearChanged(void* /*app*/, const bmw::BasicString<unsigned short>& value) {
    char text[160];
    size_t n = 0;
    const unsigned short* p = value.raw();
    while (p && p[n] && n < sizeof(text) - 1) { text[n] = (p[n] < 0x80) ? (char)p[n] : '?'; ++n; }
    text[n] = 0;
    int gear = -1;
    for (size_t i = 0; i < n; ++i) {
        if (text[i] >= '0' && text[i] <= '9') { gear = atoi(text + i); break; }
    }
    if (gear != g_lastGear) {
        alog("CDS %s = '%s' -> gear %d", kGearProperty, text, gear);
        g_lastGear = gear;
        FILE* f = fopen(kGearFile, "w");
        if (f) { fprintf(f, "gear %d\n", gear); fclose(f); }
        char msg[32];
        snprintf(msg, sizeof(msg), "gear %d", gear);
        sendToDaemon(msg);
    }
    return true;
}

// ---- phone calls (Fork F): iDrive may leave our held state for its own call screen, while the call is shown in
// Android Auto. CDS communication.callStatus0 / callStatus1 (one per phone line; names from libhmiremoting.so's
// property table). The value is UTF-16 text, car-verified as '{"callStatus":{"id":<line>,"state":<n>}}'; a line is
// "in a call" while "state" is not 0 (cds_parse.hpp). rhmiapp-10 and older read the first number instead, i.e. the
// line id, so callStatus1 (id 1) counted as a permanent call and the BACK / OPTIONS re-hold never fired.
const char* const kCallProps[2] = { "communication.callStatus0", "communication.callStatus1" };
bool g_callLine[2] = { false, false };
time_t g_callEnd = 0;          // when the last call ended

bool callActive() { return g_callLine[0] || g_callLine[1]; }

bool onCallStatus(int line, const bmw::BasicString<unsigned short>& value) {
    char text[200];
    size_t n = 0;
    const unsigned short* p = value.raw();
    while (p && p[n] && n < sizeof(text) - 1) { text[n] = (p[n] < 0x80) ? (char)p[n] : '?'; ++n; }
    text[n] = 0;
    bool active = nbt::cdsCallActive(text);
    bool was = callActive();
    g_callLine[line] = active;
    if (was && !callActive()) g_callEnd = time(NULL);
    alog("CDS %s = '%s' -> %s", kCallProps[line], text, active ? "in a call" : "idle");
    return true;
}
bool onCallStatus0(void*, const bmw::BasicString<unsigned short>& v) { return onCallStatus(0, v); }
bool onCallStatus1(void*, const bmw::BasicString<unsigned short>& v) { return onCallStatus(1, v); }

int g_lastPdc = -1000;

bool onPdcChanged(void* /*app*/, const bmw::BasicString<unsigned short>& value) {
    char text[160];
    size_t n = 0;
    const unsigned short* p = value.raw();
    while (p && p[n] && n < sizeof(text) - 1) { text[n] = (p[n] < 0x80) ? (char)p[n] : '?'; ++n; }
    text[n] = 0;
    int pdc = -1;
    for (size_t i = 0; i < n; ++i) {
        if (text[i] >= '0' && text[i] <= '9') { pdc = atoi(text + i); break; }
    }
    if (pdc != g_lastPdc) {
        alog("CDS %s = '%s' -> pdc %d", kPdcProperty, text, pdc);
        g_lastPdc = pdc;
        FILE* f = fopen(kPdcFile, "w");
        if (f) { fprintf(f, "pdc %d\n", pdc); fclose(f); }
        char msg[32];
        snprintf(msg, sizeof(msg), "pdc %d", pdc);
        sendToDaemon(msg);
    }
    return true;
}

int g_lastDayNight = -1000;

bool onDayNightChanged(void* /*app*/, const bmw::BasicString<unsigned short>& value) {
    char text[160];
    size_t n = 0;
    const unsigned short* p = value.raw();
    while (p && p[n] && n < sizeof(text) - 1) { text[n] = (p[n] < 0x80) ? (char)p[n] : '?'; ++n; }
    text[n] = 0;
    int v = nbt::cdsIntField(text, "dayNightChangeOver", -1);
    if (v != g_lastDayNight) {
        alog("CDS %s = '%s' -> %d (1 day, 2 night, 0 no change, 3 invalid)", kDayNightProperty, text, v);
        g_lastDayNight = v;
        FILE* f = fopen(kDayNightFile, "w");
        if (f) { fprintf(f, "daynight %d\n", v); fclose(f); }
        char msg[32];
        snprintf(msg, sizeof(msg), "daynight %d", v);
        sendToDaemon(msg);
    }
    return true;
}

// bmw::Runnable stand-in (vtable: D1, D0, run): posted once to subscribe on the app thread, like SecurityMenu does
// ~1 s after onInit. Static object, so the destructor slots are no-ops.
struct GearRunnable { void** vptr; };
GearRunnable g_gearRunnable;
void* g_runVtbl[5];
bmw::PluginApp* g_runApp = NULL;

void runnableNoop(void*) {}

bool holdEnabled();
// Names from BimmerGestalt IDriveConnectKit's public CDSProperty list. We only
// observe changes; numeric ignition-state semantics are NOT assumed here.
const char* const kPowerProps[2] = { "vehicle.powerStateHeadUnit", "vehicle.statePWF" };
bool onPowerState0(void*, const bmw::BasicString<unsigned short>&);
bool onPowerState1(void*, const bmw::BasicString<unsigned short>&);

void runnableRun(void* /*self*/) {
    if (!g_runApp) return;
    for (int i = 0; i < 2; ++i) {
        bmw::BasicString<char> prop(kPowerProps[i]);
        bmw::CdsDelegate d;
        d.object = g_runApp;
        d.stub = i == 0 ? &onPowerState0 : &onPowerState1;
        g_runApp->cdsAddPropertyChangedEventHandler(prop, 0, d);
        alog("[Reconnect] observing CDS %s (waiting for actual values)", kPowerProps[i]);
    }
    if (!fileExists("/opt/aa/gear.disable")) {
        bmw::BasicString<char> prop(kGearProperty);
        bmw::CdsDelegate d;
        d.object = g_runApp;
        d.stub = &onGearChanged;
        g_runApp->cdsAddPropertyChangedEventHandler(prop, 0, d);
        alog("CDS handler for %s registered", kGearProperty);
    }
    {
        bmw::BasicString<char> pprop(kPdcProperty);
        bmw::CdsDelegate pd;
        pd.object = g_runApp;
        pd.stub = &onPdcChanged;
        g_runApp->cdsAddPropertyChangedEventHandler(pprop, 0, pd);
        alog("CDS handler for %s registered", kPdcProperty);
    }
    {
        bmw::BasicString<char> dprop(kDayNightProperty);
        bmw::CdsDelegate dd;
        dd.object = g_runApp;
        dd.stub = &onDayNightChanged;
        g_runApp->cdsAddPropertyChangedEventHandler(dprop, 0, dd);
        alog("CDS handler for %s registered", kDayNightProperty);
    }
    if (holdEnabled()) {                                 // CDS handlers are keyed by property name
        for (int i = 0; i < 2; ++i) {
            bmw::BasicString<char> prop(kCallProps[i]);
            bmw::CdsDelegate d;
            d.object = g_runApp;
            d.stub = i == 0 ? &onCallStatus0 : &onCallStatus1;
            g_runApp->cdsAddPropertyChangedEventHandler(prop, 0, d);
            alog("CDS handler for %s registered", kCallProps[i]);
        }
    }
}

void subscribeGear(bmw::PluginApp* app) {
    void** base = reinterpret_cast<void**>(dlsym(RTLD_DEFAULT, "_ZTVN3bmw8RunnableE"));
    if (base) memcpy(g_runVtbl, base, sizeof(g_runVtbl));   // keep offset-to-top + typeinfo of bmw::Runnable
    else { g_runVtbl[0] = NULL; g_runVtbl[1] = NULL; }
    g_runVtbl[2] = reinterpret_cast<void*>(&runnableNoop);
    g_runVtbl[3] = reinterpret_cast<void*>(&runnableNoop);
    g_runVtbl[4] = reinterpret_cast<void*>(&runnableRun);
    g_gearRunnable.vptr = g_runVtbl + 2;
    g_runApp = app;
    app->post(reinterpret_cast<bmw::Runnable*>(&g_gearRunnable), 1500);
    alog("gear subscription posted (Runnable vtable %s)", base ? "from libCoreFramework" : "built locally");
}

// ---- daemon state -> entry text, menu rows, stop / restart, return to the main menu ---------------------------------
// Polled on the app thread (a self-reposting Runnable, like the gear subscription). nbt-aad writes
// "<idle|connecting|connected> <pid> [overlay]" to /dev/shmem/nbt-aad-state; a file left behind by a daemon that is no
// longer running counts as idle.
GearRunnable g_statusRunnable;
void* g_statusVtbl[5];
unsigned g_shownText = kTextDefault;
unsigned g_shownRow = kTextStart;

struct DaemonState {
    char state[24];     // idle | connecting | connected
    int pid;            // > 0 and alive, else 0
    int overlay;        // 1 = Android Auto on screen, knob goes to the phone; 2 = same, BACK held (v1.8.26+);
                        // 3 = same, MENU / COM / MEDIA / NAV / MAP just went to the phone (v1.8.29+ / v1.8.34+): re-hold
    unsigned setupFailures, setupCycle, sppWaitSeconds; // optional fields; zero if absent/partial
    unsigned confirmedCycle;
    bool confirmationKnown;
};

bool pidAlive(int pid) {
    if (pid <= 0) return false;
    if (kill((pid_t)pid, 0) == 0) return true;
    return errno == EPERM;                               // exists, just not ours to signal
}

// Independent of the state file: a running nbt-aad owns UDP 127.0.0.1:52790 (single-instance control port).
bool controlPortBusy() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(kDaemonCtlPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool busy = bind(fd, (struct sockaddr*)&a, sizeof(a)) != 0 && errno == EADDRINUSE;
    close(fd);
    return busy;
}

// pid > 0: running with a known pid. pid == -1: running (control port taken) but no usable state file.
DaemonState readDaemonState() {
    DaemonState d;
    strcpy(d.state, "idle");
    d.pid = 0;
    d.overlay = 0;
    d.setupFailures = d.setupCycle = d.sppWaitSeconds = 0;
    d.confirmedCycle = 0;
    d.confirmationKnown = false;
    FILE* f = fopen(kStateFile, "r");
    if (f) {
        char st[24] = "";
        int pid = 0, ov = 0;
        unsigned failures = 0, cycle = 0, sppWait = 0, confirmed = 0;
        int n = fscanf(f, "%23s %d %d %u %u %u %u", st, &pid, &ov, &failures, &cycle, &sppWait, &confirmed);
        fclose(f);
        if (n >= 2 && pidAlive(pid)) {
            strcpy(d.state, st);
            d.pid = pid;
            d.overlay = (n >= 3) ? ov : 0;
            if (n >= 5) { d.setupFailures = failures; d.setupCycle = cycle; }
            if (n >= 6) d.sppWaitSeconds = sppWait;
            if (n >= 7) { d.confirmedCycle = confirmed; d.confirmationKnown = true; }
            return d;
        }
    }
    if (controlPortBusy()) {
        static bool s_logged = false;
        if (!s_logged) { alog("nbt-aad is running but %s is missing or stale (old daemon build?)", kStateFile); s_logged = true; }
        strcpy(d.state, "running");
        d.pid = -1;
    }
    return d;
}

bool g_focusKnown = false;     // a focus notification for our menu arrived at least once
bool g_menuFocused = false;
bool g_wantHome = false;       // Start was pressed: return to the main menu once the phone is connected
time_t g_startTime = 0;
char g_prevState[24] = "idle";
// Stop / restart in progress.
pid_t g_stopPid = 0;
time_t g_stopTime = 0;
int g_stopStage = 0;           // 0 quit sent, 1 SIGTERM sent, 2 SIGKILL sent
bool g_restartAfterStop = false;
bool g_autoRestart = false;
bool g_manualStop = false;
uint64_t g_autoStopTime = 0;
nbt::ReconnectPolicy g_reconnectPolicy;
nbt::ReconnectLaunchDelay g_autoRelaunch;

uint64_t monotonicSeconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec;
}
bool autostartOn();

nbt::ReconnectTiming reconnectTiming() {
    FILE* f = fopen(kReconnectProfile, "r");
    if (!f) return nbt::ReconnectTiming(errno != ENOENT);
    char profile[24] = "", extra[2] = "";
    int fields = fscanf(f, "%23s %1s", profile, extra);
    bool error = ferror(f) != 0;
    fclose(f);
    if (error || fields != 1) return nbt::ReconnectTiming(true);
    return nbt::ReconnectTiming::fromProfileName(profile);
}

char g_lastPowerText[2][256] = { "", "" };
bool onPowerState(int index, const bmw::BasicString<unsigned short>& value) {
    char text[256];
    const unsigned short* p = value.raw();
    size_t n = 0;
    while (p && p[n] && n < sizeof(text) - 1) { text[n] = p[n] < 0x80 ? (char)p[n] : '?'; ++n; }
    text[n] = 0;
    const char* field = index == 0 ? "powerStateHeadUnit" : "statePWF";
    if (!strstr(text, field) || !strcmp(text, g_lastPowerText[index])) return true;
    bool initial = !g_lastPowerText[index][0];
    strcpy(g_lastPowerText[index], text);
    alog("[Reconnect] CDS %s = '%s' (%s)", kPowerProps[index], text, initial ? "baseline" : "changed");
    if (!initial) {
        uint64_t now = monotonicSeconds();
        g_reconnectPolicy.powerChanged(now);
        if (g_autoRelaunch.pending()) g_autoRelaunch.schedule(now, reconnectTiming().quietSeconds);
        alog("[Reconnect] power-state change rearmed recovery; allowing 10 s to settle");
    }
    return true;
}
bool onPowerState0(void*, const bmw::BasicString<unsigned short>& v) { return onPowerState(0, v); }
bool onPowerState1(void*, const bmw::BasicString<unsigned short>& v) { return onPowerState(1, v); }

// Where to go once connected: NBT_MENU_RETURN in /opt/aa/nbt-aad.env = home (default) | media | off.
unsigned menuReturnEvent() {
    FILE* f = fopen(kDaemonEnv, "r");
    if (!f) return kEvGoHome;
    char line[160];
    unsigned ev = kEvGoHome;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "NBT_MENU_RETURN=", 16) != 0) continue;
        const char* v = line + 16;
        if (!strncmp(v, "media", 5)) ev = kEvGoMediaList;
        else if (!strncmp(v, "off", 3) || v[0] == '0') ev = 0;
        else ev = kEvGoHome;
    }
    fclose(f);
    return ev;
}

bool envFlagOn(const char* name, bool def) {
    FILE* f = fopen(kDaemonEnv, "r");
    if (!f) return def;
    char line[160];
    size_t n = strlen(name);
    bool v = def;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, name, n) != 0 || line[n] != '=') continue;
        const char* p = line + n + 1;
        v = !(p[0] == '0' || !strncmp(p, "off", 3) || !strncmp(p, "no", 2) || !strncmp(p, "false", 5));
    }
    fclose(f);
    return v;
}

// ---- screen exclusivity (kAaState) ----------------------------------------------------------------------------------
int  g_holdOn = -1;            // NBT_AA_HOLD, read once
bool g_aaVisible = false;      // iDrive reports our state 13 visible / focused
bool g_aaFocused = false;
bool g_holding = false;        // we put iDrive on state 13 for the current Android Auto overlay
bool g_hideSent = false;       // we asked the daemon to step aside (a hard key took iDrive elsewhere)
int  g_prevOverlay = 0;
time_t g_holdTime = 0;         // when event 52 was last fired
int  g_holdTries = 0;          // fired without iDrive showing the state yet (it refuses > 10 while we are not active)
bool g_aaSeen = false;         // state 13 was shown since we last fired event 52
bool g_holdGaveUp = false;

bool holdEnabled() {
    if (g_holdOn < 0) {
        g_holdOn = envFlagOn("NBT_AA_HOLD", true) ? 1 : 0;
        alog("screen exclusivity (NBT_AA_HOLD): %s", g_holdOn ? "on" : "off");
    }
    return g_holdOn != 0;
}

void fireHold(bmw::PluginApp* app, const char* why);
DaemonState readDaemonState();

bool onAaFocus(void*, const bmw::Parcel& p) {
    bool f = false;
    p.readBool(f);
    g_aaFocused = f;
    alog("hold: state %u focus -> %d", kAaState, (int)f);
    // MENU went to Android Auto (daemon overlay 3) but iDrive also left for its main menu: come straight back, so the
    // knob does not drive iDrive's main menu under the picture until the next poll.
    if (!f && g_runApp && holdEnabled() && g_holding && !callActive()) {
        DaemonState d = readDaemonState();
        if (d.pid > 0 && d.overlay == 3) fireHold(g_runApp, "a hard key went to Android Auto (immediate)");
    }
    return true;
}

bool onAaVisible(void*, const bmw::Parcel& p) {
    bool v = false;
    p.readBool(v);
    g_aaVisible = v;
    if (v) { g_aaSeen = true; g_holdTries = 0; }
    alog("hold: state %u visible -> %d", kAaState, (int)v);
    // iDrive came back to our blank state by itself while Android Auto is minimized (BACK in iDrive walks its history
    // back to the Android Auto screen; car 2026-10-29: blank screen for 3 s). That is the driver going back to Android
    // Auto, as with CarPlay: bring it to the front. Not in reverse / park assist / a call.
    if (v && !g_holding && holdEnabled() && !callActive() && g_lastGear != kReverseGear && g_lastPdc != kPdcActive) {
        DaemonState d = readDaemonState();
        if (d.pid > 0 && !d.overlay && !strcmp(d.state, "connected")) {
            alog("hold: iDrive went back to the Android Auto screen: asking nbt-aad to come to the front");
            sendToDaemon("show");
            g_prevOverlay = 0;                           // serviceHold() takes the coming overlay as new (holding)
        }
    }
    return true;
}

void fireHold(bmw::PluginApp* app, const char* why) {
    if (g_holdTries >= 8) { alog("hold: not entering state %u (%s): already tried %d times", kAaState, why, g_holdTries); return; }
    ++g_holdTries;
    g_aaSeen = false;
    g_holdTime = time(NULL);
    app->triggerEvent(kEvHoldAa);
    alog("hold: event %u -> state %u (%s, try %d)", kEvHoldAa, kAaState, why, g_holdTries);
}

// From the status poll (1 s). d.overlay = Android Auto is on screen.
void serviceHold(bmw::PluginApp* app, const DaemonState& d) {
    if (!holdEnabled()) return;
    bool overlay = d.pid > 0 && d.overlay;
    bool rose = overlay && !g_prevOverlay, fell = !overlay && g_prevOverlay;
    g_prevOverlay = overlay ? 1 : 0;
    if (rose) {
        g_holdTries = 0;
        g_hideSent = false;
        g_holdGaveUp = false;
        g_holding = true;
        if (!g_aaVisible) fireHold(app, "Android Auto came on screen");
        return;
    }
    if (fell) {
        // Android Auto stepped aside by itself (phone's exit icon, BACK hold, signal lost) while iDrive still shows our
        // blank state: give the driver the iDrive main menu. Not in reverse (the camera returns to us, then the
        // overlay comes back) and not when a hard key already took iDrive somewhere else.
        bool reverse = g_lastGear == kReverseGear, pdc = g_lastPdc == kPdcActive;
        if (g_aaVisible && !g_hideSent && !reverse && !pdc) {
            app->triggerEvent(kEvGoHome);
            alog("hold: Android Auto stepped aside: event %u -> iDrive main menu", kEvGoHome);
        } else {
            alog("hold: Android Auto stepped aside (state %u visible %d, hard key %d, reverse %d, park assist %d)", kAaState,
                 (int)g_aaVisible, (int)g_hideSent, (int)reverse, (int)pdc);
        }
        g_holding = false;
        return;
    }
    if (g_aaVisible) g_callEnd = 0;                      // back on our state after a call: nothing to restore
    // BACK is held on the knob (daemon v1.8.26+ writes overlay 2): iDrive took the press as well and left the blank
    // state. Leave it alone: the hold may be about to minimize Android Auto (then `fell` above sends iDrive home), and a
    // short press is re-held on the poll after the release. rhmiapp-11 re-focused here and fought the minimize.
    if (d.overlay == 2) {
        static time_t s_logged = 0;
        if (time(NULL) - s_logged > 10) { s_logged = time(NULL); alog("hold: BACK held on the knob: not re-holding until it is released"); }
        return;
    }
    // OPTIONS (car 2026-10-28): iDrive opens its options panel over our blank state, under the Android Auto picture;
    // state 13 may stay visible but lose focus, so the knob drives the hidden panel. Put the focus back on state 13
    // (closes the panel, like BACK would). Seen on two polls in a row; popups (calls, volume) refuse focus events, and
    // fireHold() gives up after 8 tries.
    static int s_unfocused = 0;
    if (overlay && g_holding && g_aaVisible && !g_aaFocused && !callActive()) {
        if (++s_unfocused >= 2 && time(NULL) - g_holdTime >= 1) { s_unfocused = 0; fireHold(app, "state lost focus (OPTIONS panel?)"); }
        return;
    }
    s_unfocused = 0;
    if (!overlay || !g_holding || g_aaVisible) return;
    if (time(NULL) - g_holdTime < 1) return;            // iDrive needs a moment to show the state
    if (g_aaSeen && g_menuFocused) {
        // BACK popped iDrive from state 13 to our menu (state 10): BACK belongs to the phone, put iDrive straight back
        // on the blank state. Also during a call: iDrive's own call screen never focuses our menu.
        fireHold(app, callActive() ? "BACK returned to our menu during a call" : "BACK returned to our menu");
        return;
    }
    if (callActive()) {
        // A call: Android Auto shows it, so it stays on screen even though iDrive switched to its own call screen
        // (that is not a hard key). Do not pull iDrive away from the call either; we come back when it ends.
        static time_t s_logged = 0;
        if (s_logged != g_holdTime) { s_logged = g_holdTime; alog("hold: call in progress: Android Auto stays up, state %u released", kAaState); }
        g_aaSeen = true;                                 // the state was ours before the call: re-enter afterwards
        return;
    }
    if (g_callEnd && time(NULL) - g_callEnd < 10) {      // call just ended: iDrive is still on its call screen
        g_callEnd = 0;
        fireHold(app, "call ended");
        return;
    }
    if (!g_aaSeen) {
        // Event 52 not (yet) honoured: iDrive refuses focus events while a popup is open, and more than 10 while our
        // app is not the active one. Retry a few times, then leave things as before (overlay up, iDrive underneath).
        // A burst of hard keys (COM, MAP...) can keep iDrive refusing event 52 for several seconds (car 2026-10-29: gave
        // up after 3 tries during a COM burst, iDrive's COM screen stayed under Android Auto): keep trying up to the
        // fireHold() cap while keys keep going to the phone, and for 6 s after the last one.
        static time_t s_keyTime = 0;
        if (d.overlay == 3) s_keyTime = time(NULL);
        int maxTries = (s_keyTime && time(NULL) - s_keyTime < 6) ? 8 : 3;
        if (g_holdTries < maxTries) fireHold(app, "retry: state not shown yet");
        else if (!g_holdGaveUp) { g_holdGaveUp = true; alog("hold: iDrive did not show state %u: knob/touch may reach iDrive this time", kAaState); }
        return;
    }
    if (g_menuFocused) {
        fireHold(app, "BACK returned to our menu");      // BACK goes to the phone; keep iDrive on the blank state
    } else {
        // Neither our blank state nor our menu: a hard key (MENU / MEDIA / NAV / COM / MAP / OPTION) took iDrive
        // elsewhere, the way MENU leaves native CarPlay. Android Auto steps aside; the iDrive entry brings it back.
        // Confirmed on the next poll, so an incoming call's status (CDS) can arrive first.
        // Except MENU by default (daemon overlay 3): it opened Android Auto's own menu, so iDrive goes back to state 13.
        if (d.overlay == 3) { fireHold(app, "a hard key went to Android Auto"); return; }
        if (g_lastGear == kReverseGear || g_lastPdc == kPdcActive) return;   // camera / PDC view: the daemon steps aside itself
        static time_t s_leftSeen = 0;
        if (!s_leftSeen || time(NULL) - s_leftSeen > 3) { s_leftSeen = time(NULL); return; }
        s_leftSeen = 0;
        g_hideSent = true;
        g_holding = false;
        sendToDaemon("hide");
        alog("hold: iDrive left state %u (hard key): asked nbt-aad to step aside", kAaState);
    }
}

void startDaemonOrShow() {
    DaemonState d = readDaemonState();
    g_wantHome = true;
    g_startTime = time(NULL);
    if (d.pid != 0) {
        alog("start: nbt-aad already running (pid %d, %s): asking it to come to the front", d.pid, d.state);
        sendToDaemon("show");
        if (!strcmp(d.state, "connected") && holdEnabled()) {
            g_wantHome = false;                          // serviceHold() puts iDrive on state 13 once the overlay is up
            g_prevOverlay = 0;                           // treat the coming overlay as new (also when it never went down)
        } else if (!strcmp(d.state, "connected")) {      // already projecting: leave our menu right away
            g_wantHome = false;
            unsigned ev = menuReturnEvent();
            if (ev && g_runApp) { g_runApp->triggerEvent(ev); alog("start: already connected -> event %u", ev); }
        }
    } else {
        launchDaemon();
    }
}

void beginStop(bool restart) {
    DaemonState d = readDaemonState();
    g_autoRestart = false;
    g_restartAfterStop = restart;
    if (d.pid == 0) {
        alog("%s: nbt-aad is not running", restart ? "restart" : "force stop");
        if (restart) { g_lastLaunch = 0; startDaemonOrShow(); }
        return;
    }
    g_stopPid = (pid_t)d.pid;                            // -1: pid unknown, watch the control port instead
    g_stopTime = time(NULL);
    g_stopStage = 0;
    g_wantHome = false;
    sendToDaemon("quit");
    alog("%s: sent 'quit' to nbt-aad pid %d", restart ? "restart" : "force stop", d.pid);
}

// ---- screen recovery after an unclean daemon exit -----------------------------------------------------------------
// A clean exit (quit, SIGTERM/SIGINT, Force Stop's first two stages) hides the overlay, hands LayerManager surface
// 1280 back to the factory CarPlay geometry and removes the state file. SIGKILL (Force Stop's last stage, slay -9) or
// a crash does none of that: the state file stays behind with a dead pid. Then we do the daemon's hide sequence
// ourselves (only if Android Auto was on screen) and always restore the CarPlay regions the daemon saved at start.
const char* const kLmTool  = "/opt/sys/bin/LayerManagerControl";
const char* const kLmSaved = "/dev/shmem/nbt-aad-lm1280";   // "src x y w h dst x y w h" (src/lm_geometry.hpp)

void runLm(const char* a1, const char* a2, const char* a3, const char* a4, const char* a5 = NULL,
           const char* a6 = NULL, const char* a7 = NULL, const char* a8 = NULL, const char* a9 = NULL) {
    char* argv[] = { const_cast<char*>(kLmTool), const_cast<char*>(a1), const_cast<char*>(a2), const_cast<char*>(a3),
                     const_cast<char*>(a4), const_cast<char*>(a5), const_cast<char*>(a6), const_cast<char*>(a7),
                     const_cast<char*>(a8), const_cast<char*>(a9), NULL };
    int nul = open("/dev/null", O_RDWR);
    if (nul < 0) return;
    pid_t pid = -1;
#ifdef __QNX__
    int fdmap[3] = { nul, nul, nul };
    struct inheritance inh;
    memset(&inh, 0, sizeof(inh));
    inh.flags = SPAWN_SETSIGDEF;
    sigfillset(&inh.sigdefault);
    pid = spawn(kLmTool, 3, fdmap, &inh, argv, environ);
#else
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, nul, 0);
    posix_spawn_file_actions_adddup2(&fa, nul, 1);
    posix_spawn_file_actions_adddup2(&fa, nul, 2);
    if (posix_spawn(&pid, kLmTool, &fa, NULL, argv, environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
#endif
    close(nul);
    if (pid > 0) { int st; waitpid(pid, &st, 0); }    // in order, like the daemon's system() calls
}

void recoverScreenIfUnclean() {
    FILE* f = fopen(kStateFile, "r");
    if (!f) return;                                      // clean exit (or never started)
    char st[24] = "";
    int pid = 0, ov = 0;
    int n = fscanf(f, "%23s %d %d", st, &pid, &ov);
    fclose(f);
    if (n < 2 || pid <= 0 || pidAlive(pid)) return;
    alog("nbt-aad pid %d is gone without a clean exit (state '%s', overlay %d): restoring the screen", pid, st, n >= 3 ? ov : 0);
    if (n >= 3 && ov) {                                  // Android Auto was on screen: the daemon's hide sequence
        runLm("set", "layer", "0x80000000", "render", "order", "0x100");
        runLm("set", "surface", "256", "visibility", "1");
        runLm("set", "surface", "1280", "visibility", "0");
    }
    FILE* g = fopen(kLmSaved, "r");
    int r[8];
    if (g && fscanf(g, "src %d %d %d %d dst %d %d %d %d", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5], &r[6], &r[7]) == 8 &&
        r[2] > 0 && r[3] > 0 && r[6] > 0 && r[7] > 0) {
        char v[8][12];
        for (int i = 0; i < 8; ++i) snprintf(v[i], sizeof(v[i]), "%d", r[i]);
        runLm("set", "surface", "1280", "source", "region", v[0], v[1], v[2], v[3]);
        runLm("set", "surface", "1280", "destination", "region", v[4], v[5], v[6], v[7]);
        alog("CarPlay geometry restored from %s: source %s,%s %sx%s, destination %s,%s %sx%s", kLmSaved,
             v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    } else {
        alog("no saved CarPlay geometry (%s): surface 1280 regions left as they are", kLmSaved);
    }
    if (g) fclose(g);
    unlink(kStateFile);
}

// Stop / restart progress, called from the status poll (1 s).
void serviceStop() {
    if (!g_stopPid) return;
    bool gone = g_stopPid > 0 ? !pidAlive((int)g_stopPid) : !controlPortBusy();
    if (g_stopPid < 0 && !gone && time(NULL) - g_stopTime >= 3) {
        DaemonState d = readDaemonState();               // a state file may have appeared: escalate by pid then
        if (d.pid > 0) g_stopPid = (pid_t)d.pid;
    }
    if (gone) {
        alog("nbt-aad (pid %d) has exited", (int)g_stopPid);
        g_stopPid = 0;
        recoverScreenIfUnclean();                        // SIGKILL stage: the daemon could not clean up
        unlink(kStateFile);
        bool automatic = g_autoRestart;
        g_autoRestart = false;
        if (g_restartAfterStop) {
            g_restartAfterStop = false;
            g_lastLaunch = 0;
            if (automatic) {
                if (autostartOn() && !g_manualStop) {
                    unsigned quietSeconds = reconnectTiming().quietSeconds;
                    g_autoRelaunch.schedule(monotonicSeconds(), quietSeconds);
                    alog("[Reconnect] old daemon exited; waiting %u s before quiet relaunch", quietSeconds);
                }
            } else {
                alog("restart: starting nbt-aad again");
                startDaemonOrShow();
            }
        }
        return;
    }
    if (g_autoRestart) {
        // 'recover' is conditional in the daemon. A late connection may reject
        // it. Automatic recovery NEVER escalates to SIGTERM / SIGKILL.
        DaemonState d = readDaemonState();
        if (!autostartOn() || g_manualStop || (d.pid > 0 && d.pid != (int)g_stopPid) ||
            (d.pid == (int)g_stopPid && !strcmp(d.state, "connected")) ||
            monotonicSeconds() - g_autoStopTime >= 12) {
            alog("[Reconnect] recovery wait cancelled (connected, disabled, changed pid or 12 s timeout)");
            g_stopPid = 0;
            g_autoRestart = g_restartAfterStop = false;
        }
        return;
    }
    long waited = (long)(time(NULL) - g_stopTime);
    if (g_stopPid < 0) {                                 // no pid: 'quit' is all we can do
        if (waited >= 10) { alog("nbt-aad (pid unknown) still running 10 s after 'quit': giving up"); g_stopPid = 0; g_restartAfterStop = false; }
        return;
    }
    if (g_stopStage == 0 && waited >= 3) {
        alog("nbt-aad pid %d still running 3 s after 'quit': SIGTERM", (int)g_stopPid);
        kill(g_stopPid, SIGTERM);
        g_stopStage = 1;
    } else if (g_stopStage == 1 && waited >= 6) {
        alog("nbt-aad pid %d still running 6 s after 'quit': SIGKILL", (int)g_stopPid);
        kill(g_stopPid, SIGKILL);
        g_stopStage = 2;
    } else if (g_stopStage == 2 && waited >= 12) {
        alog("nbt-aad pid %d survived SIGKILL; giving up", (int)g_stopPid);
        g_stopPid = 0;
        g_restartAfterStop = false;
    }
}

// ---- connecting animation ------------------------------------------------------------------------------------------
GearRunnable g_spinRunnable;
void* g_spinVtbl[5];
bool g_spinWanted = false;     // set by the status poll while nbt-aad is connecting
bool g_spinRunning = false;    // the Runnable is posted
unsigned g_spinFrame = 0;

void spinRun(void* /*self*/) {
    bmw::PluginApp* app = g_runApp;
    if (!app) { g_spinRunning = false; return; }
    if (!g_spinWanted) {                                 // done: back to the normal icon
        bmw::ImageID icon(kImgEntryIcon);
        app->updateModel(kEntryImageModel, icon);
        g_spinRunning = false;
        return;
    }
    bmw::ImageID frame(kImgSpinFirst + (g_spinFrame++ % kSpinFrames));
    app->updateModel(kEntryImageModel, frame);
    app->post(reinterpret_cast<bmw::Runnable*>(&g_spinRunnable), kSpinMs);
}

void setConnectingAnimation(bmw::PluginApp* app, bool on) {
    if (on == g_spinWanted) return;
    g_spinWanted = on;
    bmw::Parcel busy(on);
    app->setProperty(kStartButton, kPropWaiting, busy);  // busy ring on our menu (when it is on screen)
    alog("connecting animation %s (entry icon frames, Start row busy ring)", on ? "on" : "off");
    if (on && !g_spinRunning) {
        void** base = reinterpret_cast<void**>(dlsym(RTLD_DEFAULT, "_ZTVN3bmw8RunnableE"));
        if (base) memcpy(g_spinVtbl, base, sizeof(g_spinVtbl));
        else { g_spinVtbl[0] = NULL; g_spinVtbl[1] = NULL; }
        g_spinVtbl[2] = reinterpret_cast<void*>(&runnableNoop);
        g_spinVtbl[3] = reinterpret_cast<void*>(&runnableNoop);
        g_spinVtbl[4] = reinterpret_cast<void*>(&spinRun);
        g_spinRunnable.vptr = g_spinVtbl + 2;
        g_spinRunning = true;
        app->post(reinterpret_cast<bmw::Runnable*>(&g_spinRunnable), kSpinMs);
    }
}

// ---- start with the car --------------------------------------------------------------------------------------------
time_t g_initTime = 0;
bool g_autostartDone = false;
bool g_autostartShown = false;

bool autostartOn() { return fileExists(kAutostartFile) || fileExists(kAutostartManual); }

void showAutostart(bmw::PluginApp* app) {
    bool on = autostartOn();
    app->updateModel(kAutostartModel, on);
    g_autostartShown = true;
}

// Checkbox pressed: toggles the flag file. The Parcel is not read (its layout for checkboxes is not confirmed), the
// file is the state, and the checkbox is set from it afterwards.
bool onAutostartToggle(void* /*app*/, const bmw::Parcel& /*args*/) {
    bool on = autostartOn();
    if (on) {
        if (fileExists(kAutostartFile) && unlink(kAutostartFile) != 0)
            alog("start with the car: cannot remove %s (errno %d)", kAutostartFile, errno);
        if (fileExists(kAutostartManual) && unlink(kAutostartManual) != 0)
            alog("start with the car: cannot remove %s (errno %d, read-only?): remove it by hand", kAutostartManual, errno);
    } else {
        mkdir(kAutostartDir, 0755);                      // EEXIST is fine
        int fd = open(kAutostartFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) alog("start with the car: cannot create %s (errno %d)", kAutostartFile, errno);
        else { ssize_t w = write(fd, "1\n", 2); (void)w; close(fd); }
    }
    bool now = autostartOn();
    if (now && !on) { g_manualStop = false; g_reconnectPolicy.manualStart(); }
    alog("menu: start with the car %s -> %s", on ? "on" : "off", now ? "on" : "off");
    if (g_runApp) showAutostart(g_runApp);
    return true;
}

// ---- save logs to USB ---------------------------------------------------------------------------------------------
bool g_usbLogShown = false;

void showUsbLog(bmw::PluginApp* app) {
    app->updateModel(kUsbLogModel, nbt::usblog::enabled());
    g_usbLogShown = true;
}

// Same pattern as the autostart checkbox: the flag file is the state. The daemon notices within 5 s.
bool onUsbLogToggle(void* /*app*/, const bmw::Parcel& /*args*/) {
    bool on = nbt::usblog::enabled();
    if (on) {
        alog("menu: save logs to USB -> off");             // last line into the USB app log
        if (fileExists(nbt::usblog::kFlag) && unlink(nbt::usblog::kFlag) != 0)
            alog("save logs to USB: cannot remove %s (errno %d)", nbt::usblog::kFlag, errno);
        if (fileExists(nbt::usblog::kFlagManual) && unlink(nbt::usblog::kFlagManual) != 0)
            alog("save logs to USB: cannot remove %s (errno %d, read-only?): remove it by hand",
                 nbt::usblog::kFlagManual, errno);
    } else {
        mkdir(nbt::usblog::kFlagDir, 0755);               // EEXIST is fine
        int fd = open(nbt::usblog::kFlag, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) alog("save logs to USB: cannot create %s (errno %d)", nbt::usblog::kFlag, errno);
        else { ssize_t w = write(fd, "1\n", 2); (void)w; close(fd); }
    }
    usbLogRecheck();
    char path[192];
    bool now = nbt::usblog::enabled();
    if (now) {
        if (appLogPath(path, sizeof(path))) alog("menu: save logs to USB -> on (%s, %s)", path, NBT_AAD_APP_VERSION);
        else alog("menu: save logs to USB -> on, no USB stick found under /mnt yet (checked every %ld s)", kUsbLogRecheckS);
    }
    if (g_runApp) showUsbLog(g_runApp);
    return true;
}

// ---- MENU key opens iDrive ----------------------------------------------------------------------------------------
bool g_menuIdriveShown = false;
bool menuIdriveOn() { return fileExists(kMenuIdriveFlag) || fileExists(kMenuIdriveManual); }

void showMenuIdrive(bmw::PluginApp* app) {
    app->updateModel(kMenuIdriveModel, menuIdriveOn());
    g_menuIdriveShown = true;
}

bool onMenuIdriveToggle(void* /*app*/, const bmw::Parcel& /*args*/) {
    bool on = menuIdriveOn();
    if (on) {
        if (fileExists(kMenuIdriveFlag) && unlink(kMenuIdriveFlag) != 0)
            alog("MENU opens iDrive: cannot remove %s (errno %d)", kMenuIdriveFlag, errno);
        if (fileExists(kMenuIdriveManual) && unlink(kMenuIdriveManual) != 0)
            alog("MENU opens iDrive: cannot remove %s (errno %d, read-only?): remove it by hand", kMenuIdriveManual, errno);
    } else {
        mkdir(kAutostartDir, 0755);                      // /var/opt/aa, EEXIST is fine
        int fd = open(kMenuIdriveFlag, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) alog("MENU opens iDrive: cannot create %s (errno %d)", kMenuIdriveFlag, errno);
        else { ssize_t w = write(fd, "1\n", 2); (void)w; close(fd); }
    }
    bool now = menuIdriveOn();
    alog("menu: MENU key opens iDrive %s -> %s (%s)", on ? "on" : "off", now ? "on" : "off",
         now ? "MENU minimizes Android Auto" : "MENU opens Android Auto's menu");
    if (g_runApp) showMenuIdrive(g_runApp);
    return true;
}

// From the status poll: once, after the profile's startup delay from onInit.
void serviceAutostart() {
    if (g_autostartDone || !g_initTime || time(NULL) - g_initTime < (long)reconnectTiming().autostartSeconds) return;
    g_autostartDone = true;
    if (!autostartOn() || g_manualStop || g_autoRelaunch.pending()) return;
    DaemonState d = readDaemonState();
    if (d.pid != 0) { alog("start with the car: nbt-aad already running (pid %d)", d.pid); return; }
    alog("start with the car: launching nbt-aad (%s)", fileExists(kAutostartFile) ? kAutostartFile : kAutostartManual);
    launchDaemon();                                      // no "show", no menu navigation: it waits for the phone
}

void serviceAutomaticLaunch() {
    if (!g_autoRelaunch.pending()) return;
    DaemonState d = readDaemonState();
    if (g_autoRelaunch.ready(monotonicSeconds(), autostartOn(), g_manualStop, d.pid != 0)) {
        g_lastLaunch = 0;
        alog("[Reconnect] settle interval complete; launching a fresh daemon quietly");
        launchDaemon();
    }
}

// Recover either completed setup failures or a continuous absence of SPP nodes.
// The daemon validates the current TCP cycle and missing-node condition again.
void serviceReconnect() {
    nbt::ReconnectTiming timing = reconnectTiming();
    DaemonState d = readDaemonState();
    bool recover = g_reconnectPolicy.evaluate(monotonicSeconds(), autostartOn(), g_manualStop,
                                    g_stopPid != 0 || g_autoRelaunch.pending(), d.pid, !strcmp(d.state, "connected"),
                                    d.setupFailures, d.setupCycle, d.sppWaitSeconds, timing.missingSppSeconds,
                                    d.confirmedCycle, d.confirmationKnown);
    if (g_reconnectPolicy.confirmationRearmed())
        alog("[Reconnect] phone video confirmed (pid %d cycle %u): recovery budget rearmed", d.pid, d.confirmedCycle);
    if (!recover) return;
    // Re-read immediately; the daemon also checks PID/cycle/TCP under its mutex.
    DaemonState latest = readDaemonState();
    if (latest.pid != d.pid || latest.setupCycle != d.setupCycle ||
        (latest.setupFailures < nbt::kReconnectFailedSetups && latest.sppWaitSeconds < timing.missingSppSeconds) ||
        !strcmp(latest.state, "connected")) return;
    char msg[96];
    snprintf(msg, sizeof(msg), "recover %d %u", latest.pid, latest.setupCycle);
    g_stopPid = (pid_t)latest.pid;
    g_autoRestart = g_restartAfterStop = true;
    g_autoStopTime = monotonicSeconds();
    g_wantHome = false;
    sendToDaemon(msg);
    alog("[Reconnect] %u setups without TCP, %u s waiting for SPP; requesting one guarded restart (pid %d cycle %u)",
         latest.setupFailures, latest.sppWaitSeconds, latest.pid, latest.setupCycle);
}

void statusRun(void* /*self*/) {
    bmw::PluginApp* app = g_runApp;
    if (!app) return;                                   // finalized: stop polling
    if (!g_autostartShown) showAutostart(app);
    if (!g_usbLogShown) showUsbLog(app);
    if (!g_menuIdriveShown) showMenuIdrive(app);
    if (!g_stopPid) recoverScreenIfUnclean();            // killed / crashed outside Force Stop
    serviceStop();
    serviceAutomaticLaunch();
    serviceAutostart();
    serviceReconnect();
    DaemonState d = readDaemonState();
    bool connecting = !strcmp(d.state, "connecting"), connected = !strcmp(d.state, "connected");

    unsigned want = connecting ? kTextConnecting : kTextDefault;          // the Media/Radio entry row
    if (want != g_shownText) {
        bmw::TextID text(want);
        app->updateModel(kEntryTextModel, text);
        alog("entry text -> %u (%s)", want, want == kTextConnecting ? "connecting" : "default");
        g_shownText = want;
    }
    if (!fileExists("/opt/aa/spinner.disable")) setConnectingAnimation(app, connecting);
    unsigned row = connected ? kTextRowOpen : connecting ? kTextRowConnecting : kTextStart;   // our menu's Start row
    if (row != g_shownRow) {
        bmw::TextID text(row);
        app->updateModel(kStartRowModel, text);
        alog("start row text -> %u (%s)", row, d.state);
        g_shownRow = row;
    }
    serviceHold(app, d);
    if (connected && strcmp(g_prevState, "connected") != 0 && g_wantHome && holdEnabled()) {
        g_wantHome = false;                              // serviceHold() holds iDrive on state 13 instead of the main menu
    } else if (connected && strcmp(g_prevState, "connected") != 0 && g_wantHome) {
        g_wantHome = false;
        long since = (long)(time(NULL) - g_startTime);
        unsigned ev = menuReturnEvent();
        bool onTop = g_focusKnown ? g_menuFocused : since <= kHomeWindowS;
        if (!ev) alog("connected: return to menu off (NBT_MENU_RETURN=off)");
        else if (!onTop) alog("connected %ld s after Start, our menu not on top (focus %s): no navigation", since,
                              g_focusKnown ? "reported off" : "unknown");
        else {
            app->triggerEvent(ev);
            alog("connected %ld s after Start: event %u -> %s", since, ev, ev == kEvGoHome ? "iDrive main menu" : "Media/Radio list");
        }
    }
    strncpy(g_prevState, d.state, sizeof(g_prevState) - 1);
    app->post(reinterpret_cast<bmw::Runnable*>(&g_statusRunnable), kStatusPollMs);
}

void startStatusPolling(bmw::PluginApp* app) {
    if (fileExists(kStatusOff)) { alog("status polling disabled by %s (no live texts, no return to menu, no stop timeouts)", kStatusOff); return; }
    void** base = reinterpret_cast<void**>(dlsym(RTLD_DEFAULT, "_ZTVN3bmw8RunnableE"));
    if (base) memcpy(g_statusVtbl, base, sizeof(g_statusVtbl));
    else { g_statusVtbl[0] = NULL; g_statusVtbl[1] = NULL; }
    g_statusVtbl[2] = reinterpret_cast<void*>(&runnableNoop);
    g_statusVtbl[3] = reinterpret_cast<void*>(&runnableNoop);
    g_statusVtbl[4] = reinterpret_cast<void*>(&statusRun);
    g_statusRunnable.vptr = g_statusVtbl + 2;
    g_runApp = app;
    app->post(reinterpret_cast<bmw::Runnable*>(&g_statusRunnable), 2000);
    alog("status polling posted (%s every %u ms)", kStateFile, kStatusPollMs);
}

// ---- the app object -----------------------------------------------------------------------------------------------
void* g_vtbl[2 + kVtblSlots];   // [offset-to-top, typeinfo, slots...] copied from bmw::PluginApp's vtable
void* g_baseOnInit = NULL;
bmw::PluginApp* g_app = NULL;

bool onEntryAction(void* /*app*/, const bmw::Parcel& /*args*/) {
    alog("entry button pressed (raAction %u, direct-start XML)", kEntryAction);
    if (g_stopPid) return true;
    g_autoRelaunch.cancel();
    g_manualStop = false;
    g_reconnectPolicy.manualStart();
    startDaemonOrShow();
    return true;
}

// Force Stop / Restart are refused while Android Auto is on screen: then the iDrive menu under the overlay only gets
// knob input meant for the phone. Exit to iDrive first (phone's car icon / BACK hold), then use the menu.
bool guardedByOverlay(const char* what) {
    DaemonState d = readDaemonState();
    if (d.pid > 0 && d.overlay) {
        alog("%s ignored: Android Auto is on screen (stray knob input); exit to iDrive first", what);
        return true;
    }
    return false;
}

bool onStartRow(void*, const bmw::Parcel&) {
    alog("menu: Start (raAction %u)", kActStart);
    if (g_stopPid) { alog("start ignored: a stop/restart is in progress"); return true; }
    g_autoRelaunch.cancel();
    g_manualStop = false;
    g_reconnectPolicy.manualStart();
    startDaemonOrShow();
    return true;
}

bool onStopRow(void*, const bmw::Parcel&) {
    alog("menu: Force Stop (raAction %u)", kActStop);
    g_autoRelaunch.cancel();
    if (g_autoRestart) {
        g_stopPid = 0;
        g_autoRestart = g_restartAfterStop = false;
        alog("[Reconnect] manual Force Stop cancelled the pending automatic relaunch");
    }
    if (g_stopPid || guardedByOverlay("force stop")) return true;
    g_manualStop = true;
    beginStop(false);
    return true;
}

bool onRestartRow(void*, const bmw::Parcel&) {
    alog("menu: Restart (raAction %u)", kActRestart);
    if (g_stopPid || guardedByOverlay("restart")) return true;
    g_autoRelaunch.cancel();
    g_manualStop = false;
    g_reconnectPolicy.manualStart();
    beginStop(true);
    return true;
}

bool onMenuFocus(void*, const bmw::Parcel& p) {
    bool f = false;
    p.readBool(f);
    g_focusKnown = true;
    g_menuFocused = f;
    alog("menu state %u focus -> %d", kMenuState, (int)f);
    // BACK and tilt-left also reach iDrive and pop it from the blank state 13 to its parent, our menu. Until the 1 s
    // status poll put it back, knob turns / pushes drove our menu rows under the Android Auto picture (car
    // 2026-10-28, nbt-aad-app-11: ten pops in 10 s). Put iDrive back on state 13 right here instead.
    if (f && g_app && holdEnabled() && g_holding && g_aaSeen && !callActive()) {
        DaemonState d = readDaemonState();
        if (d.pid > 0 && d.overlay == 1) {               // 2 = BACK held: the hold may be about to minimize
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            long long now = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            static long long s_last = 0;
            if (now - s_last >= 300) {
                s_last = now;
                fireHold(g_app, "BACK/tilt popped to our menu (immediate)");
            }
        }
    }
    return true;
}

// About page text: one-cell Richtext list (see kAboutModel).
void showAbout(bmw::PluginApp* app) {
    bmw::Map<unsigned, bmw::Variant> cells;
    bmw::DataTableBase table(&cells, 1);
    unsigned key = 0;
    bmw::Variant& v = cells[key];
    delete v.m_obj;                                      // fresh cell: NULL; ~Map frees whatever is left
    v.m_obj = new bmw::Variant::ObjectContainer<bmw::TextID>(bmw::TextID(kAboutText));
    app->updateModel(kAboutModel, table);
    alog("About: Richtext list model %u <- TextID %u", kAboutModel, kAboutText);
}

void addAction(bmw::PluginApp* self, unsigned id, bool (*stub)(void*, const bmw::Parcel&)) {
    bmw::ActionDelegate d;
    d.object = self;
    d.stub = stub;
    self->addHandler(id, d);
}

bool appOnInit(bmw::PluginApp* self) {
    addAction(self, kEntryAction, &onEntryAction);
    addAction(self, kActStart, &onStartRow);
    addAction(self, kActStop, &onStopRow);
    addAction(self, kActRestart, &onRestartRow);
    addAction(self, kActAutostart, &onAutostartToggle);
    addAction(self, kActUsbLog, &onUsbLogToggle);
    addAction(self, kActMenuIdrive, &onMenuIdriveToggle);
    g_initTime = time(NULL);
    {
        bmw::ActionDelegate d;
        d.object = self;
        d.stub = &onMenuFocus;
        self->addHandler(kMenuState, 1, d);
        bmw::ActionDelegate f;
        f.object = self;
        f.stub = &onAaFocus;
        self->addHandler(kAaState, kHandlerFocus, f);
        bmw::ActionDelegate v;
        v.object = self;
        v.stub = &onAaVisible;
        self->addHandler(kAaState, kHandlerVisible, v);
    }
    bool ok = self->onInit();
    showAbout(self);
    nbt::ReconnectTiming timing = reconnectTiming();
    alog("[Reconnect] timing profile %s: startup %u s, missing SPP %u s, quiet relaunch %u s",
         timing.conservative ? "conservative" : "fast", timing.autostartSeconds,
         timing.missingSppSeconds, timing.quietSeconds);
    alog("onInit: raActions %u/%u/%u/%u/%u/%u/%u + menu focus handler registered, base onInit -> %d, start with the car %s, "
         "save logs to USB %s, MENU opens iDrive %s", kEntryAction, kActStart, kActStop, kActRestart, kActAutostart,
         kActUsbLog, kActMenuIdrive, (int)ok, autostartOn() ? "on" : "off", nbt::usblog::enabled() ? "on" : "off",
         menuIdriveOn() ? "on" : "off");
    subscribeGear(self);                                 // gear (unless gear.disable) + call status (Fork F hold)
    startStatusPolling(self);
    return ok;
}

void appOnError(bmw::PluginApp* /*self*/, unsigned code, const bmw::BasicString<char>& text) {
    const char* s = text.raw();
    alog("onError %u: %s", code, s ? s : "(null)");
}

void appOnFinalize(bmw::PluginApp* self) {
    alog("onFinalize");
    g_runApp = NULL;
    self->onFinalize();
}

bmw::PluginApp* createApp(bmw::IDispatcher& dispatcher, const char* name, const bmw::Bundle& params, const bmw::Bundle& config) {
    alog("create(name=%s) %s", name ? name : "(null)", NBT_AAD_APP_VERSION);
    if (fileExists(kDisable)) { alog("disabled by %s", kDisable); return NULL; }
    if (g_app) { alog("create: already created"); return NULL; }

    void* mem = ::operator new(bmw::kPluginAppStorage);
    memset(mem, 0, bmw::kPluginAppStorage);
    bmw::PluginApp* app = new (mem) bmw::PluginApp(dispatcher, bmw::BasicString<char>(name ? name : "NbtAad"), 1, 0,
                                                   bmw::RHMI_INTERFACE_VERSION_1, params, config);

    // Own vtable: identical to bmw::PluginApp's, with our onInit/onError/onFinalize.
    void** base = *reinterpret_cast<void***>(app);
    memcpy(g_vtbl, base - 2, sizeof(g_vtbl));
    g_baseOnInit = g_vtbl[2 + kSlotOnInit];
    g_vtbl[2 + kSlotOnInit]     = reinterpret_cast<void*>(&appOnInit);
    g_vtbl[2 + kSlotOnError]    = reinterpret_cast<void*>(&appOnError);
    g_vtbl[2 + kSlotOnFinalize] = reinterpret_cast<void*>(&appOnFinalize);
    *reinterpret_cast<void***>(app) = g_vtbl + 2;

    g_app = app;
    alog("create: app %p (PluginApp vtable %p)", (void*)app, (void*)base);
    return app;
}

void destroyApp(void* app) {
    alog("destroy(%p)", app);
    if (!app) return;
    void** vt = *reinterpret_cast<void***>(app);
    reinterpret_cast<void (*)(void*)>(vt[1])(app);   // deleting destructor (PluginApp::~PluginApp + operator delete)
    if (app == g_app) g_app = NULL;
}

__attribute__((constructor)) void libraryLoaded() {
    alog("library loaded into pid %d, %s", (int)getpid(), NBT_AAD_APP_VERSION);
}

} // namespace

// ---- entry point looked up by ManagedPluginApp::init (dlsym "pluginAppInfo") -----------------------------------
extern "C" {
struct NbtPluginAppInfo {
    uint16_t versionMajor;
    uint16_t versionMinor;
    const char* name;
    const char* vendor;
    uint32_t rhmiInterfaceVersion;
    bmw::PluginApp* (*create)(bmw::IDispatcher&, const char*, const bmw::Bundle&, const bmw::Bundle&);
    void (*destroy)(void*);
};
__attribute__((visibility("default"))) NbtPluginAppInfo pluginAppInfo = {
    1, 0, "Android Auto", "NBT-AAD", 1, &createApp, &destroyApp
};
}

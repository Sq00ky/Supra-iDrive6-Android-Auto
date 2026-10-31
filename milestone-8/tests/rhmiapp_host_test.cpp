// Host test: load libNbtAad.so like ManagedPluginApp (dlsym pluginAppInfo), drive create/onInit/action/onError/destroy.
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
namespace bmw {
class IDispatcher; class Bundle; class Parcel;
template <typename T> class BasicString { public: BasicString(const T*); BasicString(const BasicString&); ~BasicString(); T* m_data; };
class Runnable;
template <typename R, typename A1, typename A2, typename A3, typename A4> struct Delegate { void* object; R (*stub)(void*, A1); };
typedef Delegate<bool, const Parcel&, void, void, void> ActionDelegate;
typedef Delegate<bool, const BasicString<unsigned short>&, void, void, void> CdsDelegate;
#define D(n) virtual void s##n();
class PluginApp {
public:
    virtual ~PluginApp();
    D(2) D(3) D(4) D(5) D(6) D(7) D(8) D(9) D(10) D(11) D(12) D(13) D(14) D(15) D(16) D(17) D(18) D(19) D(20)
    virtual void onError(unsigned code, const BasicString<char>& text);
    virtual bool onInit();
    virtual void onFinalize();
    D(24) D(25) D(26) D(27) D(28) D(29) D(30) D(31) D(32) D(33)
    char name[64]; unsigned handlerId; ActionDelegate handler; Runnable* posted; CdsDelegate cds; char cdsName[64];
};
}
struct Info { uint16_t a, b; const char* name; const char* vendor; uint32_t rhmi;
              bmw::PluginApp* (*create)(bmw::IDispatcher&, const char*, const bmw::Bundle&, const bmw::Bundle&); void (*destroy)(void*); };
int main(int argc, char** argv) {
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    Info* info = (Info*)dlsym(h, "pluginAppInfo");
    if (!info) { printf("no pluginAppInfo\n"); return 1; }
    printf("pluginAppInfo: %u.%u name=%s vendor=%s rhmi=%u\n", info->a, info->b, info->name, info->vendor, info->rhmi);
    char dummy[16];
    bmw::PluginApp* app = info->create(*(bmw::IDispatcher*)dummy, "NbtAad", *(bmw::Bundle*)dummy, *(bmw::Bundle*)dummy);
    if (!app) { printf("create returned NULL\n"); return 1; }
    bool ok = app->onInit();                       // framework calls these virtually
    printf("onInit -> %d, handler id %u\n", (int)ok, app->handlerId);
    if (app->handlerId != 2 || !app->handler.stub) { printf("FAIL: no handler for raAction 2\n"); return 1; }
    app->handler.stub(app->handler.object, *(const bmw::Parcel*)dummy);   // entry button press
    if (!app->posted) { printf("FAIL: gear subscription not posted\n"); return 1; }
    void** rv = *(void***)app->posted;
    ((void (*)(void*))rv[2])(app->posted);                                 // framework runs the Runnable
    if (strcmp(app->cdsName, "driving.gear") || !app->cds.stub) { printf("FAIL: no CDS handler for driving.gear\n"); return 1; }
    const unsigned short v1[] = { '{','"','g','e','a','r','"',':','2','}',0 };
    const unsigned short v2[] = { '{','"','g','e','a','r','"',':','1','}',0 };
    bmw::BasicString<unsigned short> s1(v1), s2(v2);
    app->cds.stub(app->cds.object, s1);                                    // shift to R
    app->cds.stub(app->cds.object, s1);                                    // repeat: must not log again
    app->cds.stub(app->cds.object, s2);
    bmw::BasicString<char> msg("integration error");
    app->onError(7, msg);
    app->onFinalize();
    info->destroy(app);
    printf("rhmiapp host test OK\n");
    return 0;
}

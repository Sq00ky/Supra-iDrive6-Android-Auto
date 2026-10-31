// Host test stand-in for libAppFramework.A.so: a bmw::PluginApp with the same 34-slot vtable layout.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
namespace bmw {
class IDispatcher; class Bundle; class Parcel;
enum RHMIInterfaceVersion { V1 = 1 };
template <typename T> class BasicString {
public:
    BasicString(const T* s) : m_data(strdup(s)) {}
    BasicString(const BasicString& o) : m_data(strdup(o.m_data)) {}
    ~BasicString() { free(m_data); }
    T* m_data;
};
template class BasicString<char>;
template <> BasicString<unsigned short>::BasicString(const unsigned short* s) { size_t n = 0; while (s[n]) ++n; m_data = (unsigned short*)malloc((n + 1) * 2); memcpy(m_data, s, (n + 1) * 2); }
template <> BasicString<unsigned short>::~BasicString() { free(m_data); }
class Runnable { public: virtual ~Runnable(); virtual void run() = 0; };
Runnable::~Runnable() {}

template <typename R, typename A1, typename A2, typename A3, typename A4> struct Delegate { void* object; R (*stub)(void*, A1); };
typedef Delegate<bool, const Parcel&, void, void, void> ActionDelegate;
typedef Delegate<bool, const BasicString<unsigned short>&, void, void, void> CdsDelegate;
#define D(n) virtual void s##n() { printf("FAKE slot %d\n", n); }
class PluginApp {
public:
    PluginApp(IDispatcher&, BasicString<char> name, unsigned a, unsigned b, RHMIInterfaceVersion v, const Bundle&, const Bundle&);
    virtual ~PluginApp();
    D(2) D(3) D(4) D(5) D(6) D(7) D(8) D(9) D(10) D(11) D(12) D(13) D(14) D(15) D(16) D(17) D(18) D(19) D(20)
    virtual void onError(unsigned code, const BasicString<char>& text);
    virtual bool onInit();
    virtual void onFinalize();
    D(24) D(25) D(26) D(27) D(28) D(29) D(30) D(31) D(32) D(33)
    void addHandler(unsigned id, const ActionDelegate& d);
    void post(Runnable* r, unsigned ms);
    void cdsAddPropertyChangedEventHandler(BasicString<char>& name, unsigned id, const CdsDelegate& d);
    char name[64];
    unsigned handlerId;
    ActionDelegate handler;
    Runnable* posted; CdsDelegate cds; char cdsName[64];
};
PluginApp::PluginApp(IDispatcher&, BasicString<char> n, unsigned a, unsigned b, RHMIInterfaceVersion v, const Bundle&, const Bundle&) {
    snprintf(name, sizeof(name), "%s", n.m_data); posted = 0; cds.object = 0; cds.stub = 0; cdsName[0] = 0; handlerId = 0; handler.object = 0; handler.stub = 0;
    printf("FAKE PluginApp(%s, %u, %u, %d)\n", name, a, b, (int)v);
}
PluginApp::~PluginApp() { printf("FAKE ~PluginApp(%s)\n", name); }
void PluginApp::onError(unsigned c, const BasicString<char>& t) { printf("FAKE base onError %u %s\n", c, t.m_data); }
bool PluginApp::onInit() { printf("FAKE base onInit\n"); return true; }
void PluginApp::onFinalize() { printf("FAKE base onFinalize\n"); }
void PluginApp::addHandler(unsigned id, const ActionDelegate& d) { handlerId = id; handler = d; printf("FAKE addHandler(%u)\n", id); }
void PluginApp::post(Runnable* r, unsigned ms) { posted = r; printf("FAKE post(runnable, %u ms)\n", ms); }
void PluginApp::cdsAddPropertyChangedEventHandler(BasicString<char>& n, unsigned id, const CdsDelegate& d) {
    snprintf(cdsName, sizeof(cdsName), "%s", n.m_data); cds = d; printf("FAKE cdsAddPropertyChangedEventHandler(%s, %u)\n", cdsName, id);
}
} // namespace bmw

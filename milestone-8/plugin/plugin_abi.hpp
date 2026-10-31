// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_AAD_PLUGIN_ABI_HPP
#define NBT_AAD_PLUGIN_ABI_HPP

// ABI of the HmiMain3D plugin framework (PluginFramework in /opt/hmi/ID5/lib/libhmiplugin.so), reversed from
// the shipped A4A bundle (libhmia4a.so, A4ABundle.cpp) on 2026-10-23:
//
//  * HmiMain3D scans <hmi data ro>/hmiplugins/*.xml at boot. Each <Bundle path="stem"> names lib<stem>.so
//    in the same directory; every exportedSymbolName N (bundle and each <Plugin>) must be exported as the
//    data symbol "N" + "Struct" (dlsym, see DynamicLibraryPluginProxy::getPluginStructName).
//  * NStruct = { create, destroy, compiler }. PluginFramework::CompilerCheck refuses the library unless
//    compiler equals the HMI's own string "GCC, Version 4.4.2".
//  * create(void** iface, void** plugin, void** object): iface = the implemented-interface subobject,
//    plugin = the subobject with the plugin vtable, object = the complete object (handed to destroy).
//    A4ABundle returns the same pointer three times; CA4AAudioService returns this+0x14 / this+0x10 / this.
//  * Plugin vtable (bundle and plugins alike): [0] ~D1, [1] ~D0 (deleting), [2] int start(IDependencyResolver&),
//    [3] int stop(IDependencyResolver&). start/stop return 0 on success.
//  * IDependencyResolver vtable: [2] int resolve(const capu::String& name, void** out, int), [4] release(void**).
//    A4ABundle::start resolves "IHmiPluginManager", "CHmiMainDispatcher", "CSvcPlatformBase" (not used yet).
//  * Confirmed in libhmiplugin.so (PluginFramework, 2026-10-23):
//    - XmlScanner::VisitEnter: <Bundle> needs 'path' (lib prefix/suffix added); the library is looked up next to
//      the XML, in ./QNX_ARMV7L/ or ./QNX/ first. Each <Plugin> gets an automatic dependency on its bundle's
//      exportedSymbolName, so the bundle is started before its plugins.
//    - DynamicLibraryPluginProxy::loadInternal: dlsym(name + "Struct"), CompilerCheck::areCompatible against the
//      HMI's own "GCC, Version 4.4.2", then create(&iface, &plugin, &object); ALL THREE must be non-NULL or the
//      proxy logs an error and refuses the plugin (returns 4). LibraryCheck::isValid always returns 1 (no signature).
//    - startInternal / stopInternal call plugin->vtable[2] / [3]; non-zero = logged error, proxy state 4.
//    - unloadInternal calls destroy(object) then dlclose.
//  * BMW's plugins use GNU libstdc++ (libstdc++.so.6), so this library must be built with the _gpp variant,
//    never the Dinkumware _cpp variant the daemon uses.

#include <stddef.h>

namespace nbt {

typedef void (*PluginCreateFn)(void** iface, void** plugin, void** object);
typedef void (*PluginDestroyFn)(void* object);

struct PluginStruct {
    PluginCreateFn  create;
    PluginDestroyFn destroy;
    const char*     compiler;
};

// Layout-compatible with the framework's plugin interface (first four vtable slots).
class IHmiPluginLike {
public:
    virtual ~IHmiPluginLike() {}
    virtual int start(void* dependencyResolver) = 0;
    virtual int stop(void* dependencyResolver) = 0;
};

#define NBT_AAD_PLUGIN_COMPILER "GCC, Version 4.4.2"

// ---------------------------------------------------------------------------------------------
// Stage 3 reverse-engineering notes: HmiMain3D (ET_EXEC, fixed load address, no ASLR; addresses
// below are HmiMain3D virtual addresses = file offset + 0x100000). Firmware-build specific.
//
// MAIN MENU = registered "applications" (players). IHmiHome is a plugin interface (resolve
// "IHmiHome" through the start() dependency resolver, like A4ABundle resolves IHmiPluginManager).
//   IHmiHome vtable: [0] D1, [1] D0, [2] registerPlayer(CHmiApplicationAbstract*) -> bool,
//   [3] unregisterPlayer(app), [6] getActivePlayer(int). Impl: CHmiHomeApplication 0x2f2dac/0x2f2dfc
//   -> CHmiActivityMainMenu (this+0x50) vtbl slot 101 registerPlayer @0x305820 (Mini variant 0x312e94).
//   registerPlayer refuses duplicates (same type) and apps whose slot 22 returns non-zero.
//
// CHmiApplicationAbstract (vtable _ZTV23CHmiApplicationAbstract @0x37afce8 is EXPORTED, address
// point 0x37afcf0, 29 slots); base ctor @0x182a004(this, int type, const SHmiResourceHandle* text,
// int stateId): +0x04 u8, +0x08 type, +0x0c text handle, +0x10 u8, +0x14 0, +0x18 stateId,
// +0x1c..0x28 list, +0x2c 0, +0x30 10.  CHmiCarPlayApplication ctor @0x452cc0 = base(0x10, h, 0x8ef),
// +0x34 = 0, object >= 0x38 B (vtable 0x2b05370).
//   slot  2 0x08 collectDependencies(list)   (pure)        slot 15 0x3c out: handle -> entry+0x4c
//   slot  3 0x0c isAvailable? (CarPlay: 1)    (pure)        slot 16 0x40 out: icon handle (CarPlay
//   slot  4 0x10 init()                        (pure)                0x50000859) -> entry vtbl+0x48
//   slot  6 0x18 forwards to slot 8           (pure)        slot 17 0x44 out: text handle -> entry vtbl+0x3c
//   slot  8 0x20 stateId (CarPlay 0x8ef)       (pure)        slot 19 0x4c out: string handle
//   slot  9 0x24 out: text handle (base: +0x0c) -> entry ctor arg3
//   slot 10 0x28 focusId (CarPlay 0x2535)  -> entry ctor arg1
//   slot 11 0x2c type (base: +0x08)        -> picks entry class: 0x10 CarPlay, 0x11 CarLife, else generic
//   slot 21 0x54 stateId (base: +0x18)     -> entry ctor arg2 (entry+0x68)
//   slot 22 0x58 must return 0 or IHmiHome::registerPlayer ignores the app
//   slot 23 0x5c value -> 0x332464(entry, v)
// App types in this build: 1 Ent, 2 Communication, 3 Navi, 4 ConnectedDrive, 5 Notifications, 6 Car,
// 7 Home, 8 Parking, 9 SideView, 10 NightVision, 15 Common, 16 CarPlay, 17 CarLife, 19 Speech.
// Unknown types only trigger a non-fatal HmiAssertComment in 0x3031a8 (returns -1).
//
// Generic entry CHmiMainMenuEntry: new(0x9c), ctor @0x332c00(this, focusId, stateId, textHandle);
// vtable 0x2ac4b08 (60 slots + secondary at +8). CarPlay's entry only overrides slots 0,1,2.
// slot 2 = execute: default @0x3326a4 = stateManager(FUN_00227768()->+8)->vtbl[7](stateId, 1, 0),
// i.e. it opens HMI state <stateId>. CarPlay's execute 0x460bcc requests the CarPlay UI instead.
// Setters on the entry: slot 15 0x2518fd0 text resource (+0x80), slot 18 0x25188b8 (+0x30/+0x84,
// notify 5), slot 35 0x2518824 (+0x74 hi16).
// Menu providers: CHmiMenuProviderRegistry (plugin instance "CHmiMenuProviderRegistry", getter
// 0x300084); lookup by stateId 0x294528; find entry by focusId over all providers 0x2945fc
// (provider vtbl+0x18(out, provider, focusId)). CHmiMenuProviderBase::addEntry 0x251d160 (vtbl[2]);
// main menu provider created for "ST_Start_Main_Selection" in 0x30b224.
//
// ENTERTAINMENT list = CEntPlayerBase objects (ctor 0x535948(this, IHmiContext*); 47-slot vtable
// 0x2b293a8, slots 15..29 pure). CEntPlayerCarPlay: new(0xb0), ctor 0x45ff08, created by
// CHmiCarPlayApplication::init 0x453008 and initialised via its vtbl[4]; its slot 38 0x4601fc
// creates CHmiCarPlayEntMenuEntry. Needed for the Media/Entertainment entry + audio source (later).
//
// MEDIA/RADIO SOURCE LIST (2026-10-24, Ghidra): the Entertainment app init FUN_00605a5c creates
//   CEntSelectionMenuProvider (new 0xcc, ctor 0x672240(this, entApp+0x10), vtable 0x2b88130) named
//   "ST_Ent_Selection" (base ctor 0x251ea00(this, name, 0x14, 1, 0x16a, 1)) and registers it in
//   CHmiMenuProviderRegistry exactly like Communication does. Same vtable as CHmiMenuProviderWithConfig except
//   slots 0/1 (dtors) and 23 (0x671d80, state filter for 0x3f5/0x226/0x6ba); addEntry is the same 0x251d160.
//   Players create their source entries in vtbl slot 38 (0x98) from slot 18 stateId, slot 19 focusId,
//   slot 20 list order, slot 22 text, slot 23 icon:
//     CarPlay   state 0x8ef focus 0x2003 order 0x2003      Bluetooth state 0x3f5 focus 0x8de order 0x1bae
//     USB       state 0x226 focus 0x379  order 0x1d7d
//   Default placement of our entry: order 0x1c00 (between Bluetooth audio and USB).
//
// HOOK-FREE ROUTE (2026-10-24, in progress):
//   * Menu lookup (native): resolve "CHmiMenuProviderRegistry" through the plugin framework (initMenus does it via
//     getCurrentThreadPluginScope()->resolve). HmiDescription::getListOrder 0x111ee5c shows the lookup: holder =
//     0x300084() (instance cache, registry at holder+8), then 0x294528(&intrusive_ptr out, registry, &stateId) ->
//     provider (release 0x2891bc). provider->vtbl[4]() = its StateID. Registry vtable 0x2ab2b38 has only dtors.
//   * Selection: our own subclass of CHmiGenericMenuEntry (own object + own vtable) or its delegate (+0x78/+0x90).
//   * TIMING SOLUTION (libhmicom.so, Ghidra 2026-10-24) - run our code on the HMI (AppProcUnit) thread with
//     iDrive's own timer service, no patching:
//       - IHmiProcUnit vtbl[3] (+0xc) = getTimer() -> IHmiTimer* (CHmiProcUnit::getTimer = this+0x1a4).
//         getHmiTimer(IHmiProcUnit* pu) = pu ? pu->vtbl[3]() : getHmiProcUnit()->vtbl[3]() (current thread only).
//       - IHmiTimer vtbl[1] = _setTimer(this, void* target, memfn{ptr, adj}, uint firstMs, uint periodMs,
//         ushort user) -> timer id (exactly how CPrcTimer::start calls it: r1=target, r2/r3=memfn, sp0=+0x20,
//         sp4=+0x24, sp8=ldrh +0x28). periodMs < 1 = one-shot. Minimum 10 ms. CHmiTimerSource::addEntry takes
//         its own pthread mutex, so arming from another thread is safe; the expiry is posted as a
//         CHmiTimerEvent to the proc unit and runs there: target->memfn(timerId, user32 (+0x24), ushort).
//         memfn ABI: ptr = function, adj = (thisAdj << 1) | isVirtual -> {fn, 0} for a plain function.
//       - Finding the AppProcUnit from a plugin thread: CHmiMainDispatcher (plugin name "CHmiMainDispatcher",
//         A4ABundle resolves it) holds std::map<uint eventGroup, std::vector<IHmiProcUnit*>> at this+4 (header,
//         leftmost at +0xc; node key +0x10, vector begin/end +0x14/+0x18), guarded by COSMutex at this+0x18
//         (COSMutex::take/give exported). Pick the unit whose RTTI name is "15CHmiAppProcUnit".
//         CHmiProcUnit::getID() const = byte at +0xce.
//       - Exported from HmiMain3D (dlsym, no fixed address needed): THmiString<ushort>(const char*)
//         _ZN10THmiStringItEC1ItvEEPKc, ~THmiString _ZN10THmiStringItED1Ev. Menu classes are NOT exported.
//   * Timing (earlier notes): apps are initialised in list order Home, Common, Notifications, Parking, SideView,
//     NightVision, Car, Ent, CarPlay, CarLife, Speech, Communication, Navi, CDr (initializeApplications 0x21be04:
//     loop 1 = slot 3 "init services" on all apps, loop 2 = slot 4 init). Navi init 0x1228234 -> 0x1226504
//     registers "IRbController" (0x16bd2cc) and "IRbHmiCurrentRoadService" on the HMI thread right after
//     Communication's initMenus. PluginProxy::start -> notifyPluginAvailable -> PluginUsage::setPlugin ->
//     CHmiPluginUsage::notifyPluginAvailable -> user->vtbl[2](plugin), synchronous on the registering thread.
//     Unknown: whether a usage can be registered before the proxy exists (registerProxy does not start dependents;
//     "No proxy for plugin id" suggests not). A4A uses CHmiDispatcher listeners (onFinalizeApp) + IHmiTimer.
//
// STAGE 3 IMPLEMENTATION FACTS (2026-10-24, used by nbt_aad_plugin.cpp):
//   ST_Com_Selection provider = CHmiMenuProviderWithConfig (vtable 0x2fe4e98, 32 slots, slot 2 = addEntry 0x251d160,
//   typeinfo name @0x2fe4ff0). initMenus resolves CHmiMenuProviderRegistry through getCurrentThreadPluginScope(),
//   keeps the provider only in a register, and adds entries with provider->vtbl[2](provider, &intrusive_ptr).
//   Entry class CHmiGenericMenuEntry: vtable 0x2fe4320 (61 slots, single inheritance, typeinfo name @0x2fe4434),
//   slot 2 execute 0x2519fdc(this, int), slot 56 (+0xe0) focus getter 0x333f94 (returns +0x78). Refcount +4 (plain
//   ldr/add/str). THmiString<unsigned short>(const char*) ctor 0x25bd0c, dtor 0x228b10. Mobile devices entry is kept
//   in CHmiCommunicationApplication +0xac. Order of boot: CHmiMainProcess::init (stack object in main) constructs
//   CHmiAppProcUnit (0x21b8a4), loads hmiplugins (0x23a2ac), THEN starts the AppProcUnit thread, whose
//   init -> createApplications (0x2179f4) and finalizeInitialization -> initializeApplications (0x21be04) ->
//   app init (vtbl slot 4) -> CHmiCommunicationApplication::initMenus 0xc046b8. autostartDelay > 0 starts the
//   plugin on PluginFramework::Executor (worker thread), so keep autostartDelay="0".
// COMMUNICATION MENU (2026-10-23, preferred launch point: native CarPlay pairing lives in
// Communication > Mobile devices). CHmiCommunicationApplication vtbl slot 4 (init) 0xc070b8 calls
// initMenus 0xc046b8, which builds the "ST_Com_Selection" menu provider: new(0xc0), ctor
// 0x251e898(this, &THmiString name, 0x1e, 2, 0x28c, 2), registered with CHmiMenuProviderRegistry via
// 0x2947e8(registry, provider->vtbl[4](), provider->vtbl[16](), &intrusive_ptr). Entries are generic
// state-opening entries: new(0x94), ctor 0x251a3a4(this, &THmiString targetState, focusId, order,
// [sp] &text, &text2, &icon, 0, &0x20000001, 0, &0x20000001); added with provider->vtbl[2]
// (= CHmiMenuProviderBase::addEntry 0x251d160). Example "Mobile devices": state "ST_MyV_DeviceList",
// focusId 0x1176, order 0x58f, text 0x2000ed20, icon 0x50000513. Contacts: "ST_Com_Contacts", 0x200,
// 0x2dd, text 0x20000d90, icon 0x50000216. Calls use CHmiMenuEntryCarPlayTelephony (: CHmiMenuEntry-
// ContentPreview, vtable 0x2ca06a0, ctor 0xc28cd4, execute slot 2 0xc291cc).
// MEDIA & RADIO: sources are CEntPlayerBase objects; CEntPlayerCarPlay slot 6 0x45f8f0 creates its
// CEntModeHandlerCarPlay (new 0x38, ctor 0x46cd14 -> base 0x525248(this), 22-slot vtable 0x2b08858
// overriding only 15/16 dtors and 21 activate 0x46d05c = IHmiCarPlayService->vtbl[7]("hu:play")),
// and slot 38 0x4601fc creates CHmiCarPlayEntMenuEntry (0xb4, ctor 0x46a83c, "nowplaying:"). Needs a
// full player object -> heavier than the Communication entry. With NBT_AUDIO_BT the existing
// "Bluetooth audio" media source already plays AA media.
//
// Resource handles: top byte = SHmiResourceHandle::EType (0x10 PNG, 0x11 JPG, 0x12 TGA, 0x13 BMP,
// 0x16 ARGB, 0x17 Bitmap, 0x20 String, 0x22 StringBundle, 0x24 Ascii, 0x28 SpeechBundle, 0x30 ASSET,
// 0x50 WidgetAsset, 0x60 Widget). Dynamic images: libhmidb CHmiResourceManager::
// getDynamicFileResourceProvider() / CHmiDynamicFileResourceProvider::registerDynamicResource(
// EType, const THmiString<char>& path, bool). CHmiResourceManager is registered as plugin
// "IHmiResourceManager" (registerStaticPlugin<CHmiResourceManager, IHmiResourceManager>).
// ---------------------------------------------------------------------------------------------

} // namespace nbt

#endif // NBT_AAD_PLUGIN_ABI_HPP

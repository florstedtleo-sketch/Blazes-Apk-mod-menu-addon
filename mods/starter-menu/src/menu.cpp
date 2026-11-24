// Leo's Menu: a hand menu in the style of ii's Stupid Menu (Gorilla Tag), built on the
// Blaze starter menu.
//   Hold Y (left hand)      -> menu on your left hand, tipped like ii's; turn your palm to you
//                              (B and the right hand with "Right Hand Menu")
//   Touch a button with your other index finger to press it
//   < and > on the sides    -> previous / next page
//   Notifications show right above the menu.
//   Pages: Settings, Movement, Spawning, Overpowered, Rig Mods, Soundboard,
//          Visual, Fun, Enabled Mods
// Update 4: HSounds soundboard (wav/mp3/ogg, sent to voice chat), projectile sliders, Infinite Cotton, Levitation Gun, MenuItem Rain,
// Update 10: Search + keyboards, Owner code, Favourites, Other Players, Games (weapons, blood), Info, Target Lock, Nametags, Trail ESP,
//   Ripple, Black theme, Text Colour, Gun Effect, 3-step Size, potion fallbacks, platform fixes, Freecam.
// Update 8: Noclip (platforms stay solid), Sticky / Follow Hand platforms, Ghost + Inv Monkey, Floating + Toggle menu, Tracers, Box ESP,
//   Leaderboard Color, glow Bloom, Disable Boundary scripts, Rainbow Sky, Fireball (Experimental), Spawn Presets (Frogling), Drone,
//   Mic Changer, Solid Players, Purple / Yellow / White themes, Colorblind Mode; placeholder spawners removed.
// Update 7: smooth noclip only (Noclip Fly removed), Joystick Fly, Zero Gravity Fly, CS2 Strafe, Size Changer (Experimental), Punch Fling,
//   Pixelated menu type; Time Rewind removed; Enabled Mods first on the main page.
// Update 6 (V6.5): Translucent theme + Translucency slider, Round Corners slider, Page Buttons category (under menu name /
//   beside / bottom), Long Menu type (every mod of a category on one tall page).
//   server-sided Fireball / Orbit. Update 5: Bloom, flashlight light in Horror, All Hear Sounds, HSounds folder fallbacks.
// Update 3: Menu Type (Hand / Corner / Pizza menu), Dark Gray theme, MenuItem Orbit, 9x9 / row / YEEPS-text spawns,
//   placeholder-cube gun + 3x3, Spider Yeeps, Smooth Noclip Fly, Flip Gravity, Dash, Teleport Dash, Time Rewind,
//   Rain, shader-based Fullbright and Horror, Fake Lag, Air Swim; No Knockback and Cotton Limit reworked;
//   Bullet Amount / Bullet Spread removed.
// (Iron Man, Velocity Fly, Teleport To Closest, Save/Goto Position, Spawn Circle, Delete MenuItem Gun,
//  No Knockback, item sliders, Fireball, Bounce and Wave were added in the Nov 2026 update.)
// Almost everything only changes your own game. Rig mods and Spawning are seen by the room,
// within the limits in AGENTS.md ("Things others see"). Read docs/GUIDE.md before adding.
#include "ymsdk.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <string>
#include <unordered_set>
#include <vector>
#include "phone_icons.h"
#include "sky_textures.h"

using namespace BNM;
using namespace BNM::Structures::Unity;
using BNM::IL2CPP::Il2CppObject;

YEEPS_MOD("Leo's Menu")

// ---- small helpers ---------------------------------------------------------

static std::string TypeName(const BNM::IL2CPP::Il2CppType *t) {
    Class c(t);
    if (!c.IsValid() || !c.GetClass() || !c.GetClass()->name) return "?";
    auto *k = c.GetClass();
    return (k->namespaze && k->namespaze[0]) ? std::string(k->namespaze) + "." + k->name : k->name;
}
// Pick an overload by its exact parameter types, e.g. GetComponent(System.Type).
static MethodBase Overload(Class c, const char *name, std::vector<std::string> types) {
    for (auto &m : c.GetMethods(false)) {
        auto *mi = m.GetInfo();
        if (!mi || !mi->name || strcmp(mi->name, name) != 0 || mi->parameters_count != types.size()) continue;
        bool ok = true;
        for (size_t i = 0; i < types.size() && ok; i++) ok = TypeName(mi->parameters[i]) == types[i];
        if (ok) return m;
    }
    return {};
}

// Unity objects we keep between frames must be held by a GC handle, or the
// garbage collector can free them under us.
static void *(*p_handleNew)(void *, bool);
static void *(*p_handleTarget)(void *);
static void (*p_handleFree)(void *);
struct Ref {
    void *h = nullptr;
    void set(Il2CppObject *o) { clear(); if (o && p_handleNew) h = p_handleNew(o, false); }
    Il2CppObject *get() const { return h && p_handleTarget ? (Il2CppObject *)p_handleTarget(h) : nullptr; }
    void clear() { if (h && p_handleFree) p_handleFree(h); h = nullptr; }
};

static Color Lerp(Color a, Color b, float t) {
    return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1);
}
static Color Rainbow(float h) {             // hue 0..1 -> bright color
    h = h - floorf(h);
    float r = fabsf(h * 6 - 3) - 1, g = 2 - fabsf(h * 6 - 2), b = 2 - fabsf(h * 6 - 4);
    auto c = [](float v) { return v < 0 ? 0.f : v > 1 ? 1.f : v; };
    return Color(c(r), c(g), c(b), 1);
}

// ---- game lookups, resolved once -------------------------------------------

struct XRDevice { uint64_t id; bool initialized; };
static struct {
    bool ok = false;
    Method<Il2CppObject *> createPrimitive, goTransform, goGetComponent, goAddComponent, compTransform, compGameObject;
    Method<void> goSetActive, destroy, trSetParent, trSetPosition, trSetRotation, trSetLocalPosition,
                 trSetLocalRotation, trSetLocalScale, rbSetVelocity, matSetColor, matSetShader,
                 textSetFont, textSetText, textSetFontSize, textSetAlignment, graphicSetColor,
                 canvasSetRenderMode, rtSetSizeDelta, textSetFontStyle, textSetBestFit, textSetMinSize,
                 textSetMaxSize, teleportTo, rbAddForce, rbSetUseGravity, colliderSetEnabled,
                 lrSetPositionCount, lrSetPosition, lrSetStartWidth, lrSetEndWidth, lrSetUseWorldSpace;
    Method<Quaternion> trGetRotation;
    Method<Vector3> trGetPosition, trGetForward, trGetRight, trGetUp, trInverseTransformPoint, trTransformPoint, rbGetVelocity;
    Method<bool> alive, tryGetBool, haptic, raycast, tryGetVec3, tryGetQuat;
    Method<XRDevice> deviceAtNode;
    Method<Il2CppObject *> trGetParent, trGetRoot, getComponentsInChildren, netLocal;
    Method<void> pvRpc;
    BNM::IL2CPP::MethodInfo *rpcCreateItem = nullptr;     // only read for its parameter types
    BNM::IL2CPP::Il2CppClass *objectClass = nullptr, *stringClass = nullptr;
    BNM::MonoType *tPhotonView = nullptr, *tItem = nullptr;
    Method<BNM::Structures::Mono::String *> itemKey, itemId;
    Method<bool> itemHeld;
    Method<void> itemDrop, setHeldItem;
    void *(*arrayNew)(void *, uintptr_t) = nullptr;
    void *(*valueBox)(void *, void *) = nullptr;
    Method<bool> behaviourGetEnabled;
    Method<void> behaviourSetEnabled;
    std::vector<BNM::MonoType *> trackerTypes;
    Method<Il2CppObject *> rendererGetMaterial, shaderFind, findAll, findScene, findSceneByType, builtinResource;
    Field<Il2CppObject *> leftHand, rightHand, headTransform, bodyCollider, headCollider;
    Field<float> maxJumpSpeed, jumpMultiplier;
    Method<Vector3> plGetVelocity;
    Method<void> plSetVelocity, plNoGravityFor;
    Field<int> locomotionLayers, collisionLayers;
    Method<int> goGetLayer;
    Method<bool> colliderIsTrigger, behaviourGetEnabledCollider;
    Method<void> ignoreLayers;            // a LayerMask: which layers your hands can climb on
    Method<void> goSetLayer;
    BNM::MonoType *tRigidbody = nullptr, *tCollider = nullptr, *tCanvas = nullptr, *tText = nullptr,
                  *tFont = nullptr, *tRenderer = nullptr, *tMeshCollider = nullptr, *tLine = nullptr;
    Method<void> matSetInt, matSetRenderQueue, audioSetData, srcSetClip, srcPlay, srcStop, srcSetVolume, srcSetSpatial;
    Method<Il2CppObject *> rendererGetShared, matGetShader;       // shader-based Fullbright / Horror
    Method<bool> matHasProp;
    Method<Color> matGetColorN;
    Method<void> matSetColorN, pnSetRate;
    Method<int> matGetRenderQueue, pnGetRate;                     // pn*: Photon send rate (Fake Lag)
    Method<Il2CppObject *> audioCreate;
    BNM::MonoType *tPlayer = nullptr, *tAudioSource = nullptr;
    BNM::IL2CPP::Il2CppClass *floatClass = nullptr;
    Ref shader, font, usagePrimary, usageSecondary, usageGrip, usageTrigger, usageStick, usageDevPos, usageDevRot;
    Ref usageStickClick, unlitShader;
    // added for: boundary, legs joystick, fullbright / horror, mp4 soundboard
    Method<BNM::Structures::Mono::String *> objGetName;
    Method<bool> goActiveInHierarchy, tryGetVec2, vpIsPrepared, rsGetFog;
    Method<void> rsSetFog, rsSetAmbientMode, rsSetAmbientLight, rsSetFogColor, rsSetFogDensity, rsSetFogMode,
                 rsSetSkybox, lightSetIntensity, lightSetRange, lightSetColor, lightSetType, vpSetUrl, vpSetSource,
                 vpSetAudioMode, vpSetRenderMode, vpSetTrackCount, vpSetTarget, vpSetPlayOnAwake, vpPrepare, vpPlay, vpStop;
    Method<int> rsGetAmbientMode, rsGetFogMode;
    Method<Color> rsGetAmbientLight, rsGetFogColor;
    Method<float> rsGetFogDensity, lightGetIntensity;
    Method<Il2CppObject *> rsGetSkybox;
    BNM::MonoType *tLight = nullptr, *tVideo = nullptr;
} G;

static bool Lookups() {
    if (G.ok) return true;
    auto R = ym::api()->resolve;
    p_handleNew = (decltype(p_handleNew))R("il2cpp_gchandle_new");
    p_handleTarget = (decltype(p_handleTarget))R("il2cpp_gchandle_get_target");
    p_handleFree = (decltype(p_handleFree))R("il2cpp_gchandle_free");
    Class go("UnityEngine", "GameObject"), tr("UnityEngine", "Transform"), obj("UnityEngine", "Object"),
          rb("UnityEngine", "Rigidbody"), mat("UnityEngine", "Material"), rend("UnityEngine", "Renderer"),
          sh("UnityEngine", "Shader"), res("UnityEngine", "Resources"), text("UnityEngine.UI", "Text"),
          graphic("UnityEngine.UI", "Graphic"), canvas("UnityEngine", "Canvas"), rt("UnityEngine", "RectTransform"),
          dev("UnityEngine.XR", "InputDevices"), player("", "MasterPlayer", Image("Assembly-CSharp.dll"));
    if (!go || !tr || !player || !dev) { ym::log("leos-menu: game classes missing"); return false; }
    G.createPrimitive = go.GetMethod("CreatePrimitive", 1);
    G.goTransform = go.GetMethod("get_transform", 0);
    G.goGetComponent = Overload(go, "GetComponent", {"System.Type"});
    G.goAddComponent = Overload(go, "AddComponent", {"System.Type"});
    G.goSetActive = go.GetMethod("SetActive", 1);
    Class comp("UnityEngine", "Component");
    G.compTransform = comp.GetMethod("get_transform", 0);
    G.compGameObject = comp.GetMethod("get_gameObject", 0);
    G.destroy = Overload(obj, "Destroy", {"UnityEngine.Object"});
    G.alive = obj.GetMethod("op_Implicit", 1);
    G.trSetParent = Overload(tr, "SetParent", {"UnityEngine.Transform", "System.Boolean"});
    G.trSetPosition = tr.GetMethod("set_position", 1);
    G.trSetRotation = tr.GetMethod("set_rotation", 1);
    G.trSetLocalPosition = tr.GetMethod("set_localPosition", 1);
    G.trSetLocalRotation = tr.GetMethod("set_localRotation", 1);
    G.trSetLocalScale = tr.GetMethod("set_localScale", 1);
    G.trGetPosition = tr.GetMethod("get_position", 0);
    G.trGetRotation = tr.GetMethod("get_rotation", 0);
    G.trGetForward = tr.GetMethod("get_forward", 0);
    G.trGetRight = tr.GetMethod("get_right", 0);
    G.trGetUp = tr.GetMethod("get_up", 0);
    G.trInverseTransformPoint = Overload(tr, "InverseTransformPoint", {"UnityEngine.Vector3"});
    G.trTransformPoint = Overload(tr, "TransformPoint", {"UnityEngine.Vector3"});
    G.rbSetVelocity = rb.GetMethod("set_velocity", 1);
    G.rbGetVelocity = rb.GetMethod("get_velocity", 0);
    G.rendererGetMaterial = rend.GetMethod("get_material", 0);
    G.matSetColor = Overload(mat, "set_color", {"UnityEngine.Color"});
    G.matSetShader = mat.GetMethod("set_shader", 1);
    G.shaderFind = sh.GetMethod("Find", 1);
    G.findAll = Overload(res, "FindObjectsOfTypeAll", {"System.Type"});
    G.textSetFont = text.GetMethod("set_font", 1);
    G.textSetFontStyle = text.GetMethod("set_fontStyle", 1);
    G.textSetBestFit = text.GetMethod("set_resizeTextForBestFit", 1);
    G.textSetMinSize = text.GetMethod("set_resizeTextMinSize", 1);
    G.textSetMaxSize = text.GetMethod("set_resizeTextMaxSize", 1);
    G.textSetText = text.GetMethod("set_text", 1);
    G.textSetFontSize = text.GetMethod("set_fontSize", 1);
    G.textSetAlignment = text.GetMethod("set_alignment", 1);
    G.graphicSetColor = graphic.GetMethod("set_color", 1);
    G.canvasSetRenderMode = canvas.GetMethod("set_renderMode", 1);
    G.rtSetSizeDelta = rt.GetMethod("set_sizeDelta", 1);
    G.deviceAtNode = dev.GetMethod("GetDeviceAtXRNode", 1);
    G.tryGetBool = dev.GetMethod("TryGetFeatureValue_bool", 3);
    G.haptic = dev.GetMethod("SendHapticImpulse", 4);    // optional: a small buzz on press
    G.leftHand = player.GetField("leftHandTransform");
    G.rightHand = player.GetField("rightHandTransform");
    G.headTransform = player.GetField("headTransform");
    G.bodyCollider = player.GetField("bodyCollider");
    G.headCollider = player.GetField("headCollider");
    G.maxJumpSpeed = player.GetField("maxJumpSpeed");
    G.locomotionLayers = player.GetField("locomotionEnabledLayers");
    G.collisionLayers = player.GetField("collisionEnabledLayers");
    G.goGetLayer = go.GetMethod("get_layer", 0);
    G.ignoreLayers = Class("UnityEngine", "Physics").GetMethod("IgnoreLayerCollision", 3);
    // Yeeps moves your body through these, not through a Rigidbody on the player object
    G.plGetVelocity = player.GetMethod("GetVelocity", 0);
    G.plSetVelocity = player.GetMethod("ApplySetVelocityNoHaptic", 1);
    if (!G.plSetVelocity.IsValid()) G.plSetVelocity = player.GetMethod("ApplySetVelocity", 1);
    G.plNoGravityFor = player.GetMethod("ApplyNoGravityDuration", 1);
    ym::log("leos-menu: velocity %d/%d, no-gravity %d", G.plGetVelocity.IsValid(), G.plSetVelocity.IsValid(),
            G.plNoGravityFor.IsValid());
    G.goSetLayer = go.GetMethod("set_layer", 1);
    G.jumpMultiplier = player.GetField("jumpMultiplier");
    G.teleportTo = player.GetMethod("TeleportTo", 1);
    G.rbAddForce = Overload(rb, "AddForce", {"UnityEngine.Vector3", "UnityEngine.ForceMode"});
    G.rbSetUseGravity = rb.GetMethod("set_useGravity", 1);
    G.colliderSetEnabled = Class("UnityEngine", "Collider").GetMethod("set_enabled", 1);
    G.behaviourGetEnabledCollider = Class("UnityEngine", "Collider").GetMethod("get_enabled", 0);
    G.colliderIsTrigger = Class("UnityEngine", "Collider").GetMethod("get_isTrigger", 0);
    G.raycast = Overload(Class("UnityEngine", "Physics"), "Raycast",
                         {"UnityEngine.Vector3", "UnityEngine.Vector3", "UnityEngine.RaycastHit", "System.Single", "System.Int32"});
    G.findScene = Overload(obj, "FindObjectsOfType", {"System.Type"});      // only when noclip starts
    G.findSceneByType = Overload(obj, "FindObjectsByType", {"System.Type", "UnityEngine.FindObjectsSortMode"});
    G.builtinResource = Overload(res, "GetBuiltinResource", {"System.Type", "System.String"});
    Class line("UnityEngine", "LineRenderer");
    G.tLine = line.GetMonoType();
    G.lrSetPositionCount = line.GetMethod("set_positionCount", 1);
    G.lrSetPosition = line.GetMethod("SetPosition", 2);
    G.lrSetStartWidth = line.GetMethod("set_startWidth", 1);
    G.lrSetEndWidth = line.GetMethod("set_endWidth", 1);
    G.lrSetUseWorldSpace = line.GetMethod("set_useWorldSpace", 1);
    G.tMeshCollider = Class("UnityEngine", "MeshCollider").GetMonoType();
    G.trGetParent = tr.GetMethod("get_parent", 0);
    G.trGetRoot = tr.GetMethod("get_root", 0);
    G.getComponentsInChildren = Overload(go, "GetComponentsInChildren", {"System.Type", "System.Boolean"});
    Class net("", "NetworkMessenger", Image("Assembly-CSharp.dll"));
    if (net.IsValid()) {
        G.netLocal = net.GetMethod("get_local", 0);
        for (auto &m : net.GetMethods(false))
            if (m.GetInfo() && m.GetInfo()->name && strcmp(m.GetInfo()->name, "RpcCreateItem") == 0) G.rpcCreateItem = m.GetInfo();
    }
    {
        Class pv("Photon.Pun", "PhotonView");
        G.tPhotonView = pv.IsValid() ? pv.GetMonoType() : nullptr;
        if (pv.IsValid()) G.pvRpc = Overload(pv, "RPC", {"System.String", "Photon.Pun.RpcTarget", "System.Object[]"});
        Class item("", "MenuItem", Image("Assembly-CSharp.dll"));
        if (item.IsValid()) {
            G.tItem = item.GetMonoType();
            G.itemKey = item.GetMethod("get_itemKey", 0);
            G.itemHeld = item.GetMethod("IsHeldOrAnchoredToAnyPlayer", 0);
            G.itemId = item.GetMethod("get_itemID", 0);
            G.itemDrop = item.GetMethod("TrySendDrop", 0);
            G.setHeldItem = Class("", "MasterPlayer", Image("Assembly-CSharp.dll")).GetMethod("SetHeldItem", 2);
        }
        G.objectClass = Class("System", "Object").GetClass();
        G.stringClass = Class("System", "String").GetClass();
        G.arrayNew = (decltype(G.arrayNew))ym::api()->resolve("il2cpp_array_new");
        G.valueBox = (decltype(G.valueBox))ym::api()->resolve("il2cpp_value_box");
        ym::log("leos-menu: items rpc=%d pv=%d item=%d box=%d", G.rpcCreateItem != nullptr, G.pvRpc.IsValid(),
                G.itemKey.IsValid(), G.arrayNew && G.valueBox);
    }
    Class behaviour("UnityEngine", "Behaviour");
    G.behaviourGetEnabled = behaviour.GetMethod("get_enabled", 0);
    G.behaviourSetEnabled = behaviour.GetMethod("set_enabled", 1);
    // What moves your hands with the controllers (paused while you dance)
    for (auto nc : std::vector<std::pair<const char *, const char *>>{
             {"UnityEngine.XR.Interaction.Toolkit", "XRBaseController"},
             {"UnityEngine.InputSystem.XR", "TrackedPoseDriver"},
             {"UnityEngine.SpatialTracking", "TrackedPoseDriver"}}) {
        Class c(nc.first, nc.second);
        if (c.IsValid()) G.trackerTypes.push_back(c.GetMonoType());
    }
    G.tRigidbody = rb.GetMonoType();
    G.tCollider = Class("UnityEngine", "Collider").GetMonoType();
    G.tRenderer = rend.GetMonoType();
    G.tCanvas = canvas.GetMonoType();
    G.tText = text.GetMonoType();
    G.tFont = Class("UnityEngine", "Font").GetMonoType();
    G.usagePrimary.set((Il2CppObject *)CreateMonoString("PrimaryButton"));
    G.usageSecondary.set((Il2CppObject *)CreateMonoString("SecondaryButton"));
    G.usageGrip.set((Il2CppObject *)CreateMonoString("GripButton"));
    G.usageTrigger.set((Il2CppObject *)CreateMonoString("TriggerButton"));
    G.usageStick.set((Il2CppObject *)CreateMonoString("Primary2DAxis"));
    G.usageDevPos.set((Il2CppObject *)CreateMonoString("DevicePosition"));
    G.usageDevRot.set((Il2CppObject *)CreateMonoString("DeviceRotation"));
    G.usageStickClick.set((Il2CppObject *)CreateMonoString("Primary2DAxisClick"));
    for (const char *s : {"Universal Render Pipeline/Unlit", "Universal Render Pipeline/Lit"})
        if (!G.shader.get() && G.shaderFind.IsValid()) G.shader.set(G.shaderFind((BNM::Structures::Mono::String *)CreateMonoString(s)));
    for (const char *s : {"Universal Render Pipeline/Unlit", "Unlit/Texture", "Sprites/Default"})
        if (!G.unlitShader.get() && G.shaderFind.IsValid()) G.unlitShader.set(G.shaderFind((BNM::Structures::Mono::String *)CreateMonoString(s)));
    // Arial, like ii's menu (Unity 6 calls it LegacyRuntime.ttf)
    for (const char *f : {"LegacyRuntime.ttf", "Arial.ttf"})
        if (!G.font.get() && G.builtinResource.IsValid() && G.tFont)
            G.font.set(G.builtinResource((Il2CppObject *)G.tFont, CreateMonoString(f)));
    if (!G.font.get() && G.findAll.IsValid() && G.tFont) {   // else any font the game loaded (one time)
        auto *arr = (uint8_t *)G.findAll((Il2CppObject *)G.tFont);
        if (arr && *(uintptr_t *)(arr + 0x18) > 0) G.font.set(((Il2CppObject **)(arr + 0x20))[0]);
    }
    // Leo's menu: ESP, effects and the soundboard
    G.matSetInt = Overload(mat, "SetInt", {"System.String", "System.Int32"});
    G.matSetRenderQueue = mat.GetMethod("set_renderQueue", 1);
    G.rendererGetShared = rend.GetMethod("get_sharedMaterial", 0);
    G.matGetShader = mat.GetMethod("get_shader", 0);
    G.matHasProp = Overload(mat, "HasProperty", {"System.String"});
    G.matGetColorN = Overload(mat, "GetColor", {"System.String"});
    G.matSetColorN = Overload(mat, "SetColor", {"System.String", "UnityEngine.Color"});
    G.matGetRenderQueue = mat.GetMethod("get_renderQueue", 0);
    {
        Class pn("Photon.Pun", "PhotonNetwork");
        if (pn.IsValid()) {
            G.pnGetRate = pn.GetMethod("get_SerializationRate", 0);
            G.pnSetRate = pn.GetMethod("set_SerializationRate", 1);
        }
        ym::log("leos-menu: photon rate %d/%d, shader swap %d", G.pnGetRate.IsValid(), G.pnSetRate.IsValid(),
                G.rendererGetShared.IsValid() && G.matGetShader.IsValid() && G.matSetColorN.IsValid());
    }
    G.tPlayer = player.GetMonoType();
    Class aclip("UnityEngine", "AudioClip"), asrc("UnityEngine", "AudioSource");
    if (aclip.IsValid() && asrc.IsValid()) {
        G.audioCreate = aclip.GetMethod("Create", 5);
        G.audioSetData = aclip.GetMethod("SetData", 2);
        G.srcSetClip = asrc.GetMethod("set_clip", 1);
        G.srcPlay = asrc.GetMethod("Play", 0);
        G.srcStop = asrc.GetMethod("Stop", 0);
        G.srcSetVolume = asrc.GetMethod("set_volume", 1);
        G.srcSetSpatial = asrc.GetMethod("set_spatialBlend", 1);
        G.tAudioSource = asrc.GetMonoType();
        G.floatClass = Class("System", "Single").GetClass();
    }
    G.objGetName = obj.GetMethod("get_name", 0);
    G.goActiveInHierarchy = go.GetMethod("get_activeInHierarchy", 0);
    // The joystick: the 2D-axis twin of the game's TryGetFeatureValue_bool wrapper (name is looked up)
    for (auto &m : dev.GetMethods(false)) {
        auto *mi = m.GetInfo();
        if (!mi || !mi->name || mi->parameters_count != 3) continue;
        std::string n = mi->name;
        for (auto &ch : n) ch = (char)tolower((unsigned char)ch);
        if (n.find("tryget") == 0 && (n.find("vector2") != std::string::npos || n.find("2d") != std::string::npos ||
                                      n.find("axis") != std::string::npos)) {
            G.tryGetVec2 = m;
            ym::log("leos-menu: joystick reader %s", mi->name);
            break;
        }
    }
    if (!G.tryGetVec2.IsValid()) ym::log("leos-menu: no joystick reader found");
    // The same family has a Vector3 and a Quaternion reader: the raw controller pose (Long Arms uses it)
    for (auto &m : dev.GetMethods(false)) {
        auto *mi = m.GetInfo();
        if (!mi || !mi->name || mi->parameters_count != 3) continue;
        std::string n = mi->name;
        for (auto &ch : n) ch = (char)tolower((unsigned char)ch);
        if (n.find("tryget") != 0) continue;
        if (!G.tryGetVec3.IsValid() && n.find("vector3") != std::string::npos) G.tryGetVec3 = m;
        if (!G.tryGetQuat.IsValid() && n.find("quaternion") != std::string::npos) G.tryGetQuat = m;
    }
    ym::log("leos-menu: controller pose readers %d/%d", G.tryGetVec3.IsValid(), G.tryGetQuat.IsValid());
    {
        Class rs("UnityEngine", "RenderSettings"), light("UnityEngine", "Light");
        if (rs.IsValid()) {
            G.rsSetFog = rs.GetMethod("set_fog", 1);              G.rsGetFog = rs.GetMethod("get_fog", 0);
            G.rsSetAmbientMode = rs.GetMethod("set_ambientMode", 1); G.rsGetAmbientMode = rs.GetMethod("get_ambientMode", 0);
            G.rsSetAmbientLight = rs.GetMethod("set_ambientLight", 1); G.rsGetAmbientLight = rs.GetMethod("get_ambientLight", 0);
            G.rsSetFogColor = rs.GetMethod("set_fogColor", 1);    G.rsGetFogColor = rs.GetMethod("get_fogColor", 0);
            G.rsSetFogDensity = rs.GetMethod("set_fogDensity", 1); G.rsGetFogDensity = rs.GetMethod("get_fogDensity", 0);
            G.rsSetFogMode = rs.GetMethod("set_fogMode", 1);      G.rsGetFogMode = rs.GetMethod("get_fogMode", 0);
            G.rsSetSkybox = rs.GetMethod("set_skybox", 1);        G.rsGetSkybox = rs.GetMethod("get_skybox", 0);
        }
        if (light.IsValid()) {
            G.tLight = light.GetMonoType();
            G.lightSetIntensity = light.GetMethod("set_intensity", 1); G.lightGetIntensity = light.GetMethod("get_intensity", 0);
            G.lightSetRange = light.GetMethod("set_range", 1);
            G.lightSetColor = light.GetMethod("set_color", 1);
            G.lightSetType = light.GetMethod("set_type", 1);
        }
        Class vp("UnityEngine.Video", "VideoPlayer");               // only in builds that ship the video module
        if (vp.IsValid()) {
            G.tVideo = vp.GetMonoType();
            G.vpSetUrl = vp.GetMethod("set_url", 1);               G.vpSetSource = vp.GetMethod("set_source", 1);
            G.vpSetAudioMode = vp.GetMethod("set_audioOutputMode", 1); G.vpSetRenderMode = vp.GetMethod("set_renderMode", 1);
            G.vpSetTrackCount = vp.GetMethod("set_controlledAudioTrackCount", 1);
            G.vpSetTarget = vp.GetMethod("SetTargetAudioSource", 2);
            G.vpSetPlayOnAwake = vp.GetMethod("set_playOnAwake", 1);
            G.vpPrepare = vp.GetMethod("Prepare", 0); G.vpPlay = vp.GetMethod("Play", 0); G.vpStop = vp.GetMethod("Stop", 0);
            G.vpIsPrepared = vp.GetMethod("get_isPrepared", 0);
        }
        ym::log("leos-menu: fog/ambient %d, light %d, video player %d", G.rsSetFog.IsValid(), G.tLight != nullptr, G.tVideo != nullptr);
    }
    G.ok = G.createPrimitive.IsValid() && G.goTransform.IsValid() && G.trSetPosition.IsValid() &&
           G.deviceAtNode.IsValid() && G.tryGetBool.IsValid() && G.leftHand.IsValid();
    if (!G.ok) ym::log("leos-menu: some game methods are missing, menu off");
    return G.ok;
}

static bool Alive(Il2CppObject *o) { return o && G.alive((Il2CppObject *)o); }
static Il2CppObject *Tr(Il2CppObject *gameObject) { return G.goTransform[gameObject](); }
static void Show(Ref &r, bool on) { if (Alive(r.get())) G.goSetActive[r.get()](on); }
static void Kill(Ref &r) { if (Alive(r.get())) G.destroy(r.get()); r.clear(); }

// A primitive with a flat color. collider=false removes its collider so it can't push you.
// Always give new objects the URP shader: Unity's default material only draws in one
// eye on Quest.
static Il2CppObject *Prim(int type, Color c, bool collider) {
    Il2CppObject *o = G.createPrimitive(type);   // 0 = Sphere, 3 = Cube
    if (!o) return nullptr;
    if (!collider) if (auto *col = G.goGetComponent[o](G.tCollider)) G.destroy(col);
    if (auto *r = G.goGetComponent[o](G.tRenderer)) {
        Il2CppObject *m = G.rendererGetMaterial[r]();
        if (m && G.shader.get()) G.matSetShader[m](G.shader.get());
        if (m) G.matSetColor[m](c);
    }
    return o;
}
static Il2CppObject *Cube(Color c, bool collider) { return Prim(3, c, collider); }
static void SetColor(Il2CppObject *o, Color c) {
    if (!Alive(o)) return;
    if (auto *r = G.goGetComponent[o](G.tRenderer)) if (Il2CppObject *m = G.rendererGetMaterial[r]()) G.matSetColor[m](c);
}
static void SetText(Ref &text, const char *s) {
    if (Alive(text.get())) G.textSetText[text.get()](CreateMonoString(s));
}

// ---- input -------------------------------------------------------------------

struct Buttons { bool a = false, b = false, x = false, y = false, lgrip = false, rgrip = false, ltrig = false, rtrig = false, lstick = false, rstick = false; };
static bool Pressed(int node, Ref &usage) {
    XRDevice d = G.deviceAtNode(node);
    bool v = false;
    if (d.initialized) G.tryGetBool(d.id, (BNM::Structures::Mono::String *)usage.get(), &v);
    return v;
}
static Buttons ReadButtons() {
    const int left = 4, right = 5;              // XRNode.LeftHand / RightHand
    Buttons b;
    b.x = Pressed(left, G.usagePrimary);  b.y = Pressed(left, G.usageSecondary);
    b.a = Pressed(right, G.usagePrimary); b.b = Pressed(right, G.usageSecondary);
    b.lgrip = Pressed(left, G.usageGrip); b.rgrip = Pressed(right, G.usageGrip);
    b.ltrig = Pressed(left, G.usageTrigger); b.rtrig = Pressed(right, G.usageTrigger);
    b.lstick = Pressed(left, G.usageStickClick); b.rstick = Pressed(right, G.usageStickClick);   // thumbstick clicks
    return b;
}

// ---- menu contents -------------------------------------------------------------


// Feature switches. Everything here only changes your own game, except Rig Mods and
// Spawning, which the room sees within the limits in AGENTS.md ("Things others see").
static bool f_platforms, f_tpGun, f_longArms, f_lowGravity, f_noGravity, f_highGravity, f_noclipFly, f_speedBoost;
static bool f_legs;
static bool f_itemGun, f_spawnOnHand;
static bool f_bullets, f_esp, f_draw, f_trail, f_noBounds, f_fullbright, f_horror, f_snow, f_pizza;
static bool f_fps, f_clock, f_rightHand, f_notify = true;
static bool f_infCotton, f_potionGun, f_itemRain, f_hearAll, f_bloom;
static bool f_longMenu, f_pbUnder, f_pbBottom, f_pbSides = true, f_pbModTop;   // menu type Long Menu, and where the page buttons are
static bool f_noOutline, f_pinBtn;
static int g_roundLvl = 0, g_transLvl = 3;                           // Round Corners / Translucency sliders
static const char *kVersion = "V2.3";
static bool f_joyFly, f_zeroFly, f_sizeChanger, f_punch, f_strafe, f_pixelMenu;
static bool f_miniMap, f_revVel, f_noBody, f_phoneMenu, f_wideMenu, f_revolver, f_stayLocked, f_spaceStars, f_galaxySky, f_flingGun;
static int g_flingPower = 1;
static std::string g_presetName;
static bool f_blood1, f_blood2, f_pistol, f_shotgun, f_sniper, f_knife, f_nametags, f_trailEsp, f_ripple, f_targetLock, f_targetInfo, f_spectate, f_addFav;
static int g_textCol = 0, g_gunFx = 0, g_lockRad = 2, g_sizeSel = 1, g_selEffect = 0, g_specView = 0;
static const char *kGunFxN[] = {"Straight Line", "Spiral", "Wave"};
static const char *GunFxName(int i) { return kGunFxN[i]; }
static const float kLockR[] = {2, 5, 10, 20, 40, 80};
static const char *kLockN[] = {"2 m", "5 m", "10 m", "20 m", "40 m", "80 m"};
static const char *LockRadName(int i) { return kLockN[i]; }
static const char *kSizeSelN[] = {"Small", "Normal", "Big"};
static const char *SizeSelName(int i) { return kSizeSelN[i]; }
static const char *kSpecN[] = {"First Person", "3D Person"};
static const char *SpecViewName(int i) { return kSpecN[i]; }
static std::string g_query, g_pin;                            // Search text / Owner code being typed
static int g_kbMode = 0;                                      // 0 no keyboard, 1 search keyboard, 2 number pad
static bool g_ownerOk = false, g_pinLocked = false;
static int g_pinFails = 0, g_selPlayer = -1;
static std::vector<std::string> g_favs;
static int g_platType = 0, g_potionGun = 0, g_potionSelf = 0;          // Platform Type slider, potion sliders
static const char *kPlatN[] = {"Default", "Sticky Ball", "Follow Hand"};
static const char *PlatName(int i) { return kPlatN[i]; }
static const char *kPotionN[] = {"Legs", "Visibility", "Love", "Blindness", "Small", "Big", "Slow Falling", "Levitation"};
static const char *PotionName(int i) { return kPotionN[i]; }
static bool f_noclip, f_stickyPlat, f_followPlat, f_ghost, f_inv, f_floatMenu, f_toggleMenu, f_tracers, f_boxEsp, f_lbColor,
            f_rainbowSky, f_drone, f_micFx, f_solidPlayers, f_colorblind;
static int g_fireSpeed = 2, g_droneSpeed = 3, g_micMode = 0;
static const float kFireSpeeds[] = {10, 20, 35, 60, 100};
static const char *kFireN[] = {"10", "20", "35", "60", "100"};
static const char *FireSpeedName(int i) { return kFireN[i]; }
static const char *kMicN[] = {"Default", "High Pitch", "Low Pitch", "Broken"};
static const char *MicName(int i) { return kMicN[i]; }
static bool g_menuToggled = false, g_floatPlaced = false;   // Toggle Menu / Floating Menu state
static bool g_micRun = false;                               // the mic changer is feeding the voice recorder
static int g_joySpeed = 3, g_zeroSpeed = 3, g_strafeLvl = 2, g_punchPower = 2, g_sizeSpeed = 1;
static const float kStrafeCap[] = {0.6f, 1.0f, 1.5f, 2.5f, 4.0f};
static const char *kStrafeN[] = {"0.6", "1", "1.5", "2.5", "4"};
static const char *StrafeName(int i) { return kStrafeN[i]; }
static const float kPunchPow[] = {20, 35, 50, 80, 120};
static const char *kPunchN[] = {"20", "35", "50", "80", "120"};
static const char *PunchName(int i) { return kPunchN[i]; }
static const float kSizeRates[] = {0.6f, 1.2f, 2.4f};
static const char *kSizeN[] = {"Slow", "Normal", "Fast"};
static const char *SizeSpeedName(int i) { return kSizeN[i]; }
static float g_size = 1.0f;                                    // Size Changer: 1 = normal
static bool g_sizeApplied = false, g_pixelApplied = false;
static float SizeMul() { return f_sizeChanger ? powf(g_size, 0.8f) : 1.0f; }   // speeds follow your size
static bool f_allHear = true;                    // sounds from the soundboard also go out through your voice
static bool f_ironMan, f_velFly, f_noKnockback, f_delGun, f_fireball, f_bounce, f_noCotton;
// Update 3
static bool f_textMenu, f_smoothFly, f_flipGravity, f_spider, f_dash, f_tpDash, f_rewind, f_rain, f_fakeLag, f_airSwim,
            f_orbit, f_grid9Gun, f_cubeGun;
static bool g_recording = false, g_rewinding = false;          // Time Rewind state
static char g_note[96] = "";                                    // the last notification (the Pizza Menu shows it)
static double g_noteUntil = 0;
static std::vector<std::string> g_orbitIds;                     // item ids spawned for MenuItem Orbit
static char g_itemKey[64] = "";                // the last item you held (learned on grab)
struct PendingItem { Vector3 pos, vel; char key[64] = {0}; bool orbit = false; int track = 0; };   // track 1 = fireball
struct FbTrack { std::string id; Vector3 dir; double born = 0; int kind = 1; };      // kind 1 fireball, 2 item rain
static std::vector<FbTrack> g_fbPend;                           // fireball items waiting to be found by id   // key empty = the item you held last
static std::vector<PendingItem> g_itemQueue;
static double g_nextItem = 0, g_nextBatch = 0;
static int g_dupeArmed = 0;
// Sliders (the number is the slider position)
static int g_drawColor = 0, g_gunColor = 0, g_noclipSpeed = 3;
static const char *kDrawColors[] = {"Black", "Red", "Blue", "Yellow", "Orange", "Rainbow", "Green", "White"};
static const char *DrawColorName(int i) { return kDrawColors[i]; }
static const float kNoclipSpeeds[] = {2, 4, 6, 9, 14, 20, 30, 45};
static const char *kNoclipNames[] = {"2", "4", "6", "9", "14", "20", "30", "45"};
static const char *NoclipSpeedName(int i) { return kNoclipNames[i]; }
// Overpowered sliders (position 0 / the marked position = the game's normal value, nothing is touched)
static constexpr int kKnockDefault = 2;
static const float kKnockMuls[] = {0.f, 0.5f, 1.f, 2.f, 3.f, 5.f, 10.f, 15.f, 25.f, 50.f, 100.f, 500.f, 100000000.f};
static const char *kKnockNames[] = {"x0", "x0.5", "x1", "x2", "x3", "x5", "x10", "x15", "x25", "x50", "x100", "x500", "x100000000"};
static const char *KnockName(int i) { return kKnockNames[i]; }
static int g_knockSlider = kKnockDefault;
// Overpowered projectile sliders: position 0 = Default (nothing is touched), the rest are the value
static const int kPCounts[] = {-1, 1, 2, 3, 4, 5, 6, 8, 10, 15, 20, 25, 30, 40, 50, 60, 75, 100};
static const char *kPCountN[] = {"Default", "1", "2", "3", "4", "5", "6", "8", "10", "15", "20", "25", "30", "40", "50", "60", "75", "100"};
static const int kPSpreads[] = {-1, 0, 1, 2, 3, 5, 8, 10, 15, 20, 25, 30};
static const char *kPSpreadN[] = {"Default", "0", "1", "2", "3", "5", "8", "10", "15", "20", "25", "30"};
static const int kPCosts[] = {-1, 0, 1, 2, 3, 5, 10, 15, 20, 25, 30, 40, 50, 60, 75, 100};
static const char *kPCostN[] = {"Default", "0", "1", "2", "3", "5", "10", "15", "20", "25", "30", "40", "50", "60", "75", "100"};
static const char *PCountName(int i) { return kPCountN[i]; }
static const char *PSpreadName(int i) { return kPSpreadN[i]; }
static const char *PCostName(int i) { return kPCostN[i]; }
static int g_projCount = 0, g_projSpread = 0, g_projCost = 0;
static int g_pressArg = 0;                                   // MenuItem::arg of the button being pressed
static const char *kRoundN[] = {"Off", "4 mm", "8 mm", "12 mm", "16 mm", "22 mm", "28 mm", "35 mm", "45 mm"};
static const char *RoundName(int i) { return kRoundN[i]; }
static const float kTransA[] = {0.10f, 0.15f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.70f, 0.85f, 1.0f};
static const char *kTransN[] = {"10%", "15%", "20%", "30%", "40%", "50%", "60%", "70%", "85%", "100%"};
static const char *TransName(int i) { return kTransN[i]; }
// Movement / Spawning sliders
static const int kCircleCounts[] = {6, 10, 16, 24};
static const char *kCircleNames[] = {"6", "10", "16", "24"};
static const char *CircleName(int i) { return kCircleNames[i]; }
static const char *kSlotNames[] = {"1", "2", "3", "4", "5"};
static const char *PosSlotName(int i) { return kSlotNames[i]; }
static int g_circleSlider = 1, g_posSlot = 0;
// Update 3 sliders
static int g_smoothSpeed = 3, g_dashSpeed = 2, g_tpDist = 3, g_lagLevel = 1;
static const float kDashSpeeds[] = {6, 10, 15, 22, 32, 45, 70};
static const char *kDashNames[] = {"6", "10", "15", "22", "32", "45", "70"};
static const char *DashName(int i) { return kDashNames[i]; }
static const float kTpDists[] = {1, 2, 3, 5, 8, 12, 20};
static const char *kTpNames[] = {"1 m", "2 m", "3 m", "5 m", "8 m", "12 m", "20 m"};
static const char *TpDistName(int i) { return kTpNames[i]; }
static const int kLagRates[] = {5, 3, 2, 1};                    // Photon updates per second, lower = laggier
static const char *kLagNames[] = {"Light", "Medium", "Heavy", "Extreme"};
static const char *LagName(int i) { return kLagNames[i]; }

static constexpr int kDances = 8;
static bool g_danceSel[kDances];
static int g_dance = -1;            // the picked rig mod, -1 = none
static Ref g_player, g_rigidbody, g_leftHand, g_rightHand, g_head;

static void Notify(const char *fmt, ...);
static void AllOff();
static void PointerToHand();
static void NextPointer();
static void ThemeBlue();
static void ResetSize();
static void ThemeBlack();
static void ThemeDarkBlue();
static void WideChanged();
static void PresetLeo();
static void PresetStupid();
static void PresetModder();
static void PresetMalachi();
static void PresetSeralyth();
static void PresetAdd();
static void PresetDelete();
static void PresetsEnsure();
static void PresetSaveCurrent(const std::string &name);
static void MenuRebuild();
static void SkyPicked();
static int LockTarget(Vector3 from);
static int LockTargetPersist(Vector3 from, bool gripHeld);
static void FlingGunAim(Vector3 start, Vector3 dir);
static void SkyboxRestore();
static int g_stayTarget = -1;
static bool g_stayGripPrev = false;
static void Radio(bool **flags, bool *prev, int n);
static const char *FlingName(int i);
static void EggEnsure();
static void TextDefault();
static void TextRed();
static void TextWhite();
static void TextBlack();
static void TextPurple();
static void TextPurpleWhite();
static void TextBlueWhite();
static void RefreshPlayersBtn();
static void GiveSelectedEffect();
static void SpawnAtSelected();
static void WeaponPicked();
static bool IsFav(const char *label);
static void FavToggle(const char *label);
static void SearchFill();
static void OwnerOpen();
static void OpenSearch();
static void CloseKeyboard();
static void KeyPress(int k);
static int KeyAt(Vector3 p);
static void SearchUi();
static void PlayersEnsure();
static void Extra10Reset();
static void Extra11Reset();
static int LockTarget(Vector3 from);
static Vector3 Cross(Vector3 a, Vector3 b);
static float Len2(Vector3 v);
static void GiveSelfPotionImpl();
static void PotionOnTarget(Il2CppObject *obj, Il2CppObject *head, int fx);
static void RewindToggled();
static void GiveSelfPotion();
static void PotionGunAim(Vector3 start, Vector3 dir);
static void Extra8Reset();
static void ThemePurple();
static void ThemeYellow();
static void ThemeWhite();
static void ThemeTranslucent();
static void PageBtnPicked();
static void ThemeOrange();
static void ThemeRainbow();
static void ApplySpeedBoost();
static void ApplyGravity();
static void GravityPicked();
static void AtmoPicked();
static void PizzaChanged();
static void SetTransparent(bool on);
static void GunBlue();
static void GunOrange();
static void GunRainbow();
static Color DrawColor();
static void DancePicked();
static void ArmDupe9();
static void DeleteDraw();
static void StopSound();
static void SoundReload();
static void HearAllToggled();
static void SoundEnsure();
static void ProjRefresh();
static void ItemRainToggled();
static bool *kSkyFlags[] = {&f_rainbowSky, &f_spaceStars, &f_galaxySky};
static bool g_skyPrev[3];
static void SkyPicked() { Radio(kSkyFlags, g_skyPrev, 3); }
static void BloomToggled();
static void BloomTick();
static void FlashRestore();
static void SoundReloadBtn();
static void InfCottonToggled();
static uint8_t *ScanAll(BNM::MonoType *t);
static size_t ArrLen(uint8_t *a);
static Il2CppObject *ArrAt(uint8_t *a, size_t i);
static void SyncItem(Il2CppObject *it);
static bool LeftHandItem();
static void FlyPicked();
static void GunPicked();
static void TpClosest();
static void SavePos();
static void GoToPos();
static void ArmCircle();
static void CottonToggled();
static void DeleteAimedItem(Vector3 start, Vector3 dir);
static void ThemeDark();
static void HandPicked();
static void MenuTypePicked();
static void OutlineToggled();
static void HandMenuPicked();
static void OrbitToggled();
static void ArmGrid81();
static void ArmRow();
static void ArmYeepsText();
static void ArmCubes9();
static void LagChanged();
static void SoundRoomReset();
static void ExtraRoomReset();
static void ItemRainTick();
static void LevTick(float dt);
static void ProjectileTick();
static void InfCottonTick();
static const char *PlaceholderKey();
static void Grid9Gun(Vector3 at);

enum Cat { MAIN, SETTINGS, THEMES, GUNCOLOR, MENUTYPE, MOVEMENT, SPAWNING, OVERPOWERED, RIG, SOUNDBOARD, VISUAL, FUN, ENABLED, PAGEBTN, EXPERIMENTAL, SPAWNPRESETS, TEXTCOLOR, INFO, OTHERPLAYERS, FAVOURITES, GAMES, OWNERPIN, OWNER, SEARCH, PRESETS, PRESETNAME, NCAT };
enum Kind { OPEN, TOGGLE, ACTION, SLIDER };
struct MenuItem {
    Cat cat;
    const char *label;
    Kind kind;
    Cat target = MAIN;            // OPEN: the page it opens
    bool *flag = nullptr;         // TOGGLE: the switch
    void (*fn)() = nullptr;       // ACTION, or called after a TOGGLE changes
    int *ival = nullptr;          // SLIDER: the position 0..icount-1
    int icount = 0;
    const char *(*iname)(int) = nullptr;   // SLIDER: the text for a position
    int arg = 0;                          // ACTION: a number the callback can read (g_pressArg)
};
// Leo's layout. Keep this order.
static MenuItem g_items[] = {
    {MAIN, "Enabled Mods", OPEN, ENABLED},
    {MAIN, "Settings", OPEN, SETTINGS},
    {MAIN, "Movement Mods", OPEN, MOVEMENT},
    {MAIN, "Spawning", OPEN, SPAWNING},
    {MAIN, "Overpowered Mods", OPEN, OVERPOWERED},
    {MAIN, "Rig Mods", OPEN, RIG},
    {MAIN, "Soundboard", OPEN, SOUNDBOARD},
    {MAIN, "Visual Mods", OPEN, VISUAL},
    {MAIN, "Fun Mods", OPEN, FUN},
    {MAIN, "Experimental Mods", OPEN, EXPERIMENTAL},
    {MAIN, "Games", OPEN, GAMES},
    {MAIN, "Info", OPEN, INFO},
    {MAIN, "Other Players", OPEN, OTHERPLAYERS},
    {MAIN, "Favourites", OPEN, FAVOURITES},
    {MAIN, "Owner", OPEN, OWNER},

    {SETTINGS, "Exit Settings", OPEN, MAIN},
    {SETTINGS, "Themes", OPEN, THEMES},
    {SETTINGS, "Text Colour", OPEN, TEXTCOLOR},
    {SETTINGS, "Theme Presets", OPEN, PRESETS},
    {SETTINGS, "Wide Menu", TOGGLE, MAIN, &f_wideMenu, WideChanged},
    {SETTINGS, "Gun Pointer Color", OPEN, GUNCOLOR},
    {SETTINGS, "Change Pointer Position", ACTION, MAIN, nullptr, NextPointer},
    {SETTINGS, "Right Hand Menu", TOGGLE, MAIN, &f_rightHand, PointerToHand},
    {SETTINGS, "Menu Type", OPEN, MENUTYPE},
    {SETTINGS, "Page Buttons", OPEN, PAGEBTN},
    {SETTINGS, "Round Corners", SLIDER, MAIN, nullptr, nullptr, &g_roundLvl, 9, RoundName},
    {SETTINGS, "Target Lock", TOGGLE, MAIN, &f_targetLock},
    {SETTINGS, "Lock Radius", SLIDER, MAIN, nullptr, nullptr, &g_lockRad, 6, LockRadName},
    {SETTINGS, "Gun Effect", SLIDER, MAIN, nullptr, nullptr, &g_gunFx, 3, GunFxName},
    {SETTINGS, "Remove Outline", TOGGLE, MAIN, &f_noOutline, OutlineToggled},
    {SETTINGS, "Pin Button", TOGGLE, MAIN, &f_pinBtn},
    {SETTINGS, "Stay Locked", TOGGLE, MAIN, &f_stayLocked},
    {SETTINGS, "Floating Menu", TOGGLE, MAIN, &f_floatMenu},
    {SETTINGS, "Toggle Menu", TOGGLE, MAIN, &f_toggleMenu},
    {SETTINGS, "Colorblind Mode", TOGGLE, MAIN, &f_colorblind},
    {SETTINGS, "Show FPS", TOGGLE, MAIN, &f_fps},
    {SETTINGS, "Show Clock", TOGGLE, MAIN, &f_clock},
    {SETTINGS, "Notifications", TOGGLE, MAIN, &f_notify},
    {SETTINGS, "Turn Everything Off", ACTION, MAIN, nullptr, AllOff},

    {THEMES, "Exit Themes", OPEN, SETTINGS},
    {THEMES, "Blue", ACTION, MAIN, nullptr, ThemeBlue},
    {THEMES, "Orange", ACTION, MAIN, nullptr, ThemeOrange},
    {THEMES, "Rainbow", ACTION, MAIN, nullptr, ThemeRainbow},
    {THEMES, "Dark Gray", ACTION, MAIN, nullptr, ThemeDark},
    {THEMES, "Purple", ACTION, MAIN, nullptr, ThemePurple},
    {THEMES, "Yellow", ACTION, MAIN, nullptr, ThemeYellow},
    {THEMES, "White", ACTION, MAIN, nullptr, ThemeWhite},
    {THEMES, "Black", ACTION, MAIN, nullptr, ThemeBlack},
    {THEMES, "Dark Blue", ACTION, MAIN, nullptr, ThemeDarkBlue},
    {THEMES, "Translucent", ACTION, MAIN, nullptr, ThemeTranslucent},
    {THEMES, "Translucency", SLIDER, MAIN, nullptr, nullptr, &g_transLvl, 10, TransName},

    {GUNCOLOR, "Exit Gun Color", OPEN, SETTINGS},
    {GUNCOLOR, "Gun Blue", ACTION, MAIN, nullptr, GunBlue},
    {GUNCOLOR, "Gun Orange", ACTION, MAIN, nullptr, GunOrange},
    {GUNCOLOR, "Gun Rainbow", ACTION, MAIN, nullptr, GunRainbow},

    {MENUTYPE, "Exit Menu Type", OPEN, SETTINGS},
    {MENUTYPE, "Hand Menu", ACTION, MAIN, nullptr, HandMenuPicked},
    {MENUTYPE, "Corner Menu", TOGGLE, MAIN, &f_pizza, MenuTypePicked},
    {MENUTYPE, "Pizza Menu", TOGGLE, MAIN, &f_textMenu, MenuTypePicked},
    {MENUTYPE, "Long Menu", TOGGLE, MAIN, &f_longMenu, MenuTypePicked},
    {MENUTYPE, "Pixelated", TOGGLE, MAIN, &f_pixelMenu, MenuTypePicked},

    {PAGEBTN, "Exit Page Buttons", OPEN, SETTINGS},
    {PAGEBTN, "Under Menu Name", TOGGLE, MAIN, &f_pbUnder, PageBtnPicked},
    {PAGEBTN, "Beside Menu (Default)", TOGGLE, MAIN, &f_pbSides, PageBtnPicked},
    {PAGEBTN, "Bottom Of Menu", TOGGLE, MAIN, &f_pbBottom, PageBtnPicked},
    {PAGEBTN, "Mod Buttons", TOGGLE, MAIN, &f_pbModTop, PageBtnPicked},

    {MOVEMENT, "Exit Movement Mods", OPEN, MAIN},
    {MOVEMENT, "Platforms", TOGGLE, MAIN, &f_platforms},
    {MOVEMENT, "Platform Type", SLIDER, MAIN, nullptr, nullptr, &g_platType, 3, PlatName},
    {MOVEMENT, "Rewind", TOGGLE, MAIN, &f_rewind, RewindToggled},
    {MOVEMENT, "Reverse Velocity", TOGGLE, MAIN, &f_revVel},
    {MOVEMENT, "Teleport Gun", TOGGLE, MAIN, &f_tpGun, GunPicked},
    {MOVEMENT, "Long Arms", TOGGLE, MAIN, &f_longArms},
    {MOVEMENT, "Low Gravity", TOGGLE, MAIN, &f_lowGravity, GravityPicked},
    {MOVEMENT, "No Gravity", TOGGLE, MAIN, &f_noGravity, GravityPicked},
    {MOVEMENT, "High Gravity", TOGGLE, MAIN, &f_highGravity, GravityPicked},
    {MOVEMENT, "Flip Gravity", TOGGLE, MAIN, &f_flipGravity, GravityPicked},
    {MOVEMENT, "Noclip", TOGGLE, MAIN, &f_noclip},
    {MOVEMENT, "Smooth Noclip", TOGGLE, MAIN, &f_smoothFly, FlyPicked},
    {MOVEMENT, "Smooth Noclip Speed", SLIDER, MAIN, nullptr, nullptr, &g_smoothSpeed, 8, NoclipSpeedName},
    {MOVEMENT, "Joystick Fly", TOGGLE, MAIN, &f_joyFly, FlyPicked},
    {MOVEMENT, "Joystick Fly Speed", SLIDER, MAIN, nullptr, nullptr, &g_joySpeed, 8, NoclipSpeedName},
    {MOVEMENT, "Zero Gravity Fly", TOGGLE, MAIN, &f_zeroFly, FlyPicked},
    {MOVEMENT, "Zero Gravity Speed", SLIDER, MAIN, nullptr, nullptr, &g_zeroSpeed, 8, NoclipSpeedName},
    {MOVEMENT, "CS2 Strafe", TOGGLE, MAIN, &f_strafe},
    {MOVEMENT, "Strafe Power", SLIDER, MAIN, nullptr, nullptr, &g_strafeLvl, 5, StrafeName},
    {MOVEMENT, "Speed Boost", TOGGLE, MAIN, &f_speedBoost, ApplySpeedBoost},
    {MOVEMENT, "Legs", TOGGLE, MAIN, &f_legs},
    {MOVEMENT, "Iron Man", TOGGLE, MAIN, &f_ironMan, HandPicked},
    {MOVEMENT, "Spider Yeeps", TOGGLE, MAIN, &f_spider, HandPicked},
    {MOVEMENT, "Velocity Fly", TOGGLE, MAIN, &f_velFly, FlyPicked},
    {MOVEMENT, "Teleport To Closest Player", ACTION, MAIN, nullptr, TpClosest},
    {MOVEMENT, "Save Position", ACTION, MAIN, nullptr, SavePos},
    {MOVEMENT, "Go To Saved Position", ACTION, MAIN, nullptr, GoToPos},
    {MOVEMENT, "Position Slot", SLIDER, MAIN, nullptr, nullptr, &g_posSlot, 5, PosSlotName},
    {MOVEMENT, "Dash", TOGGLE, MAIN, &f_dash},
    {MOVEMENT, "Dash Speed", SLIDER, MAIN, nullptr, nullptr, &g_dashSpeed, 7, DashName},
    {MOVEMENT, "Teleport Dash", TOGGLE, MAIN, &f_tpDash},
    {MOVEMENT, "Teleport Distance", SLIDER, MAIN, nullptr, nullptr, &g_tpDist, 7, TpDistName},

    {EXPERIMENTAL, "Exit Experimental", OPEN, MAIN},
    {EXPERIMENTAL, "Size Changer", TOGGLE, MAIN, &f_sizeChanger},
    {EXPERIMENTAL, "Size", SLIDER, MAIN, nullptr, nullptr, &g_sizeSel, 3, SizeSelName},
    {EXPERIMENTAL, "Inv Monkey", TOGGLE, MAIN, &f_inv},
    {EXPERIMENTAL, "Mini Map", TOGGLE, MAIN, &f_miniMap},
    {EXPERIMENTAL, "Freecam", TOGGLE, MAIN, &f_drone},
    {EXPERIMENTAL, "Freecam Speed", SLIDER, MAIN, nullptr, nullptr, &g_droneSpeed, 8, NoclipSpeedName},

    {SPAWNPRESETS, "Exit Spawn Presets", OPEN, SPAWNING},

    {PRESETS, "Exit Presets", OPEN, SETTINGS},
    {PRESETS, "Leo's Menu", ACTION, MAIN, nullptr, PresetLeo},
    {PRESETS, "Stupid Menu", ACTION, MAIN, nullptr, PresetStupid},
    {PRESETS, "Modder 3x Menu", ACTION, MAIN, nullptr, PresetModder},
    {PRESETS, "Malachi's Theme", ACTION, MAIN, nullptr, PresetMalachi},
    {PRESETS, "Seralyth's Theme", ACTION, MAIN, nullptr, PresetSeralyth},
    {PRESETS, "Add Custom Preset", ACTION, MAIN, nullptr, PresetAdd},
    {PRESETS, "Delete Last Custom", ACTION, MAIN, nullptr, PresetDelete},
    {PRESETNAME, "Cancel", OPEN, PRESETS},

    {TEXTCOLOR, "Exit Text Colour", OPEN, SETTINGS},
    {TEXTCOLOR, "White Text", ACTION, MAIN, nullptr, TextDefault},
    {TEXTCOLOR, "Red Text", ACTION, MAIN, nullptr, TextRed},
    {TEXTCOLOR, "Black Text", ACTION, MAIN, nullptr, TextWhite},
    {TEXTCOLOR, "Purple Text", ACTION, MAIN, nullptr, TextPurple},
    {TEXTCOLOR, "Purple/White Text", ACTION, MAIN, nullptr, TextPurpleWhite},
    {TEXTCOLOR, "Blue/White Text", ACTION, MAIN, nullptr, TextBlueWhite},

    {INFO, "Exit Info", OPEN, MAIN},
    {INFO, "Target Info", TOGGLE, MAIN, &f_targetInfo},

    {OTHERPLAYERS, "Exit Other Players", OPEN, MAIN},
    {OTHERPLAYERS, "Refresh Players", ACTION, MAIN, nullptr, RefreshPlayersBtn},
    {OTHERPLAYERS, "Spectate Selected", TOGGLE, MAIN, &f_spectate},
    {OTHERPLAYERS, "Spectate View", SLIDER, MAIN, nullptr, nullptr, &g_specView, 2, SpecViewName},
    {OTHERPLAYERS, "Give Selected Effect", ACTION, MAIN, nullptr, GiveSelectedEffect},
    {OTHERPLAYERS, "Selected Effect", SLIDER, MAIN, nullptr, nullptr, &g_selEffect, 8, PotionName},
    {OTHERPLAYERS, "Spawn MenuItem At Selected", ACTION, MAIN, nullptr, SpawnAtSelected},

    {FAVOURITES, "Exit Favourites", OPEN, MAIN},
    {FAVOURITES, "Add Favourite Mods", TOGGLE, MAIN, &f_addFav},

    {GAMES, "Exit Games", OPEN, MAIN},
    {GAMES, "Pistol", TOGGLE, MAIN, &f_pistol, WeaponPicked},
    {GAMES, "Revolver", TOGGLE, MAIN, &f_revolver, WeaponPicked},
    {GAMES, "Shotgun", TOGGLE, MAIN, &f_shotgun, WeaponPicked},
    {GAMES, "Sniper", TOGGLE, MAIN, &f_sniper, WeaponPicked},
    {GAMES, "Knife", TOGGLE, MAIN, &f_knife, WeaponPicked},
    {GAMES, "Blood Shader Test 1", TOGGLE, MAIN, &f_blood1},
    {GAMES, "Blood Test 2", TOGGLE, MAIN, &f_blood2},

    {OWNERPIN, "Cancel", OPEN, MAIN},
    {OWNER, "Exit Owner", OPEN, MAIN},
    {OWNER, "Phone Menu", TOGGLE, MAIN, &f_phoneMenu, MenuTypePicked},
    {OWNER, "Fling Gun", TOGGLE, MAIN, &f_flingGun, GunPicked},
    {OWNER, "Fling Power", SLIDER, MAIN, nullptr, nullptr, &g_flingPower, 3, FlingName},
    {SEARCH, "Exit Search", OPEN, MAIN},

    {SPAWNING, "Exit Spawning", OPEN, MAIN},
    {SPAWNING, "Trigger Spawn Held MenuItem", TOGGLE, MAIN, &f_spawnOnHand},
    {SPAWNING, "Spawn MenuItem Gun", TOGGLE, MAIN, &f_itemGun, GunPicked},
    {SPAWNING, "Spawn 3x3 Held MenuItem", ACTION, MAIN, nullptr, ArmDupe9},
    {SPAWNING, "Spawn Circle Held MenuItem", ACTION, MAIN, nullptr, ArmCircle},
    {SPAWNING, "Circle Amount", SLIDER, MAIN, nullptr, nullptr, &g_circleSlider, 4, CircleName},
    {SPAWNING, "Delete MenuItem Gun", TOGGLE, MAIN, &f_delGun, GunPicked},
    {SPAWNING, "MenuItem Orbit", TOGGLE, MAIN, &f_orbit, OrbitToggled},
    {SPAWNING, "MenuItem Rain", TOGGLE, MAIN, &f_itemRain, ItemRainToggled},
    {SPAWNING, "Spawn 9x9 Held MenuItem", ACTION, MAIN, nullptr, ArmGrid81},
    {SPAWNING, "Spawn 9x9 Held MenuItem Gun", TOGGLE, MAIN, &f_grid9Gun, GunPicked},
    {SPAWNING, "Spawn Held MenuItem Row", ACTION, MAIN, nullptr, ArmRow},
    {SPAWNING, "Yeeps Text Held MenuItem", ACTION, MAIN, nullptr, ArmYeepsText},
    {SPAWNING, "Spawn Presets", OPEN, SPAWNPRESETS},

    {OVERPOWERED, "Exit Overpowered Mods", OPEN, MAIN},
    {OVERPOWERED, "Ghost Monkey", TOGGLE, MAIN, &f_ghost},
    {OVERPOWERED, "Potion Effect Gun", TOGGLE, MAIN, &f_potionGun, GunPicked},
    {OVERPOWERED, "Potion Gun Effect", SLIDER, MAIN, nullptr, nullptr, &g_potionGun, 8, PotionName},
    {OVERPOWERED, "Give Self Potion", ACTION, MAIN, nullptr, GiveSelfPotion},
    {OVERPOWERED, "Self Potion Effect", SLIDER, MAIN, nullptr, nullptr, &g_potionSelf, 8, PotionName},

    {RIG, "Exit Rig Mods", OPEN, MAIN},
    {RIG, "Trigger Wave", TOGGLE, MAIN, &g_danceSel[0], DancePicked},
    {RIG, "Trigger T-Pose", TOGGLE, MAIN, &g_danceSel[2], DancePicked},
    {RIG, "Trigger Helicopter", TOGGLE, MAIN, &g_danceSel[1], DancePicked},

    {SOUNDBOARD, "Exit Soundboard", OPEN, MAIN},
    {SOUNDBOARD, "Stop Sounds", ACTION, MAIN, nullptr, StopSound},
    {SOUNDBOARD, "All Hear Sounds", TOGGLE, MAIN, &f_allHear},
    {SOUNDBOARD, "Hear Everyone", TOGGLE, MAIN, &f_hearAll, HearAllToggled},
    {SOUNDBOARD, "Reload Sounds", ACTION, MAIN, nullptr, SoundReloadBtn},

    {VISUAL, "Exit Visual Mods", OPEN, MAIN},
    {VISUAL, "Player ESP", TOGGLE, MAIN, &f_esp},
    {VISUAL, "Tracers", TOGGLE, MAIN, &f_tracers},
    {VISUAL, "Trail ESP", TOGGLE, MAIN, &f_trailEsp},
    {VISUAL, "Ripple", TOGGLE, MAIN, &f_ripple},
    {VISUAL, "Box ESP", TOGGLE, MAIN, &f_boxEsp},
    {VISUAL, "Rainbow Sky", TOGGLE, MAIN, &f_rainbowSky, SkyPicked},
    {VISUAL, "Space Stars", TOGGLE, MAIN, &f_spaceStars, SkyPicked},
    {VISUAL, "Galaxy Sky", TOGGLE, MAIN, &f_galaxySky, SkyPicked},
    {VISUAL, "Fullbright", TOGGLE, MAIN, &f_fullbright, AtmoPicked},
    {VISUAL, "Horror", TOGGLE, MAIN, &f_horror, AtmoPicked},
    {VISUAL, "Snow", TOGGLE, MAIN, &f_snow},
    {VISUAL, "Rain", TOGGLE, MAIN, &f_rain},

    {FUN, "Exit Fun Mods", OPEN, MAIN},
    {FUN, "Punch Fling", TOGGLE, MAIN, &f_punch},
    {FUN, "Punch Fling Power", SLIDER, MAIN, nullptr, nullptr, &g_punchPower, 5, PunchName},
    {FUN, "Draw", TOGGLE, MAIN, &f_draw},
    {FUN, "Draw Color", SLIDER, MAIN, nullptr, nullptr, &g_drawColor, 8, DrawColorName},
    {FUN, "Delete Draw", ACTION, MAIN, nullptr, DeleteDraw},
    {FUN, "Self Trail", TOGGLE, MAIN, &f_trail},
    {FUN, "Nametags", TOGGLE, MAIN, &f_nametags},
    {FUN, "Disable Boundary", TOGGLE, MAIN, &f_noBounds},
    {FUN, "Trigger Bullets", TOGGLE, MAIN, &f_bullets},
    {FUN, "Mic Changer", TOGGLE, MAIN, &f_micFx},
    {FUN, "Mic Effect", SLIDER, MAIN, nullptr, nullptr, &g_micMode, 4, MicName},
    {FUN, "Solid Players", TOGGLE, MAIN, &f_solidPlayers},
    {FUN, "Disable Body Collision", TOGGLE, MAIN, &f_noBody},
    {FUN, "Bounce", TOGGLE, MAIN, &f_bounce},
    {FUN, "Fake Lag", TOGGLE, MAIN, &f_fakeLag, LagChanged},
    {FUN, "Lag Amount", SLIDER, MAIN, nullptr, nullptr, &g_lagLevel, 4, LagName},
    {FUN, "Air Swim", TOGGLE, MAIN, &f_airSwim},
};
static constexpr int kItems = sizeof(g_items) / sizeof(g_items[0]);
static std::deque<MenuItem> g_eggItems;
static MenuItem g_returnItem = {ENABLED, "Exit Enabled Mods", OPEN, MAIN};

// Tooltips, shown under the menu while your finger is on a button (like ii's).
static const struct { const char *label, *tip; } kTips[] = {
    {"Settings", "Menu settings, themes and pointer colors."},
    {"Movement Mods", "Opens the movement mods."},
    {"Spawning", "Spawn the item in your left hand. Everyone sees them."},
    {"Overpowered Mods", "The strong stuff."},
    {"Rig Mods", "Pick one, then hold left trigger. Everyone sees it."},
    {"Soundboard", "Plays your sounds from the HSounds folder, in voice chat too."},
    {"Stop Sounds", "Stops the sound and gives you your microphone back."},
    {"All Hear Sounds", "On: everyone in the room hears your sounds through your voice. Off: only you."},
    {"Hear Everyone", "Boosts how far and how loud other players voices are."},
    {"No Sounds Found", "Put .wav / .mp3 / .ogg files in the HSounds folder, then press this."},
    {"Reload Sounds", "Looks in the HSounds folder again."},
    {"MenuItem Rain", "Hold an item in your left hand: copies fall around you. Everyone sees them."},
    {"Infinite Cotton", "Sets the cotton manager max and current to 1000000."},
    {"Projectile Count", "Projectiles per shot for every (Clone) item. Default = untouched."},
    {"Projectile Spread Angle", "Spread angle for every (Clone) item. Default = untouched."},
    {"Reload Stuffing Cost", "Reload stuffing cost for every (Clone) item. Default = untouched."},
    {"Refresh Projectiles", "Checks every (Clone) item again and applies the sliders."},
    {"Visual Mods", "Things only you see."},
    {"Fun Mods", "Just for fun."},
    {"Enabled Mods", "Shows every mod you have on."},
    {"Themes", "Blue, Orange, Rainbow, Dark Gray or Translucent."},
    {"Translucent", "See-through menu. Use the Translucency slider for how see-through."},
    {"Translucency", "How solid the Translucent theme is (10% = almost invisible, 100% = solid)."},
    {"Round Corners", "Rounds the corners of the menu board. Off = square."},
    {"Page Buttons", "Where the switch page buttons are: under the menu name, beside the menu, or at the bottom."},
    {"Under Menu Name", "Page buttons sit right under the menu name."},
    {"Beside Menu (Default)", "Page buttons sit on both sides of the menu."},
    {"Bottom Of Menu", "Page buttons sit at the bottom of the menu."},
    {"Mod Buttons", "<- and -> page buttons sit at the top of the mod list, just under the title."},
    {"Long Menu", "Shows every mod of a category on one long page."},
    {"Dark Gray", "Dark gray menu."},
    {"Blue", "Blue menu."},
    {"Orange", "Orange menu."},
    {"Rainbow", "The whole menu cycles through colors."},
    {"Change Pointer Position", "Moves the ball on your finger."},
    {"Right Hand Menu", "Hold B, menu on your right hand."},
    {"Show FPS", "Frames per second in the title."},
    {"Show Clock", "The time in the title."},
    {"Notifications", "Messages above the menu when you switch something."},
    {"Turn Everything Off", "Switches every mod off."},
    {"Platforms", "Hold a grip to stand on a platform."},
    {"Teleport Gun", "Hold right grip to aim, trigger to teleport."},
    {"Long Arms", "Gorilla Tag long arms: your hands reach 1.75x further, for you and everyone."},
    {"Low Gravity", "Jump higher, fall slower."},
    {"No Gravity", "Turns your gravity off."},
    {"High Gravity", "You fall faster and jump lower."},
    {"Speed Boost", "You pick up speed while you move, and jump faster and higher."},
    {"Iron Man", "Hold a trigger: that hand's palm is a thruster that pushes you away from it. (Not with Spider Yeeps.)"},
    {"Velocity Fly", "Hold A: fly where you look, faster and faster. Let go to keep your speed."},
    {"Teleport To Closest Player", "Takes you to the other player closest to you."},
    {"Save Position", "Remembers where you stand, in the slot picked below."},
    {"Go To Saved Position", "Takes you to the position saved in the slot below."},
    {"Position Slot", "Slide your finger along the bar to pick one of 5 save slots."},
    {"Spawn Circle Held MenuItem", "A ring of the item in your left hand appears around you."},
    {"Circle Amount", "How many items the ring has: slide along the bar."},
    {"Delete MenuItem Gun", "Right grip aims, trigger deletes the item you point at."},
    {"No Knockback", "Hits and pushes from others don't move you."},
    {"MenuItem Knockback", "Knockback of the item in your hand: slide right for stronger. x1 is normal."},
    {"Remove Cotton Limit", "Raises the game's cotton limit."},
    {"Trigger Wave", "Your right hand waves. Hold left trigger."},
    {"Bounce", "You bounce back up when you fall and hit the ground."},
    {"Trigger Spawn Held MenuItem", "Hold an item in your left hand. Right trigger spawns it at your right hand."},
    {"Spawn MenuItem Gun", "Hold an item in your left hand. Right grip aims, trigger spawns it."},
    {"Spawn 3x3 Held MenuItem", "9 of the item in your left hand appear in front of you."},
    {"Trigger Bullets", "Hold right trigger to shoot bullets from your hand. Only you see them."},
    {"Trigger T-Pose", "The classic T-pose. Hold left trigger."},
    {"Trigger Helicopter", "Both arms spin around you. Hold left trigger."},
    {"Stop Sound", "Stops the sound."},
    {"Player ESP", "A red rig on every other player, through walls and blocks."},
    {"Draw", "Hold right trigger to draw in the air. Only you see it."},
    {"Draw Color", "Slide your finger along the bar to pick the color of Draw and Trail."},
    {"Legs", "Left joystick walks, X or A jumps. Legs only you see."},
    {"Fullbright", "Every surface is drawn at full brightness (shader swap), no fog, plus a light on your head."},
    {"Horror", "Dark and scary: surfaces are tinted dark by shader, fog closes in, the light flickers and blacks out."},
    {"Snow", "It snows around you. Only you see it."},
    {"Disable Boundary", "Go past the map edge without being stopped or sent back."},
    {"Menu Type", "Hand Menu, Corner Menu, Pizza Menu or Long Menu."},
    {"Hand Menu", "The normal menu on your hand (hold Y, or B on the right hand)."},
    {"Corner Menu", "Menu in the top corner of your screen. Aim your hand at it, pull trigger."},
    {"Pizza Menu", "Text-only menu in the top corner, no pointer: X up, Y down, B select. Back / Next at the bottom change page."},
    {"Gun Pointer Color", "Color of the teleport and item gun pointer."},
    {"Gun Blue", "Blue gun pointer."},
    {"Gun Orange", "Orange gun pointer."},
    {"Gun Rainbow", "Rainbow gun pointer."},
    {"Delete Draw", "Erases everything you drew."},
    {"Flip Gravity", "You fall up instead of down."},
    {"Tracers", "Lines from you to every other player."},
    {"Box ESP", "A box around every other player."},
    {"Rainbow Sky", "The sky color slowly cycles through the rainbow."},
    {"Floating Menu", "The menu stays where you opened it instead of following your hand."},
    {"Toggle Menu", "Press the menu button once to open the menu and again to close it."},
    {"Colorblind Mode", "Shows ON / OFF text on the buttons so you don't need to tell colors apart."},
    {"Purple", "Purple menu theme."},
    {"Yellow", "Yellow menu theme."},
    {"White", "White menu theme."},
    {"Spawn Presets", "Spawn ready-made things."},
    {"Mic Changer", "Changes how your voice sounds to everyone. Pick the sound with Mic Effect."},
    {"Mic Effect", "Default, High Pitch, Low Pitch or Broken."},
    {"Rewind", "Records your movement while it is on. Hold the right trigger to go back along where you were. Turning it off deletes the recording."},
    {"Reverse Velocity", "Press B (X if the menu is on your right hand) to reverse the way you are moving."},
    {"Noclip", "Hold A to pass through walls and floors. Platforms stay solid."},
    {"Ghost Monkey", "With it on, press X (A if the menu is on your right hand) to freeze your rig for everyone else while you walk around. Press again to unfreeze. Balls sit on your hands while frozen."},
    {"Inv Monkey", "With it on, click the left stick to turn invisible for everyone else while you still move around. Click again to come back. Balls sit on your hands."},
    {"Potion Effect Gun", "Right grip + trigger at a player: gives them the effect picked with Potion Gun Effect."},
    {"Potion Gun Effect", "Legs, Visibility, Love, Blindness, Small, Big, Slow Falling or Levitation."},
    {"Give Self Potion", "Gives you the effect picked with Self Potion Effect for 30 seconds."},
    {"Self Potion Effect", "Legs, Visibility, Love, Blindness, Small, Big, Slow Falling or Levitation."},
    {"CS2 Strafe", "Hold A to run forward where you look (the left stick curves you). In the air you get faster and faster."},
    {"Mini Map", "A screen in the top right corner shows a camera high above you, following you."},
    {"Disable Body Collision", "Your body no longer collides with anything."},
    {"Solid Players", "Other players become solid: you can walk on top of their heads."},
    {"Spawn Presets", "One button per mob egg; spawns it in front of you with its normal colors."},
    {"Platform Type", "Default: a platform under your hand while you hold the grip. Sticky: the same, but your hand is also in a solid ball. Follow Hand: the same, but the platform starts turned like your hand (and then stays that way)."},
    {"Self Trail", "A line follows behind you. Color comes from Draw Color."},
    {"Size Changer", "Changes your size to the one picked with Size. Your speed and jump follow your size."},
    {"Size", "Small, Normal or Big."},
    {"Freecam", "A free camera shows on a screen in the top left. Left stick moves it, right stick turns it and goes up / down. You stand still."},
    {"Freecam Speed", "Top speed of the Freecam."},
    {"Nametags", "Shows every other player's name, how often their position updates (net / s) and their skin color code above their head."},
    {"Trail ESP", "Every other player leaves a short trail behind them in your Draw Color."},
    {"Ripple", "A water ripple spreads out where your hands or feet touch the ground."},
    {"Text Colour", "Pick the color of the menu text."},
    {"Black", "Black menu theme."},
    {"Dark Blue", "Dark blue menu theme."},
    {"Target Lock", "Guns that affect other players (like the Potion Effect Gun and the Games weapons) lock onto the closest player inside the Lock Radius."},
    {"Lock Radius", "How far away Target Lock looks for a player."},
    {"Gun Effect", "The shape of the line your guns draw: straight, spiral or wave."},
    {"Target Info", "With Target Lock on and one of those guns out: a box and info (name, net / s, color) on the locked player."},
    {"Other Players", "Refresh the list, pick a player, then do things to them."},
    {"Refresh Players", "Looks at who is in the lobby now."},
    {"Spectate Selected", "Shows what the selected player sees, or a view of them from behind, in the top middle."},
    {"Spectate View", "First Person or 3D Person."},
    {"Give Selected Effect", "Gives the selected player the effect picked with Selected Effect."},
    {"Selected Effect", "Legs, Visibility, Love, Blindness, Small, Big, Slow Falling or Levitation."},
    {"Spawn MenuItem At Selected", "Spawns your held item in front of the selected player."},
    {"Favourites", "Your favourite mods in one place."},
    {"Add Favourite Mods", "While on, pressing a mod adds it to (or removes it from) your favourites instead of using it."},
    {"Games", "Weapons and blood."},
    {"Pistol", "A pistol in your right hand. Right trigger shoots a fast bullet and pushes you back a little."},
    {"Shotgun", "A shotgun in your right hand. Right trigger shoots 5 spread pellets that vanish at close range and pushes you back."},
    {"Sniper", "A sniper in your right hand. Right trigger shoots a very fast bullet and pushes you back."},
    {"Knife", "A knife in your right hand. Stab a player with it."},
    {"Blood Shader Test 1", "Players bleed where the Games weapons hit them, with soft see-through blood and splats on the ground."},
    {"Blood Test 2", "Players bleed where the Games weapons hit them, with solid blood drops and splats on the ground."},
    {"Owner", "Needs a code."},
    {"Remove Outline", "No outline around the menu board."},
    {"Pin Button", "A pin on the left of the menu name: a shortcut for Floating Menu."},
    {"Stay Locked", "A gun that affects other players (Potion Effect Gun, Fling Gun, the Games weapons) keeps the first player it finds while you hold the grip, instead of switching to whoever is closest."},
    {"Space Stars", "The sky becomes a starfield."},
    {"Galaxy Sky", "The sky becomes a colorful nebula."},
    {"Fling Gun", "Right grip + trigger: flings the player you are pointing at (or the locked-on one with Target Lock) using the game's own knockback."},
    {"Fling Power", "Light, Medium or Hard."},
    {"Phone Menu", "The menu becomes a phone: apps for Settings, Camera, Photos, Music, Maps and Notes. Exit Menu Type (red, at the bottom of Settings) turns it off."},
    {"Theme Presets", "Pick a ready-made look, or save your current look as a preset of your own."},
    {"Leo's Menu", "Blue theme, normal text, side page buttons, normal menu, blue gun pointer."},
    {"Stupid Menu", "Orange theme, normal text, side page buttons, normal menu, orange gun pointer."},
    {"Modder 3x Menu", "Black theme, purple text, wide menu, no outline, mod-button page switcher, blue gun pointer."},
    {"Malachi's Theme", "Dark blue theme, mod-button page switcher, yellow buttons that turn purple when on."},
    {"Seralyth's Theme", "Purple theme, white text, default page switcher."},
    {"Add Custom Preset", "Saves how the menu looks now (theme, text color, page buttons, menu type, gun pointer, corners, width). You type a name."},
    {"Delete Last Custom", "Removes the newest custom preset."},
    {"Wide Menu", "Makes the menu a bit wider."},
    {"Revolver", "A revolver in your right hand. Right trigger shoots like the pistol. A spins it on your finger."},
    {"Smooth Noclip", "Hold A: fly where you look through walls, smoothly (speeds up and slows down gently)."},
    {"Joystick Fly", "Left stick flies where you look (forward, back, sideways), right stick up / down. Let go to hover."},
    {"Joystick Fly Speed", "Top speed of Joystick Fly."},
    {"Zero Gravity Fly", "Hold A to fly where you look. Let go and you stay in the air, not moving, until you turn it off or fly again."},
    {"Zero Gravity Speed", "Top speed of Zero Gravity Fly."},
    {"Strafe Power", "How much speed one strafe can add."},
    {"Experimental Mods", "Things that might not work in every game version."},
    {"Punch Fling", "When another player punches you, you get flung away from them."},
    {"Punch Fling Power", "How hard you get flung."},
    {"Pixelated", "Menu type: the menu text and look get blocky and pixelated."},
    {"Spider Yeeps", "Hold a trigger: shoots a web where that hand points and swings you. Let go to release."},
    {"Dash", "Click the left thumbstick: you are launched where you look."},
    {"Dash Speed", "How fast Dash launches you."},
    {"Teleport Dash", "Click the right thumbstick: you jump forward where you look."},
    {"Teleport Distance", "How far Teleport Dash takes you."},
    {"MenuItem Orbit", "Hold an item in your left hand: copies of it circle around you."},
    {"Spawn 9x9 Held MenuItem", "81 of the item in your left hand appear in front of you."},
    {"Spawn 9x9 Held MenuItem Gun", "Hold an item in your left hand. Right grip aims, trigger spawns a 9x9 grid there."},
    {"Spawn Held MenuItem Row", "A row of the item in your left hand appears in front of you."},
    {"Yeeps Text Held MenuItem", "Spells YEEPS in front of you with the item in your left hand."},
    {"Rain", "It rains around you. Only you see it."},
    {"Fake Lag", "Others see you lag: your network update rate is lowered."},
    {"Lag Amount", "How bad the fake lag is: slide along the bar."},
    {"Air Swim", "Float in the air. Hold a grip and sweep your hand to swim."},
};
static const char *TipFor(const char *label) {
    if (strncmp(label, "Exit ", 5) == 0) return "Returns you back to the main category.";
    for (auto &t : kTips) if (strcmp(t.label, label) == 0) return t.tip;
    return "";
}

// Themes: Blue, Orange and Rainbow. Rainbow animates the colors every frame.
struct Theme { const char *name; Color bgA, bgB, button, buttonOn, title; };
static constexpr int kRainbow = 2, kTranslucent = 4;
static const Theme kThemes[] = {
    {"Blue",    Color(0.20f, 0.50f, 1.0f, 1), Color(0.10f, 0.30f, 0.85f, 1), Color(0.10f, 0.28f, 0.65f, 1), Color(0.04f, 0.12f, 0.32f, 1), Color(0.70f, 0.85f, 1.0f, 1)},
    {"Orange",  Color(1.0f, 0.50f, 0.0f, 1), Color(1.0f, 0.40f, 0.0f, 1), Color(0.67f, 0.33f, 0.0f, 1), Color(0.33f, 0.16f, 0.0f, 1), Color(1.0f, 0.75f, 0.49f, 1)},
    {"Rainbow", Color(0.50f, 0.20f, 0.90f, 1), Color(0.90f, 0.20f, 0.60f, 1), Color(0.22f, 0.10f, 0.40f, 1), Color(0.10f, 0.05f, 0.20f, 1), Color(1.0f, 0.82f, 1.0f, 1)},
    {"Dark Gray", Color(0.20f, 0.20f, 0.22f, 1), Color(0.12f, 0.12f, 0.14f, 1), Color(0.17f, 0.17f, 0.19f, 1), Color(0.05f, 0.05f, 0.06f, 1), Color(0.80f, 0.80f, 0.84f, 1)},
    {"Translucent", Color(0.55f, 0.75f, 1.0f, 1), Color(0.35f, 0.55f, 0.95f, 1), Color(0.15f, 0.25f, 0.50f, 1), Color(0.05f, 0.10f, 0.25f, 1), Color(0.85f, 0.93f, 1.0f, 1)},
    {"Purple", Color(0.55f, 0.25f, 0.85f, 1), Color(0.35f, 0.12f, 0.60f, 1), Color(0.30f, 0.12f, 0.50f, 1), Color(0.12f, 0.04f, 0.25f, 1), Color(0.95f, 0.85f, 1.0f, 1)},
    {"Yellow", Color(1.0f, 0.90f, 0.25f, 1), Color(0.85f, 0.70f, 0.05f, 1), Color(0.65f, 0.50f, 0.05f, 1), Color(0.25f, 0.18f, 0.0f, 1), Color(1.0f, 0.97f, 0.80f, 1)},
    {"White", Color(0.97f, 0.97f, 0.97f, 1), Color(0.80f, 0.80f, 0.85f, 1), Color(0.90f, 0.90f, 0.93f, 1), Color(0.25f, 0.25f, 0.30f, 1), Color(0.10f, 0.10f, 0.15f, 1)},
    {"Black", Color(0.03f, 0.03f, 0.03f, 1), Color(0.0f, 0.0f, 0.0f, 1), Color(0.10f, 0.10f, 0.11f, 1), Color(0.30f, 0.30f, 0.34f, 1), Color(0.92f, 0.92f, 0.95f, 1)},
    {"Dark Blue", Color(0.08f, 0.14f, 0.32f, 1), Color(0.04f, 0.08f, 0.2f, 1), Color(0.06f, 0.1f, 0.22f, 1), Color(0.02f, 0.04f, 0.1f, 1), Color(0.75f, 0.82f, 1.0f, 1)},
};
static constexpr int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
static int g_theme = 0;
static const Theme &T() { return kThemes[g_theme]; }
static bool g_btnOverride = false;
static Color g_btnCol, g_btnOnCol;
static Color BtnCol() { return g_btnOverride ? g_btnCol : kThemes[g_theme].button; }
static Color BtnOnCol() { return g_btnOverride ? g_btnOnCol : kThemes[g_theme].buttonOn; }
static bool Trans() { return f_pizza || g_theme == kTranslucent; }      // the panels are see-through


// ---- menu objects ----------------------------------------------------------------

// Layout in meters, matching ii's menu as it was ported to Yeeps. Everything is drawn
// on a "board" object: +x goes across, +y goes up, the player looks at it from -z.
static constexpr int kRows = 8;                                   // buttons per page
static constexpr int kMaxRows = 40;                               // Long Menu: the most buttons one page can show
static constexpr int kNextHit = 1000, kSearchHit = 1500, kPinHit = 1600, kKeyHit = 2000;                             // RowAt result for the "next page" button
static float kBoardW = 0.30f, kBtnW = 0.27f;                       // Wide Menu changes these
static const float kBoardH = 0.3825f, kBtnH = 0.0306f;
static const float kTitleY = 0.235f, kSideW = 0.06f, kSideH = 0.344f;
static float kSideX = 0.2f;
static float RowY(int r) { return 0.13f - r * 0.04f; }

static Ref g_menu, g_board, g_bg, g_outline, g_pointer, g_title, g_titleBar;
static Ref g_rowCube[kMaxRows], g_rowLabel[kMaxRows], g_rowText[kMaxRows], g_knob[kMaxRows];
static Ref g_menuCanvas[2];
static Ref g_pinBtn, g_pinLbl;
static Ref g_bgX[5], g_outX[5], g_prevLbl, g_nextLbl, g_verGo;   // extra parts of the rounded board, page button labels, version text
static float g_boardH = kBoardH, g_boardCy = 0;                   // the board grows downward in the Long Menu
static Ref g_prev, g_next, g_notifyGo, g_notifyBar, g_notifyText, g_tipText, g_leftPlatform, g_rightPlatform;
static double g_openTime = 0, g_flashUntil = 0;
static int g_flashRow = -2, g_hover = -2;
static bool g_built = false, g_menuShown = false, g_dirty = true;
static bool g_pizzaHover = false, g_pizzaTrig = false, g_cursorOn = false;
// True while your trigger belongs to the menu, so Draw / Bullets / Spawn don't fire.
static bool MenuBusy() { return f_pizza ? g_pizzaHover : g_menuShown; }
static Cat g_cat = MAIN;
static int g_page = 0;
static bool IsMenuFlag(const bool *f) { return f == &f_notify || f == &f_pizza || f == &f_textMenu || f == &f_wideMenu || f == &f_noOutline || f == &f_pinBtn || f == &f_addFav || f == &f_phoneMenu || f == &f_floatMenu || f == &f_toggleMenu || f == &f_colorblind || f == &f_lbColor || f == &f_longMenu || f == &f_pixelMenu || f == &f_pbUnder || f == &f_pbBottom || f == &f_pbSides; }
static std::deque<MenuItem> g_soundItems;
static std::deque<MenuItem> g_playerItems;
static std::deque<MenuItem> g_presetItems;
static bool g_playersInit = false;                             // one button per sound file
static std::vector<MenuItem *> g_view;                                // what the current page list shows
static double g_time = 0, g_nextPress = 0, g_notifyUntil = 0, g_nextTitle = 0;
static int g_touching = -2, g_frames = 0;
static float g_fps = 0;

static int RPP() { return f_longMenu ? kMaxRows : kRows; }        // buttons per page right now
static int PageCount() { return g_view.empty() ? 1 : (int)(g_view.size() + RPP() - 1) / RPP(); }

static void BuildView() {
    if (g_cat == OTHERPLAYERS) PlayersEnsure();
    g_view.clear();
    if (g_cat == ENABLED) {
        g_view.push_back(&g_returnItem);
        for (int _gi = 0; _gi < kItems; _gi++) { auto &it = g_items[_gi]; if (it.kind == TOGGLE && *it.flag && !IsMenuFlag(it.flag)) g_view.push_back(&it); }
    } else {
        for (int _gi = 0; _gi < kItems; _gi++) { auto &it = g_items[_gi]; if (it.cat == g_cat) g_view.push_back(&it); }
        if (g_cat == SOUNDBOARD) {
            SoundEnsure();
            for (auto &it : g_soundItems) g_view.push_back(&it);
            static MenuItem noSounds{SOUNDBOARD, "No Sounds Found", ACTION, MAIN, nullptr, SoundReloadBtn};
            if (g_soundItems.empty()) g_view.push_back(&noSounds);
        }
        if (g_cat == OTHERPLAYERS) for (auto &it : g_playerItems) g_view.push_back(&it);
        if (g_cat == FAVOURITES) for (int _gi = 0; _gi < kItems; _gi++) { auto &it = g_items[_gi]; if (it.cat != FAVOURITES && it.kind != OPEN && IsFav(it.label)) g_view.push_back(&it); }
        if (g_cat == SEARCH) SearchFill();
        if (g_cat == PRESETS) { PresetsEnsure(); for (auto &it : g_presetItems) g_view.push_back(&it); }
        if (g_cat == SPAWNPRESETS) { EggEnsure(); for (auto &it : g_eggItems) g_view.push_back(&it); }
    }
    if (g_page >= PageCount()) g_page = 0;
}

static Il2CppObject *Label(Il2CppObject *canvasT, const char *s, Vector3 pos, float w, float h, int size, Ref *textOut) {
    if (!G.font.get() || !G.tText) return nullptr;
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("Label"));
    Il2CppObject *text = go ? G.goAddComponent[go](G.tText) : nullptr;
    if (!text) return nullptr;
    Il2CppObject *t = Tr(go);
    G.trSetParent[t](canvasT, false);
    G.rtSetSizeDelta[t](Vector2(w * 1000, h * 1000));
    G.trSetLocalPosition[t](pos * 1000);
    G.trSetLocalRotation[t](Quaternion(0, 0, 0, 1));
    G.trSetLocalScale[t](Vector3(1, 1, 1));
    G.textSetFont[text](G.font.get());
    G.textSetText[text](CreateMonoString(s));
    G.textSetFontSize[text](size);
    G.textSetAlignment[text](4);                 // MiddleCenter
    if (G.textSetFontStyle.IsValid()) G.textSetFontStyle[text](2);   // Italic, like ii's menu
    if (G.textSetBestFit.IsValid()) {            // shrink long labels to fit their button
        G.textSetBestFit[text](true);
        G.textSetMinSize[text](1);
        G.textSetMaxSize[text](size);
    }
    G.graphicSetColor[text](Color(1, 1, 1, 1));
    if (textOut) textOut->set(text);
    return go;
}

// A world-space canvas (1 unit = 1 mm) under parent.
static Il2CppObject *Canvas(Il2CppObject *parentT, Vector3 pos, float w, float h) {
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("LeoMenuText"));
    Il2CppObject *canvas = go && G.tCanvas ? G.goAddComponent[go](G.tCanvas) : nullptr;
    if (!canvas) return nullptr;
    G.canvasSetRenderMode[canvas](2);            // WorldSpace
    Il2CppObject *ct = Tr(go);
    G.trSetParent[ct](parentT, false);
    G.rtSetSizeDelta[ct](Vector2(w * 1000, h * 1000));
    G.trSetLocalPosition[ct](pos);
    G.trSetLocalRotation[ct](Quaternion(0, 0, 0, 1));
    G.trSetLocalScale[ct](Vector3(0.001f, 0.001f, 0.001f));
    return go;
}

static Il2CppObject *Part(Il2CppObject *parentT, Color c, Vector3 pos, Vector3 scale) {
    Il2CppObject *o = Cube(c, false);
    if (!o) return nullptr;
    Il2CppObject *t = Tr(o);
    G.trSetParent[t](parentT, false);
    G.trSetLocalPosition[t](pos);
    G.trSetLocalScale[t](scale);
    return o;
}

// Where the ball sits on your hand (ii's four pointer positions).
static const Vector3 kPointerSpots[] = {Vector3(0.01f, -0.017f, 0.19f), Vector3(0, -0.1f, 0.05f),
                                        Vector3(0, 0.1f, -0.05f), Vector3(0, 0.0666f, 0.1f)};
static int g_pointerSpot = 0;

static void PointerToHand() {
    Il2CppObject *p = g_pointer.get(), *hand = f_rightHand ? g_leftHand.get() : g_rightHand.get();
    if (!Alive(p) || !hand) return;
    Il2CppObject *t = Tr(p);
    Vector3 at = kPointerSpots[g_pointerSpot];
    if (f_rightHand) at.x = -at.x;
    G.trSetParent[t](hand, false);
    G.trSetLocalPosition[t](at);
}
static void NextPointer() { g_pointerSpot = (g_pointerSpot + 1) % 4; PointerToHand(); Notify("Pointer position %d", g_pointerSpot + 1); }

// ---- rounded board, page button spots ----
// A rounded rectangle is built from the board cube (made narrower) plus a second cube (made shorter) plus four flat
// cylinders in the corners. With Round Corners on Off it is just the one cube.
static const float kRoundR[] = {0, 0.004f, 0.008f, 0.012f, 0.016f, 0.022f, 0.028f, 0.035f, 0.045f};
static void MakeRoundParts(Il2CppObject *bt, Ref *x, Color c) {
    x[0].set(Part(bt, c, Vector3(0, 0, 0), Vector3(0.01f, 0.01f, 0.01f)));
    for (int i = 1; i < 5; i++) {
        Il2CppObject *o = Prim(2, c, false);                       // cylinder
        if (!o) continue;
        Il2CppObject *t = Tr(o);
        G.trSetParent[t](bt, false);
        G.trSetLocalRotation[t](Quaternion(0.70710678f, 0, 0, 0.70710678f));   // its length runs along the board's depth
        x[i].set(o);
    }
}
static void LayoutRound(Ref &main, Ref *x, float cy, float w, float h, float z, float th, float r) {
    Il2CppObject *m = main.get();
    if (!Alive(m)) return;
    float lim = (w < h ? w : h) / 2 - 0.0005f;
    if (r > lim) r = lim;
    if (r < 0.0005f) r = 0;
    Il2CppObject *mt = Tr(m);
    G.trSetLocalPosition[mt](Vector3(0, cy, z));
    if (r == 0) {
        G.trSetLocalScale[mt](Vector3(w, h, th));
        for (int i = 0; i < 5; i++) Show(x[i], false);
        return;
    }
    G.trSetLocalScale[mt](Vector3(w - 2 * r, h, th));
    if (Alive(x[0].get())) {
        G.trSetLocalPosition[Tr(x[0].get())](Vector3(0, cy, z));
        G.trSetLocalScale[Tr(x[0].get())](Vector3(w, h - 2 * r, th));
        Show(x[0], true);
    }
    for (int i = 1; i < 5; i++) {
        if (!Alive(x[i].get())) continue;
        float sx = ((i & 1) ? 1.0f : -1.0f) * (w / 2 - r), sy = ((i & 2) ? 1.0f : -1.0f) * (h / 2 - r);
        G.trSetLocalPosition[Tr(x[i].get())](Vector3(sx, cy + sy, z));
        G.trSetLocalScale[Tr(x[i].get())](Vector3(2 * r, th / 2, 2 * r));
        Show(x[i], true);
    }
}
static void SetBG(Color c) { SetColor(g_bg.get(), c); for (auto &r : g_bgX) SetColor(r.get(), c); }
static void SetOutline(Color c) { Color c2 = f_noOutline ? Color(c.r, c.g, c.b, 0) : c; SetColor(g_outline.get(), c2); for (auto &r : g_outX) SetColor(r.get(), c2); }
static void OutlineToggled() { g_dirty = true; }
// Where the page buttons go: 0 beside the menu (the old place), 1 right under the menu name, 2 at the bottom of the menu.
static int PageMode() { return f_pbUnder ? 1 : f_pbBottom ? 2 : f_pbModTop ? 3 : 0; }
struct PBox { float x, y, w, h; };                    // the "<" button; the ">" button is the mirror image
static PBox PageBox() {
    switch (PageMode()) {
        case 1: return {-0.075f, 0.1795f, 0.12f, 0.032f};
        case 2: return {-0.075f, g_boardCy - g_boardH / 2 + 0.0132f, 0.12f, 0.022f};
        case 3: return {-0.075f, kBoardH / 2 - 0.028f, 0.12f, 0.03f};        // the top of the mod list, just under the title
        default: return {-kSideX, g_boardCy, kSideW, g_boardH * 0.9f};
    }
}

// Built once per room; afterwards only shown/hidden, moved and relabeled.
static void MatTransparent(Il2CppObject *go, bool on);
static int g_rowsBuilt = 0;
// Row buttons are made when they are needed: 8 at first, more only when the Long Menu shows more (each row is 3 objects,
// and a lot of text objects at menu load was too much at once).
static void BuildRows(int from, int to) {
    Il2CppObject *board = g_board.get(), *canvas = g_menuCanvas[0].get();
    if (!Alive(board) || !Alive(canvas)) return;
    Il2CppObject *bt = Tr(board), *ct = Tr(canvas);
    for (int r = from; r < to && r < kMaxRows; r++) {
        g_rowCube[r].set(Part(bt, BtnCol(), Vector3(0, RowY(r), -0.005f), Vector3(kBtnW, kBtnH, 0.009f)));
        g_knob[r].set(Part(bt, Color(1, 1, 1, 1), Vector3(0, RowY(r), -0.004f), Vector3(0.014f, kBtnH * 0.9f, 0.012f)));
        Show(g_knob[r], false);
        g_rowLabel[r].set(Label(ct, "", Vector3(0, RowY(r), 0), kBtnW, kBtnH, 19, &g_rowText[r]));
        if (Trans()) MatTransparent(g_rowCube[r].get(), true);
    }
    g_rowsBuilt = to < kMaxRows ? to : kMaxRows;
}
static void BuildMenu() {
    ym::log("leos-menu: building the menu");
    auto NewObject = [](const char *name) {
        return Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString(name));
    };
    Il2CppObject *menu = NewObject("LeoMenu"), *board = NewObject("LeoMenuBoard");
    if (!menu || !board) return;
    g_menu.set(menu);
    g_board.set(board);
    Il2CppObject *bt = Tr(board);
    G.trSetParent[bt](Tr(menu), false);
    // Like ii's menu: the board stands 10 cm out from the hand on its +x side, facing +x.
    G.trSetLocalPosition[bt](Vector3(0.1f, 0, 0));
    G.trSetLocalRotation[bt](Quaternion::LookRotation(Vector3(-1, 0, 0), Vector3(0, 0, 1)));

    g_outline.set(Part(bt, T().title, Vector3(0, 0, 0.004f), Vector3(kBoardW + 0.008f, kBoardH + 0.008f, 0.004f)));
    g_bg.set(Part(bt, T().bgA, Vector3(0, 0, 0), Vector3(kBoardW, kBoardH, 0.01f)));
    g_prev.set(Part(bt, BtnCol(), Vector3(-kSideX, 0, 0), Vector3(kSideW, kSideH, 0.009f)));
    // The title sits on its own dark bar above the board (where ii's Disconnect button was)
    g_titleBar.set(Part(bt, BtnOnCol(), Vector3(0, kTitleY, -0.002f), Vector3(kBtnW, 0.045f, 0.009f)));
    g_next.set(Part(bt, BtnCol(), Vector3(kSideX, 0, 0), Vector3(kSideW, kSideH, 0.009f)));

    if (Il2CppObject *canvas = Canvas(bt, Vector3(0, 0, -0.0105f), kBoardW + 2 * kSideX, kBoardH + 0.1f)) {
        g_menuCanvas[0].set(canvas);
        Il2CppObject *ct = Tr(canvas);
        Label(ct, "Leo's Menu", Vector3(0, kTitleY, 0), kBtnW, 0.045f, 32, &g_title);
        g_rowsBuilt = 0;
        BuildRows(0, kRows);
        g_pinLbl.set(Label(ct, "o-", Vector3(-kBtnW / 2 + 0.019f, kTitleY, -0.0005f), 0.03f, 0.022f, 15, nullptr));
        g_prevLbl.set(Label(ct, "<", Vector3(-kSideX, 0, -0.0005f), kSideW, 0.05f, 34, nullptr));
        g_nextLbl.set(Label(ct, ">", Vector3(kSideX, 0, -0.0005f), kSideW, 0.05f, 34, nullptr));
        SetText(g_prevLbl, PageMode() == 3 ? "<-" : "<");
        SetText(g_nextLbl, PageMode() == 3 ? "->" : ">");
        Label(ct, "", Vector3(0, -kBoardH / 2 - 0.02f, 0), kBoardW + 0.1f, 0.03f, 15, &g_tipText);
        char ver[64];
        snprintf(ver, sizeof ver, "Leo's Menu %s - build %s", kVersion, ym::api()->gameVersion);
        g_verGo.set(Label(ct, ver, Vector3(0, -kBoardH / 2 + 0.012f, 0), kBoardW, 0.02f, 11, nullptr));
    }
    // Pointer: a small ball on the index finger of the other hand
    if (Il2CppObject *p = Prim(0, T().bgB, false)) {
        G.trSetLocalScale[Tr(p)](Vector3(0.01f, 0.01f, 0.01f));
        g_pointer.set(p);
        PointerToHand();
    }
    // Notifications: a small bar right above the menu, like ii's notifications
    g_notifyBar.set(Part(bt, BtnOnCol(), Vector3(0, kTitleY + 0.075f, -0.002f), Vector3(kBtnW, 0.03f, 0.009f)));
    if (Il2CppObject *n = Canvas(bt, Vector3(0, kTitleY + 0.075f, -0.0105f), kBtnW, 0.03f)) {
        g_notifyGo.set(n);
        g_menuCanvas[1].set(n);
        Label(Tr(n), "", Vector3(0, 0, 0), kBtnW, 0.03f, 19, &g_notifyText);
        G.goSetActive[n](false);
    }
    Show(g_notifyBar, false);
    if (Trans()) SetTransparent(true);
    G.goSetActive[menu](false);
    Show(g_pointer, false);
    g_built = true;
    g_dirty = true;
    ym::log("leos-menu: menu built");
}

static void Notify(const char *fmt, ...) {
    if (!f_notify) return;
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    snprintf(g_note, sizeof g_note, "%s", buf);          // the Pizza Menu shows this line
    g_noteUntil = g_time + 3.0;
    if (!Alive(g_notifyGo.get())) return;
    SetText(g_notifyText, buf);
    G.goSetActive[g_notifyGo.get()](true);
    Show(g_notifyBar, true);
    g_notifyUntil = g_time + 2.0;
}

// In the Pizza Menu the panels are see-through (alpha a); in the hand menu colors stay solid.
static Color TextCol(bool on) {                                  // Settings > Text Colour
    (void)on;
    switch (g_textCol) {
    case 1: return Color(1.0f, 0.15f, 0.15f, 1);                  // Red
    case 2: return Color(0, 0, 0, 1);                              // Black
    case 3: return Color(0.72f, 0.3f, 1.0f, 1);                    // Purple
    case 4: return Color(0.82f, 0.68f, 1.0f, 1);                   // Purple/White
    case 5: return Color(0.75f, 0.85f, 1.0f, 1);                   // Blue/White
    default: return Color(1, 1, 1, 1);                             // White
    }
}
static Color Tint(Color c, float a) {
    if (f_pizza) return Color(c.r, c.g, c.b, a);
    if (g_theme == kTranslucent) { float k = a * kTransA[g_transLvl] / 0.3f; return Color(c.r, c.g, c.b, k > 1 ? 1.0f : k); }   // the Translucency slider
    return c;
}

// Sizes the board to the rows on show, rounds it, and puts the page buttons where Settings > Page Buttons says.
static void ApplyLayout(int shown) {
    int n = shown < kRows ? kRows : shown;
    float bottom = RowY(n - 1) - 0.04125f, top = kBoardH / 2;
    g_boardH = top - bottom;
    g_boardCy = (top + bottom) / 2;
    float r = kRoundR[g_roundLvl];
    if (r > 0 && !Alive(g_bgX[0].get()) && Alive(g_board.get())) {          // the rounded corner parts are made the first time they are wanted
        Il2CppObject *bt = Tr(g_board.get());
        MakeRoundParts(bt, g_outX, T().title);
        MakeRoundParts(bt, g_bgX, T().bgA);
        if (Trans()) { for (auto &p : g_bgX) MatTransparent(p.get(), true); for (auto &p : g_outX) MatTransparent(p.get(), true); }
    }
    LayoutRound(g_bg, g_bgX, g_boardCy, kBoardW, g_boardH, 0.0f, 0.01f, r);
    LayoutRound(g_outline, g_outX, g_boardCy, kBoardW + 0.008f, g_boardH + 0.008f, 0.004f, 0.004f, r > 0 ? r + 0.004f : 0.0f);
    PBox b = PageBox();
    if (Alive(g_prev.get())) { G.trSetLocalPosition[Tr(g_prev.get())](Vector3(b.x, b.y, 0)); G.trSetLocalScale[Tr(g_prev.get())](Vector3(b.w, b.h, 0.009f)); }
    if (Alive(g_next.get())) { G.trSetLocalPosition[Tr(g_next.get())](Vector3(-b.x, b.y, 0)); G.trSetLocalScale[Tr(g_next.get())](Vector3(b.w, b.h, 0.009f)); }
    if (Alive(g_prevLbl.get())) G.trSetLocalPosition[Tr(g_prevLbl.get())](Vector3(b.x, b.y, -0.0005f) * 1000.0f);
    if (Alive(g_nextLbl.get())) G.trSetLocalPosition[Tr(g_nextLbl.get())](Vector3(-b.x, b.y, -0.0005f) * 1000.0f);
    float low = g_boardCy - g_boardH / 2;
    if (Alive(g_tipText.get())) G.trSetLocalPosition[G.compTransform[g_tipText.get()]()](Vector3(0, low - 0.02f, 0) * 1000.0f);
    if (Alive(g_verGo.get())) G.trSetLocalPosition[Tr(g_verGo.get())](Vector3(0, PageMode() == 2 ? low - 0.045f : low + 0.012f, 0) * 1000.0f);
}

static bool g_lastTrans = false;
// Relabels and recolors the rows. Only runs after something changed.
static void RenderPage() {
    BuildView();
    SearchUi();
    if (Trans() != g_lastTrans) { g_lastTrans = Trans(); SetTransparent(g_lastTrans); }
    int rpp = RPP(), shown = 0;
    {   // Long Menu: make the extra rows the first time they are needed
        int want = (int)g_view.size() - g_page * rpp;
        if (want > rpp) want = rpp;
        if (want > g_rowsBuilt) BuildRows(g_rowsBuilt, want);
    }
    for (int r = 0; r < kMaxRows; r++) {
        int i = g_page * rpp + r;
        bool used = r < rpp && i < (int)g_view.size();
        if (used) shown = r + 1;
        Show(g_rowCube[r], used);
        Show(g_rowLabel[r], used);
        if (!used) { Show(g_knob[r], false); continue; }
        MenuItem *it = g_view[i];
        bool on = it->kind == TOGGLE && *it->flag;
        if (it->kind == SLIDER) {
            char buf[64];
            snprintf(buf, sizeof buf, "%s: %s", it->label, it->iname ? it->iname(*it->ival) : "");
            SetText(g_rowText[r], buf);
            float u = it->icount > 1 ? (float)*it->ival / (float)(it->icount - 1) : 0.f;
            if (Alive(g_knob[r].get()))
                G.trSetLocalPosition[Tr(g_knob[r].get())](Vector3(-kBtnW / 2 + 0.007f + (kBtnW - 0.014f) * u, RowY(r), -0.004f));
            SetColor(g_knob[r].get(), it->ival == &g_drawColor ? DrawColor() : Color(1, 1, 1, 1));
            Show(g_knob[r], true);
        } else {
            if (it->kind == TOGGLE && f_colorblind) {                // Colorblind Mode: the state is written out
                char cb[80];
                snprintf(cb, sizeof cb, "%s  [%s]", it->label, on ? "ON" : "OFF");
                SetText(g_rowText[r], cb);
            } else SetText(g_rowText[r], it->label);
            Show(g_knob[r], false);
        }
        SetColor(g_rowCube[r].get(), r == g_flashRow ? Tint(Color(1, 0.9f, 0.6f, 1), 0.6f)
                                     : on ? Tint(BtnOnCol(), 0.55f) : Tint(BtnCol(), 0.3f));
        if (Alive(g_rowText[r].get())) G.graphicSetColor[g_rowText[r].get()](TextCol(on));
    }
    ApplyLayout(shown);
    bool pages = PageCount() > 1;
    Show(g_prev, pages);
    Show(g_next, pages);
    Show(g_prevLbl, pages);
    Show(g_nextLbl, pages);
    SetColor(g_prev.get(), Tint(BtnCol(), 0.35f));
    SetColor(g_next.get(), Tint(BtnCol(), 0.35f));
    SetOutline(Tint(T().title, 0.4f));
    SetColor(g_titleBar.get(), Tint(BtnOnCol(), 0.45f));
    SetColor(g_pointer.get(), T().bgB);
    if (Alive(g_title.get())) G.graphicSetColor[g_title.get()](TextCol(true));
    g_nextTitle = 0;                     // refresh the title now
    g_dirty = false;
    static bool first = true;
    if (first) { first = false; ym::log("leos-menu: first page drawn"); }
}

static int Hex(float v) { return (int)(v < 0 ? 0 : v > 1 ? 255 : v * 255); }

// The title is "Leo's Menu" with a moving gradient, one color per letter (rich text),
// in the colors of the theme (all colors of the rainbow for Rainbow). Rebuilt ten times
// a second, not every frame.
static void UpdateTitle() {
    if (g_time < g_nextTitle) return;
    g_nextTitle = g_time + 0.1;
    char buf[512];
    int n = 0;
    if (g_cat == SEARCH || g_cat == OWNERPIN || g_cat == PRESETNAME) {
        std::string st = g_cat == SEARCH ? g_query : g_cat == PRESETNAME ? g_presetName : std::string(g_pin.size(), '*');
        snprintf(buf, sizeof buf, "<b>%s: %s_</b>", g_cat == SEARCH ? "Search" : g_cat == PRESETNAME ? "Name" : "Code", st.c_str());
        SetText(g_title, buf);
        return;
    }
    const char *word = "Leo's Menu";
    for (int i = 0; word[i]; i++) {
        float t = (float)g_time * 1.5f - i * 0.35f;
        Color c = g_theme == kRainbow ? Rainbow(t * 0.2f) : Lerp(T().bgB, T().title, 0.5f + 0.5f * sinf(t));
        n += snprintf(buf + n, sizeof buf - n, "<b><color=#%02X%02X%02X>%c</color></b>", Hex(c.r), Hex(c.g), Hex(c.b), word[i]);
    }
    n += snprintf(buf + n, sizeof buf - n, " <color=grey>[</color><color=white>%d</color><color=grey>]</color>", g_page + 1);
    if (f_fps) n += snprintf(buf + n, sizeof buf - n, "<size=16>  %d fps</size>", (int)(g_fps + 0.5f));
    if (f_clock) {
        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        n += snprintf(buf + n, sizeof buf - n, "<size=16>  %d:%02d</size>", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
    }
    SetText(g_title, buf);
}

static void ShowMenu(bool show) {
    if (show == g_menuShown) return;
    g_menuShown = show;
    if (show) { g_openTime = g_time; g_dirty = true; g_hover = -2; g_floatPlaced = false; g_kbMode = 0; if (g_cat == SEARCH || g_cat == OWNERPIN) g_cat = MAIN; if (g_cat == PRESETNAME) g_cat = PRESETS; SetText(g_tipText, ""); }
    Show(g_menu, show);
    Show(g_pointer, show && !f_pizza);      // the Pizza Menu has no dot on your hand
    g_touching = -2;
}

static float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Like ii's menu: the menu follows your hand and is tipped 45 degrees, so you read it
// by turning your palm toward your face. "Floating Menu" instead keeps the board above
// your hand, always facing you.
static void PlaceMenu() {
    Il2CppObject *menu = g_menu.get(), *board = g_board.get(), *head = g_head.get();
    Il2CppObject *hand = f_rightHand ? g_rightHand.get() : g_leftHand.get();
    if (!Alive(menu) || !Alive(board) || !hand || !head) return;
    Il2CppObject *t = Tr(menu), *bt = Tr(board);
    Vector3 handPos = G.trGetPosition[hand]();
    if (!G.trGetRotation.IsValid()) {                  // fallback: float above the hand, facing you
        Vector3 center = handPos + Vector3(0, 0.25f, 0), away = center - G.trGetPosition[head]();
        if (away.x * away.x + away.y * away.y + away.z * away.z < 1e-6f) return;
        G.trSetPosition[t](handPos);
        G.trSetPosition[bt](center);
        G.trSetRotation[bt](Quaternion::LookRotation(away));
        return;
    }
    const float s = 0.38268343f, c = 0.92387953f;                  // sin/cos of 22.5 degrees
    Quaternion rot = G.trGetRotation[hand]() * Quaternion(-s, 0, 0, c);   // tip -45 degrees on x
    if (f_rightHand) rot = rot * Quaternion(0, 0, 1, 0);           // mirrored: 180 degrees on z
    G.trSetPosition[t](handPos);
    G.trSetRotation[t](rot);
    G.trSetLocalPosition[bt](Vector3(0.1f, 0, 0));
    G.trSetLocalRotation[bt](Quaternion::LookRotation(Vector3(-1, 0, 0), Vector3(0, 0, 1)));
}

static void Press(MenuItem *it) {
    if (f_addFav && it->kind != OPEN && it->flag != &f_addFav) { FavToggle(it->label); g_dirty = true; return; }   // Add Favourite Mods
    switch (it->kind) {
    case OPEN:
        if (g_kbMode && it->target != SEARCH && it->target != OWNERPIN && it->target != PRESETNAME) CloseKeyboard();
        if (it->target == OWNER && !g_ownerOk) { OwnerOpen(); break; }
        g_cat = it->target;
        g_page = 0;
        break;
    case TOGGLE:
        *it->flag = !*it->flag;
        if (it->fn) it->fn();
        Notify("[%s] %s", *it->flag ? "ON" : "OFF", it->label);
        break;
    case ACTION:
        g_pressArg = it->arg;
        if (it->fn) it->fn();
        break;
    case SLIDER:                      // sliders are moved by touching them (see ApplyHit)
        break;
    }
    g_dirty = true;
    ym::log("leos-menu: %s", it->label);
}

// Which part of the menu a point on the board is over: a row, -1 = <, kNextHit = >, -2 = nothing.
// For a row it also remembers where along the row (g_hitX), which sliders use.
static float g_hitX = 0;
static int RowAt(Vector3 p) {
    if (g_kbMode) { int k = KeyAt(p); if (k >= 0) return kKeyHit + k; }
    if (fabsf(p.x - (-kBtnW / 2 + 0.019f)) < 0.019f && fabsf(p.y - kTitleY) < 0.022f) return kPinHit;
    if (g_cat != OWNERPIN && p.x > kBtnW / 2 - 0.04f && p.x < kBtnW / 2 && fabsf(p.y - kTitleY) < 0.022f) return kSearchHit;   // the magnifying glass
    PBox b = PageBox();
    if (PageCount() > 1 && fabsf(fabsf(p.x) - fabsf(b.x)) < b.w / 2 && fabsf(p.y - b.y) < b.h / 2)
        return p.x < 0 ? -1 : kNextHit;
    if (fabsf(p.x) >= kBtnW / 2) return -2;
    for (int r = 0; r < RPP(); r++)
        if (fabsf(p.y - RowY(r)) < kBtnH / 2) { g_hitX = p.x; return r; }
    return -2;
}
// Hand menu: the pointer ball on your finger touches the board.
static int Touching() {
    Il2CppObject *board = g_board.get(), *pointer = g_pointer.get();
    if (!Alive(board) || !Alive(pointer)) return -2;
    Vector3 p = G.trInverseTransformPoint[Tr(board)](G.trGetPosition[Tr(pointer)]());
    if (p.z < -0.035f || p.z > 0.015f) return -2;
    return RowAt(p);
}

static void Buzz() {
    if (!G.haptic.IsValid()) return;
    XRDevice d = G.deviceAtNode(f_rightHand ? 4 : 5);    // the hand that presses
    if (d.initialized) G.haptic(d.id, 0u, 0.35f, 0.04f);
}

// One place for both ways of using the menu. press = a new press this frame;
// engaged = the finger is on it / the trigger is held, so sliders follow it.
static void ApplyHit(int hit, bool press, bool engaged) {
    int i = g_page * RPP() + hit;
    if (hit >= kKeyHit || hit == kSearchHit) {           // a keyboard key, or the search button
        g_hover = hit;
        if (!press || g_time < g_nextPress) return;
        g_nextPress = g_time + 0.2;
        Buzz();
        if (hit == kSearchHit) OpenSearch();
        else if (hit == kPinHit) { f_floatMenu = !f_floatMenu; g_floatPlaced = false; Notify(f_floatMenu ? "Floating Menu on" : "Floating Menu off"); }
        else KeyPress(hit - kKeyHit);
        return;
    }
    if (hit != g_hover) {                                 // tooltip for the button under your finger
        g_hover = hit;
        if (hit >= 0 && hit < RPP() && i < (int)g_view.size()) SetText(g_tipText, TipFor(g_view[i]->label));
    }
    if (hit >= 0 && hit < RPP() && i < (int)g_view.size() && g_view[i]->kind == SLIDER) {
        MenuItem *it = g_view[i];                             // a slider: where you touch is the value
        if (!engaged || it->icount < 2) return;
        float u = (g_hitX + kBtnW / 2) / kBtnW;
        int v = (int)(u * it->icount);
        if (v < 0) v = 0;
        if (v >= it->icount) v = it->icount - 1;
        if (v != *it->ival) { *it->ival = v; g_dirty = true; Buzz(); }
        return;
    }
    if (!press || g_time < g_nextPress) return;           // one press per touch
    g_nextPress = g_time + 0.25;
    Buzz();
    if (hit >= 0 && hit < RPP()) { g_flashRow = hit; g_flashUntil = g_time + 0.12; }
    if (hit == -1 || hit == kNextHit) {
        int n = PageCount();
        g_page = (g_page + (hit == kNextHit ? 1 : n - 1)) % n;
        g_dirty = true;
    } else if (i < (int)g_view.size()) {
        Press(g_view[i]);
    }
}

static void CheckPress() {
    int hit = Touching();
    ApplyHit(hit, hit != -2 && hit != g_touching, hit != -2);
    g_touching = hit;
}

// ---- Pizza Menu --------------------------------------------------------------------
// The board sits in the top corner of your view, see-through, and is aimed at with the
// hand that would press (a small dot shows where you point, there is no dot on your hand).
static void PlacePizza() {
    Il2CppObject *menu = g_menu.get(), *board = g_board.get(), *head = g_head.get();
    if (!Alive(menu) || !Alive(board) || !head || !G.trGetRotation.IsValid()) return;
    Il2CppObject *t = Tr(menu), *bt = Tr(board);
    Vector3 at = G.trTransformPoint[head](Vector3(f_rightHand ? 0.23f : -0.23f, 0.15f, 0.6f));
    G.trSetPosition[t](at);
    G.trSetRotation[t](G.trGetRotation[head]());           // faces you, upright
    G.trSetLocalScale[t](Vector3(0.7f, 0.7f, 0.7f));
    G.trSetLocalPosition[bt](Vector3(0, 0, 0));
    G.trSetLocalRotation[bt](Quaternion(0, 0, 0, 1));
}

static void PizzaPress(const Buttons &in) {
    Il2CppObject *hand = f_rightHand ? g_leftHand.get() : g_rightHand.get(), *board = g_board.get(), *ptr = g_pointer.get();
    bool trig = f_rightHand ? in.ltrig : in.rtrig;
    int hit = -2;
    bool cursor = false;
    if (hand && Alive(board)) {
        Il2CppObject *bt = Tr(board);
        Vector3 o = G.trGetPosition[hand](), d = G.trGetForward[hand](), n = G.trGetForward[bt](), c = G.trGetPosition[bt]();
        float dn = Dot(d, n);
        if (fabsf(dn) > 0.05f) {                          // where your hand's line crosses the board
            float k = Dot(c - o, n) / dn;
            if (k > 0.05f && k < 4.0f) {
                Vector3 w = o + d * k;
                Vector3 p = G.trInverseTransformPoint[bt](w);
                if (fabsf(p.x) < 0.36f && fabsf(p.y) < 0.3f) {
                    cursor = true;
                    hit = RowAt(p);
                    if (Alive(ptr)) G.trSetPosition[Tr(ptr)](w - n * 0.004f);
                }
            }
        }
    }
    if (cursor != g_cursorOn) { g_cursorOn = cursor; Show(g_pointer, cursor); }
    g_pizzaHover = hit != -2;
    ApplyHit(hit, hit != -2 && trig && !g_pizzaTrig, hit != -2 && trig);
    g_pizzaTrig = trig;
}

static void MatTransparent(Il2CppObject *go, bool on) {
    if (!Alive(go) || !G.matSetInt.IsValid()) return;
    auto *r = G.goGetComponent[go](G.tRenderer);
    Il2CppObject *m = r ? G.rendererGetMaterial[r]() : nullptr;
    if (!m) return;
    G.matSetInt[m]((BNM::Structures::Mono::String *)CreateMonoString("_Surface"), on ? 1 : 0);
    G.matSetInt[m]((BNM::Structures::Mono::String *)CreateMonoString("_SrcBlend"), on ? 5 : 1);      // SrcAlpha / One
    G.matSetInt[m]((BNM::Structures::Mono::String *)CreateMonoString("_DstBlend"), on ? 10 : 0);     // OneMinusSrcAlpha / Zero
    G.matSetInt[m]((BNM::Structures::Mono::String *)CreateMonoString("_ZWrite"), on ? 0 : 1);
    if (G.matSetRenderQueue.IsValid()) G.matSetRenderQueue[m](on ? 3000 : 2000);
}
static void SetTransparent(bool on) {
    for (Ref *r : {&g_bg, &g_outline, &g_titleBar, &g_prev, &g_next, &g_notifyBar}) MatTransparent(r->get(), on);
    for (auto &r : g_bgX) MatTransparent(r.get(), on);
    for (auto &r : g_outX) MatTransparent(r.get(), on);
    for (auto &r : g_rowCube) MatTransparent(r.get(), on);
}
static void PizzaChanged() {
    SetTransparent(Trans());
    g_pizzaHover = g_pizzaTrig = g_cursorOn = false;
    if (f_pizza) {
        Show(g_pointer, false);                           // no dot on your hand
    } else {
        if (Alive(g_menu.get())) G.trSetLocalScale[Tr(g_menu.get())](Vector3(1, 1, 1));
        PointerToHand();
        g_openTime = g_time - 1;                          // no pop-open
        Show(g_pointer, g_menuShown);
    }
    g_dirty = true;
}

static void AnimateMenu() {
    float open = (float)(g_time - g_openTime) / 0.15f;     // pop open like ii's menu
    if (!f_pizza && open <= 1.05f && Alive(g_menu.get())) {
        float k = open >= 1 ? 1 : 1 - (1 - open) * (1 - open);
        G.trSetLocalScale[Tr(g_menu.get())](Vector3(k, k, k));
    }
    if (g_flashRow != -2 && g_time >= g_flashUntil) { g_flashRow = -2; g_dirty = true; }
    float t = 0.5f + 0.5f * sinf((float)g_time * 2.0f);
    bool rb = g_theme == kRainbow;
    float hue = (float)g_time * 0.15f;
    SetBG(Tint(rb ? Rainbow(hue) : Lerp(T().bgA, T().bgB, t), 0.18f));
    if (rb) {                                     // Rainbow: the frame and pointer cycle too
        SetOutline(Tint(Rainbow(hue + 0.5f), 0.4f));
        SetColor(g_titleBar.get(), Tint(Lerp(Rainbow(hue + 0.3f), Color(0, 0, 0, 1), 0.6f), 0.45f));
        SetColor(g_pointer.get(), Rainbow(hue + 0.5f));
    }
    if (g_drawColor == 5)                         // the Draw Color knob shows the rainbow moving
        for (int r = 0; r < RPP(); r++) {
            int i = g_page * RPP() + r;
            if (i < (int)g_view.size() && g_view[i]->kind == SLIDER && g_view[i]->ival == &g_drawColor)
                SetColor(g_knob[r].get(), Rainbow((float)g_time * 0.4f));
        }
}


// ---- features ---------------------------------------------------------------

static Il2CppObject *PlayerTransform() {
    Il2CppObject *player = g_player.get();
    return player ? G.compTransform[player]() : nullptr;
}

// Your body's velocity, through the game's own player functions (Rigidbody as backup).
static bool GetVel(Vector3 &v) {
    Il2CppObject *player = g_player.get(), *rb = g_rigidbody.get();
    if (player && G.plGetVelocity.IsValid()) { v = G.plGetVelocity[player](); return true; }
    if (rb && G.rbGetVelocity.IsValid()) { v = G.rbGetVelocity[rb](); return true; }
    return false;
}
static void SetVel(Vector3 v) {
    Il2CppObject *player = g_player.get(), *rb = g_rigidbody.get();
    if (player && G.plSetVelocity.IsValid()) G.plSetVelocity[player](v);
    else if (rb && G.rbSetVelocity.IsValid()) G.rbSetVelocity[rb](v);
}

// Where your body is (the body collider), which is what teleports aim for.
static bool BodyPos(Vector3 &out) {
    Il2CppObject *player = g_player.get(), *t = PlayerTransform();
    if (!player || !t) return false;
    Il2CppObject *body = G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr;
    out = body ? G.trGetPosition[G.compTransform[body]()]() : G.trGetPosition[t]();
    return true;
}
static void StopMoving() { SetVel(Vector3(0, 0, 0)); }
static bool g_selfMove = false;               // set when this menu moves you (Teleport Gun)
// Moves your body to pos, using the game's own TeleportTo when it has one.
static void TeleportPlayer(Vector3 pos) {
    Il2CppObject *player = g_player.get(), *t = PlayerTransform();
    Vector3 body;
    if (!player || !t || !BodyPos(body)) return;
    g_selfMove = true;
    Vector3 target = pos - body + G.trGetPosition[t]();
    if (G.teleportTo.IsValid()) G.teleportTo[player](target);
    else G.trSetPosition[t](target);
    StopMoving();
}


// Platforms (and Solid Players) go on a layer of their own that nobody else uses, which is added to your hands'
// and body layer lists while a platform mod is on. That lets Noclip leave exactly that layer solid.
static int PlatformLayerIdx() {
    static int layer = -2;
    if (layer != -2) return layer;
    layer = -1;
    Class lm("UnityEngine", "LayerMask");
    Method<BNM::Structures::Mono::String *> toName;
    toName = lm.GetMethod("LayerToName", 1);
    if (toName.IsValid())
        for (int i = 30; i >= 8 && layer < 0; i--) {
            auto *nm = toName(i);
            if (!nm || nm->str().empty()) layer = i;
        }
    ym::log("leos-menu: platform layer %d", layer);
    return layer;
}
static bool PlatformsOn() { return f_platforms || f_solidPlayers; }
static int PlatLayer() {
    int layer = PlatformLayerIdx();
    if (layer < 0) {                                        // no free layer: the first of your hand layers, like before
        static int fb = -2;
        if (fb == -2) {
            fb = -1;
            int mask = G.locomotionLayers.IsValid() && g_player.get() ? G.locomotionLayers[g_player.get()]() : 0;
            for (int i = 0; i < 32 && fb < 0; i++) if (mask & (1 << i)) fb = i;
            ym::log("leos-menu: hand layers 0x%x, platforms on layer %d", mask, fb);
        }
        layer = fb;
    }
    return layer;
}
static Il2CppObject *NewPlatform(Vector3 pos, Vector3 scale, Color c, bool ball = false) {
    Il2CppObject *p = ball ? Prim(0, c, true) : Cube(c, true);   // keeps its collider: you stand on it
    if (!p) return nullptr;
    int layer = PlatLayer();
    if (layer >= 0 && G.goSetLayer.IsValid()) G.goSetLayer[p](layer);
    Il2CppObject *t = Tr(p);
    G.trSetPosition[t](pos);
    G.trSetLocalScale[t](scale);
    return p;
}
// Sticky platform: a hollow ball around the hand. The hole is a 13 cm cube that barely fits a hand, the walls are 5 cm thick.
static Il2CppObject *NewHollowBall(Vector3 pos) {
    Il2CppObject *root = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("LeoBall"));
    if (!root) return nullptr;
    Il2CppObject *rt = Tr(root);
    G.trSetPosition[rt](pos);
    int layer = PlatLayer();
    struct Wall { Vector3 p, sc; };
    const Wall walls[6] = {{Vector3(0.09f, 0, 0), Vector3(0.05f, 0.23f, 0.23f)}, {Vector3(-0.09f, 0, 0), Vector3(0.05f, 0.23f, 0.23f)},
                           {Vector3(0, 0.09f, 0), Vector3(0.13f, 0.05f, 0.23f)}, {Vector3(0, -0.09f, 0), Vector3(0.13f, 0.05f, 0.23f)},
                           {Vector3(0, 0, 0.09f), Vector3(0.13f, 0.13f, 0.05f)}, {Vector3(0, 0, -0.09f), Vector3(0.13f, 0.13f, 0.05f)}};
    for (auto &w : walls) {
        Il2CppObject *c = Cube(BtnCol(), true);
        if (!c) continue;
        if (layer >= 0 && G.goSetLayer.IsValid()) G.goSetLayer[c](layer);
        Il2CppObject *t = Tr(c);
        G.trSetParent[t](rt, false);
        G.trSetLocalPosition[t](w.p);
        G.trSetLocalScale[t](w.sc);
    }
    if (Il2CppObject *sk = Prim(0, Color(0.5f, 0.7f, 1.0f, 0.25f), false)) {       // a see-through skin so it looks like a ball
        MatTransparent(sk, true);
        Il2CppObject *t = Tr(sk);
        G.trSetParent[t](rt, false);
        G.trSetLocalPosition[t](Vector3(0, 0, 0));
        G.trSetLocalScale[t](Vector3(0.27f, 0.27f, 0.27f));
    }
    return root;
}
static Ref g_ballL, g_ballR;                                        // Sticky platform: the ball around the hand
static void PlatformFor(bool held, Ref &platform, Ref &hand) {
    Ref &ball = (&platform == &g_leftPlatform) ? g_ballL : g_ballR;
    bool want = held && f_platforms;
    Il2CppObject *h = hand.get();
    if (!want || !h) { Kill(platform); Kill(ball); return; }
    if (Alive(platform.get())) return;                                // it stays where it was put until you let go
    Vector3 hp = G.trGetPosition[h]();
    if (Il2CppObject *p = NewPlatform(hp - Vector3(0, 0.08f, 0), Vector3(0.28f, 0.02f, 0.28f), BtnCol())) {
        if (g_platType == 2 && G.trGetRotation.IsValid()) G.trSetRotation[Tr(p)](G.trGetRotation[h]());    // Follow Hand: starts turned like the hand, then stays
        platform.set(p);
    }
    if (g_platType == 1)                                              // Sticky: a ball around the hand, so the hand is in it
        if (Il2CppObject *b = NewHollowBall(hp)) ball.set(b);
}
// Sticky platforms (Platform Type: Sticky): each grip press leaves a platform where your hand is. It stays; the oldest of ten goes.
static std::vector<Ref> g_sticky;
static bool g_stickyPrev[2];
static void StickyTick(const Buttons &in) {
    if (!(f_platforms && g_platType == 1)) {
        for (auto &r : g_sticky) Kill(r);
        g_sticky.clear();
        g_stickyPrev[0] = g_stickyPrev[1] = false;
        return;
    }
    bool held[2] = {in.lgrip, in.rgrip};
    Il2CppObject *hands[2] = {g_leftHand.get(), g_rightHand.get()};
    for (int i = 0; i < 2; i++) {
        if (held[i] && !g_stickyPrev[i] && hands[i] && !MenuBusy()) {
            if (g_sticky.size() >= 10) { Kill(g_sticky.front()); g_sticky.erase(g_sticky.begin()); }
            Ref r;
            if (Il2CppObject *p = NewPlatform(G.trGetPosition[hands[i]](), Vector3(0.42f, 0.42f, 0.42f), BtnCol(), true)) { r.set(p); g_sticky.push_back(r); }
        }
        g_stickyPrev[i] = held[i];
    }
}


// Speed Boost and No Gravity change your own player; turning them off puts the old
// values back. Checked every frame so they also come back after a room change.
static float g_oldJumpSpeed = 0, g_oldJumpMultiplier = 0;
static Il2CppObject *g_boosted = nullptr;      // which player has the boost (compared only)
static bool g_gravityOff = false;

static void ApplySpeedBoost() {
    Il2CppObject *player = g_player.get();
    bool ok = G.maxJumpSpeed.IsValid() && G.jumpMultiplier.IsValid();
    if (f_speedBoost && ok && player && g_boosted != player) {
        g_oldJumpSpeed = G.maxJumpSpeed[player]();
        g_oldJumpMultiplier = G.jumpMultiplier[player]();
        G.maxJumpSpeed[player] = g_oldJumpSpeed * 2.0f;          // relative: the old fixed numbers could be lower than the game's
        G.jumpMultiplier[player] = g_oldJumpMultiplier * 1.5f;
        g_boosted = player;
    } else if (!f_speedBoost && g_boosted) {
        if (ok && player == g_boosted) {
            G.maxJumpSpeed[player] = g_oldJumpSpeed;
            G.jumpMultiplier[player] = g_oldJumpMultiplier;
        }
        g_boosted = nullptr;
    }
}

static void ApplyGravity() {
    // The game's own "no gravity for a while" (what its levitation items use), renewed
    // every frame while the mod is on; it wears off by itself once you turn it off.
    Il2CppObject *player = g_player.get();
    if ((f_noGravity || f_flipGravity) && player && G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.25f);
    g_gravityOff = f_noGravity || f_flipGravity;
}


// Noclip Fly: while you hold A you pass through everything. Yeeps moves
// your body with its own layer lists (locomotionEnabledLayers for your hands,
// collisionEnabledLayers for your body), so those are emptied, physics ignores your
// body against those layers, and your own colliders are off. Gravity stays normal, like
// Gorilla Tag noclip. All of it goes back when you let go. Only your own player changes; nothing is sent.
static bool g_noclipOn = false;
static Ref g_noclipOff[24];                    // your own colliders switched off during noclip
static int g_noclipOffN = 0;
// Every solid collider on your own player (body, head, hands...), switched off while
// noclip is held so nothing of you hits the level.
static void PlayerColliders(bool off) {
    if (!off) {
        for (int i = 0; i < g_noclipOffN; i++) {
            if (Alive(g_noclipOff[i].get())) G.colliderSetEnabled[g_noclipOff[i].get()](true);
            g_noclipOff[i].clear();
        }
        g_noclipOffN = 0;
        return;
    }
    Il2CppObject *player = g_player.get();
    if (!player || !G.colliderSetEnabled.IsValid()) return;
    auto offOne = [](Il2CppObject *c) {
        if (!Alive(c) || g_noclipOffN >= 24 || !G.behaviourGetEnabledCollider[c]()) return;
        if (G.colliderIsTrigger.IsValid() && G.colliderIsTrigger[c]()) return;
        for (int i = 0; i < g_noclipOffN; i++) if (g_noclipOff[i].get() == c) return;
        G.colliderSetEnabled[c](false);
        g_noclipOff[g_noclipOffN++].set(c);
    };
    // Your body and head colliders (fields on the player), then every solid collider in
    // the rig they belong to.
    Il2CppObject *own[2] = {G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr,
                            G.headCollider.IsValid() ? G.headCollider[player]() : nullptr};
    for (Il2CppObject *c : own) {
        if (!c) continue;
        offOne(c);
        Il2CppObject *rig = G.compGameObject[G.trGetRoot[G.compTransform[c]()]()]();
        auto *arr = rig && G.getComponentsInChildren.IsValid()
                        ? (uint8_t *)G.getComponentsInChildren[rig]((Il2CppObject *)G.tCollider, false) : nullptr;
        size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
        for (size_t i = 0; i < n; i++) offOne(((Il2CppObject **)(arr + 0x20))[i]);
    }
}
static int g_savedLoco = 0, g_savedColl = 0, g_bodyLayer = -1;
static bool g_masksSaved = false, g_collOff = false;
static int g_appliedLoco = 0, g_appliedColl = 0;
static void SetCollOff(bool want) { if (want != g_collOff) { PlayerColliders(want); g_collOff = want; } }
// Owns your hand / body layer lists. Platform mods add the platform layer to them; noclip empties them except
// for that layer (so platforms stay solid) and makes physics ignore your body against the rest.
static void NoclipTick(const Buttons &in) {
    Il2CppObject *player = g_player.get();
    if (!player || !G.locomotionLayers.IsValid()) return;
    bool wantNoclip = (f_noclip || f_smoothFly) && in.a;        // only while the button (A) is held
    int pl = PlatformsOn() ? PlatformLayerIdx() : -1;
    int bit = pl >= 0 ? (1 << pl) : 0;
    if (!wantNoclip && !bit) {
        if (g_masksSaved) {
            G.locomotionLayers[player] = g_savedLoco;
            if (G.collisionLayers.IsValid()) G.collisionLayers[player] = g_savedColl;
            if (g_noclipOn && g_bodyLayer >= 0 && G.ignoreLayers.IsValid())
                for (int l = 0; l < 32; l++) if ((g_savedLoco | g_savedColl) & (1 << l)) G.ignoreLayers(g_bodyLayer, l, false);
            g_masksSaved = false; g_noclipOn = false;
        }
        SetCollOff(f_noBody);                               // Disable Body Collision (Fun) keeps them off
        return;
    }
    if (!g_masksSaved) {                                    // what the game had before we touched anything
        g_savedLoco = G.locomotionLayers[player]();
        g_savedColl = G.collisionLayers.IsValid() ? G.collisionLayers[player]() : 0;
        Il2CppObject *body = G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr;
        Il2CppObject *bodyGo = body ? G.compGameObject[body]() : nullptr;
        g_bodyLayer = bodyGo && G.goGetLayer.IsValid() ? G.goGetLayer[bodyGo]() : -1;
        g_masksSaved = true; g_noclipOn = false; g_collOff = false; g_appliedLoco = g_appliedColl = -12345;
    }
    int loco = wantNoclip ? bit : (g_savedLoco | bit), coll = wantNoclip ? bit : (g_savedColl | bit);
    if (loco != g_appliedLoco) { G.locomotionLayers[player] = loco; g_appliedLoco = loco; }
    if (G.collisionLayers.IsValid() && coll != g_appliedColl) { G.collisionLayers[player] = coll; g_appliedColl = coll; }
    if (wantNoclip != g_noclipOn) {
        g_noclipOn = wantNoclip;
        if (g_bodyLayer >= 0 && G.ignoreLayers.IsValid())
            for (int l = 0; l < 32; l++)
                if ((g_savedLoco | g_savedColl) & (1 << l)) G.ignoreLayers(g_bodyLayer, l, wantNoclip);
        if (wantNoclip) ym::log("leos-menu: noclip on (layers 0x%x, body layer %d, platform layer %d)", g_savedLoco | g_savedColl, g_bodyLayer, pl);
    }
    SetCollOff((wantNoclip && !bit) || f_noBody);           // with platforms on your colliders stay on so they can stand on them
}


static Color GunColor() {                       // Settings > Gun Pointer Color
    if (g_gunColor == 1) return Color(1.0f, 0.5f, 0.0f, 1);
    if (g_gunColor == 2) return Rainbow((float)g_time * 0.3f);
    return Color(0.2f, 0.5f, 1.0f, 1);
}
// Teleport Gun: hold right grip to aim (a line and a ball show where you land), press
// the right trigger to go there.
static Ref g_gunLine, g_gunLineGo, g_gunBall;
static bool g_gunTrigger = false;
// Settings > Gun Effect: the line the guns draw is straight, or winds around the aim (spiral), or swings side to side (wave).
static void GunLineDraw(Il2CppObject *line, Vector3 start, Vector3 end, Vector3 dir) {
    if (g_gunFx == 0) { G.lrSetPositionCount[line](2); G.lrSetPosition[line](0, start); G.lrSetPosition[line](1, end); return; }
    Vector3 u = Cross(dir, Vector3(0, 1, 0));
    float ul = sqrtf(Len2(u));
    u = ul < 0.01f ? Vector3(1, 0, 0) : u * (1.0f / ul);
    Vector3 w = Cross(dir, u);
    float len = sqrtf(Len2(end - start)), span = len < 25.0f ? len : 25.0f;
    const int N = 48;
    G.lrSetPositionCount[line](N + 1);
    for (int k = 0; k < N; k++) {
        float d = span * (float)k / (float)(N - 1), a = d * 3.0f - (float)g_time * 9.0f, fade = d * 3.0f > 1.0f ? 1.0f : d * 3.0f;
        Vector3 off = g_gunFx == 1 ? (u * cosf(a) + w * sinf(a)) * 0.07f : u * (sinf(a) * 0.1f);
        G.lrSetPosition[line](k, start + dir * d + off * fade);
    }
    G.lrSetPosition[line](N, end);
}
static void TeleportGunTick(const Buttons &in) {
    bool aiming = (f_tpGun || f_itemGun || f_delGun || f_grid9Gun || f_cubeGun || f_potionGun || f_flingGun) && in.rgrip;
    if (!aiming) {
        Show(g_gunLineGo, false);
        Show(g_gunBall, false);
        g_gunTrigger = in.rtrig;
        return;
    }
    Il2CppObject *hand = g_rightHand.get();
    if (!hand || !G.raycast.IsValid() || !G.lrSetPosition.IsValid()) return;
    if (!g_gunBall.get()) {                              // made once per room
        Il2CppObject *ball = Prim(0, T().bgB, false);
        Il2CppObject *lineGo = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("LeoGunLine"));
        Il2CppObject *line = lineGo && G.tLine ? G.goAddComponent[lineGo](G.tLine) : nullptr;
        if (!ball || !line) return;
        G.trSetLocalScale[Tr(ball)](Vector3(0.1f, 0.1f, 0.1f));
        if (Il2CppObject *m = G.rendererGetMaterial[line]()) {
            if (G.shader.get()) G.matSetShader[m](G.shader.get());
            G.matSetColor[m](T().bgB);
        }
        G.lrSetStartWidth[line](0.025f);
        G.lrSetEndWidth[line](0.025f);
        G.lrSetPositionCount[line](2);
        G.lrSetUseWorldSpace[line](true);
        g_gunBall.set(ball);
        g_gunLine.set(line);
        g_gunLineGo.set(lineGo);
    }
    Vector3 start = G.trGetPosition[hand](), dir = G.trGetForward[hand]();
    BNM::Structures::Unity::RaycastHit hit{};
    bool found = G.raycast(start + dir * 0.25f, dir, &hit, 512.0f, -5);   // Physics.DefaultRaycastLayers
    Vector3 end = found ? hit.point : start + dir * 512.0f;
    Show(g_gunLineGo, true);
    Show(g_gunBall, true);
    G.trSetPosition[Tr(g_gunBall.get())](end);
    GunLineDraw(g_gunLine.get(), start, end, dir);
    {
        Color gc = GunColor();                            // the ball and the line use the chosen color
        SetColor(g_gunBall.get(), in.rtrig ? Color(1, 1, 1, 1) : gc);
        if (Il2CppObject *m = G.rendererGetMaterial[g_gunLine.get()]()) G.matSetColor[m](gc);
    }
    if (in.rtrig && !g_gunTrigger && f_delGun) {
        DeleteAimedItem(start, dir);                      // Delete MenuItem Gun: needs no hit point
    } else if (in.rtrig && !g_gunTrigger && f_flingGun) {
        FlingGunAim(start, dir);                          // Fling Gun: needs no hit point
    } else if (in.rtrig && !g_gunTrigger && f_potionGun) {
        PotionGunAim(start, dir);                         // Potion Effect Gun: needs no hit point
    } else if (in.rtrig && !g_gunTrigger && found) {
        if (f_itemGun) {                                  // Spawn MenuItem Gun: an item at that spot
            if (g_time >= g_nextItem && LeftHandItem()) {
                g_nextItem = g_time + 0.5;
                g_itemQueue.push_back({end + Vector3(0, 0.3f, 0), Vector3(0, 0, 0)});
            }
        } else if (f_grid9Gun) {                          // Spawn 9x9 Held MenuItem Gun
            Grid9Gun(end);
        } else if (f_cubeGun) {                           // Spawn Placeholder Cube Gun: no held item needed
            if (g_time >= g_nextItem) {
                g_nextItem = g_time + 0.4;
                PendingItem pi{end + Vector3(0, 0.3f, 0), Vector3(0, 0, 0)};
                snprintf(pi.key, sizeof pi.key, "%s", PlaceholderKey());
                g_itemQueue.push_back(pi);
            }
        } else TeleportPlayer(end + Vector3(0, 1.0f, 0));
    }
    g_gunTrigger = in.rtrig;
}


// ---- vector helpers ----------------------------------------------------------------

static Vector3 Cross(Vector3 a, Vector3 b) { return Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static Vector3 Flat(Vector3 v) {                      // v without its up/down part, length 1
    v.y = 0;
    float l = sqrtf(v.x * v.x + v.z * v.z);
    return l > 1e-4f ? v * (1.0f / l) : Vector3(0, 0, 1);
}

static Quaternion QInv(Quaternion q) { return Quaternion(-q.x, -q.y, -q.z, q.w); }       // for unit quaternions
static Vector3 QRot(Quaternion q, Vector3 v) {          // v turned by q
    Vector3 u(q.x, q.y, q.z);
    Vector3 t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}

// ---- rig mods: dances -----------------------------------------------------------
// While you hold the left trigger, your hands are moved through a dance instead of
// following your controllers. The game sends your hand positions to the room as
// usual, so other players see you dance. Nothing extra is sent.

// The dance buttons work like radio buttons: picking one turns the others off,
// pressing the picked one again turns dancing off.
static void DancePicked() {
    int picked = -1;
    for (int i = 0; i < kDances; i++) if (g_danceSel[i] && i != g_dance) picked = i;
    for (int i = 0; i < kDances; i++) g_danceSel[i] = (i == picked);
    g_dance = picked;
    if (picked >= 0) Notify("Hold left trigger to dance");
}
static float Rand01() { static uint32_t x = 2463534242u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (x & 0xffff) / 65535.0f; }

// The controller trackers would put your hands back every frame, so they are paused
// while you dance (on the hand and the two objects above it) and turned back on after.
static Ref g_paused[12];
static int g_pausedN = 0;
static bool g_dancing = false;
static void PauseTrackers(bool pause) {
    if (!pause) {
        for (int i = 0; i < g_pausedN; i++) {
            if (Alive(g_paused[i].get())) G.behaviourSetEnabled[g_paused[i].get()](true);
            g_paused[i].clear();
        }
        g_pausedN = 0;
        return;
    }
    if (!G.behaviourSetEnabled.IsValid() || !G.trGetParent.IsValid()) return;
    for (Il2CppObject *t : {g_leftHand.get(), g_rightHand.get()})
        for (int up = 0; up < 3 && t; up++, t = G.trGetParent[t]())
            for (BNM::MonoType *type : G.trackerTypes) {
                Il2CppObject *go = G.compGameObject[t]();
                Il2CppObject *c = go ? G.goGetComponent[go]((Il2CppObject *)type) : nullptr;
                if (!c || !G.behaviourGetEnabled[c]() || g_pausedN >= 12) continue;
                G.behaviourSetEnabled[c](false);
                g_paused[g_pausedN++].set(c);
            }
}

static void DanceTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *lh = g_leftHand.get(), *rh = g_rightHand.get();
    bool want = g_dance >= 0 && in.ltrig && head && lh && rh;
    if (want != g_dancing) {
        PauseTrackers(want);
        g_dancing = want;
        if (want) ym::log("leos-menu: dancing, paused %d trackers", g_pausedN);
    }
    if (!want) return;
    Vector3 hp = G.trGetPosition[head](), f = Flat(G.trGetForward[head]()), up(0, 1, 0);
    Vector3 r = Cross(up, f);                            // your right
    Vector3 shoulder = hp + up * -0.25f;
    float t = (float)g_time;
    Vector3 L, R;                                        // hand positions
    switch (g_dance) {
    case 0: { // Wave: right hand up high and waving side to side, left hand down at your side
        float ws = sinf(t * 11.0f);
        R = shoulder + r * (0.3f + 0.17f * ws) + up * 0.5f + f * 0.12f;
        L = shoulder + r * -0.25f + up * -0.25f + f * 0.05f;
        break;
    }
    case 1:   // Helicopter: both hands spin around you
        R = shoulder + r * (0.6f * cosf(t * 8)) + f * (0.6f * sinf(t * 8)) + up * 0.15f;
        L = shoulder - r * (0.6f * cosf(t * 8)) - f * (0.6f * sinf(t * 8)) + up * 0.15f;
        break;
    case 2:   // T-Pose
        R = shoulder + r * 0.7f + up * 0.1f;
        L = shoulder - r * 0.7f + up * 0.1f;
        break;
    case 3:   // Windmill: big circles at your sides
        R = shoulder + r * 0.4f + up * (0.5f * sinf(-t * 5)) + f * (0.5f * cosf(-t * 5));
        L = shoulder - r * 0.4f - up * (0.5f * sinf(-t * 5)) - f * (0.5f * cosf(-t * 5));
        break;
    case 4: {  // Swim: breaststroke
        float p = t * 3;
        R = shoulder + f * (0.3f + 0.25f * cosf(p)) + r * (0.08f + 0.35f * fabsf(sinf(p)));
        L = shoulder + f * (0.3f + 0.25f * cosf(p)) - r * (0.08f + 0.35f * fabsf(sinf(p)));
        break;
    }
    case 5: {  // Jumping jacks: arms from down by your sides to up over your head
        float a = (sinf(t * 6) * 0.5f + 0.5f) * 3.1f - 1.55f;
        R = shoulder + r * (0.6f * cosf(a)) + up * (0.6f * sinf(a));
        L = shoulder - r * (0.6f * cosf(a)) + up * (0.6f * sinf(a));
        break;
    }
    case 6:   // Spaz: hands in front, shaking everywhere
        R = shoulder + f * 0.35f + r * 0.2f + Vector3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f) * 0.3f;
        L = shoulder + f * 0.35f - r * 0.2f + Vector3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f) * 0.3f;
        break;
    default:  // Beyblade: hands spin fast around your waist
        R = shoulder - up * 0.3f + r * (0.5f * cosf(t * 20)) + f * (0.5f * sinf(t * 20));
        L = shoulder - up * 0.3f - r * (0.5f * cosf(t * 20)) - f * (0.5f * sinf(t * 20));
        break;
    }
    // Keep each hand within arm's reach of its shoulder, then glide there instead of
    // jumping, so the arms move smoothly for you and for everyone watching.
    auto reach = [&](Vector3 hand, Vector3 sh) {
        Vector3 d = hand - sh;
        float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        return len > 0.7f ? sh + d * (0.7f / len) : hand;
    };
    R = reach(R, shoulder + r * 0.18f);
    L = reach(L, shoulder - r * 0.18f);
    float k = g_dance == 6 ? 1.0f : fminf(1.0f, dt * 14.0f);     // Spaz stays twitchy
    G.trSetPosition[lh](G.trGetPosition[lh]() + (L - G.trGetPosition[lh]()) * k);
    G.trSetPosition[rh](G.trGetPosition[rh]() + (R - G.trGetPosition[rh]()) * k);
    Quaternion look = Quaternion::LookRotation(f, up);
    G.trSetRotation[lh](look);
    G.trSetRotation[rh](look);
}

// ---- items (everyone sees these) ------------------------------------------------
// Items are made with the game's own item message (RpcCreateItem on the room's
// NetworkMessenger), the same one the game sends when an item appears. Owner-approved
// 2026-10-31. Every way of spawning has a cooldown, and big batches are sent a few per
// frame, so the room can't be flooded.


static void Put(Il2CppObject *arr, size_t i, void *v) { ((void **)((uint8_t *)arr + 0x20))[i] = v; }

// Builds RpcCreateItem's arguments from its own parameter list, in the order the game
// uses: item id, item key, spawn kind 1, no owner, position, rotation, velocity, spin,
// properties, 1, 1.
static bool SendItem(const char *key, Vector3 pos, Quaternion rot, Vector3 vel, char *idOut = nullptr) {
    auto *mi = G.rpcCreateItem;
    Il2CppObject *nm = G.netLocal.IsValid() ? G.netLocal() : nullptr;
    if (!mi || !nm || !G.pvRpc.IsValid() || !G.arrayNew || !G.valueBox || !G.tPhotonView) return false;
    Il2CppObject *go = G.compGameObject[nm]();
    Il2CppObject *pv = go ? G.goGetComponent[go]((Il2CppObject *)G.tPhotonView) : nullptr;
    if (!pv) return false;
    // The last parameter (PhotonMessageInfo) is filled in by Photon on the receiving
    // side; it is never sent.
    int count = mi->parameters_count;
    if (count > 0 && TypeName(mi->parameters[count - 1]) == "Photon.Pun.PhotonMessageInfo") count--;
    auto *args = (Il2CppObject *)G.arrayNew(G.objectClass, count);
    if (!args) return false;
    char id[8];
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 4; i++) id[i] = kChars[(int)(Rand01() * 35.99f)];
    id[4] = 0;
    if (idOut) memcpy(idOut, id, 5);
    int strings = 0, vectors = 0, others = 0;
    for (int i = 0; i < count; i++) {
        std::string tn = TypeName(mi->parameters[i]);
        Class pc(mi->parameters[i]);
        auto *k = pc.IsValid() ? pc.GetClass() : nullptr;
        void *v = nullptr;
        if (tn == "System.String") {
            if (strings == 0) v = CreateMonoString(id);
            else if (strings == 1) v = CreateMonoString(key);
            strings++;                                   // later strings stay null
        } else if (tn == "UnityEngine.Vector3") {
            Vector3 vec = vectors == 0 ? pos : vectors == 1 ? vel : Vector3(0, 0, 0);
            vectors++;
            v = G.valueBox(k, &vec);
        } else if (tn == "UnityEngine.Quaternion") {
            v = G.valueBox(k, &rot);
        } else if (tn == "System.String[]") {
            v = G.arrayNew(G.stringClass, 0);
        } else if (k && k->byval_arg.valuetype) {
            uint8_t raw[64] = {};
            raw[0] = 1;                                 // spawn kind 1 / the two bytes = 1
            // Photon can't send the game's enum types, only plain numbers (like the kit's
            // Int32 1), so an enum goes as its underlying number type.
            auto *boxAs = k->enumtype && k->element_class ? k->element_class : k;
            v = G.valueBox(boxAs, raw);
            others++;
        }
        Put(args, i, v);
    }
    static bool loggedTypes = false;
    if (!loggedTypes) {                                   // once: what the game expects, for fixing
        loggedTypes = true;
        std::string sig;
        for (int i = 0; i < mi->parameters_count; i++) {
            Class pc(mi->parameters[i]);
            auto *k = pc.IsValid() ? pc.GetClass() : nullptr;
            std::string tn = TypeName(mi->parameters[i]);
            if (tn.size() > 30) tn = k && k->byval_arg.valuetype ? (k->enumtype ? "enum" : "struct" + std::to_string(k->instance_size - 16)) : "class";
            sig += (i ? "," : "") + tn;
        }
        ym::log("leos-menu: item message takes %s", sig.c_str());
    }
    try {
        G.pvRpc[pv](CreateMonoString("RpcCreateItem"), 0, args);      // 0 = everyone in the room
    } catch (BNM::IL2CPP::Il2CppExceptionWrapper &w) {   // the game said no: log why, don't crash the menu
        auto *ex = w.ex;
        auto *msg = ex ? (BNM::Structures::Mono::String *)ex->message : nullptr;
        auto *cls = ex ? (BNM::Structures::Mono::String *)ex->className : nullptr;
        ym::log("leos-menu: item message error %s: %s", cls ? cls->str().c_str() : "?", msg ? msg->str().c_str() : "?");
        return false;
    }
    return true;
}

// Big batches (a 9x9 is 81 items) are sent two per frame and at most one batch every 3 seconds.
static bool QueueItems(const std::vector<PendingItem> &list) {
    if (!g_itemKey[0]) return false;
    if (g_time < g_nextBatch) { Notify("Wait a moment"); return false; }
    g_nextBatch = g_time + 3.0;
    for (auto &p : list) if (g_itemQueue.size() < 96) g_itemQueue.push_back(p);
    return true;
}

static void ItemQueueTick() {
    for (int sent = 0; sent < 2 && !g_itemQueue.empty(); sent++) {   // 2 a frame at most
        PendingItem p = g_itemQueue.front();
        g_itemQueue.erase(g_itemQueue.begin());
        const char *useKey = p.key[0] ? p.key : g_itemKey;
        char idc[8] = {0};
        bool ok = SendItem(useKey, p.pos, Quaternion(0, 0, 0, 1), p.vel, idc);
        if (ok && p.orbit && g_orbitIds.size() < 8) g_orbitIds.push_back(idc);
        if (ok && (p.track == 1 || p.track == 2) && g_fbPend.size() < 24) { FbTrack ft{idc, p.vel, g_time}; ft.kind = p.track; g_fbPend.push_back(ft); }
        static int logged = 0;
        if (logged++ < 3) ym::log("leos-menu: spawn %s ok=%d", useKey, ok ? 1 : 0);
        if (!ok) { g_itemQueue.clear(); Notify("MenuItem spawning isn't ready in this room"); }
    }
}

// The grid in front of you: 3 across, 3 deep, and 3 high for 3x3x3.
static void SpawnGrid(int count) {
    Il2CppObject *head = g_head.get();
    if (!head) return;
    Vector3 f = Flat(G.trGetForward[head]()), r = Cross(Vector3(0, 1, 0), f);
    Vector3 base = G.trGetPosition[head]() + f * 1.2f + Vector3(0, -0.4f, 0);
    std::vector<PendingItem> list;
    for (int y = 0; y < (count > 9 ? 3 : 1); y++)
        for (int z = 0; z < 3; z++)
            for (int x = 0; x < 3; x++)
                list.push_back({base + r * ((x - 1) * 0.45f) + f * (z * 0.45f) + Vector3(0, y * 0.45f, 0), Vector3(0, 0, 0)});
    QueueItems(list);
}
// The item you're holding in your LEFT hand right now (owner 2026-10-31: items spawn as
// whatever is in your left hand when you press). One scene search per press.
static bool LeftHandItem() {
    Il2CppObject *hand = g_leftHand.get();
    if (!hand || !G.tItem || !G.itemKey.IsValid() || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return false;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem)
                                                  : G.findSceneByType((Il2CppObject *)G.tItem, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    Vector3 hp = G.trGetPosition[hand]();
    Il2CppObject *best = nullptr;
    float bestD = 0.4f * 0.4f;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
        if (!Alive(it) || (G.itemHeld.IsValid() && !G.itemHeld[it]())) continue;
        Vector3 d = G.trGetPosition[G.compTransform[it]()]() - hp;
        float dd = d.x * d.x + d.y * d.y + d.z * d.z;
        if (dd < bestD) { bestD = dd; best = it; }
    }
    auto *key = best ? G.itemKey[best]() : nullptr;
    if (!key) { Notify("Hold an item in your left hand"); return false; }
    snprintf(g_itemKey, sizeof g_itemKey, "%s", key->str().c_str());
    return true;
}
static void ArmDupe9() { if (LeftHandItem()) SpawnGrid(9); }
static void ArmDupe27() { if (LeftHandItem()) SpawnGrid(27); }

// Spawn On Hand: each right-trigger press (grip open) spawns the item at your hand.
static bool g_onHandTrigger = false;
static void SpawnOnHandTick(const Buttons &in) {
    bool press = in.rtrig && !g_onHandTrigger;
    g_onHandTrigger = in.rtrig;
    Il2CppObject *hand = g_rightHand.get();
    if (!f_spawnOnHand || !press || in.rgrip || MenuBusy() || !hand || g_time < g_nextItem) return;
    if (!LeftHandItem()) return;
    g_nextItem = g_time + 0.3;
    g_itemQueue.push_back({G.trGetPosition[hand]() + G.trGetForward[hand]() * 0.1f, Vector3(0, 0, 0)});
}


// ---- Leo's menu features ---------------------------------------------------------

static float Len2(Vector3 v) { return v.x * v.x + v.y * v.y + v.z * v.z; }

static void ThemeBlue() { g_theme = 0; Notify("Theme: Blue"); }
static void ThemeTranslucent() { g_theme = kTranslucent; Notify("Theme: Translucent"); }
static void ThemeOrange() { g_theme = 1; Notify("Theme: Orange"); }
static void ThemeRainbow() { g_theme = kRainbow; Notify("Theme: Rainbow"); }
static void GunBlue() { g_gunColor = 0; Notify("Gun pointer: Blue"); }
static void GunOrange() { g_gunColor = 1; Notify("Gun pointer: Orange"); }
static void GunRainbow() { g_gunColor = 2; Notify("Gun pointer: Rainbow"); }

// Radio groups: switching one on turns the others of its group off.
static void Radio(bool **flags, bool *prev, int n) {
    int picked = -1;
    for (int i = 0; i < n; i++) if (*flags[i] && !prev[i]) picked = i;
    if (picked >= 0) for (int i = 0; i < n; i++) *flags[i] = (i == picked);
    for (int i = 0; i < n; i++) prev[i] = *flags[i];
}
static bool *kGravFlags[] = {&f_lowGravity, &f_noGravity, &f_highGravity, &f_flipGravity};
static bool g_gravPrev[4];
static void GravityPicked() { Radio(kGravFlags, g_gravPrev, 4); ApplyGravity(); }
static bool *kAtmoFlags[] = {&f_fullbright, &f_horror};     // Fullbright and Horror can not both be on
static bool g_atmoPrev[2];
static void AtmoPicked() { Radio(kAtmoFlags, g_atmoPrev, 2); }
static bool *kFlyFlags[] = {&f_noclipFly, &f_velFly, &f_smoothFly, &f_joyFly, &f_zeroFly};   // only one fly mod at a time
static bool g_flyPrev[5];
static void FlyPicked() { Radio(kFlyFlags, g_flyPrev, 5); }
static bool *kHandFlags[] = {&f_ironMan, &f_spider};                   // both use the triggers
static bool g_handPrev[2];
static void HandPicked() { Radio(kHandFlags, g_handPrev, 2); }
static bool *kGunFlags[] = {&f_tpGun, &f_itemGun, &f_delGun, &f_grid9Gun, &f_cubeGun, &f_potionGun, &f_flingGun};   // the guns all use right grip + trigger
static bool g_gunPrev[7];
static void GunPicked() { Radio(kGunFlags, g_gunPrev, 7); }

static void AllOff() {
    for (int _gi = 0; _gi < kItems; _gi++) { auto &it = g_items[_gi]; if (it.kind == TOGGLE && !IsMenuFlag(it.flag)) *it.flag = false; }   // ticks undo each mod
    g_dance = -1;
    g_dupeArmed = 0;
    g_itemQueue.clear();
    g_knockSlider = kKnockDefault;                // the Overpowered sliders go back to normal too
    g_projCount = g_projSpread = g_projCost = 0;
    g_recording = false;
    g_rewinding = false;
    GravityPicked();
    HandPicked();
    AtmoPicked();
    FlyPicked();
    GunPicked();
    PointerToHand();
    Notify("Everything off");
}

// Low and High Gravity push your own velocity a little every frame.
static void GravityTick(float dt) {
    Vector3 v;
    if ((!f_lowGravity && !f_highGravity && !f_flipGravity) || f_noGravity || !GetVel(v)) return;
    if (f_flipGravity) {                              // gravity is off (ApplyGravity) and you are pushed up instead
        if (v.y < 14.0f) SetVel(v + Vector3(0, 9.81f * dt, 0));
        return;
    }
    SetVel(v + Vector3(0, f_lowGravity ? 9.81f * 0.7f * dt : -9.81f * 1.5f * dt, 0));
}

// Noclip Fly: hold A to fly where you look. While A is held, NoclipTick has already
// switched your body's collisions off, so you go through walls.
static void NoclipFlyTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *t = PlayerTransform();
    if (!f_noclipFly || !in.a || !head || !t) return;
    G.trSetPosition[t](G.trGetPosition[t]() + G.trGetForward[head]() * (kNoclipSpeeds[g_noclipSpeed] * dt));
    StopMoving();
}

// Long Arms, like Gorilla Tag: each hand is moved out from its own shoulder by a constant
// multiplier (kArmScale), so a small reach becomes a big one and everything in between scales
// evenly. The shoulders sit just under your head, turn with where you face and are a little apart.
//
// Why the old version did nothing: the controller trackers write your hands back to the real
// controller pose after this menu has run (and again right before drawing), so a moved hand
// was put back before the game or anyone else saw it. Dance mods work because they pause the trackers.
// So now Long Arms pauses them too and drives the hands itself from the raw controller pose:
//   1. Calibration (about a tenth of a second, hands untouched): the tracker's own pose is read from the
//      controller (DevicePosition / DeviceRotation, placed in the world through the tracker's parent) and
//      compared with where the hand really is. If the hand follows that pose with a fixed offset for
//      several frames in a row, the model is right.
//   2. Then the trackers are paused and every frame hand = (controller pose + that offset), pushed out from
//      the shoulder. The stretch eases in over a fraction of a second so it can't fling you.
// If the controller pose can't be read in this build it falls back to the simple version (the old one).
static const float kArmScale = 1.75f;
static Vector3 g_armLast[2];
static bool g_armHave[2];
struct ArmCal { Ref driver; Vector3 offP; Quaternion offR; bool have = false; int stable = 0; };
static ArmCal g_armCal[2];
static bool g_armReady = false, g_armOwnsPause = false, g_armLegacy = false;
static int g_armTries = 0;
static float g_armK = 1.0f;
static bool ReadPose(int node, Vector3 &pos, Quaternion &rot) {
    if (!G.tryGetVec3.IsValid() || !G.tryGetQuat.IsValid()) return false;
    XRDevice d = G.deviceAtNode(node);
    if (!d.initialized) return false;
    bool a = G.tryGetVec3(d.id, (BNM::Structures::Mono::String *)G.usageDevPos.get(), &pos);
    bool b = G.tryGetQuat(d.id, (BNM::Structures::Mono::String *)G.usageDevRot.get(), &rot);
    return a && b;
}
// The object the controller tracker moves (the hand itself or one of the two above it).
static Il2CppObject *TrackerDriver(Il2CppObject *hand) {
    Il2CppObject *t = hand;
    for (int up = 0; up < 3 && t; up++, t = G.trGetParent[t]())
        for (BNM::MonoType *type : G.trackerTypes) {
            Il2CppObject *go = G.compGameObject[t]();
            if (go && G.goGetComponent[go]((Il2CppObject *)type)) return t;
        }
    return nullptr;
}
// The controller's pose in the world, the way the tracker would place it.
static bool ControllerPose(int i, Il2CppObject *driver, Vector3 &pos, Quaternion &rot) {
    Vector3 dp;
    Quaternion dr;
    if (!ReadPose(i ? 5 : 4, dp, dr)) return false;
    Il2CppObject *par = G.trGetParent[driver]();
    if (par) {
        pos = G.trTransformPoint[par](dp);
        rot = G.trGetRotation[par]() * dr;
    } else {
        pos = dp;
        rot = dr;
    }
    return true;
}
static void ArmRelease() {
    if (g_armOwnsPause) { PauseTrackers(false); g_armOwnsPause = false; }
    g_armReady = false;
    g_armK = 1.0f;
    for (auto &c : g_armCal) { c.have = false; c.stable = 0; }
    g_armHave[0] = g_armHave[1] = false;
}
static void LongArmsTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get();
    bool danceWant = g_dance >= 0 && in.ltrig;
    if (!f_longArms || g_dancing || danceWant || !head) { ArmRelease(); return; }
    Il2CppObject *hands[2] = {g_leftHand.get(), g_rightHand.get()};
    if (!hands[0] || !hands[1]) return;
    Vector3 hp = G.trGetPosition[head](), f = Flat(G.trGetForward[head]()), r = Cross(Vector3(0, 1, 0), f);
    Vector3 neck = hp + Vector3(0, -0.22f, 0);
    bool canModel = !g_armLegacy && G.trGetRotation.IsValid() && G.tryGetVec3.IsValid() && G.tryGetQuat.IsValid() &&
                    !G.trackerTypes.empty();
    if (canModel && !g_armReady) {                       // 1. calibrate, hands untouched
        bool allOk = true;
        for (int i = 0; i < 2; i++) {
            ArmCal &c = g_armCal[i];
            if (!Alive(c.driver.get())) {
                Il2CppObject *d = TrackerDriver(hands[i]);
                if (!d) { allOk = false; continue; }
                c.driver.set(d);
            }
            Vector3 cp;
            Quaternion cr;
            if (!ControllerPose(i, c.driver.get(), cp, cr)) { allOk = false; continue; }
            Quaternion inv = QInv(cr);
            Vector3 offP = QRot(inv, G.trGetPosition[hands[i]]() - cp);
            Quaternion offR = inv * G.trGetRotation[hands[i]]();
            if (c.have && Len2(offP - c.offP) < 0.02f * 0.02f) c.stable++; else c.stable = 0;
            c.offP = offP;
            c.offR = offR;
            c.have = true;
            if (c.stable < 6) allOk = false;
        }
        if (allOk) {
            PauseTrackers(true);
            g_armOwnsPause = true;
            g_armReady = true;
            g_armK = 1.0f;
            g_armTries = 0;
            ym::log("leos-menu: long arms: controller model fits, trackers paused (%d)", g_pausedN);
        } else if (++g_armTries > 300) {                 // never fit: use the simple version
            g_armLegacy = true;
            ym::log("leos-menu: long arms: controller model doesn't fit, simple mode");
            Notify("Long Arms: simple mode");
        }
        return;
    }
    if (canModel && g_armReady) {                        // 2. drive the hands
        float target = g_menuShown ? 1.0f : kArmScale;   // the menu stays easy to use
        float step = 3.0f * dt;
        g_armK += g_armK < target ? fminf(step, target - g_armK) : -fminf(step, g_armK - target);
        for (int i = 0; i < 2; i++) {
            ArmCal &c = g_armCal[i];
            Vector3 cp;
            Quaternion cr;
            if (!Alive(c.driver.get()) || !ControllerPose(i, c.driver.get(), cp, cr)) continue;
            Vector3 raw = cp + QRot(cr, c.offP);
            Vector3 shoulder = neck + r * (i ? 0.17f : -0.17f);
            G.trSetPosition[hands[i]](shoulder + (raw - shoulder) * g_armK);
            G.trSetRotation[hands[i]](cr * c.offR);
        }
        return;
    }
    // Simple version: scale the hand the trackers just set (only works if nothing resets it afterwards)
    for (int i = 0; i < 2; i++) {
        Vector3 p = G.trGetPosition[hands[i]]();
        if (g_armHave[i] && Len2(p - g_armLast[i]) < 1e-8f) continue;
        Vector3 shoulder = neck + r * (i ? 0.17f : -0.17f);
        Vector3 np = shoulder + (p - shoulder) * kArmScale;
        G.trSetPosition[hands[i]](np);
        g_armLast[i] = np;
        g_armHave[i] = true;
    }
}

// Legs (Movement): two legs only you see, and you walk with them: left joystick walks the
// way you look (forward, back, sideways), X or A jumps (A only while Noclip Fly is off).
static bool ReadStick(int node, Vector2 &out) {
    if (!G.tryGetVec2.IsValid()) return false;
    XRDevice d = G.deviceAtNode(node);
    if (!d.initialized) return false;
    G.tryGetVec2(d.id, (BNM::Structures::Mono::String *)G.usageStick.get(), &out);
    return true;
}
static Ref g_legL, g_legR;
static double g_nextJump = 0;
static float g_legPhase = 0;
static bool g_legsWalked = false, g_legsWarned = false;
static void LegsTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get();
    if (!f_legs || !head) { Kill(g_legL); Kill(g_legR); g_legsWalked = false; return; }
    Vector3 hp = G.trGetPosition[head](), f = Flat(G.trGetForward[head]()), r = Cross(Vector3(0, 1, 0), f);
    Vector2 st(0, 0);
    bool have = ReadStick(4, st);                         // left thumbstick
    if (!have && !g_legsWarned) { g_legsWarned = true; Notify("Joystick isn't readable in this build"); }
    float mag = sqrtf(st.x * st.x + st.y * st.y);
    bool walking = have && mag > 0.15f;
    Vector3 v;
    if (GetVel(v)) {
        if (walking) {                                    // walk where you look: up = forward, sideways = strafe
            Vector3 dir = f * st.y + r * st.x;
            float sp = 3.5f * (mag > 1.f ? 1.f : mag);
            SetVel(Vector3(dir.x * sp, v.y, dir.z * sp));
        } else if (g_legsWalked) {
            SetVel(Vector3(0, v.y, 0));                   // let go of the stick: stop, don't slide
        }
        bool jump = in.x || (in.a && !f_noclipFly && !f_velFly && !f_smoothFly && !f_zeroFly && !f_strafe);
        if (jump && g_time >= g_nextJump && fabsf(v.y) < 1.0f) {
            g_nextJump = g_time + 0.5;
            SetVel(Vector3(walking ? v.x : 0.f, 5.0f, walking ? v.z : 0.f));
        }
    }
    g_legsWalked = walking;
    if (walking) g_legPhase += dt * 9.0f * (mag > 1.f ? 1.f : mag);
    for (Ref *lr : {&g_legL, &g_legR})
        if (!Alive(lr->get())) if (Il2CppObject *o = Cube(T().bgB, false)) lr->set(o);
    Vector3 base = hp + Vector3(0, -0.75f, 0);
    float swing = walking ? sinf(g_legPhase) * 0.22f : 0.f;      // the legs step while you walk
    int i = 0;
    for (Ref *leg : {&g_legL, &g_legR}) {
        Il2CppObject *o = leg->get();
        if (!Alive(o)) { i++; continue; }
        Il2CppObject *t = Tr(o);
        float side = i ? 0.1f : -0.1f, step = i ? swing : -swing;
        i++;
        G.trSetPosition[t](base + r * side + f * step);
        G.trSetLocalScale[t](Vector3(0.09f, 0.9f, 0.09f));
    }
}

// Trigger Bullets: hold right trigger (grip open, menu closed) to shoot small balls out of
// your hand, at most 12 at a time. They are objects only you see: nobody is hit.
struct Bullet { Ref obj; Vector3 vel; double dieAt = 0; bool live = false; };
static Bullet g_bullets[12];
static double g_nextBullet = 0;
static void BulletTick(const Buttons &in, float dt) {
    for (auto &b : g_bullets) {
        if (!b.live) continue;
        Il2CppObject *o = b.obj.get();
        if (g_time >= b.dieAt || !Alive(o)) { Show(b.obj, false); b.live = false; continue; }
        Il2CppObject *t = Tr(o);
        G.trSetPosition[t](G.trGetPosition[t]() + b.vel * dt);
    }
    Il2CppObject *hand = g_rightHand.get();
    if (!f_bullets || !in.rtrig || in.rgrip || MenuBusy() || !hand || g_time < g_nextBullet) return;
    g_nextBullet = g_time + 0.12;
    for (auto &b : g_bullets) {
        if (b.live) continue;
        if (!Alive(b.obj.get())) {
            Il2CppObject *o = Prim(0, T().bgB, false);
            if (!o) return;
            G.trSetLocalScale[Tr(o)](Vector3(0.07f, 0.07f, 0.07f));
            b.obj.set(o);
        }
        Vector3 f = G.trGetForward[hand]();
        G.trSetPosition[Tr(b.obj.get())](G.trGetPosition[hand]() + f * 0.15f);
        Show(b.obj, true);
        b.vel = f * 30.0f;
        b.dieAt = g_time + 2.0;
        b.live = true;
        return;
    }
}

// A world-space line (a GameObject with a LineRenderer), colored c. Used by Draw, Trail and ESP.
static bool MakeLine(Color c, float w, const char *name, Ref &goOut, Ref &lineOut) {
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString(name));
    Il2CppObject *line = go && G.tLine ? G.goAddComponent[go](G.tLine) : nullptr;
    if (!line) return false;
    if (Il2CppObject *m = G.rendererGetMaterial[line]()) {
        if (G.shader.get()) G.matSetShader[m](G.shader.get());
        G.matSetColor[m](c);
    }
    G.lrSetUseWorldSpace[line](true);
    G.lrSetStartWidth[line](w);
    G.lrSetEndWidth[line](w);
    G.lrSetPositionCount[line](0);
    goOut.set(go);
    lineOut.set(line);
    return true;
}

// The color picked with the Draw Color slider (Fun Mods). Black is the default.
static Color DrawColor() {
    switch (g_drawColor) {
    case 1: return Color(1.0f, 0.05f, 0.05f, 1);       // Red
    case 2: return Color(0.1f, 0.3f, 1.0f, 1);         // Blue
    case 3: return Color(1.0f, 0.92f, 0.1f, 1);        // Yellow
    case 4: return Color(1.0f, 0.5f, 0.0f, 1);         // Orange
    case 5: return Rainbow((float)g_time * 0.3f);      // Rainbow
    case 6: return Color(0.1f, 0.85f, 0.2f, 1);        // Green
    case 7: return Color(1, 1, 1, 1);                  // White
    default: return Color(0, 0, 0, 1);                 // Black
    }
}

// Draw: hold right trigger (grip open, menu closed) to draw lines in the air. 120 strokes,
// 600 points each, and Delete Draw erases them. Only you see them. Rainbow is drawn in short
// pieces of 12 points so the color really changes along the line.
static Ref g_stroke[120], g_curLine;
static int g_strokeN = 0, g_curPts = 0;
static bool g_drawing = false;
static double g_nextPoint = 0;
static Vector3 g_lastDrawPt;
static void DeleteDraw() {
    for (int i = 0; i < g_strokeN; i++) Kill(g_stroke[i]);
    g_strokeN = 0;
    g_curPts = 0;
    g_drawing = false;
    g_curLine.clear();
    Notify("Drawings deleted");
}
static void DrawTick(const Buttons &in) {
    Il2CppObject *hand = g_rightHand.get();
    bool want = f_draw && in.rtrig && !in.rgrip && !MenuBusy() && hand;
    if (!want) { g_drawing = false; return; }
    bool rainbow = g_drawColor == 5;
    bool begin = !g_drawing;
    bool piece = g_drawing && rainbow && g_curPts >= 12;
    g_drawing = true;
    if (begin || piece) {                                // a new stroke (or the next piece of a rainbow one)
        g_curLine.clear();
        if (g_strokeN >= 120) { if (begin) Notify("Draw limit: use Delete Draw"); g_curPts = 0; return; }
        Ref go;
        if (!MakeLine(DrawColor(), 0.015f, "LeoDraw", go, g_curLine)) return;
        g_stroke[g_strokeN++] = go;
        g_curPts = 0;
        if (piece) {                                     // start where the last piece ended, so the line stays joined
            G.lrSetPositionCount[g_curLine.get()](1);
            G.lrSetPosition[g_curLine.get()](0, g_lastDrawPt);
            g_curPts = 1;
        }
    }
    Il2CppObject *line = g_curLine.get();
    if (!Alive(line) || g_time < g_nextPoint || g_curPts >= 600) return;
    g_nextPoint = g_time + 0.02;
    g_lastDrawPt = G.trGetPosition[hand]() + G.trGetForward[hand]() * 0.1f;
    G.lrSetPositionCount[line](g_curPts + 1);
    G.lrSetPosition[line](g_curPts++, g_lastDrawPt);
}

// Trail (Fun): a line that follows your body, drawn in short pieces so old ones can be dropped:
// 10 pieces of 30 points. The color is the Draw Color.
static constexpr int kTrailChunks = 10, kTrailPts = 30;
static Ref g_trGo[kTrailChunks], g_trLine[kTrailChunks];
static int g_trHead = -1, g_trPts = 0;
static Vector3 g_trLast;
static bool g_trHave = false;
static double g_nextTrail = 0;
static float Len2(Vector3 v);
static void ClearTrail() {
    for (auto &r : g_trGo) Kill(r);
    for (auto &r : g_trLine) r.clear();
    g_trHead = -1; g_trPts = 0; g_trHave = false;
}
static void TrailTick() {
    Vector3 p;
    if (!f_trail || !BodyPos(p)) { if (g_trHave || g_trHead >= 0) ClearTrail(); return; }
    if (g_time < g_nextTrail) return;
    if (g_trHave && Len2(p - g_trLast) < 0.04f * 0.04f) return;      // only when you moved
    g_nextTrail = g_time + 0.03;
    if (g_trHead < 0 || g_trPts >= kTrailPts) {
        int n = (g_trHead + 1) % kTrailChunks;
        Kill(g_trGo[n]);                                 // the oldest piece disappears
        g_trLine[n].clear();
        if (!MakeLine(DrawColor(), 0.05f, "LeoTrail", g_trGo[n], g_trLine[n])) return;
        g_trHead = n;
        g_trPts = 0;
        if (g_trHave) {                                  // join onto the previous piece
            G.lrSetPositionCount[g_trLine[n].get()](1);
            G.lrSetPosition[g_trLine[n].get()](0, g_trLast);
            g_trPts = 1;
        }
    }
    Il2CppObject *line = g_trLine[g_trHead].get();
    if (!Alive(line)) return;
    G.lrSetPositionCount[line](g_trPts + 1);
    G.lrSetPosition[line](g_trPts++, p);
    g_trLast = p;
    g_trHave = true;
}

// Player ESP: a red stick-figure rig on every other player, drawn through walls and blocks.
// Other players are not always the same class as you (MasterPlayer), so the game's classes are
// searched once for ones with a head and hand Transform (a player-like rig); every second the
// objects of those classes are listed, and every frame the rigs follow them.
struct EspClass { BNM::MonoType *type; void *head, *left, *right; bool hands; std::string name; };
static std::vector<EspClass> g_espClasses;
static bool g_espScanned = false;
static void *(*p_classFields)(void *, void **);
static const char *(*p_fieldName)(void *);
static const void *(*p_fieldType)(void *);
static void (*p_fieldGet)(void *, void *, void *);
static void *(*p_classParent)(void *);
static std::string Lower(std::string x) { for (auto &ch : x) ch = (char)tolower((unsigned char)ch); return x; }
static bool Has(const std::string &x, const char *w) { return x.find(w) != std::string::npos; }

static void EspScan() {
    g_espScanned = true;
    auto R = ym::api()->resolve;
    p_classFields = (decltype(p_classFields))R("il2cpp_class_get_fields");
    p_fieldName = (decltype(p_fieldName))R("il2cpp_field_get_name");
    p_fieldType = (decltype(p_fieldType))R("il2cpp_field_get_type");
    p_fieldGet = (decltype(p_fieldGet))R("il2cpp_field_get_value");
    p_classParent = (decltype(p_classParent))R("il2cpp_class_get_parent");
    if (!p_classFields || !p_fieldName || !p_fieldType || !p_fieldGet) { ym::log("leos-menu: esp can't read class fields"); return; }
    std::vector<EspClass> all;
    for (auto &c : Image("Assembly-CSharp.dll").GetClasses(false)) {
        auto *kl = c.GetClass();
        if (!kl || !kl->name) continue;
        EspClass e{c.GetMonoType(), nullptr, nullptr, nullptr, false, kl->name};
        int guard = 0;
        for (void *k = kl; k && guard < 8; k = p_classParent ? p_classParent(k) : nullptr, guard++) {
            void *iter = nullptr;
            while (void *f = p_classFields(k, &iter)) {
                const char *fnm = p_fieldName(f);
                if (!fnm || TypeName((const BNM::IL2CPP::Il2CppType *)p_fieldType(f)) != "UnityEngine.Transform") continue;
                std::string n = Lower(fnm);
                if (!e.head && Has(n, "head")) e.head = f;
                else if (!e.left && Has(n, "left") && Has(n, "hand")) e.left = f;
                else if (!e.right && Has(n, "right") && Has(n, "hand")) e.right = f;
            }
        }
        if (!e.head || !e.type) continue;
        e.hands = e.left && e.right;
        all.push_back(e);
    }
    bool anyHands = false;
    for (auto &e : all) anyHands |= e.hands;
    for (auto &e : all)
        if ((!anyHands || e.hands) && g_espClasses.size() < 8) {
            g_espClasses.push_back(e);
            ym::log("leos-menu: esp watches class %s (hands %d)", e.name.c_str(), e.hands ? 1 : 0);
        }
    if (g_espClasses.empty()) ym::log("leos-menu: esp found no player-like classes");
}

struct EspRig { Ref obj, head, lh, rh, go, line, headGo; };
static EspRig g_rig[16];
static double g_nextEsp = 0;
static bool g_espShown = false;

// A material that draws on top of everything (through walls). Tries the shaders that ignore
// depth; if the build has none of them the rig still shows but hides behind walls.
static bool g_xrayTried = false, g_xraySee = false;
static Ref g_xrayShader;
static void XRay(Il2CppObject *m, Color c) {
    if (!g_xrayTried) {
        g_xrayTried = true;
        for (const char *n : {"GUI/Text Shader", "Hidden/Internal-Colored"}) {
            if (g_xrayShader.get() || !G.shaderFind.IsValid()) break;
            Il2CppObject *sh = G.shaderFind((BNM::Structures::Mono::String *)CreateMonoString(n));
            if (sh && Alive(sh)) { g_xrayShader.set(sh); g_xraySee = true; ym::log("leos-menu: esp shader %s", n); }
        }
        if (!g_xraySee) ym::log("leos-menu: esp has no see-through shader, rigs hide behind walls");
    }
    if (g_xrayShader.get()) G.matSetShader[m](g_xrayShader.get());
    else if (G.shader.get()) G.matSetShader[m](G.shader.get());
    G.matSetColor[m](c);
    if (!g_xraySee) return;
    if (G.matSetInt.IsValid()) {
        G.matSetInt[m]((BNM::Structures::Mono::String *)CreateMonoString("_ZTest"), 8);     // Always
        G.matSetInt[m]((BNM::Structures::Mono::String *)CreateMonoString("_ZWrite"), 0);
    }
    if (G.matSetRenderQueue.IsValid()) G.matSetRenderQueue[m](4000);
}

static void EspRefresh() {
    if (!g_espScanned) EspScan();
    Il2CppObject *me = g_player.get(), *myHead = g_head.get();
    if (!myHead || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return;
    Vector3 mine = G.trGetPosition[myHead]();
    int k = 0;
    for (auto &e : g_espClasses) {
        auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)e.type)
                                                      : G.findSceneByType((Il2CppObject *)e.type, 0));
        size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
        for (size_t i = 0; i < n && k < 16; i++) {
            Il2CppObject *p = ((Il2CppObject **)(arr + 0x20))[i];
            if (!Alive(p) || p == me) continue;
            Il2CppObject *head = nullptr, *lh = nullptr, *rh = nullptr;
            p_fieldGet(p, e.head, &head);
            if (e.left) p_fieldGet(p, e.left, &lh);
            if (e.right) p_fieldGet(p, e.right, &rh);
            if (!Alive(head)) continue;
            if (Len2(G.trGetPosition[head]() - mine) < 0.35f * 0.35f) continue;      // that is you
            Il2CppObject *hgo = G.compGameObject[head]();
            if (G.goActiveInHierarchy.IsValid() && hgo && !G.goActiveInHierarchy[hgo]()) continue;
            bool dup = false;
            for (int j = 0; j < k; j++) if (g_rig[j].head.get() == head) dup = true;
            if (dup) continue;
            g_rig[k].obj.set(p);
            g_rig[k].head.set(head);
            g_rig[k].lh.set(Alive(lh) ? lh : nullptr);
            g_rig[k].rh.set(Alive(rh) ? rh : nullptr);
            k++;
        }
    }
    int found = k;
    for (; k < 16; k++) { g_rig[k].obj.clear(); g_rig[k].head.clear(); g_rig[k].lh.clear(); g_rig[k].rh.clear(); }
    static int lastFound = -1;
    if (found != lastFound) { lastFound = found; ym::log("leos-menu: esp sees %d other players", found); }
}

static void MakeRig(int i) {
    EspRig &g = g_rig[i];
    Kill(g.go);
    Kill(g.headGo);
    g.line.clear();
    const Color red(1.0f, 0.1f, 0.1f, 1);
    if (!MakeLine(red, 0.035f, "LeoRig", g.go, g.line)) return;
    Il2CppObject *line = g.line.get();
    G.lrSetPositionCount[line](12);
    if (Il2CppObject *m = G.rendererGetMaterial[line]()) XRay(m, red);
    if (Il2CppObject *h = Prim(0, red, false)) {          // the head
        G.trSetLocalScale[Tr(h)](Vector3(0.22f, 0.22f, 0.22f));
        if (auto *r = G.goGetComponent[h](G.tRenderer))
            if (Il2CppObject *m = G.rendererGetMaterial[r]()) XRay(m, red);
        g.headGo.set(h);
    }
}

static void EspTick() {
    if (!f_esp) {
        if (g_espShown) { for (auto &g : g_rig) { Show(g.go, false); Show(g.headGo, false); } g_espShown = false; }
        return;
    }
    if (g_time >= g_nextEsp) { g_nextEsp = g_time + 1.0; EspRefresh(); }
    g_espShown = true;
    Vector3 up(0, 1, 0);
    for (int i = 0; i < 16; i++) {
        EspRig &g = g_rig[i];
        Il2CppObject *head = g.head.get();
        if (!Alive(head)) { Show(g.go, false); Show(g.headGo, false); continue; }
        if (!Alive(g.go.get())) MakeRig(i);
        Il2CppObject *line = g.line.get();
        if (!Alive(g.go.get()) || !Alive(line)) continue;
        Show(g.go, true);
        Show(g.headGo, true);
        Vector3 h = G.trGetPosition[head](), f = Flat(G.trGetForward[head]()), r = Cross(up, f);
        Vector3 neck = h + up * -0.2f, chest = h + up * -0.35f, hip = h + up * -0.85f;
        Vector3 lsh = chest - r * 0.18f, rsh = chest + r * 0.18f;
        Il2CppObject *lh = g.lh.get(), *rh = g.rh.get();
        Vector3 lHand = Alive(lh) ? G.trGetPosition[lh]() : lsh - r * 0.1f + up * -0.4f;
        Vector3 rHand = Alive(rh) ? G.trGetPosition[rh]() : rsh + r * 0.1f + up * -0.4f;
        Vector3 feet(hip.x, h.y - 1.6f, hip.z);
        Vector3 pts[12] = {feet - r * 0.15f, hip, feet + r * 0.15f, hip, neck, lsh, lHand, lsh, rsh, rHand, rsh, neck};
        for (int k = 0; k < 12; k++) G.lrSetPosition[line](k, pts[k]);
        if (Alive(g.headGo.get())) G.trSetPosition[Tr(g.headGo.get())](h);
    }
}

// Soundboard: your own sound files (.wav, .mp3, .ogg) from the HSounds folder inside the game's
// persistent data folder. The Soundboard page gets one button per file. A sound plays for you and is
// sent to the room through the voice recorder (Photon Voice), the same way the old soundboard did;
// your microphone is given back when the sound is over (or with Stop Sounds). A .wav is read a little
// each frame so the game doesn't stutter; .mp3 / .ogg go through Unity's audio request.
struct SoundEntry { std::string name, path; int type = 0; };      // type: 0 wav, 1 mp3, 2 ogg
static std::vector<SoundEntry> g_sounds;
static std::string g_soundDir;
static bool g_soundInit = false;
static Ref g_audioGo, g_audioSrc;
struct Loader { FILE *f = nullptr; int ch = 0, hz = 0, bits = 16, fmt = 1; size_t total = 0, done = 0; Ref arr; std::string name, path; };
static Loader g_ld;
static Ref g_req;
static std::string g_reqName, g_reqPath;
static int g_reqAge = 0;
static double g_micRestoreAt = 0, g_nextHear = 0;
static std::unordered_map<std::string, Ref> g_clipCache;

static void SbLog(const char *m) { ym::log("leos-menu: soundboard: %s", m); }
static std::string PersistentPath() {
    static Method<BNM::Structures::Mono::String *> getPath = Class("UnityEngine", "Application").GetMethod("get_persistentDataPath", 0);
    try { if (getPath.IsValid()) { auto *p = getPath(); if (p) return p->str(); } } catch (...) {}
    return std::string();
}
static void CancelLoad() {
    if (g_ld.f) fclose(g_ld.f);
    g_ld.f = nullptr;
    g_ld.arr.clear();
    if (Alive(g_req.get())) {
        static Method<void> abort = Class("UnityEngine.Networking", "UnityWebRequest").GetMethod("Abort", 0);
        try { if (abort.IsValid()) abort[g_req.get()](); } catch (...) {}
    }
    g_req.clear();
}
// ---- the voice recorder ----
static Il2CppObject *VoiceRecorder() {
    Class rc("Photon.Voice.Unity", "Recorder");
    if (!rc.IsValid()) return nullptr;
    try {
        static Method<Il2CppObject *> find = Class("UnityEngine", "GameObject").GetMethod("Find", 1);
        if (find.IsValid()) {
            Il2CppObject *go = find((BNM::Structures::Mono::String *)CreateMonoString("PhotonVRVoice"));
            if (Alive(go)) { Il2CppObject *r = G.goGetComponent[go]((Il2CppObject *)rc.GetMonoType()); if (r) return r; }
        }
        uint8_t *arr = ScanAll(rc.GetMonoType());
        if (ArrLen(arr) > 0) return ArrAt(arr, 0);
    } catch (...) {}
    return nullptr;
}
template <class... A> static bool RecCall(Il2CppObject *rec, std::initializer_list<const char *> names, A... a) {
    Class rc("Photon.Voice.Unity", "Recorder");
    for (const char *n : names) {
        Method<void> m;
        m = rc.GetMethod(n, (int)sizeof...(A));
        if (!m.IsValid()) continue;
        try { m[rec](a...); return true; } catch (...) {}
    }
    return false;
}
static void RestoreMic() {
    Il2CppObject *rec = VoiceRecorder();
    if (!rec) return;
    RecCall(rec, {"set_SourceType", "set_sourceType"}, 0);
    RecCall(rec, {"set_AudioClip", "set_audioClip"}, (Il2CppObject *)nullptr);
    RecCall(rec, {"set_DebugEchoMode", "set_debugEchoMode"}, false);
    RecCall(rec, {"set_TransmitEnabled"}, true);
    RecCall(rec, {"set_IsRecording"}, true);
    RecCall(rec, {"RestartRecording"}, true);
    RecCall(rec, {"StartRecording"});
    g_micRestoreAt = 0;
}
static bool InRoom() {
    static Method<bool> inRoom = Class("Photon.Pun", "PhotonNetwork").GetMethod("get_InRoom", 0);
    try { return inRoom.IsValid() ? inRoom() : true; } catch (...) { return true; }
}
static void SendToVoice(Il2CppObject *clip) {
    if (!f_allHear) return;
    if (!InRoom()) { SbLog("not in a room, only you hear it"); return; }
    Il2CppObject *rec = VoiceRecorder();
    if (!rec) { SbLog("no voice recorder found, only you hear it"); return; }
    int ok = 0;
    ok += RecCall(rec, {"set_SourceType", "set_sourceType"}, 1);                 // AudioClip
    ok += RecCall(rec, {"set_AudioClip", "set_audioClip"}, clip);
    ok += RecCall(rec, {"set_LoopAudioClip", "set_loopAudioClip"}, false);
    ok += RecCall(rec, {"set_TransmitEnabled"}, true);
    ok += RecCall(rec, {"set_IsRecording"}, true);
    ok += RecCall(rec, {"set_RecordOnlyWhenJoined"}, true);
    ok += RecCall(rec, {"set_RecordOnlyWhenEnabled"}, false);
    ok += RecCall(rec, {"set_DebugEchoMode", "set_debugEchoMode"}, false);
    ok += RecCall(rec, {"RestartRecording"}, true);
    ok += RecCall(rec, {"StartRecording"});
    ym::log("leos-menu: soundboard: voice recorder calls that worked: %d of 9", ok);
}
// ---- local playback ----
static bool EnsureAudio() {
    if (Alive(g_audioSrc.get())) return true;
    Il2CppObject *head = g_head.get();
    if (!head || !G.tAudioSource) return false;
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("LeoSound"));
    Il2CppObject *src = go ? G.goAddComponent[go](G.tAudioSource) : nullptr;
    if (!src) return false;
    G.trSetParent[Tr(go)](head, false);
    G.trSetLocalPosition[Tr(go)](Vector3(0, 0, 0));
    if (G.srcSetSpatial.IsValid()) G.srcSetSpatial[src](0.0f);        // 2D: same in both ears
    if (G.srcSetVolume.IsValid()) G.srcSetVolume[src](1.0f);
    g_audioGo.set(go);
    g_audioSrc.set(src);
    return true;
}
static void PlayClipObj(Il2CppObject *clip) {
    if (!clip) return;
    if (EnsureAudio()) {
        Il2CppObject *src = g_audioSrc.get();
        if (G.srcStop.IsValid()) G.srcStop[src]();
        G.srcSetClip[src](clip);
        G.srcPlay[src]();
    }
    SendToVoice(clip);
    static Method<float> getLen = Class("UnityEngine", "AudioClip").GetMethod("get_length", 0);
    float len = 5.0f;
    try { if (getLen.IsValid()) len = getLen[clip](); } catch (...) {}
    g_micRestoreAt = g_time + len + 0.5;
}
static void SoundRoomReset() {
    CancelLoad();
    for (auto &c : g_clipCache) c.second.clear();
    g_clipCache.clear();
    g_micRestoreAt = 0;
}
static void StopSound() {
    CancelLoad();
    if (Alive(g_audioSrc.get()) && G.srcStop.IsValid()) { G.srcStop[g_audioSrc.get()](); G.srcSetClip[g_audioSrc.get()]((Il2CppObject *)nullptr); }
    RestoreMic();
    Notify("Sounds stopped");
}
// ---- the file list ----
static void PlaySoundIdx(int i);
static void SoundPressed() { PlaySoundIdx(g_pressArg); }
static std::string ShortName(const std::string &n) { return n.size() <= 15 ? n : n.substr(0, 12) + "..."; }
static void ScanSoundDir(const std::string &dir, bool create) {
    try {
        if (create) std::filesystem::create_directories(dir);
        if (!std::filesystem::is_directory(dir)) return;
        for (auto &f : std::filesystem::directory_iterator(dir)) {
            if (!f.is_regular_file()) continue;
            std::string ext = Lower(f.path().extension().string());
            if (ext != ".wav" && ext != ".mp3" && ext != ".ogg") continue;
            SoundEntry e;
            e.path = f.path().string();
            bool dup = false;
            for (auto &o : g_sounds) if (o.path == e.path) dup = true;
            if (dup) continue;
            e.name = ShortName(f.path().stem().string());
            e.type = ext == ".wav" ? 0 : ext == ".mp3" ? 1 : 2;
            g_sounds.push_back(e);
        }
    } catch (...) { ym::log("leos-menu: soundboard: couldn't read %s", dir.c_str()); }
}
static void SoundReload() {
    g_view.clear();                                  // it pointed into the old button list
    g_sounds.clear();
    g_soundItems.clear();
    std::string p = PersistentPath();
    if (!p.empty()) g_soundDir = p + "/HSounds";
    if (!g_soundDir.empty()) ScanSoundDir(g_soundDir, true);
    if (!p.empty()) ScanSoundDir(p + "/leo-sounds", false);
    ScanSoundDir("/sdcard/HSounds", false);
    std::sort(g_sounds.begin(), g_sounds.end(), [](const SoundEntry &a, const SoundEntry &b) { return Lower(a.name) < Lower(b.name); });
    for (size_t i = 0; i < g_sounds.size(); i++)
        g_soundItems.push_back(MenuItem{SOUNDBOARD, g_sounds[i].name.c_str(), ACTION, MAIN, nullptr, SoundPressed, nullptr, 0, nullptr, (int)i});
    g_dirty = true;
    ym::log("leos-menu: soundboard: %d sounds, folder %s", (int)g_sounds.size(), g_soundDir.c_str());
}
static void SoundReloadBtn() {
    SoundReload();
    Notify("%d sound(s) found", (int)g_sounds.size());
}
static void SoundEnsure() {
    if (g_soundInit) return;
    g_soundInit = true;
    SoundReload();
}
// Hear All: other players' voices get louder and reach further (and go back when you turn it off).
static bool g_hearCaptured = false;
static float g_hearVol = 1.0f, g_hearDist = 20.0f;
static void HearAllApply() {
    try {
        static Method<Il2CppObject *> find = Class("UnityEngine", "GameObject").GetMethod("Find", 1);
        if (!find.IsValid() || !G.tAudioSource) return;
        Il2CppObject *go = find((BNM::Structures::Mono::String *)CreateMonoString("PhotonVRVoice"));
        if (!Alive(go)) return;
        Il2CppObject *src = G.goGetComponent[go](G.tAudioSource);
        if (!src) return;
        Class ac("UnityEngine", "AudioSource");
        Method<float> getD, getV;
        Method<void> setD;
        getD = ac.GetMethod("get_maxDistance", 0);
        getV = ac.GetMethod("get_volume", 0);
        setD = ac.GetMethod("set_maxDistance", 1);
        if (!g_hearCaptured) {
            if (getV.IsValid()) g_hearVol = getV[src]();
            if (getD.IsValid()) g_hearDist = getD[src]();
            g_hearCaptured = true;
        }
        float vol = f_hearAll ? std::min(g_hearVol * 1.45f, 2.0f) : g_hearVol;
        float dist = f_hearAll ? g_hearDist * 8.0f : g_hearDist;
        if (G.srcSetVolume.IsValid()) G.srcSetVolume[src](vol);
        if (setD.IsValid()) setD[src](dist);
    } catch (...) {}
}
static void HearAllToggled() { g_nextHear = 0; HearAllApply(); }

// ---- loading and playing ----
static std::string UrlEncode(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '/' || c == ':' || c == '.' || c == '-' || c == '_') out.push_back((char)c);
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}
static void StartWav(const SoundEntry &e) {
    if (!(G.audioCreate.IsValid() && G.audioSetData.IsValid() && G.srcPlay.IsValid() && G.srcSetClip.IsValid() && G.floatClass && G.arrayNew)) {
        Notify("Sound isn't available here");
        return;
    }
    FILE *f = fopen(e.path.c_str(), "rb");
    if (!f) { Notify("Couldn't open that file"); return; }
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); Notify("That isn't a .wav file"); return; }
    int ch = 0, hz = 0, bits = 0, fmt = 0;
    uint32_t dataSize = 0;
    bool gotData = false;
    for (int guard = 0; guard < 32 && !gotData; guard++) {   // walk the chunks until the data chunk
        uint8_t c[8];
        if (fread(c, 1, 8, f) != 8) break;
        uint32_t sz = (uint32_t)c[4] | (uint32_t)c[5] << 8 | (uint32_t)c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "fmt ", 4) && sz >= 16) {
            uint8_t b[16];
            if (fread(b, 1, 16, f) != 16) break;
            fmt = b[0] | b[1] << 8;
            ch = b[2] | b[3] << 8;
            hz = (int)((uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24);
            bits = b[14] | b[15] << 8;
            fseek(f, (long)(sz - 16 + (sz & 1)), SEEK_CUR);
        } else if (!memcmp(c, "data", 4)) { dataSize = sz; gotData = true; }
        else fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
    }
    bool okFmt = (fmt == 1 && (bits == 16 || bits == 8)) || (fmt == 3 && bits == 32);
    if (!gotData || !okFmt || (ch != 1 && ch != 2) || hz < 8000 || hz > 192000) {
        fclose(f);
        Notify("Use a PCM .wav (8 or 16 bit, or 32 bit float)");
        return;
    }
    if (dataSize > 100u * 1024 * 1024) { fclose(f); Notify("File too big (max 100 MB)"); return; }
    size_t total = dataSize / (size_t)(bits / 8);
    total -= total % ch;
    auto *arr = (Il2CppObject *)G.arrayNew(G.floatClass, total);
    if (!arr || total == 0) { fclose(f); Notify("Couldn't load that file"); return; }
    g_ld.f = f;
    g_ld.ch = ch;
    g_ld.hz = hz;
    g_ld.bits = bits;
    g_ld.fmt = fmt;
    g_ld.total = total;
    g_ld.done = 0;
    g_ld.name = e.name;
    g_ld.path = e.path;
    g_ld.arr.set(arr);
    Notify("Loading %s...", e.name.c_str());
}
static void StartWeb(const SoundEntry &e) {
    Class mm("UnityEngine.Networking", "UnityWebRequestMultimedia"), wr("UnityEngine.Networking", "UnityWebRequest");
    if (!mm.IsValid() || !wr.IsValid()) { Notify("This game build can't play .mp3 / .ogg, use a .wav"); return; }
    Method<Il2CppObject *> get, send;
    get = mm.GetMethod("GetAudioClip", 2);
    send = wr.GetMethod("SendWebRequest", 0);
    if (!get.IsValid() || !send.IsValid()) { Notify("This game build can't play .mp3 / .ogg, use a .wav"); return; }
    try {
        std::string url = "file://" + UrlEncode(e.path);
        Il2CppObject *req = get((BNM::Structures::Mono::String *)CreateMonoString(url.c_str()), e.type == 1 ? 13 : 14);   // MPEG / OGGVORBIS
        if (!req) { Notify("Couldn't load that file"); return; }
        g_req.set(req);
        send[req]();
        g_reqName = e.name;
        g_reqPath = e.path;
        g_reqAge = 0;
        Notify("Loading %s...", e.name.c_str());
    } catch (...) { g_req.clear(); Notify("Couldn't load that file"); }
}
static void PlaySoundIdx(int i) {
    if (i < 0 || i >= (int)g_sounds.size()) return;
    const SoundEntry &e = g_sounds[(size_t)i];
    if (g_ld.f || Alive(g_req.get())) { Notify("Still loading a sound"); return; }
    auto c = g_clipCache.find(e.path);
    if (c != g_clipCache.end() && Alive(c->second.get())) { PlayClipObj(c->second.get()); Notify("Playing %s", e.name.c_str()); return; }
    if (e.type == 0) StartWav(e); else StartWeb(e);
}
static void CacheClip(const std::string &path, Il2CppObject *clip) {
    if (g_clipCache.size() >= 5) { for (auto &c : g_clipCache) c.second.clear(); g_clipCache.clear(); }
    g_clipCache[path].set(clip);
}
static void SoundLoadTick() {
    if (f_hearAll && g_time >= g_nextHear) { g_nextHear = g_time + 1.0; HearAllApply(); }
    if (g_micRestoreAt > 0 && g_time >= g_micRestoreAt) RestoreMic();
    if (Alive(g_req.get())) {                                   // .mp3 / .ogg
        try {
            static Method<bool> isDone = Class("UnityEngine.Networking", "UnityWebRequest").GetMethod("get_isDone", 0);
            static Method<Il2CppObject *> content = Class("UnityEngine.Networking", "DownloadHandlerAudioClip").GetMethod("GetContent", 1);
            if (++g_reqAge > 900) { CancelLoad(); Notify("Couldn't load that file"); }
            else if (isDone.IsValid() && isDone[g_req.get()]()) {
                Il2CppObject *clip = content.IsValid() ? content(g_req.get()) : nullptr;
                g_req.clear();
                if (clip) { CacheClip(g_reqPath, clip); PlayClipObj(clip); Notify("Playing %s", g_reqName.c_str()); }
                else Notify("Couldn't load that file");
            }
        } catch (...) { g_req.clear(); Notify("Couldn't load that file"); }
    }
    if (!g_ld.f) return;
    Il2CppObject *arr = g_ld.arr.get();
    if (!arr) { CancelLoad(); return; }
    static uint8_t buf[65536 * 4];
    int bytes = g_ld.bits / 8;
    size_t want = g_ld.total - g_ld.done;
    if (want > 65536) want = 65536;
    size_t got = fread(buf, (size_t)bytes, want, g_ld.f);
    float *dst = (float *)((uint8_t *)arr + 0x20) + g_ld.done;
    for (size_t k = 0; k < got; k++) {
        if (g_ld.bits == 16) dst[k] = (int16_t)(buf[k * 2] | buf[k * 2 + 1] << 8) / 32768.0f;
        else if (g_ld.bits == 8) dst[k] = ((float)buf[k] - 128.0f) / 128.0f;
        else { float v; memcpy(&v, buf + k * 4, 4); dst[k] = v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v; }
    }
    g_ld.done += got;
    if (got < want) g_ld.done = g_ld.total;              // short file: the rest stays silent
    if (g_ld.done < g_ld.total) return;
    Il2CppObject *clip = G.audioCreate((BNM::Structures::Mono::String *)CreateMonoString(g_ld.name.c_str()),
                                       (int)(g_ld.total / (size_t)g_ld.ch), g_ld.ch, g_ld.hz, false);
    if (clip) {
        G.audioSetData[clip](arr, 0);
        CacheClip(g_ld.path, clip);
        PlayClipObj(clip);
        Notify("Playing %s", g_ld.name.c_str());
    }
    CancelLoad();
}

// ---- Fun: Disable Boundary ---------------------------------------------------------
// Two things, because the game's own boundary code can't be seen from here:
//  1. Colliders whose names look like map limits (bound, barrier, invisible, kill, border,
//     limit...) are switched off, a few hundred a frame, and back on when you turn this off.
//  2. If something moves you a long way in one frame that this menu didn't do (the game sending
//     you back), you are put back where you were the frame before.
// What it switched off is written to the log, so the name list can be tuned.
static const char *kBoundWords[] = {"bound", "barrier", "invisible", "kill", "border", "limit", "outofmap", "deadzone", "worldedge", "edge", "fence", "oob", "playarea", "blocker", "stopper", "respawn", "boundary", "wall_inv", "mapwall", "outofbounds"};
static Ref g_boundOff[300];
static int g_boundN = 0;
static Ref g_bsArr;
static size_t g_bsIdx = 0, g_bsN = 0;
static double g_nextBoundScan = 0;
static bool g_nbHave = false;
static Vector3 g_nbLast;
static void BoundsRestore() {
    for (int i = 0; i < g_boundN; i++) {
        if (Alive(g_boundOff[i].get())) G.colliderSetEnabled[g_boundOff[i].get()](true);
        g_boundOff[i].clear();
    }
    g_boundN = 0;
    g_bsArr.clear();
    g_bsIdx = g_bsN = 0;
    g_nextBoundScan = 0;
}
static void NoBoundsTick(float dt) {
    Il2CppObject *t = PlayerTransform();
    if (!f_noBounds || !t) {
        if (g_boundN || g_bsArr.get()) BoundsRestore();
        g_nbHave = false;
        g_selfMove = false;
        return;
    }
    // 1) boundary colliders
    if (!g_bsArr.get() && g_time >= g_nextBoundScan && G.tCollider && G.objGetName.IsValid() && G.colliderSetEnabled.IsValid() &&
        (G.findScene.IsValid() || G.findSceneByType.IsValid())) {
        g_nextBoundScan = g_time + 20.0;                  // look again every 20 seconds (new objects)
        auto *arr = (Il2CppObject *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tCollider)
                                                           : G.findSceneByType((Il2CppObject *)G.tCollider, 0));
        if (arr) { g_bsArr.set(arr); g_bsN = *(uintptr_t *)((uint8_t *)arr + 0x18); g_bsIdx = 0; }
    }
    if (Il2CppObject *ao = g_bsArr.get()) {
        auto *arr = (uint8_t *)ao;
        for (int k = 0; k < 250 && g_bsIdx < g_bsN; k++, g_bsIdx++) {
            Il2CppObject *c = ((Il2CppObject **)(arr + 0x20))[g_bsIdx];
            if (!Alive(c) || g_boundN >= 300 || !G.behaviourGetEnabledCollider[c]()) continue;
            auto *nm = G.objGetName[c]();
            if (!nm) continue;
            std::string n = Lower(nm->str());
            bool hit = false;
            for (const char *w : kBoundWords) if (Has(n, w)) hit = true;
            if (!hit) continue;
            G.colliderSetEnabled[c](false);
            g_boundOff[g_boundN++].set(c);
            static int logged = 0;
            if (logged++ < 20) ym::log("leos-menu: boundary collider off: %s", n.c_str());
        }
        if (g_bsIdx >= g_bsN) g_bsArr.clear();
    }
    // 2) being sent back
    Vector3 p = G.trGetPosition[t]();
    if (g_selfMove) { g_selfMove = false; g_nbLast = p; g_nbHave = true; return; }
    if (g_nbHave) {
        float dist = sqrtf(Len2(p - g_nbLast));
        if (dist > 3.0f && dist > 80.0f * dt) {           // much further than you can move in one frame
            Il2CppObject *player = g_player.get();
            if (player && G.teleportTo.IsValid()) G.teleportTo[player](g_nbLast);
            else G.trSetPosition[t](g_nbLast);
            static int logged = 0;
            if (logged++ < 10) ym::log("leos-menu: boundary: undid a %.1f m jump", dist);
            return;
        }
    }
    g_nbLast = p;
    g_nbHave = true;
}

// ---- Visual: Fullbright, Horror ------------------------------------------------------
// Fullbright: flat white ambient light, no fog, and two lights that follow your head: a point light and
// a directional "head light" (a directional light is always used by the render pipeline, a point light
// may not be), so dark rooms and dark corners are lit where you look.
// Horror: darkness that does not depend on how the map is lit: six see-through black shells (inside-out
// spheres, 1.4 m up to 40 m) around your head. Whatever is behind a shell is darkened by it, so the
// further away something is the darker it gets, down to almost black; the nearest shells blink to
// black now and then and a dim red light flickers on your head. Fog, ambient light, the sky and
// the map's own lights are turned down as well. Everything is put back when you turn it off.
static struct {
    bool saved = false, fog = false;
    int mode = 0, fogMode = 0;
    Color amb = Color(0.5f, 0.5f, 0.5f, 1), fogCol = Color(0.5f, 0.5f, 0.5f, 1);
    float fogDen = 0;
    Ref sky;
} g_atmo;
static Ref g_atmoGo, g_atmoLight, g_dimLight[64], g_sunGo, g_sunLight, g_shell[6];
static float g_dimOld[64];
static int g_dimN = 0, g_atmoCur = 0;
static double g_nextAtmo = 0, g_blackoutUntil = 0, g_nextBlackout = 0;
static const float kShellR[6] = {1.4f, 2.8f, 5.0f, 9.0f, 16.0f, 40.0f};
static const float kShellA[6] = {0.16f, 0.26f, 0.32f, 0.38f, 0.46f, 0.55f};

// ---- Horror + flashlight: a flashlight item lights up the dark ----
// While Horror is on, every item with "flashlight" in its name gets a bright warm spot light (the game's own light on it is
// turned up; if it has none, one is added), and while one is near your head the black shells fade so the beam can be seen.
struct FlashL { Ref light, go; float old = 0; bool made = false; };
static std::vector<FlashL> g_flashL;
static bool g_flashHeld = false;
static double g_nextFlash = 0;
static void FlashRestore() {
    for (auto &f : g_flashL) {
        if (f.made) Kill(f.go);
        else if (Alive(f.light.get()) && G.lightSetIntensity.IsValid()) G.lightSetIntensity[f.light.get()](f.old);
        f.light.clear();
    }
    g_flashL.clear();
    g_flashHeld = false;
}
static void FlashlightTick() {
    if (g_time < g_nextFlash || !G.tItem || !G.tLight || !G.objGetName.IsValid() || !G.lightSetIntensity.IsValid()) return;
    g_nextFlash = g_time + 0.5;
    Il2CppObject *head = g_head.get();
    if (!Alive(head)) return;
    Vector3 hp = G.trGetPosition[head]();
    bool held = false;
    uint8_t *arr = ScanAll(G.tItem);
    size_t n = ArrLen(arr);
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ArrAt(arr, i);
        if (!Alive(it)) continue;
        Il2CppObject *go = G.compGameObject[it]();
        auto *nm = go ? G.objGetName[go]() : nullptr;
        if (!nm || !Has(Lower(nm->str()), "flashlight")) continue;
        Il2CppObject *itTr = G.compTransform[it]();
        if (Len2(G.trGetPosition[itTr]() - hp) < 1.4f * 1.4f) held = true;
        bool have = false;
        if (G.getComponentsInChildren.IsValid()) {
            auto *ls = (uint8_t *)G.getComponentsInChildren[go]((Il2CppObject *)G.tLight, true);
            for (size_t k = 0; k < ArrLen(ls) && k < 6; k++) {
                Il2CppObject *l = ArrAt(ls, k);
                if (!Alive(l)) continue;
                have = true;
                bool known = false;
                for (auto &f : g_flashL) if (f.light.get() == l) known = true;
                if (!known) {
                    FlashL f;
                    f.light.set(l);
                    f.old = G.lightGetIntensity.IsValid() ? G.lightGetIntensity[l]() : 1.0f;
                    if (f.old < 1.0f) f.old = 1.0f;                // the Horror dimming may have lowered it already
                    g_flashL.push_back(f);
                }
                G.lightSetIntensity[l](9.0f);
                if (G.lightSetRange.IsValid()) G.lightSetRange[l](35.0f);
            }
        }
        if (!have) {                                         // none on it: add one
            Il2CppObject *lg = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("LeoFlash"));
            Il2CppObject *light = (lg && G.lightSetType.IsValid()) ? G.goAddComponent[lg](G.tLight) : nullptr;
            if (light) {
                G.trSetParent[Tr(lg)](itTr, false);
                G.trSetLocalPosition[Tr(lg)](Vector3(0, 0, 0.1f));
                G.trSetLocalRotation[Tr(lg)](Quaternion(0, 0, 0, 1));
                G.lightSetType[light](0);                    // Spot
                G.lightSetIntensity[light](9.0f);
                if (G.lightSetRange.IsValid()) G.lightSetRange[light](35.0f);
                if (G.lightSetColor.IsValid()) G.lightSetColor[light](Color(1.0f, 0.95f, 0.8f, 1.0f));
                static Method<void> setAngle = Class("UnityEngine", "Light").GetMethod("set_spotAngle", 1);
                if (setAngle.IsValid()) setAngle[light](70.0f);
                FlashL f;
                f.light.set(light);
                f.go.set(lg);
                f.made = true;
                g_flashL.push_back(f);
            } else if (lg) G.destroy(lg);
        }
    }
    g_flashHeld = held;
}

static void DimSceneLights(bool dim) {
    if (!dim) {
        for (int i = 0; i < g_dimN; i++) {
            if (Alive(g_dimLight[i].get()) && G.lightSetIntensity.IsValid()) G.lightSetIntensity[g_dimLight[i].get()](g_dimOld[i]);
            g_dimLight[i].clear();
        }
        g_dimN = 0;
        return;
    }
    if (g_dimN || !G.tLight || !G.lightGetIntensity.IsValid() || !G.lightSetIntensity.IsValid() ||
        !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tLight)
                                                  : G.findSceneByType((Il2CppObject *)G.tLight, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    for (size_t i = 0; i < n && g_dimN < 64; i++) {
        Il2CppObject *l = ((Il2CppObject **)(arr + 0x20))[i];
        if (!Alive(l) || l == g_atmoLight.get() || l == g_sunLight.get()) continue;
        float v = G.lightGetIntensity[l]();
        g_dimOld[g_dimN] = v;
        g_dimLight[g_dimN++].set(l);
        G.lightSetIntensity[l](v * 0.04f);
    }
}
static void KillShells() { for (auto &r : g_shell) Kill(r); }
static void AtmoRestore() {
    if (g_atmoCur == 0 && !g_atmo.saved && !g_dimN) return;
    DimSceneLights(false);
    FlashRestore();
    Kill(g_atmoGo);
    g_atmoLight.clear();
    Kill(g_sunGo);
    g_sunLight.clear();
    KillShells();
    if (g_atmo.saved && G.rsSetAmbientLight.IsValid()) {
        G.rsSetAmbientMode(g_atmo.mode);
        G.rsSetAmbientLight(g_atmo.amb);
        if (G.rsSetFog.IsValid()) G.rsSetFog(g_atmo.fog);
        if (G.rsSetFogColor.IsValid()) G.rsSetFogColor(g_atmo.fogCol);
        if (G.rsSetFogDensity.IsValid()) G.rsSetFogDensity(g_atmo.fogDen);
        if (G.rsSetFogMode.IsValid()) G.rsSetFogMode(g_atmo.fogMode);
        if (G.rsSetSkybox.IsValid()) G.rsSetSkybox(g_atmo.sky.get());
    }
    g_atmo.saved = false;
    g_atmo.sky.clear();
    g_atmoCur = 0;
}
// A light on your head. type: 1 = Directional, 2 = Point.
static Il2CppObject *MakeLight(Ref &goRef, Ref &lightRef, const char *name, Il2CppObject *head, int type) {
    if (Alive(lightRef.get())) return lightRef.get();
    if (!G.tLight || !G.lightSetType.IsValid()) return nullptr;
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString(name));
    Il2CppObject *light = go ? G.goAddComponent[go](G.tLight) : nullptr;
    if (!light) return nullptr;
    G.trSetParent[Tr(go)](head, false);
    G.trSetLocalPosition[Tr(go)](Vector3(0, 0.1f, 0));
    G.trSetLocalRotation[Tr(go)](Quaternion(0, 0, 0, 1));
    G.lightSetType[light](type);
    goRef.set(go);
    lightRef.set(light);
    return light;
}
static void MakeShells(Il2CppObject *head) {
    for (int i = 0; i < 6; i++) {
        if (Alive(g_shell[i].get())) continue;
        Il2CppObject *o = Prim(0, Color(0.015f, 0.0f, 0.0f, kShellA[i]), false);
        if (!o) continue;
        MatTransparent(o, true);
        Il2CppObject *t = Tr(o);
        G.trSetParent[t](head, false);
        G.trSetLocalPosition[t](Vector3(0, 0, 0));
        G.trSetLocalScale[t](Vector3(-kShellR[i] * 2, kShellR[i] * 2, kShellR[i] * 2));   // negative x: the inside is drawn
        g_shell[i].set(o);
    }
}
// Every frame while Horror is on: the red light flickers, the shells pulse and blink to black.
static void HorrorTick() {
    FlashlightTick();
    if (G.rsSetFogDensity.IsValid()) G.rsSetFogDensity(0.26f + 0.08f * sinf((float)g_time * 0.9f));   // the fog breathes
    if (Alive(g_atmoLight.get()) && G.lightSetIntensity.IsValid()) {
        float f = 0.55f + 0.3f * sinf((float)g_time * 11.f) + 0.15f * sinf((float)g_time * 27.f);
        if (Rand01() < 0.02f) f = 0.f;
        G.lightSetIntensity[g_atmoLight.get()](f);
    }
    if (g_time >= g_nextBlackout) {
        g_blackoutUntil = g_time + 0.12 + 0.25 * Rand01();
        g_nextBlackout = g_time + 6.0 + 14.0 * Rand01();
    }
    bool black = g_time < g_blackoutUntil;
    float pulse = 1.0f + 0.1f * sinf((float)g_time * 1.7f);
    for (int i = 0; i < 6; i++) {
        Il2CppObject *o = g_shell[i].get();
        if (!Alive(o)) continue;
        float a = kShellA[i] * pulse * (g_flashHeld ? 0.3f : 1.0f);
        if (black && i < 3) a = 0.97f;
        if (a > 1.f) a = 1.f;
        SetColor(o, Color(0.015f, 0.0f, 0.0f, a));
    }
}
static void AtmoTick() {
    int want = f_fullbright ? 1 : f_horror ? 2 : 0;
    Il2CppObject *head = g_head.get();
    if (want == 0 || !head || !G.rsSetAmbientLight.IsValid() || !G.rsSetAmbientMode.IsValid()) { AtmoRestore(); return; }
    bool change = want != g_atmoCur;
    if (!change && g_atmoCur == 2) HorrorTick();
    if (!change && g_time < g_nextAtmo) return;
    g_nextAtmo = g_time + 0.5;                            // re-applied twice a second, the game may reset it
    if (!g_atmo.saved) {
        g_atmo.saved = true;
        if (G.rsGetAmbientMode.IsValid()) g_atmo.mode = G.rsGetAmbientMode();
        if (G.rsGetAmbientLight.IsValid()) g_atmo.amb = G.rsGetAmbientLight();
        if (G.rsGetFog.IsValid()) g_atmo.fog = G.rsGetFog();
        if (G.rsGetFogColor.IsValid()) g_atmo.fogCol = G.rsGetFogColor();
        if (G.rsGetFogDensity.IsValid()) g_atmo.fogDen = G.rsGetFogDensity();
        if (G.rsGetFogMode.IsValid()) g_atmo.fogMode = G.rsGetFogMode();
        if (G.rsGetSkybox.IsValid()) g_atmo.sky.set(G.rsGetSkybox());
    }
    if (change) {                                         // switching between the two: clear the other one's things
        if (g_atmoCur == 2) { DimSceneLights(false); KillShells(); }
        Kill(g_sunGo);
        g_sunLight.clear();
    }
    Il2CppObject *light = MakeLight(g_atmoGo, g_atmoLight, "LeoLight", head, 2);   // Point
    G.rsSetAmbientMode(3);                                // Flat: one color everywhere
    if (want == 1) {                                      // Fullbright
        G.rsSetAmbientLight(Color(1, 1, 1, 1));
        if (G.rsSetFog.IsValid()) G.rsSetFog(false);
        if (light) {
            if (G.lightSetRange.IsValid()) G.lightSetRange[light](80.0f);
            if (G.lightSetIntensity.IsValid()) G.lightSetIntensity[light](1.4f);
            if (G.lightSetColor.IsValid()) G.lightSetColor[light](Color(1, 1, 1, 1));
        }
        if (Il2CppObject *sun = MakeLight(g_sunGo, g_sunLight, "LeoSun", head, 1)) {   // Directional, looks where you look
            if (G.lightSetIntensity.IsValid()) G.lightSetIntensity[sun](1.5f);
            if (G.lightSetColor.IsValid()) G.lightSetColor[sun](Color(1, 1, 1, 1));
        }
    } else {                                              // Horror
        G.rsSetAmbientLight(Color(0.015f, 0.0f, 0.0f, 1));
        if (G.rsSetFog.IsValid()) G.rsSetFog(true);
        if (G.rsSetFogMode.IsValid()) G.rsSetFogMode(3);    // ExponentialSquared
        if (G.rsSetFogDensity.IsValid()) G.rsSetFogDensity(0.3f);
        if (G.rsSetFogColor.IsValid()) G.rsSetFogColor(Color(0.02f, 0.0f, 0.0f, 1));
        if (G.rsSetSkybox.IsValid()) G.rsSetSkybox((Il2CppObject *)nullptr);
        if (light) {
            if (G.lightSetRange.IsValid()) G.lightSetRange[light](6.0f);
            if (G.lightSetColor.IsValid()) G.lightSetColor[light](Color(0.7f, 0.12f, 0.06f, 1));
        }
        DimSceneLights(true);
        MakeShells(head);
    }
    g_atmoCur = want;
}

// ---- Visual: Snow ----------------------------------------------------------------------
// 80 small white balls fall around you inside a 12 m box that follows you, only you see them.
static constexpr int kFlakes = 80;
static Ref g_flake[kFlakes];
static Vector3 g_flakePos[kFlakes];
static float g_flakeSpeed[kFlakes];
static bool g_snowShown = false;
static void ClearSnow() {
    for (auto &r : g_flake) Kill(r);
    g_snowShown = false;
}
static void SnowTick(float dt) {
    Il2CppObject *head = g_head.get();
    if (!f_snow || !head) { if (g_snowShown) ClearSnow(); return; }
    g_snowShown = true;
    Vector3 hp = G.trGetPosition[head]();
    int made = 0;
    for (int i = 0; i < kFlakes; i++) {
        Il2CppObject *o = g_flake[i].get();
        if (!Alive(o)) {
            if (made >= 10) continue;                       // a few new ones a frame, no hitch
            o = Prim(0, Color(1, 1, 1, 1), false);
            if (!o) continue;
            made++;
            float s = 0.03f + 0.04f * Rand01();
            G.trSetLocalScale[Tr(o)](Vector3(s, s, s));
            g_flake[i].set(o);
            g_flakePos[i] = hp + Vector3((Rand01() - 0.5f) * 12.f, -3.f + Rand01() * 7.f, (Rand01() - 0.5f) * 12.f);
            g_flakeSpeed[i] = 0.6f + Rand01() * 0.9f;
        }
        Vector3 &p = g_flakePos[i];
        p.y -= g_flakeSpeed[i] * dt;
        p.x += sinf((float)g_time * 0.8f + i) * 0.25f * dt;
        p.z += cosf((float)g_time * 0.7f + i * 1.7f) * 0.25f * dt;
        auto wrap = [](float &v, float c, float half) { if (v > c + half) v -= 2 * half; else if (v < c - half) v += 2 * half; };
        wrap(p.x, hp.x, 6.f);
        wrap(p.z, hp.z, 6.f);
        wrap(p.y, hp.y + 0.5f, 3.5f);
        G.trSetPosition[Tr(o)](p);
    }
}


// ---- Update 2: velocity watching, Iron Man, Velocity Fly, positions, circle, delete gun, fireball ------

// What your body's velocity was at the end of the last frame (after everything this menu did). A change
// that shows up at the start of the next frame was made by the game: a landing (Bounce) or a hit
// (No Knockback), not by you or by this menu.
static Vector3 g_vEnd;
static bool g_vHave = false;
static double g_nextBounce = 0;

// True while one of your hands is within 20 cm of something solid in direction d. Pushing off a wall or the
// floor is your own move (the wall is on the opposite side of the push), a hit from someone else is not.
static double g_nkUntil = 0;
static bool HandsPushingOff(Vector3 d) {
    if (!G.raycast.IsValid()) return false;
    for (Il2CppObject *hand : {g_leftHand.get(), g_rightHand.get()}) {
        if (!hand) continue;
        BNM::Structures::Unity::RaycastHit hit{};
        if (G.raycast(G.trGetPosition[hand](), d, &hit, 0.2f, -5)) return true;
    }
    return false;
}
static double g_nextPunch = 0;
static void VelStart() {
    Vector3 v;
    if (!GetVel(v)) { g_vHave = false; return; }
    if (!g_vHave) return;
    Vector3 dv = v - g_vEnd;
    float h = sqrtf(dv.x * dv.x + dv.z * dv.z);
    if (f_bounce && g_time >= g_nextBounce && g_vEnd.y < -3.5f && dv.y > 3.0f && v.y < 1.0f) {
        // You were falling fast and the game just stopped you: throw you back up with most of the speed
        float up = -g_vEnd.y * 0.85f;
        if (up > 16.0f) up = 16.0f;
        SetVel(Vector3(v.x, up, v.z));
        g_nextBounce = g_time + 0.25;
    } else if (f_noKnockback) {
        // A sudden push you didn't make is undone. Knockback often lasts a few frames, so for a third of a
        // second after one is caught even smaller pushes are undone. Pushing off a surface with your hands
        // (the surface is on the other side of the push) is not touched.
        float dl = sqrtf(Len2(dv));
        bool sudden = h > 3.5f || (dv.y > 4.5f && g_vEnd.y > -1.5f);
        bool follow = g_time < g_nkUntil && (h > 1.5f || dv.y > 2.5f);
        if ((sudden || follow) && dl > 0.01f && !HandsPushingOff(dv * (-1.0f / dl))) {
            SetVel(Vector3(g_vEnd.x, dv.y < 0 ? v.y : g_vEnd.y, g_vEnd.z));
            g_nkUntil = g_time + 0.35;
            static int logged = 0;
            if (logged++ < 8) ym::log("leos-menu: no knockback: undid %.1f %.1f %.1f", dv.x, dv.y, dv.z);
        }
    }
}
static void VelEnd() {
    Vector3 v;
    g_vHave = GetVel(v);
    if (g_vHave) g_vEnd = v;
}

// Speed Boost: while you move you keep picking up speed (about x2 a second) up to 16 m/s sideways.
// The jump values are raised in ApplySpeedBoost.
static void SpeedBoostTick(float dt) {
    Vector3 v;
    if (!f_speedBoost || !GetVel(v)) return;
    float sp = sqrtf(v.x * v.x + v.z * v.z);
    if (sp < 1.0f || sp > 16.0f) return;
    float ns = sp * (1.0f + 2.2f * dt);
    if (ns > 16.0f) ns = 16.0f;
    float m = ns / sp;
    SetVel(Vector3(v.x * m, v.y, v.z * m));
}

// Iron Man: hold a trigger and that hand is a thruster. The push goes away from your palm (the way
// Gorilla Tag's Iron Man does it: the hand's right vector), a flame comes out of the palm.
static Ref g_flame[2];
static void BuzzNode(int node, float amp) {
    if (!G.haptic.IsValid()) return;
    XRDevice d = G.deviceAtNode(node);
    if (d.initialized) G.haptic(d.id, 0u, amp, 0.03f);
}
static void IronManTick(const Buttons &in, float dt) {
    Il2CppObject *hands[2] = {g_leftHand.get(), g_rightHand.get()};
    bool fire[2] = {in.ltrig, in.rtrig};
    Vector3 push(0, 0, 0);
    for (int i = 0; i < 2; i++) {
        bool on = f_ironMan && fire[i] && hands[i] && !MenuBusy();
        if (!on) { Show(g_flame[i], false); continue; }
        Vector3 rt = G.trGetRight[hands[i]]();
        Vector3 thrust = i == 0 ? rt * -1.0f : rt;          // away from the palm
        Vector3 palm = thrust * -1.0f;                      // the flame goes this way
        push = push + thrust * (26.0f * dt);
        Il2CppObject *fl = g_flame[i].get();
        if (!Alive(fl)) {
            fl = Prim(0, Color(1.0f, 0.55f, 0.1f, 1), false);
            if (fl) g_flame[i].set(fl);
        }
        if (Alive(fl)) {
            Show(g_flame[i], true);
            float fk = 0.8f + 0.5f * Rand01();
            Il2CppObject *ft = Tr(fl);
            G.trSetPosition[ft](G.trGetPosition[hands[i]]() + palm * (0.07f + 0.09f * fk));
            G.trSetRotation[ft](Quaternion::LookRotation(palm));
            G.trSetLocalScale[ft](Vector3(0.07f * fk, 0.07f * fk, 0.2f * fk));
            SetColor(fl, Rand01() < 0.5f ? Color(1.0f, 0.55f, 0.1f, 1) : Color(1.0f, 0.9f, 0.3f, 1));
        }
        BuzzNode(i == 0 ? 4 : 5, 0.45f);
    }
    if (Len2(push) < 1e-8f) return;
    Vector3 v;
    if (!GetVel(v)) return;
    v = v + push;
    float sp = sqrtf(Len2(v));
    if (sp > 30.0f) v = v * (30.0f / sp);
    SetVel(v);
}

// Velocity Fly: hold A and you fly where you look, getting faster and faster (up to 80 m/s).
// Let go and you keep the speed you had; the game's own physics slows you down.
static float g_vfSpeed = 0;
static void VelFlyTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *player = g_player.get();
    if (!f_velFly || !in.a || !head) { g_vfSpeed = 0; return; }
    if (g_vfSpeed <= 0.0f) {
        g_vfSpeed = 3.0f;
        Vector3 v;
        if (GetVel(v)) { float sp = sqrtf(Len2(v)); if (sp > g_vfSpeed) g_vfSpeed = sp; }
    }
    g_vfSpeed += (0.4f * g_vfSpeed + 6.0f) * dt;
    if (g_vfSpeed > 80.0f) g_vfSpeed = 80.0f;
    SetVel(G.trGetForward[head]() * g_vfSpeed);
    if (player && G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.25f);
}

// Teleport To Closest Player: uses the same list of other players the ESP draws.
static void TpClosest() {
    Il2CppObject *myHead = g_head.get();
    if (!myHead) return;
    EspRefresh();
    Vector3 mine = G.trGetPosition[myHead]();
    Il2CppObject *best = nullptr;
    float bestD = 1e30f;
    for (auto &g : g_rig) {
        Il2CppObject *h = g.head.get();
        if (!Alive(h)) continue;
        float d = Len2(G.trGetPosition[h]() - mine);
        if (d < bestD) { bestD = d; best = h; }
    }
    if (!best) { Notify("No other players found"); return; }
    Vector3 their = G.trGetPosition[best]();
    Vector3 away = Flat(mine - their);                       // land next to them, on the side you came from
    TeleportPlayer(their + away * 0.9f + Vector3(0, -0.6f, 0));
    Notify("Teleported to the closest player");
}

// Save Position / Go To Saved Position: 5 slots (the Position Slot slider).
static Vector3 g_savedPos[5];
static bool g_savedHave[5];
static void SavePos() {
    Vector3 p;
    if (!BodyPos(p)) return;
    g_savedPos[g_posSlot] = p;
    g_savedHave[g_posSlot] = true;
    Notify("Saved position %d", g_posSlot + 1);
}
static void GoToPos() {
    if (!g_savedHave[g_posSlot]) { Notify("Nothing saved in slot %d", g_posSlot + 1); return; }
    TeleportPlayer(g_savedPos[g_posSlot]);
    Notify("Went to position %d", g_posSlot + 1);
}

// Spawn Circle Held MenuItem: a ring around you, 1.6 m out, at waist height.
static void SpawnCircle(int count) {
    Il2CppObject *head = g_head.get();
    if (!head) return;
    Vector3 c = G.trGetPosition[head]() + Vector3(0, -0.5f, 0);
    std::vector<PendingItem> list;
    for (int i = 0; i < count; i++) {
        float a = 6.2831853f * (float)i / (float)count;
        list.push_back({c + Vector3(cosf(a) * 1.6f, 0, sinf(a) * 1.6f), Vector3(0, 0, 0)});
    }
    QueueItems(list);
}
static void ArmCircle() { if (LeftHandItem()) SpawnCircle(kCircleCounts[g_circleSlider]); }

// Delete MenuItem Gun: the item closest to where your right hand points (within a cone that gets wider
// the further away it is). It calls the game's own destroy/delete method on the item if the MenuItem class
// has one (which one is written to the log); if not, the item is only removed on your side.
static double g_nextDelete = 0;
static Method<void> s_itemDelete;
static bool s_deleteSearched = false;
static void DeleteAimedItem(Vector3 start, Vector3 dir) {
    if (g_time < g_nextDelete || !G.tItem || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return;
    g_nextDelete = g_time + 0.3;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem)
                                                  : G.findSceneByType((Il2CppObject *)G.tItem, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    Il2CppObject *best = nullptr;
    float bestT = 1e30f;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
        if (!Alive(it)) continue;
        Vector3 to = G.trGetPosition[G.compTransform[it]()]() - start;
        float t = Dot(to, dir);
        if (t < 0.15f || t > 80.0f) continue;
        Vector3 perp = to - dir * t;
        float rad = 0.3f + 0.02f * t;
        if (Len2(perp) > rad * rad || t >= bestT) continue;
        bestT = t;
        best = it;
    }
    if (!best) { Notify("No item there"); return; }
    if (!s_deleteSearched) {
        s_deleteSearched = true;
        Class item("", "MenuItem", Image("Assembly-CSharp.dll"));
        if (item.IsValid())
            for (auto &m : item.GetMethods(false)) {
                auto *mi = m.GetInfo();
                if (!mi || !mi->name || mi->parameters_count != 0) continue;
                std::string nm = Lower(mi->name);
                if (nm == "ondestroy") continue;
                if (Has(nm, "destroy") || Has(nm, "delete") || Has(nm, "despawn")) {
                    s_itemDelete = m;
                    ym::log("leos-menu: delete gun uses MenuItem.%s", mi->name);
                    break;
                }
            }
        if (!s_itemDelete.IsValid()) ym::log("leos-menu: delete gun: MenuItem has no destroy method, deleting locally");
    }
    bool sent = false;
    if (s_itemDelete.IsValid()) {
        try { s_itemDelete[best](); sent = true; }
        catch (BNM::IL2CPP::Il2CppExceptionWrapper &) {}
    }
    if (!sent) {
        Il2CppObject *go = G.compGameObject[best]();
        if (go) { G.goSetActive[go](false); G.destroy(go); }
    }
    Notify(sent ? "MenuItem deleted" : "MenuItem removed (only for you)");
}

// Fireball: right trigger sends a flare-ammo item out of your hand, forward, through the game's own
// item message (so everyone sees the flare), plus an orange ball only you see. The flare's item key is
// found in the room's items (any item with "flare" in its key), or from a flare you hold in your left hand.
static char g_flareKey[64] = "";
static bool g_flareLogged = false;
static bool FindFlareKey() {
    if (g_flareKey[0]) return true;
    if (G.tItem && G.itemKey.IsValid() && (G.findScene.IsValid() || G.findSceneByType.IsValid())) {
        auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem)
                                                      : G.findSceneByType((Il2CppObject *)G.tItem, 0));
        size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
        int logged = 0;
        for (size_t i = 0; i < n; i++) {
            Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
            if (!Alive(it)) continue;
            auto *key = G.itemKey[it]();
            if (!key) continue;
            std::string k = key->str();
            if (!g_flareLogged && logged++ < 40) ym::log("leos-menu: item in room: %s", k.c_str());
            if (Has(Lower(k), "flare")) { snprintf(g_flareKey, sizeof g_flareKey, "%s", k.c_str()); break; }
        }
        g_flareLogged = true;
    }
    if (!g_flareKey[0] && Has(Lower(g_itemKey), "flare")) snprintf(g_flareKey, sizeof g_flareKey, "%s", g_itemKey);
    if (g_flareKey[0]) ym::log("leos-menu: flare item key is %s", g_flareKey);
    return g_flareKey[0] != 0;
}
struct Fireball { Ref obj; Vector3 vel; double dieAt = 0; bool live = false; };
static Fireball g_fb[6];
static bool g_fbTrig = false;
static void FbDrive(float dt);
static void FireballTick(const Buttons &in, float dt) {
    FbDrive(dt);
    for (auto &b : g_fb) {
        if (!b.live) continue;
        Il2CppObject *o = b.obj.get();
        if (g_time >= b.dieAt || !Alive(o)) { Show(b.obj, false); b.live = false; continue; }
        Il2CppObject *t = Tr(o);
        G.trSetPosition[t](G.trGetPosition[t]() + b.vel * dt);
        float k = 0.2f + 0.1f * Rand01();
        G.trSetLocalScale[t](Vector3(k, k, k));
        SetColor(o, Rand01() < 0.5f ? Color(1.0f, 0.4f, 0.05f, 1) : Color(1.0f, 0.8f, 0.2f, 1));
    }
    bool press = in.rtrig && !g_fbTrig;
    g_fbTrig = in.rtrig;
    Il2CppObject *hand = g_rightHand.get();
    if (!f_fireball || !press || in.rgrip || MenuBusy() || !hand || g_time < g_nextItem) return;
    if (!FindFlareKey()) {
        if (LeftHandItem() && Has(Lower(g_itemKey), "flare")) FindFlareKey();
        if (!g_flareKey[0]) { Notify("Hold flare ammo in your left hand once"); return; }
    }
    g_nextItem = g_time + 0.5;
    Vector3 f = G.trGetForward[hand](), at = G.trGetPosition[hand]() + f * 0.4f;
    PendingItem pi{at, f * kFireSpeeds[g_fireSpeed]};
    pi.track = 1;
    snprintf(pi.key, sizeof pi.key, "%s", g_flareKey);
    g_itemQueue.push_back(pi);
    for (auto &b : g_fb) {
        if (b.live) continue;
        if (!Alive(b.obj.get())) {
            Il2CppObject *o = Prim(0, Color(1.0f, 0.5f, 0.1f, 1), false);
            if (!o) break;
            b.obj.set(o);
        }
        G.trSetPosition[Tr(b.obj.get())](at);
        Show(b.obj, true);
        b.vel = f * kFireSpeeds[g_fireSpeed];
        b.dieAt = g_time + 1.6;
        b.live = true;
        break;
    }
}

// ---- Overpowered: item sliders and the cotton limit ---------------------------------
// These work on the game's own classes, which are only known at run time. They look at the numbers
// inside the item(s) in your hands (and the cotton limit anywhere in the game) by NAME:
//   MenuItem Knockback  - number fields with "knock" (or push / launch / hit force / impulse) in the name
//   Bullet Amount   - fields like bulletCount, bulletsPerShot, pellets, projectileCount
//   Bullet Spread   - fields like spread, spreadAngle, inaccuracy, scatter
//   Cotton limit    - max / limit / cap fields with "cotton" in the name
// The original value is kept and put back when the slider goes back to normal, you let go of the item
// or you turn the mod off. What was found is written to the log ("leos-menu: tweak ..."); a "scan" part
// of the log lists every field and method of interest in the game so the names can be tuned.
static void (*p_fieldSet)(void *, void *, void *);
static void (*p_fieldStaticGet)(void *, void *);
static void (*p_fieldStaticSet)(void *, void *);
static int (*p_fieldFlags)(void *);
static void *(*p_objClass)(void *);
static void *(*p_classMethods)(void *, void **);
static const char *(*p_methodName)(const void *);
static bool EnsureFieldApi() {
    static bool tried = false;
    if (!tried) {
        tried = true;
        auto R = ym::api()->resolve;
        p_classFields = (decltype(p_classFields))R("il2cpp_class_get_fields");
        p_fieldName = (decltype(p_fieldName))R("il2cpp_field_get_name");
        p_fieldType = (decltype(p_fieldType))R("il2cpp_field_get_type");
        p_fieldGet = (decltype(p_fieldGet))R("il2cpp_field_get_value");
        p_classParent = (decltype(p_classParent))R("il2cpp_class_get_parent");
        p_fieldSet = (decltype(p_fieldSet))R("il2cpp_field_set_value");
        p_fieldStaticGet = (decltype(p_fieldStaticGet))R("il2cpp_field_static_get_value");
        p_fieldStaticSet = (decltype(p_fieldStaticSet))R("il2cpp_field_static_set_value");
        p_fieldFlags = (decltype(p_fieldFlags))R("il2cpp_field_get_flags");
        p_objClass = (decltype(p_objClass))R("il2cpp_object_get_class");
        p_classMethods = (decltype(p_classMethods))R("il2cpp_class_get_methods");
        p_methodName = (decltype(p_methodName))R("il2cpp_method_get_name");
        if (!p_fieldSet || !p_objClass) ym::log("leos-menu: item tweaks can't read class fields in this build");
    }
    return p_classFields && p_fieldName && p_fieldType && p_fieldGet && p_fieldSet && p_objClass;
}
static bool HasAny(const std::string &n, std::initializer_list<const char *> ws) {
    for (const char *w : ws) if (Has(n, w)) return true;
    return false;
}
struct FInfo { void *f; std::string n; char t; };      // t: f float, i int, o other
// The instance fields of a game class and the game classes it is built on (stops at engine classes).
static void FieldsOf(void *klass, std::vector<FInfo> &out) {
    int guard = 0;
    for (void *k = klass; k && guard < 8; k = p_classParent ? p_classParent(k) : nullptr, guard++) {
        auto *kc = (BNM::IL2CPP::Il2CppClass *)k;
        if (kc->namespaze && kc->namespaze[0]) break;
        void *iter = nullptr;
        while (void *f = p_classFields(k, &iter)) {
            const char *fn = p_fieldName(f);
            if (!fn) continue;
            if (p_fieldFlags && (p_fieldFlags(f) & 0x50)) continue;       // static or const
            std::string tn = TypeName((const BNM::IL2CPP::Il2CppType *)p_fieldType(f));
            out.push_back({f, Lower(fn), tn == "System.Single" ? 'f' : tn == "System.Int32" ? 'i' : 'o'});
        }
    }
}
static bool IsKnockName(const std::string &n) {
    return HasAny(n, {"knock", "pushforce", "launchforce", "hitforce", "impulse"}) &&
           !HasAny(n, {"resist", "immune", "reduc", "cooldown", "time", "duration", "delay"});
}
// The (up to) two items in your hands: held, and within 40 cm of a hand.
static void HeldItemsNow(Il2CppObject *out[2]) {
    out[0] = out[1] = nullptr;
    Il2CppObject *hands[2] = {g_leftHand.get(), g_rightHand.get()};
    if (!G.tItem || !G.itemHeld.IsValid() || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem)
                                                  : G.findSceneByType((Il2CppObject *)G.tItem, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    float bestD[2] = {0.4f * 0.4f, 0.4f * 0.4f};
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
        if (!Alive(it) || !G.itemHeld[it]()) continue;
        Vector3 ip = G.trGetPosition[G.compTransform[it]()]();
        for (int h = 0; h < 2; h++) {
            if (!hands[h]) continue;
            float d = Len2(ip - G.trGetPosition[hands[h]]());
            if (d < bestD[h]) { bestD[h] = d; out[h] = it; }
        }
    }
}
struct Tweak { Ref comp; void *field = nullptr; int cat = 0; bool isInt = false; float oldF = 0; int oldI = 0; };
static std::vector<Tweak> g_tw;
static double g_nextTweak = 0;
static float TweakValue(int cat, float old) { (void)cat; return old * kKnockMuls[g_knockSlider]; }
static void TweakWrite(Tweak &t, Il2CppObject *c, float value) {
    if (t.isInt) {
        double d = value;
        if (d > 2e9) d = 2e9;
        if (d < -2e9) d = -2e9;
        int iv = (int)d;
        p_fieldSet(c, t.field, &iv);
    } else {
        p_fieldSet(c, t.field, &value);
    }
}
static void TweakRestore(Tweak &t) {
    Il2CppObject *c = t.comp.get();
    if (Alive(c)) { if (t.isInt) p_fieldSet(c, t.field, &t.oldI); else p_fieldSet(c, t.field, &t.oldF); }
    t.comp.clear();
}
static void RestoreAllTweaks() {
    for (auto &t : g_tw) TweakRestore(t);
    g_tw.clear();
}
static void ItemTweakTick() {
    bool wantKnock = g_knockSlider != kKnockDefault;
    if (!wantKnock) { if (!g_tw.empty()) RestoreAllTweaks(); return; }
    if (g_time < g_nextTweak || !EnsureFieldApi()) return;
    g_nextTweak = g_time + 0.5;
    static BNM::MonoType *tMono = Class("UnityEngine", "MonoBehaviour").GetMonoType();
    Il2CppObject *held[2];
    HeldItemsNow(held);
    std::vector<Il2CppObject *> live;                   // every component under the items in your hands now
    for (Il2CppObject *item : held) {
        if (!item || !G.getComponentsInChildren.IsValid() || !tMono) continue;
        Il2CppObject *go = G.compGameObject[item]();
        auto *arr = go ? (uint8_t *)G.getComponentsInChildren[go]((Il2CppObject *)tMono, true) : nullptr;
        size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
        for (size_t i = 0; i < n && i < 40; i++) {
            Il2CppObject *c = ((Il2CppObject **)(arr + 0x20))[i];
            if (!Alive(c)) continue;
            live.push_back(c);
            void *kl = p_objClass(c);
            if (!kl) continue;
            std::vector<FInfo> fs;
            FieldsOf(kl, fs);
            for (auto &fi : fs) {
                if (fi.t == 'o') continue;
                int cat = (wantKnock && IsKnockName(fi.n)) ? 1 : 0;
                if (!cat) continue;
                Tweak *tw = nullptr;
                for (auto &t : g_tw) if (t.comp.get() == c && t.field == fi.f) tw = &t;
                if (!tw) {
                    if (g_tw.size() >= 64) continue;
                    Tweak t;
                    t.comp.set(c);
                    t.field = fi.f;
                    t.cat = cat;
                    t.isInt = fi.t == 'i';
                    if (t.isInt) { p_fieldGet(c, fi.f, &t.oldI); t.oldF = (float)t.oldI; }
                    else p_fieldGet(c, fi.f, &t.oldF);
                    g_tw.push_back(t);
                    tw = &g_tw.back();
                    ym::log("leos-menu: tweak %s.%s (was %.2f)", ((BNM::IL2CPP::Il2CppClass *)kl)->name, fi.n.c_str(), tw->oldF);
                }
                tw->cat = cat;
                TweakWrite(*tw, c, TweakValue(cat, tw->oldF));
            }
        }
    }
    // Put back what is no longer wanted: slider back to normal, or the item left your hand
    for (size_t i = 0; i < g_tw.size();) {
        Tweak &t = g_tw[i];
        bool wanted = t.cat == 1 && wantKnock;
        bool still = false;
        for (Il2CppObject *c : live) if (c == t.comp.get()) still = true;
        if (wanted && still) { i++; continue; }
        TweakRestore(t);
        g_tw.erase(g_tw.begin() + (long)i);
    }
}

// Remove Cotton Limit: number fields about the cotton limit are set very high and put back when you turn it off.
// A field counts when its name has "cotton" and a limit word (max / limit / cap / allow / quota / budget), or when it
// sits in a class with "cotton" in its name and has a limit word. int, uint, short, ushort, byte, long, float and
// double fields are handled (the old version only did int and float). Static fields are set once; fields of objects
// in the room (MonoBehaviours / ScriptableObjects) are set every 2 seconds so new objects get it too. Every field it
// finds is written to the log as "leos-menu: cotton field ...", and the number of changed fields as
// "cotton limit fields found" - if that says 0 the limit lives somewhere this can't reach.
union CottonVal { float f; double d; int32_t i; uint32_t u; int64_t l; int16_t s; uint16_t S; uint8_t b; };
struct CottonField { BNM::MonoType *type = nullptr; void *f = nullptr; char kind = 'i'; bool isStatic = false, unity = false, saved = false; double oldV = 0; };
static std::vector<CottonField> g_cotton;
static bool g_cottonScanned = false, g_cottonApplied = false;
static double g_nextCotton = 0;
static bool IsUnityClass(void *k) {
    for (int g = 0; k && g < 10; g++, k = p_classParent ? p_classParent(k) : nullptr) {
        const char *nm = ((BNM::IL2CPP::Il2CppClass *)k)->name;
        if (nm && (!strcmp(nm, "MonoBehaviour") || !strcmp(nm, "ScriptableObject"))) return true;
    }
    return false;
}
static char CottonKind(const std::string &tn) {
    if (tn == "System.Single") return 'f';
    if (tn == "System.Double") return 'd';
    if (tn == "System.Int32") return 'i';
    if (tn == "System.UInt32") return 'u';
    if (tn == "System.Int64") return 'l';
    if (tn == "System.Int16") return 's';
    if (tn == "System.UInt16") return 'S';
    if (tn == "System.Byte") return 'b';
    return 0;
}
static double CottonBig(char k) {
    switch (k) {
    case 'f': case 'd': return 1e9;
    case 'i': case 'u': return 1000000000.0;
    case 'l': return 1e12;
    case 's': return 32000.0;
    case 'S': return 65000.0;
    default: return 250.0;
    }
}
static double CottonGet(CottonField &cf, Il2CppObject *obj) {
    CottonVal v;
    memset(&v, 0, sizeof v);
    if (cf.isStatic) p_fieldStaticGet(cf.f, &v); else if (obj) p_fieldGet(obj, cf.f, &v);
    switch (cf.kind) {
    case 'f': return v.f;
    case 'd': return v.d;
    case 'i': return v.i;
    case 'u': return v.u;
    case 'l': return (double)v.l;
    case 's': return v.s;
    case 'S': return v.S;
    default: return v.b;
    }
}
static void CottonPut(CottonField &cf, Il2CppObject *obj, double x) {
    CottonVal v;
    memset(&v, 0, sizeof v);
    switch (cf.kind) {
    case 'f': v.f = (float)x; break;
    case 'd': v.d = x; break;
    case 'i': v.i = x > 2147483647.0 ? 2147483647 : x < -2147483648.0 ? INT32_MIN : (int32_t)x; break;
    case 'u': v.u = x > 4294967295.0 ? 4294967295u : x < 0 ? 0u : (uint32_t)x; break;
    case 'l': v.l = (int64_t)x; break;
    case 's': v.s = x > 32767.0 ? (int16_t)32767 : x < -32768.0 ? (int16_t)-32768 : (int16_t)x; break;
    case 'S': v.S = x > 65535.0 ? (uint16_t)65535 : x < 0 ? (uint16_t)0 : (uint16_t)x; break;
    default: v.b = x > 255.0 ? (uint8_t)255 : x < 0 ? (uint8_t)0 : (uint8_t)x; break;
    }
    if (cf.isStatic) p_fieldStaticSet(cf.f, &v); else if (obj) p_fieldSet(obj, cf.f, &v);
}
static void CottonScan() {
    g_cottonScanned = true;
    if (!EnsureFieldApi()) return;
    int logged = 0;
    for (auto &c : Image("Assembly-CSharp.dll").GetClasses(false)) {
        auto *kl = c.GetClass();
        if (!kl || !kl->name) continue;
        bool cottonClass = Has(Lower(kl->name), "cotton");
        void *iter = nullptr;
        while (void *f = p_classFields(kl, &iter)) {
            const char *fnm = p_fieldName(f);
            if (!fnm) continue;
            std::string n = Lower(fnm);
            if (!cottonClass && !Has(n, "cotton")) continue;
            std::string tn = TypeName((const BNM::IL2CPP::Il2CppType *)p_fieldType(f));
            int flags = p_fieldFlags ? p_fieldFlags(f) : 0;
            if (logged++ < 60) ym::log("leos-menu: cotton field %s.%s (%s, flags 0x%x)", kl->name, fnm, tn.c_str(), flags);
            char kind = CottonKind(tn);
            if (!kind || (flags & 0x40)) continue;                               // not a number, or a const
            if (!HasAny(n, {"max", "limit", "cap", "allow", "quota", "budget", "ceil"}) ||
                HasAny(n, {"min", "cooldown", "time", "delay", "speed"})) continue;
            CottonField cf;
            cf.type = c.GetMonoType();
            cf.f = f;
            cf.kind = kind;
            cf.isStatic = (flags & 0x10) != 0;
            cf.unity = IsUnityClass(kl);
            g_cotton.push_back(cf);
        }
    }
    ym::log("leos-menu: cotton limit fields found: %d", (int)g_cotton.size());
}
static void CottonApply(bool on) {
    if (!EnsureFieldApi() || !p_fieldStaticGet || !p_fieldStaticSet) return;
    for (auto &cf : g_cotton) {
        if (cf.isStatic) {
            if (!cf.saved) { cf.oldV = CottonGet(cf, nullptr); cf.saved = true; }
            CottonPut(cf, nullptr, on ? CottonBig(cf.kind) : cf.oldV);
        } else if (cf.unity && cf.type && (G.findScene.IsValid() || G.findSceneByType.IsValid())) {
            try {
                auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)cf.type)
                                                              : G.findSceneByType((Il2CppObject *)cf.type, 0));
                size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
                for (size_t i = 0; i < n && i < 64; i++) {
                    Il2CppObject *o = ((Il2CppObject **)(arr + 0x20))[i];
                    if (!Alive(o)) continue;
                    if (!cf.saved) { cf.oldV = CottonGet(cf, o); cf.saved = true; }
                    CottonPut(cf, o, on ? CottonBig(cf.kind) : cf.oldV);
                }
            } catch (BNM::IL2CPP::Il2CppExceptionWrapper &) {}
        }
    }
    g_cottonApplied = on;
}
static void CottonToggled() { g_nextCotton = 0; }
static void CottonTick() {
    if (!f_noCotton) { if (g_cottonApplied) CottonApply(false); return; }
    if (g_time < g_nextCotton) return;
    g_nextCotton = g_time + 2.0;
    if (!g_cottonScanned) {
        CottonScan();
        if (g_cotton.empty()) Notify("No cotton limit found (see log)");
    }
    CottonApply(true);
}

// A one-time look through the game's classes, a few every frame, written to the log as
// "leos-menu: scan ...": every field/method whose name has knock / bullet / projectile / spread / ammo /
// flare / cotton / shoot / launch in it, and everything inside MasterPlayer and MenuItem. Nothing is changed.
static const char *kDumpWords[] = {"knock", "bullet", "projectile", "spread", "ammo", "flare", "cotton", "shoot", "launch"};
static bool DumpWord(const std::string &n) {
    for (const char *w : kDumpWords) if (Has(n, w)) return true;
    return false;
}
static void DumpTick() {
    static std::vector<BNM::Class> classes;
    static size_t idx = 0;
    static bool init = false;
    static int lines = 0;
    if (lines >= 450 || !EnsureFieldApi()) return;
    if (!init) {
        init = true;
        classes = Image("Assembly-CSharp.dll").GetClasses(false);
        ym::log("leos-menu: scan: %d classes", (int)classes.size());
    }
    for (int step = 0; step < 40 && idx < classes.size() && lines < 450; step++, idx++) {
        auto *kl = classes[idx].GetClass();
        if (!kl || !kl->name) continue;
        std::string cn = Lower(kl->name);
        bool whole = cn == "masterplayer" || cn == "item";
        if (DumpWord(cn)) { ym::log("leos-menu: scan class %s", kl->name); lines++; }
        void *iter = nullptr;
        while (void *f = p_classFields(kl, &iter)) {
            const char *fnm = p_fieldName(f);
            if (!fnm || !(whole || DumpWord(Lower(fnm)))) continue;
            ym::log("leos-menu: scan field %s.%s (%s)", kl->name, fnm, TypeName((const BNM::IL2CPP::Il2CppType *)p_fieldType(f)).c_str());
            lines++;
        }
        if (!p_classMethods || !p_methodName) continue;
        void *miter = nullptr;
        while (void *m = p_classMethods(kl, &miter)) {
            const char *mn = p_methodName(m);
            if (!mn || !(whole || DumpWord(Lower(mn)))) continue;
            ym::log("leos-menu: scan method %s.%s", kl->name, mn);
            lines++;
        }
    }
}

// ============================================================================================
// ---- Update 3 ---------------------------------------------------------------------------------
// ============================================================================================

static uint8_t *ScanAll(BNM::MonoType *t) {                    // every object of a type in the scene (a game array)
    if (!t) return nullptr;
    if (G.findScene.IsValid()) return (uint8_t *)G.findScene((Il2CppObject *)t);
    if (G.findSceneByType.IsValid()) return (uint8_t *)G.findSceneByType((Il2CppObject *)t, 0);
    return nullptr;
}
static size_t ArrLen(uint8_t *a) { return a ? *(uintptr_t *)(a + 0x18) : 0; }
static Il2CppObject *ArrAt(uint8_t *a, size_t i) { return ((Il2CppObject **)(a + 0x20))[i]; }

static void ThemeBlack() { g_theme = 8; Notify("Theme: Black"); }
static void ThemeDarkBlue() { g_theme = 9; Notify("Theme: Dark Blue"); }
static void TextDefault() { g_textCol = 0; g_dirty = true; Notify("Text: white"); }
static void TextRed() { g_textCol = 1; g_dirty = true; Notify("Text: red"); }
static void TextWhite() { g_textCol = 2; g_dirty = true; Notify("Text: black"); }
static void TextBlack() { g_textCol = 2; g_dirty = true; Notify("Text: black"); }
static void TextPurple() { g_textCol = 3; g_dirty = true; Notify("Text: purple"); }
static void TextPurpleWhite() { g_textCol = 4; g_dirty = true; Notify("Text: purple/white"); }
static void TextBlueWhite() { g_textCol = 5; g_dirty = true; Notify("Text: blue/white"); }
static void ThemeDark() { g_theme = 3; Notify("Theme: Dark Gray"); }
static void ThemePurple() { g_theme = 5; Notify("Theme: Purple"); }
static void ThemeYellow() { g_theme = 6; Notify("Theme: Yellow"); }
static void ThemeWhite() { g_theme = 7; Notify("Theme: White"); }

// ---- Smooth Noclip Fly --------------------------------------------------------------------
// Same as Noclip Fly (hold A, you pass through walls: NoclipTick switches your collisions off), but it steers your
// velocity toward where you look instead of moving your position every frame, so it is smooth, not stuttery.
// It speeds up and slows down gently, and eases to a stop when you let go.
static double g_sfStop = 0;
static bool g_sfWas = false;
static void SmoothFlyTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *player = g_player.get();
    Vector3 v;
    if (!f_smoothFly || !head || !GetVel(v)) { g_sfWas = false; return; }
    if (in.a) {
        Vector3 target = G.trGetForward[head]() * (kNoclipSpeeds[g_smoothSpeed] * SizeMul());
        float k = 1.0f - expf(-7.0f * dt);
        SetVel(v + (target - v) * k);
        if (player && G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.25f);
        g_sfWas = true;
    } else if (g_sfWas) {
        g_sfWas = false;
        g_sfStop = g_time + 0.5;
    } else if (g_time < g_sfStop) {                      // eases out instead of stopping dead
        float k = expf(-6.0f * dt);
        SetVel(v * k);
        if (player && G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.25f);
    }
}

// ---- Spider Yeeps ----------------------------------------------------------------------------
// Web swinging, no grappler item needed. Hold a trigger: a web shoots from that hand to where it points. While you
// hold it you are pulled toward the web and the rope can't get longer, so you swing. Let go to release.
// (Right trigger is ignored while right grip is held, and left trigger while a rig mod is picked.)
struct Web { bool on = false; Vector3 anchor; float len = 0; Ref go, line; };
static Web g_web[2];
static void WebOff(Web &w) { w.on = false; Kill(w.go); w.line.clear(); }
static void SpiderTick(const Buttons &in, float dt) {
    Il2CppObject *hands[2] = {g_leftHand.get(), g_rightHand.get()};
    bool held[2] = {in.ltrig && g_dance < 0, in.rtrig && !in.rgrip};
    Vector3 v, body;
    bool ok = f_spider && GetVel(v) && BodyPos(body);
    bool any = false;
    for (int i = 0; i < 2; i++) {
        Web &w = g_web[i];
        bool want = ok && held[i] && hands[i] && !MenuBusy();
        if (!want) { if (w.on || w.go.get()) WebOff(w); continue; }
        Vector3 hp = G.trGetPosition[hands[i]]();
        if (!w.on) {                                     // a new press: where does the web hit?
            if (!G.raycast.IsValid()) continue;
            Vector3 d = G.trGetForward[hands[i]]();
            BNM::Structures::Unity::RaycastHit hit{};
            if (!G.raycast(hp + d * 0.1f, d, &hit, 70.0f, -5)) continue;
            Ref go;
            if (!MakeLine(Color(1, 1, 1, 1), 0.012f, "LeoWeb", go, w.line)) continue;
            G.lrSetPositionCount[w.line.get()](2);
            w.go = go;
            w.anchor = hit.point;
            w.len = sqrtf(Len2(w.anchor - body));
            w.on = true;
            BuzzNode(i == 0 ? 4 : 5, 0.5f);
        }
        if (Il2CppObject *line = w.line.get()) {
            G.lrSetPosition[line](0, hp);
            G.lrSetPosition[line](1, w.anchor);
        }
        Vector3 to = w.anchor - body;
        float dist = sqrtf(Len2(to));
        if (dist < 0.3f) continue;
        Vector3 dir = to * (1.0f / dist);                // toward the anchor
        v = v + dir * (20.0f * dt);                      // reel in
        if (w.len > 1.5f) w.len -= 1.2f * dt;
        if (dist > w.len) {                              // the rope is tight: remove the speed that stretches it
            float away = -Dot(v, dir);
            if (away > 0) v = v + dir * away;
        }
        any = true;
    }
    if (!any) return;
    float sp = sqrtf(Len2(v));
    if (sp > 30.0f) v = v * (30.0f / sp);
    SetVel(v);
}

// ---- Dash / Teleport Dash ------------------------------------------------------------------------
// Left thumbstick click = Dash (launched where you look). Right thumbstick click = Teleport Dash (a hop forward).
static bool g_dashPrev = false, g_tpDashPrev = false;
static double g_nextDash = 0;
static void DashTick(const Buttons &in) {
    bool d = in.lstick && !g_dashPrev, t = in.rstick && !g_tpDashPrev;
    g_dashPrev = in.lstick;
    g_tpDashPrev = in.rstick;
    Il2CppObject *head = g_head.get();
    if (!head || g_time < g_nextDash) return;
    if (f_dash && d) {
        g_nextDash = g_time + 0.25;
        SetVel(G.trGetForward[head]() * kDashSpeeds[g_dashSpeed]);
        BuzzNode(4, 0.5f);
    } else if (f_tpDash && t) {
        Vector3 body;
        if (!BodyPos(body)) return;
        g_nextDash = g_time + 0.25;
        Vector3 f = G.trGetForward[head](), start = G.trGetPosition[head]();
        float dist = kTpDists[g_tpDist];
        BNM::Structures::Unity::RaycastHit hit{};
        if (G.raycast.IsValid() && G.raycast(start, f, &hit, dist + 0.4f, -5)) {   // don't land inside a wall
            dist = sqrtf(Len2(hit.point - start)) - 0.5f;
            if (dist < 0.f) dist = 0.f;
        }
        TeleportPlayer(body + f * dist);
        BuzzNode(5, 0.5f);
    }
}

// ---- Rewind ----------------------------------------------------------------------------------------------------------
// While the mod is on your body position is recorded 20 times a second (the last 60 seconds). Hold the right trigger and you
// are carried back along that path at 3x speed (what you went back over is forgotten); let go and recording goes on from
// there. Turning the mod off deletes the recording.
static std::vector<Vector3> g_rec;
static double g_nextRec = 0;
static float g_rewF = 0;
static void RewindToggled() {
    g_rec.clear();
    g_rewinding = false;
    g_recording = f_rewind;
    g_nextRec = 0;
    if (f_rewind) {
        Vector3 p;
        if (BodyPos(p)) g_rec.push_back(p);
        Notify("Recording. Hold right trigger to rewind");
    } else Notify("Recording removed");
}
static void RewindTick(const Buttons &in, float dt) {
    if (!f_rewind) { if (!g_rec.empty() || g_recording || g_rewinding) { g_rec.clear(); g_recording = g_rewinding = false; } return; }
    if (in.rtrig && !MenuBusy() && g_rec.size() >= 2) {
        if (!g_rewinding) { g_rewinding = true; g_recording = false; g_rewF = (float)g_rec.size() - 1.0f; }
        g_rewF -= dt / 0.05f * 3.0f;
        if (g_rewF < 0.0f) g_rewF = 0.0f;
        size_t i0 = (size_t)g_rewF;
        if (i0 + 1 >= g_rec.size()) i0 = g_rec.size() - 2;
        float k = g_rewF - (float)i0;
        if (k > 1.0f) k = 1.0f;
        TeleportPlayer(g_rec[i0] + (g_rec[i0 + 1] - g_rec[i0]) * k);
        StopMoving();
        if (g_rec.size() > i0 + 2) g_rec.resize(i0 + 2);
        return;
    }
    if (g_rewinding) { g_rewinding = false; g_nextRec = 0; }
    g_recording = true;
    if (g_time >= g_nextRec) {
        g_nextRec = g_time + 0.05;
        Vector3 p;
        if (BodyPos(p)) { g_rec.push_back(p); if (g_rec.size() > 1200) g_rec.erase(g_rec.begin()); }
    }
}

// ---- Air Swim -------------------------------------------------------------------------------------------------
// You float (no gravity), the air slows you down like water, and while you hold a grip, sweeping that hand
// (relative to your head) pushes you the opposite way.
static Vector3 g_swimHand[2];
static bool g_swimHave[2];
static void AirSwimTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *player = g_player.get();
    Vector3 v;
    if (!f_airSwim || !head || dt < 1e-4f || !GetVel(v)) { g_swimHave[0] = g_swimHave[1] = false; return; }
    if (player && G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.25f);
    v = v * expf(-1.6f * dt);                            // drag
    bool held[2] = {in.lgrip, in.rgrip};
    Il2CppObject *hands[2] = {g_leftHand.get(), g_rightHand.get()};
    Vector3 hp = G.trGetPosition[head]();
    for (int i = 0; i < 2; i++) {
        if (!hands[i]) continue;
        Vector3 loc = G.trGetPosition[hands[i]]() - hp;
        if (held[i] && g_swimHave[i]) {
            Vector3 hv = (loc - g_swimHand[i]) * (1.0f / dt);
            if (Len2(hv) > 0.8f * 0.8f) v = v - hv * (5.0f * dt);
        }
        g_swimHand[i] = loc;
        g_swimHave[i] = true;
    }
    float sp = sqrtf(Len2(v));
    if (sp > 12.0f) v = v * (12.0f / sp);
    SetVel(v);
}

// ---- Fake Lag -----------------------------------------------------------------------------------------------------
// Others see you lag because the rate your game sends your position to the room (Photon's SerializationRate) is
// lowered. The slider picks how low. Your own game is not slowed. The old rate is put back when you turn it off.
static int g_lagOrig = -1;
static bool g_lagApplied = false;
static double g_nextLag = 0;
static void LagChanged() { g_nextLag = 0; }
static void LagTick() {
    if (!G.pnSetRate.IsValid() || !G.pnGetRate.IsValid()) {
        if (f_fakeLag) { f_fakeLag = false; Notify("Fake Lag isn't available in this game"); }
        return;
    }
    if (!f_fakeLag) {
        if (g_lagApplied) { G.pnSetRate(g_lagOrig); g_lagApplied = false; }
        return;
    }
    if (g_time < g_nextLag) return;
    g_nextLag = g_time + 1.0;
    if (!g_lagApplied) { g_lagOrig = G.pnGetRate(); g_lagApplied = true; }
    int want = kLagRates[g_lagLevel];
    if (G.pnGetRate() != want) G.pnSetRate(want);
}

// ---- Visual: Rain -----------------------------------------------------------------------------------------------------
// 120 thin blue streaks fall fast around you inside a 10 m box that follows you. Only you see them.
static constexpr int kDrops = 120;
static Ref g_drop[kDrops];
static Vector3 g_dropPos[kDrops];
static float g_dropSpeed[kDrops];
static bool g_rainShown = false;
static void ClearRain() {
    for (auto &r : g_drop) Kill(r);
    g_rainShown = false;
}
static void RainTick(float dt) {
    Il2CppObject *head = g_head.get();
    if (!f_rain || !head) { if (g_rainShown) ClearRain(); return; }
    g_rainShown = true;
    Vector3 hp = G.trGetPosition[head]();
    int made = 0;
    for (int i = 0; i < kDrops; i++) {
        Il2CppObject *o = g_drop[i].get();
        if (!Alive(o)) {
            if (made >= 12) continue;                    // a few new ones a frame, no hitch
            o = Cube(Color(0.55f, 0.7f, 1.0f, 1), false);
            if (!o) continue;
            made++;
            G.trSetLocalScale[Tr(o)](Vector3(0.008f, 0.32f, 0.008f));
            g_drop[i].set(o);
            g_dropPos[i] = hp + Vector3((Rand01() - 0.5f) * 10.f, -2.5f + Rand01() * 6.f, (Rand01() - 0.5f) * 10.f);
            g_dropSpeed[i] = 9.0f + Rand01() * 4.0f;
        }
        Vector3 &p = g_dropPos[i];
        p.y -= g_dropSpeed[i] * dt;
        if (p.y < hp.y - 2.5f) {                         // hit the bottom: start again at the top
            p.y += 6.0f;
            p.x = hp.x + (Rand01() - 0.5f) * 10.f;
            p.z = hp.z + (Rand01() - 0.5f) * 10.f;
        }
        auto wrap = [](float &v, float c, float half) { if (v > c + half) v -= 2 * half; else if (v < c - half) v += 2 * half; };
        wrap(p.x, hp.x, 5.f);
        wrap(p.z, hp.z, 5.f);
        G.trSetPosition[Tr(o)](p);
    }
}

// ---- Visual: shader-based Fullbright and Horror -------------------------------------------------------------------------
// Both work on the surfaces themselves instead of on the lights. Every opaque material in the scene is looked at,
// a few every frame (and again every 15 seconds for new objects):
//   Fullbright: its shader is swapped for the URP Unlit shader, which ignores lights and shadows, so every surface
//               shows its own texture at full brightness, in dark places too. Colors are copied across.
//   Horror:     its color is multiplied down to a dark, red-leaning tone, so the world is dark and scary whatever
//               the map's lights do (the fog, flickering light and black-outs of AtmoTick come on top).
// Transparent and cut-out materials (render queue 2450 and up) and materials that already use an Unlit shader (this
// menu's own objects) are left alone. The original shader and colors are kept and put back when you turn it off
// or leave the room.
struct ShMat { Ref mat, oldShader; Color oldBase = Color(1, 1, 1, 1), oldColor = Color(1, 1, 1, 1); bool hasBase = false, hasColor = false, swapped = false; };
static std::vector<ShMat> g_shMats;
static std::unordered_set<void *> g_shSeen;
static int g_shMode = 0;                                 // 0 off, 1 Fullbright, 2 Horror
static Ref g_shArr;
static size_t g_shIdx = 0, g_shN = 0;
static double g_nextShScan = 0;
static BNM::Structures::Mono::String *MS(const char *x) { return (BNM::Structures::Mono::String *)CreateMonoString(x); }
// Horror: terrain is not a Renderer, so it keeps its own material. It is given a dark one (and the old one comes back afterwards).
struct TerrSave { Ref terr, oldMat, newMat; int oldType = 0; };
static std::vector<TerrSave> g_terr;
static double g_terrNext = 0;
static void TerrainShade(bool on) {
    Class tc("UnityEngine", "Terrain");
    if (!tc.IsValid()) return;
    Method<Il2CppObject *> getMat;
    Method<void> setMat;
    Method<int> getType;
    Method<void> setType;
    getMat = tc.GetMethod("get_materialTemplate", 0);
    setMat = tc.GetMethod("set_materialTemplate", 1);
    getType = tc.GetMethod("get_materialType", 0);
    setType = tc.GetMethod("set_materialType", 1);
    if (!getMat.IsValid() || !setMat.IsValid()) return;
    if (!on) {
        for (auto &t : g_terr) {
            Il2CppObject *o = t.terr.get();
            if (Alive(o)) {
                try {
                    setMat[o](t.oldMat.get());
                    if (setType.IsValid()) setType[o](t.oldType);
                } catch (...) {}
            }
            Kill(t.newMat);
            t.terr.clear();
            t.oldMat.clear();
        }
        g_terr.clear();
        return;
    }
    Color dark(0.025f, 0.015f, 0.015f, 1);
    uint8_t *arr = ScanAll(tc.GetMonoType());
    for (size_t i = 0; i < ArrLen(arr); i++) {
        Il2CppObject *t = ArrAt(arr, i);
        if (!Alive(t)) continue;
        bool known = false;
        for (auto &sv : g_terr) if (sv.terr.get() == t) known = true;
        if (known) continue;
        try {
            Il2CppObject *tmp = Prim(3, dark, false);                 // its material is a ready-made dark one
            Il2CppObject *r = tmp ? G.goGetComponent[tmp](G.tRenderer) : nullptr;
            Il2CppObject *m = r ? G.rendererGetMaterial[r]() : nullptr;
            if (tmp) G.destroy(tmp);
            if (!m) continue;
            TerrSave ts;
            Il2CppObject *old = getMat[t]();
            ts.terr.set(t);
            if (old) ts.oldMat.set(old);
            ts.newMat.set(m);
            ts.oldType = getType.IsValid() ? getType[t]() : 0;
            g_terr.push_back(ts);
            if (setType.IsValid()) setType[t](2);        // 2 = Custom: only then does materialTemplate actually apply
            setMat[t](m);
        } catch (...) {}
    }
}
static void ShaderRestore() {
    TerrainShade(false);
    for (auto &sm : g_shMats) {
        Il2CppObject *m = sm.mat.get();
        if (!Alive(m)) continue;
        if (sm.swapped && sm.oldShader.get()) G.matSetShader[m](sm.oldShader.get());
        if (sm.hasBase) G.matSetColorN[m](MS("_BaseColor"), sm.oldBase);
        if (sm.hasColor) G.matSetColorN[m](MS("_Color"), sm.oldColor);
        sm.mat.clear();
        sm.oldShader.clear();
    }
    g_shMats.clear();
    g_shSeen.clear();
    g_shArr.clear();
    g_shIdx = g_shN = 0;
    g_nextShScan = 0;
    g_shMode = 0;
}
static void ShaderProcess(Il2CppObject *rend, int mode) {
    Il2CppObject *m = G.rendererGetShared[rend]();
    if (!m || g_shSeen.count((void *)m)) return;
    g_shSeen.insert((void *)m);
    if (g_shMats.size() >= 4000) return;
    Il2CppObject *sh = G.matGetShader[m]();
    if (!sh || sh == G.shader.get() || sh == G.unlitShader.get()) return;
    if (G.matGetRenderQueue.IsValid() && G.matGetRenderQueue[m]() >= 2450) return;
    ShMat sm;
    sm.mat.set(m);
    sm.oldShader.set(sh);
    bool hb = G.matHasProp[m](MS("_BaseColor")), hc = G.matHasProp[m](MS("_Color"));
    if (hb) sm.oldBase = G.matGetColorN[m](MS("_BaseColor"));
    if (hc) sm.oldColor = G.matGetColorN[m](MS("_Color"));
    sm.hasBase = hb;
    sm.hasColor = hc;
    if (mode == 1) {
        Color c = hb ? sm.oldBase : hc ? sm.oldColor : Color(1, 1, 1, 1);
        G.matSetShader[m](G.unlitShader.get());
        G.matSetColorN[m](MS("_BaseColor"), Color(c.r * 1.1f, c.g * 1.1f, c.b * 1.1f, 1));   // 10% brighter than normal, not blinding white
        sm.swapped = true;
    } else {
        if (hb) G.matSetColorN[m](MS("_BaseColor"), Color(sm.oldBase.r * 0.16f, sm.oldBase.g * 0.11f, sm.oldBase.b * 0.11f, sm.oldBase.a));
        if (hc) G.matSetColorN[m](MS("_Color"), Color(sm.oldColor.r * 0.16f, sm.oldColor.g * 0.11f, sm.oldColor.b * 0.11f, sm.oldColor.a));
        if (!hb && !hc) return;
    }
    g_shMats.push_back(sm);
}
static void ShaderPassTick() {
    int want = f_fullbright ? 1 : f_horror ? 2 : 0;
    if (want != g_shMode) {
        ShaderRestore();
        g_shMode = want;
        if (want == 1 && !G.unlitShader.get()) { g_shMode = 0; Notify("No unlit shader in this game, using lights only"); }
    }
    if (g_shMode == 0) return;
    if (g_shMode == 2 && g_time >= g_terrNext) { g_terrNext = g_time + 10.0; TerrainShade(true); }
    if (!G.rendererGetShared.IsValid() || !G.matGetShader.IsValid() || !G.matHasProp.IsValid() || !G.matGetColorN.IsValid() ||
        !G.matSetColorN.IsValid() || !G.matSetShader.IsValid() || !G.tRenderer) return;
    if (!g_shArr.get() && g_time >= g_nextShScan) {
        g_nextShScan = g_time + 15.0;
        if (uint8_t *arr = ScanAll(G.tRenderer)) { g_shArr.set((Il2CppObject *)arr); g_shN = ArrLen(arr); g_shIdx = 0; }
    }
    if (uint8_t *arr = (uint8_t *)g_shArr.get()) {
        for (int k = 0; k < 120 && g_shIdx < g_shN; k++, g_shIdx++) {
            Il2CppObject *r = ArrAt(arr, g_shIdx);
            if (Alive(r)) ShaderProcess(r, g_shMode);
        }
        if (g_shIdx >= g_shN) g_shArr.clear();
    }
}

// ---- Spawning: MenuItem Orbit, 9x9, row, YEEPS text, placeholder cubes ---------------------------------------------------------
// All of these go through the same item message and the same queue as the other spawns (two a frame, one batch
// every 3 seconds, at most 96 waiting).
//
// MenuItem Orbit: six copies of the item in your left hand are spawned around you. Once they exist, they are moved in a
// circle around your head every frame. The move is done on your own game (the item's position), so whether the room
// sees them circle depends on how the game syncs item positions; you always see it.
static Ref g_orbitItem[8];
static int g_orbitN = 0;
static double g_nextOrbitScan = 0, g_orbitSince = 0;
static Vector3 HeadFlat(Il2CppObject *head, Vector3 &r) {
    Vector3 f = Flat(G.trGetForward[head]());
    r = Cross(Vector3(0, 1, 0), f);
    return f;
}
static void OrbitToggled() {
    for (auto &r : g_orbitItem) r.clear();
    g_orbitN = 0;
    g_orbitIds.clear();
    if (!f_orbit) return;
    Il2CppObject *head = g_head.get();
    if (!head || !LeftHandItem()) { f_orbit = false; return; }
    Vector3 hp = G.trGetPosition[head]();
    std::vector<PendingItem> list;
    for (int i = 0; i < 6; i++) {
        float a = 6.2831853f * (float)i / 6.0f;
        PendingItem p{hp + Vector3(cosf(a), -0.35f, sinf(a)), Vector3(0, 0, 0)};
        p.orbit = true;
        list.push_back(p);
    }
    if (!QueueItems(list)) { f_orbit = false; return; }
    g_orbitSince = g_time;
    Notify("MenuItem Orbit: spawning");
}
static void OrbitTick(float dt) {
    (void)dt;
    Il2CppObject *head = g_head.get();
    if (!f_orbit || !head || !G.tItem || !G.itemId.IsValid()) {
        if (!f_orbit && (g_orbitN || !g_orbitIds.empty())) {
            for (auto &r : g_orbitItem) r.clear();
            g_orbitN = 0;
            g_orbitIds.clear();
        }
        return;
    }
    // Find the copies once they have arrived (ids were remembered when they were sent). Gives up after 10 seconds.
    if (g_orbitN < (int)g_orbitIds.size() && g_time >= g_nextOrbitScan && g_time - g_orbitSince < 10.0) {
        g_nextOrbitScan = g_time + 0.3;
        uint8_t *arr = ScanAll(G.tItem);
        size_t n = ArrLen(arr);
        for (size_t i = 0; i < n && g_orbitN < 8; i++) {
            Il2CppObject *it = ArrAt(arr, i);
            if (!Alive(it)) continue;
            auto *idStr = G.itemId[it]();
            if (!idStr) continue;
            std::string id = idStr->str();
            bool mine = false, have = false;
            for (auto &x : g_orbitIds) if (x == id) mine = true;
            for (int j = 0; j < g_orbitN; j++) if (g_orbitItem[j].get() == it) have = true;
            if (mine && !have) g_orbitItem[g_orbitN++].set(it);
        }
    }
    float t = (float)g_time;
    Vector3 hp = G.trGetPosition[head]();
    for (int i = 0; i < g_orbitN; i++) {
        Il2CppObject *it = g_orbitItem[i].get();
        if (!Alive(it)) continue;
        float a = t * 1.8f + 6.2831853f * (float)i / (float)(g_orbitN > 0 ? g_orbitN : 1);
        G.trSetPosition[G.compTransform[it]()](hp + Vector3(cosf(a) * 1.0f, -0.35f + 0.12f * sinf(t * 3.0f + (float)i), sinf(a) * 1.0f));
        Il2CppObject *go = G.compGameObject[it]();
        Il2CppObject *rb = go ? G.goGetComponent[go](G.tRigidbody) : nullptr;
        if (rb && G.rbSetVelocity.IsValid()) G.rbSetVelocity[rb](Vector3(0, 0, 0));   // no falling, no flying off
    }
    static double nextSync = 0;                     // server side: re-send the new spots to the room a few times a second
    if (g_time >= nextSync) {
        nextSync = g_time + 0.35;
        for (int i = 0; i < g_orbitN; i++) SyncItem(g_orbitItem[i].get());
    }
}

static std::vector<PendingItem> MakeGrid(Vector3 base, Vector3 r, Vector3 f, int nx, int nz, float step, const char *key) {
    std::vector<PendingItem> list;
    for (int z = 0; z < nz; z++)
        for (int x = 0; x < nx; x++) {
            PendingItem p{base + r * ((x - (nx - 1) * 0.5f) * step) + f * (z * step), Vector3(0, 0, 0)};
            if (key) snprintf(p.key, sizeof p.key, "%s", key);
            list.push_back(p);
        }
    return list;
}
static bool QueueAny(const std::vector<PendingItem> &list) {      // like QueueItems, but not for the held item
    if (g_time < g_nextBatch) { Notify("Wait a moment"); return false; }
    g_nextBatch = g_time + 3.0;
    for (auto &p : list) if (g_itemQueue.size() < 96) g_itemQueue.push_back(p);
    return true;
}
static void ArmGrid81() {
    Il2CppObject *head = g_head.get();
    if (!head || !LeftHandItem()) return;
    Vector3 r, f = HeadFlat(head, r);
    QueueItems(MakeGrid(G.trGetPosition[head]() + f * 1.4f + Vector3(0, -0.4f, 0), r, f, 9, 9, 0.4f, nullptr));
}
static void Grid9Gun(Vector3 at) {                                  // Spawn 9x9 Held MenuItem Gun
    Il2CppObject *head = g_head.get();
    if (!head || !LeftHandItem()) return;
    Vector3 r, f = HeadFlat(head, r);
    QueueItems(MakeGrid(at + Vector3(0, 0.3f, 0) - f * 1.6f, r, f, 9, 9, 0.4f, nullptr));
}
static void ArmRow() {
    Il2CppObject *head = g_head.get();
    if (!head || !LeftHandItem()) return;
    Vector3 r, f = HeadFlat(head, r);
    QueueItems(MakeGrid(G.trGetPosition[head]() + f * 1.5f + Vector3(0, -0.4f, 0), r, f, 10, 1, 0.45f, nullptr));
}
// YEEPS in 5x5 pixel letters (65 items), standing in front of you, readable from where you stand.
static void ArmYeepsText() {
    static const char *kLetters[5][5] = {
        {"X...X", "X...X", ".X.X.", "..X..", "..X.."},   // Y
        {"XXXXX", "X....", "XXXX.", "X....", "XXXXX"},   // E
        {"XXXXX", "X....", "XXXX.", "X....", "XXXXX"},   // E
        {"XXXX.", "X...X", "XXXX.", "X....", "X...."},   // P
        {".XXXX", "X....", ".XXX.", "....X", "XXXX."},   // S
    };
    Il2CppObject *head = g_head.get();
    if (!head || !LeftHandItem()) return;
    Vector3 r, f = HeadFlat(head, r);
    const float cell = 0.2f;
    Vector3 origin = G.trGetPosition[head]() + f * 2.6f;
    std::vector<PendingItem> list;
    for (int li = 0; li < 5; li++)
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 5; col++) {
                if (kLetters[li][row][col] != 'X') continue;
                float x = ((float)(li * 6 + col) - 14.0f) * cell;     // 29 cells wide, centered
                float y = 0.5f - (float)row * cell;                   // from a little above your head down
                list.push_back({origin + r * x + Vector3(0, y, 0), Vector3(0, 0, 0)});
            }
    QueueItems(list);
}

// Placeholder / glitch / unknown cube: an item the game has no model for shows up as a plain placeholder cube.
// The key is taken from an item in the room whose key says placeholder / glitch / unknown / missing / error;
// if there is none, "placeholder" is tried. Written to the log: "leos-menu: placeholder item key ...".
static char g_phKey[64] = "";
static const char *PlaceholderKey() {
    if (g_phKey[0]) return g_phKey;
    if (G.tItem && G.itemKey.IsValid()) {
        uint8_t *arr = ScanAll(G.tItem);
        size_t n = ArrLen(arr);
        for (size_t i = 0; i < n && !g_phKey[0]; i++) {
            Il2CppObject *it = ArrAt(arr, i);
            if (!Alive(it)) continue;
            auto *key = G.itemKey[it]();
            if (!key) continue;
            std::string k = key->str();
            if (HasAny(Lower(k), {"placeholder", "glitch", "unknown", "missing", "error"})) snprintf(g_phKey, sizeof g_phKey, "%s", k.c_str());
        }
    }
    if (!g_phKey[0]) snprintf(g_phKey, sizeof g_phKey, "placeholder");
    ym::log("leos-menu: placeholder item key %s", g_phKey);
    return g_phKey;
}
static void ArmCubes9() {
    Il2CppObject *head = g_head.get();
    if (!head) return;
    Vector3 r, f = HeadFlat(head, r);
    QueueAny(MakeGrid(G.trGetPosition[head]() + f * 1.4f + Vector3(0, -0.4f, 0), r, f, 3, 3, 0.45f, PlaceholderKey()));
}

// ============================================================================================
// ---- Update 4: server-sided items, projectile sliders, Infinite Cotton, Levitation Gun -------
// ============================================================================================

// Keeps an item we moved on our side in step with the room: the item's own PhotonView (if it has one) is
// taken over, and the game's own "send drop" message (MenuItem.TrySendDrop) is tried, which sends the item's
// position and velocity to the others. Both are wrapped, so an item the game refuses is just skipped.
static void SyncItem(Il2CppObject *it) {
    if (!Alive(it)) return;
    static double nextAny = 0;                           // at most ~15 of these per second, whatever moves
    if (g_time < nextAny) return;
    nextAny = g_time + 0.065;
    try {
        Il2CppObject *go = G.compGameObject[it]();
        if (go && G.tPhotonView) {
            Il2CppObject *pv = G.goGetComponent[go]((Il2CppObject *)G.tPhotonView);
            if (pv) {
                static Method<void> req;
                static bool looked = false;
                if (!looked) { looked = true; req = Class("Photon.Pun", "PhotonView").GetMethod("RequestOwnership", 0); }
                if (req.IsValid()) req[pv]();
            }
        }
        if (G.itemDrop.IsValid()) G.itemDrop[it]();
    } catch (...) {}
}
static Il2CppObject *FindItemById(uint8_t *arr, size_t n, const std::string &id) {
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ArrAt(arr, i);
        if (!Alive(it)) continue;
        auto *s = G.itemId[it]();
        if (s && s->str() == id) return it;
    }
    return nullptr;
}
static void SetItemFlight(Il2CppObject *it, Vector3 vel, bool gravity) {
    Il2CppObject *go = G.compGameObject[it]();
    Il2CppObject *rb = go ? G.goGetComponent[go](G.tRigidbody) : nullptr;
    if (!rb) return;
    if (G.rbSetUseGravity.IsValid()) G.rbSetUseGravity[rb](gravity);
    if (G.rbSetVelocity.IsValid()) G.rbSetVelocity[rb](vel);
}

// ---- Fireball: the flare items are found by id and kept flying forward, and re-sent to the room ----
// Items that must fall: no longer held still, gravity on, physics awake. The game can put them back to sleep when they
// arrive, so it is done again for about a second and a half.
static void ItemFall(Il2CppObject *it, Vector3 vel) {
    Il2CppObject *go = G.compGameObject[it]();
    Il2CppObject *rb = go ? G.goGetComponent[go](G.tRigidbody) : nullptr;
    if (!rb) return;
    static Method<void> setKin, wake;
    static bool looked = false;
    if (!looked) { looked = true; Class rc("UnityEngine", "Rigidbody"); setKin = rc.GetMethod("set_isKinematic", 1); wake = rc.GetMethod("WakeUp", 0); }
    if (setKin.IsValid()) setKin[rb](false);
    if (G.rbSetUseGravity.IsValid()) G.rbSetUseGravity[rb](true);
    if (G.rbSetVelocity.IsValid()) G.rbSetVelocity[rb](vel);
    if (wake.IsValid()) wake[rb]();
}
struct FallLive { Ref it; Vector3 vel; double until = 0, next = 0; };
static std::vector<FallLive> g_fall;
struct FbLive { Ref it; Vector3 vel, pos; double until = 0, nextSync = 0; bool live = false; };
static FbLive g_fbLive[6];
static void FbDrive(float dt) {
    if (!g_fbPend.empty() && G.itemId.IsValid() && G.tItem) {
        uint8_t *arr = ScanAll(G.tItem);
        size_t n = ArrLen(arr);
        for (size_t p = 0; p < g_fbPend.size();) {
            FbTrack &t = g_fbPend[p];
            Il2CppObject *it = FindItemById(arr, n, t.id);
            if (it && t.kind == 2) {
                FallLive fl;
                fl.it.set(it);
                fl.vel = t.dir;
                fl.until = g_time + 1.5;
                if (g_fall.size() < 40) g_fall.push_back(fl);
                g_fbPend.erase(g_fbPend.begin() + (long)p);
            } else if (it) {
                for (auto &b : g_fbLive)
                    if (!b.live) {
                        b.it.set(it);
                        b.vel = t.dir;
                        b.pos = G.trGetPosition[G.compTransform[it]()]();
                        b.until = g_time + 1.6;
                        b.nextSync = 0;
                        b.live = true;
                        break;
                    }
                g_fbPend.erase(g_fbPend.begin() + (long)p);
            } else if (g_time - t.born > 4.0) g_fbPend.erase(g_fbPend.begin() + (long)p);
            else p++;
        }
    }
    for (size_t i = 0; i < g_fall.size();) {
        FallLive &f = g_fall[i];
        Il2CppObject *it = f.it.get();
        if (!Alive(it) || g_time >= f.until) { g_fall.erase(g_fall.begin() + (long)i); continue; }
        if (g_time >= f.next) { f.next = g_time + 0.2; ItemFall(it, f.vel); }
        i++;
    }
    for (auto &b : g_fbLive) {
        if (!b.live) continue;
        Il2CppObject *it = b.it.get();
        if (!Alive(it) || g_time >= b.until) {
            if (Alive(it)) SetItemFlight(it, b.vel * 0.2f, true);
            b.it.clear();
            b.live = false;
            continue;
        }
        b.pos = b.pos + b.vel * dt;                            // the flare is placed along the flight path every frame
        G.trSetPosition[G.compTransform[it]()](b.pos);
        SetItemFlight(it, Vector3(0, 0, 0), false);
        if (g_time >= b.nextSync) { b.nextSync = g_time + 0.1; SyncItem(it); }
    }
}

// ---- MenuItem Rain: items fall around you (spawned for the whole room, they fall on the ground) ----
static double g_nextRain = 0;
static int g_rainSpawned = 0;
static void ItemRainToggled() {
    g_rainSpawned = 0;
    g_nextRain = 0;
    if (f_itemRain && !LeftHandItem()) f_itemRain = false;
    else if (f_itemRain) Notify("MenuItem Rain on");
}
static void ItemRainTick() {
    Il2CppObject *head = g_head.get();
    if (!f_itemRain || !head || g_time < g_nextRain) return;
    if (g_itemQueue.size() > 12) return;
    g_nextRain = g_time + 0.3;
    if (g_rainSpawned >= 150) { f_itemRain = false; Notify("MenuItem Rain stopped (150 items)"); return; }
    Vector3 hp = G.trGetPosition[head]();
    float a = Rand01() * 6.2831853f, r = 1.0f + Rand01() * 5.0f;
    PendingItem p{hp + Vector3(cosf(a) * r, 5.0f + Rand01() * 2.0f, sinf(a) * r), Vector3(0, -6.0f, 0)};
    p.track = 2;                                          // found again after it arrives, to be given gravity
    g_itemQueue.push_back(p);
    g_rainSpawned++;
}

// ---- Levitation Gun: items you aim at float up for a few seconds ----
struct LevItem { Ref it; double until = 0, nextSync = 0; bool live = false; };
static LevItem g_lev[24];
static void LevitateAim(Vector3 start, Vector3 dir) {
    if (g_time < g_nextDelete || !G.tItem) return;
    g_nextDelete = g_time + 0.4;
    uint8_t *arr = ScanAll(G.tItem);
    size_t n = ArrLen(arr);
    Il2CppObject *best = nullptr;
    float bestT = 1e30f;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ArrAt(arr, i);
        if (!Alive(it)) continue;
        Vector3 to = G.trGetPosition[G.compTransform[it]()]() - start;
        float t = Dot(to, dir);
        if (t < 0.15f || t > 80.0f) continue;
        Vector3 perp = to - dir * t;
        float rad = 0.4f + 0.03f * t;
        if (Len2(perp) > rad * rad || t >= bestT) continue;
        bestT = t;
        best = it;
    }
    if (!best) {                                         // nothing aimed at: you float yourself (the game's own effect)
        Il2CppObject *player = g_player.get();
        if (player && G.plNoGravityFor.IsValid()) { G.plNoGravityFor[player](4.0f); Notify("You float for 4 seconds"); }
        else Notify("No item there");
        return;
    }
    Vector3 c = G.trGetPosition[G.compTransform[best]()]();
    int count = 0;
    for (size_t i = 0; i < n && count < 24; i++) {       // the item and the ones next to it
        Il2CppObject *it = ArrAt(arr, i);
        if (!Alive(it)) continue;
        if (Len2(G.trGetPosition[G.compTransform[it]()]() - c) > 2.5f * 2.5f) continue;
        for (auto &l : g_lev)
            if (!l.live) { l.it.set(it); l.until = g_time + 5.0; l.nextSync = 0; l.live = true; count++; break; }
    }
    Notify("Levitating %d item(s)", count);
}
static void LevTick(float dt) {
    (void)dt;
    for (auto &l : g_lev) {
        if (!l.live) continue;
        Il2CppObject *it = l.it.get();
        if (!Alive(it) || g_time >= l.until) {
            if (Alive(it)) { SetItemFlight(it, Vector3(0, 0, 0), true); SyncItem(it); }
            l.it.clear();
            l.live = false;
            continue;
        }
        SetItemFlight(it, Vector3(0, 1.3f, 0), false);
        if (g_time >= l.nextSync) { l.nextSync = g_time + 0.25; SyncItem(it); }
    }
}

// ---- Overpowered: Projectile Count / Spread Angle / Reload Stuffing Cost on every "(Clone)" item ----
// Every MenuItem in the scene whose object name has "(Clone)" in it is checked (all of them, also when many have the same name,
// like item_pelletgun_pirate(Clone)). In each script on it, and in the game objects those scripts point to one step down, the
// fields named projectileCount / projectileSpreadAngle / reloadStuffingCost are set. A slider on Default leaves its field
// alone (and puts the old value back). Every 2 seconds the list is checked again so new items get it, and "Refresh
// Projectiles" does that right now. What it found is written to the log as "leos-menu: projectile field ...".
struct PHit { void *f; char t; int cat; };
struct PCls { std::vector<PHit> hits; std::vector<void *> objFields; };
static std::unordered_map<void *, PCls> g_pcls;
static int ProjCatOf(const std::string &n) {
    if (Has(n, "projectilespreadangle")) return 2;
    if (Has(n, "projectilecount")) return 1;
    if (Has(n, "reloadstuffingcost")) return 3;
    return 0;
}
static PCls &ProjClass(void *klass) {
    auto it = g_pcls.find(klass);
    if (it != g_pcls.end()) return it->second;
    PCls &pc = g_pcls[klass];
    int guard = 0;
    for (void *k = klass; k && guard < 8; k = p_classParent ? p_classParent(k) : nullptr, guard++) {
        auto *kc = (BNM::IL2CPP::Il2CppClass *)k;
        if (kc->namespaze && kc->namespaze[0]) break;
        void *iter = nullptr;
        while (void *f = p_classFields(k, &iter)) {
            const char *fn = p_fieldName(f);
            if (!fn) continue;
            if (p_fieldFlags && (p_fieldFlags(f) & 0x50)) continue;
            auto *ty = (const BNM::IL2CPP::Il2CppType *)p_fieldType(f);
            std::string tn = TypeName(ty), ln = Lower(fn);
            int cat = ProjCatOf(ln);
            if (cat && (tn == "System.Single" || tn == "System.Int32")) { pc.hits.push_back({f, tn == "System.Single" ? 'f' : 'i', cat}); continue; }
            Class fc(ty);
            auto *fk = fc.IsValid() ? fc.GetClass() : nullptr;
            if (fk && !fk->byval_arg.valuetype && !fk->enumtype && !(fk->namespaze && fk->namespaze[0]) && fk->name && fk->name[0] != '<')
                pc.objFields.push_back(f);
        }
    }
    return pc;
}
struct PTw { Ref obj; void *field = nullptr; int cat = 0; bool isInt = false, unity = false; float oldF = 0; int oldI = 0; };
static std::vector<PTw> g_ptw;
static double g_nextProj = 0;
static int ProjSliderOf(int cat) { return cat == 1 ? g_projCount : cat == 2 ? g_projSpread : g_projCost; }
static int ProjValueOf(int cat) { int p = ProjSliderOf(cat); return cat == 1 ? kPCounts[p] : cat == 2 ? kPSpreads[p] : kPCosts[p]; }
static void ProjWrite(PTw &t) {
    Il2CppObject *o = t.obj.get();
    if (!o || (t.unity && !Alive(o))) return;
    int v = ProjValueOf(t.cat);
    if (v < 0) return;
    if (t.isInt) p_fieldSet(o, t.field, &v);
    else { float fv = (float)v; p_fieldSet(o, t.field, &fv); }
}
static void ProjRestore(PTw &t) {
    Il2CppObject *o = t.obj.get();
    if (o && (!t.unity || Alive(o))) { if (t.isInt) p_fieldSet(o, t.field, &t.oldI); else p_fieldSet(o, t.field, &t.oldF); }
    t.obj.clear();
}
static void ProjRestoreAll() { for (auto &t : g_ptw) ProjRestore(t); g_ptw.clear(); }
static void ProjApply(Il2CppObject *obj, bool unity, int depth, std::unordered_map<uint64_t, bool> &have, int &found) {
    void *klass = p_objClass(obj);
    if (!klass) return;
    PCls &pc = ProjClass(klass);
    for (auto &h : pc.hits) {
        if (ProjSliderOf(h.cat) == 0) continue;
        uint64_t key = (uint64_t)(uintptr_t)obj * 1000003ull ^ (uint64_t)(uintptr_t)h.f;
        if (have.count(key) || g_ptw.size() >= 1500) continue;
        have[key] = true;
        PTw t;
        t.obj.set(obj);
        t.field = h.f;
        t.cat = h.cat;
        t.isInt = h.t == 'i';
        t.unity = unity;
        if (t.isInt) { p_fieldGet(obj, h.f, &t.oldI); t.oldF = (float)t.oldI; } else p_fieldGet(obj, h.f, &t.oldF);
        static int logged = 0;
        if (logged++ < 40) ym::log("leos-menu: projectile field %s.%s (cat %d, was %.2f)", ((BNM::IL2CPP::Il2CppClass *)klass)->name, p_fieldName(h.f), h.cat, t.oldF);
        g_ptw.push_back(t);
        found++;
    }
    if (depth == 0)
        for (void *f : pc.objFields) {
            void *child = nullptr;
            p_fieldGet(obj, f, &child);
            if (child) ProjApply((Il2CppObject *)child, false, 1, have, found);
        }
}
static int g_projItems = 0, g_projFields = 0;
static void ProjScan() {
    if (!EnsureFieldApi() || !G.tItem || !G.objGetName.IsValid() || !G.getComponentsInChildren.IsValid()) return;
    static BNM::MonoType *tMono = Class("UnityEngine", "MonoBehaviour").GetMonoType();
    // put back what is no longer wanted
    for (size_t i = 0; i < g_ptw.size();) {
        if (ProjSliderOf(g_ptw[i].cat) == 0) { ProjRestore(g_ptw[i]); g_ptw.erase(g_ptw.begin() + (long)i); } else i++;
    }
    std::unordered_map<uint64_t, bool> have;
    for (auto &t : g_ptw) have[(uint64_t)(uintptr_t)t.obj.get() * 1000003ull ^ (uint64_t)(uintptr_t)t.field] = true;
    uint8_t *arr = ScanAll(G.tItem);
    size_t n = ArrLen(arr);
    int items = 0, found = 0;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ArrAt(arr, i);
        if (!Alive(it)) continue;
        Il2CppObject *go = G.compGameObject[it]();
        auto *nm = go ? G.objGetName[go]() : nullptr;
        if (!nm || !Has(Lower(nm->str()), "(clone)")) continue;
        items++;
        auto *comps = (uint8_t *)G.getComponentsInChildren[go]((Il2CppObject *)tMono, true);
        size_t cn = ArrLen(comps);
        for (size_t c = 0; c < cn && c < 80; c++) {
            Il2CppObject *comp = ArrAt(comps, c);
            if (Alive(comp)) ProjApply(comp, true, 0, have, found);
        }
    }
    g_projItems = items;
    g_projFields = (int)g_ptw.size();
}
static void ProjectileTick() {
    bool any = g_projCount || g_projSpread || g_projCost;
    if (!any) { if (!g_ptw.empty()) ProjRestoreAll(); return; }
    static double nextWrite = 0;
    if (g_time >= g_nextProj) { g_nextProj = g_time + 2.0; ProjScan(); }
    if (g_time >= nextWrite) { nextWrite = g_time + 0.4; for (auto &t : g_ptw) ProjWrite(t); }
}
static void ProjRefresh() {
    if (!(g_projCount || g_projSpread || g_projCost)) { Notify("Move a projectile slider first"); return; }
    ProjScan();
    for (auto &t : g_ptw) ProjWrite(t);
    g_nextProj = g_time + 2.0;
    Notify("%d (Clone) items, %d fields set", g_projItems, g_projFields);
}

// ---- Overpowered: Infinite Cotton (the game's CottonManager: every max / current amount number set to 1000000) ----
static std::vector<std::pair<CottonField, Ref>> g_cotSaved;       // the field, and the object it was on (empty for static)
static double g_nextInfCot = 0;
static bool g_cotNoted = false;
static void InfCottonToggled() { g_nextInfCot = 0; g_cotNoted = false; }
static bool CotWanted(const std::string &n) {
    if (HasAny(n, {"min", "regen", "rate", "cost", "per", "speed", "delay", "time", "interval", "cooldown", "spawn", "spend", "price", "step", "each", "pickup", "radius"})) return false;
    return HasAny(n, {"max", "current", "curr", "amount", "cotton", "count", "total"});
}
static void InfCottonRestore() {
    for (auto &p : g_cotSaved) {
        Il2CppObject *o = p.second.get();
        if (!p.first.isStatic && !o) continue;
        CottonPut(p.first, o, p.first.oldV);
        p.second.clear();
    }
    g_cotSaved.clear();
}
static void InfCottonTick() {
    if (!f_infCotton) { if (!g_cotSaved.empty()) InfCottonRestore(); return; }
    if (g_time < g_nextInfCot || !EnsureFieldApi() || !p_fieldStaticGet) return;
    g_nextInfCot = g_time + 0.5;
    static Class cm("", "CottonManager", Image("Assembly-CSharp.dll"));
    if (!cm.IsValid()) { f_infCotton = false; Notify("No CottonManager in this game"); return; }
    void *klass = cm.GetClass();
    std::vector<Il2CppObject *> objs;
    try {
        if (IsUnityClass(klass)) { uint8_t *arr = ScanAll(cm.GetMonoType()); for (size_t i = 0; i < ArrLen(arr) && i < 8; i++) if (Alive(ArrAt(arr, i))) objs.push_back(ArrAt(arr, i)); }
    } catch (...) {}
    int guard = 0;
    for (void *k = klass; k && guard < 6; k = p_classParent ? p_classParent(k) : nullptr, guard++) {      // a static instance (singleton)
        auto *kc = (BNM::IL2CPP::Il2CppClass *)k;
        if (kc->namespaze && kc->namespaze[0]) break;
        void *iter = nullptr;
        while (void *f = p_classFields(k, &iter)) {
            if (!p_fieldFlags || !(p_fieldFlags(f) & 0x10)) continue;
            Class fc((const BNM::IL2CPP::Il2CppType *)p_fieldType(f));
            if (!fc.IsValid() || fc.GetClass() != (BNM::IL2CPP::Il2CppClass *)klass) continue;
            void *inst = nullptr;
            p_fieldStaticGet(f, &inst);
            if (inst) { bool dup = false; for (auto *o : objs) if (o == inst) dup = true; if (!dup) objs.push_back((Il2CppObject *)inst); }
        }
    }
    int changed = 0;
    auto doField = [&](void *f, bool isStatic, Il2CppObject *obj, const std::string &tn, const std::string &ln) {
        char kind = CottonKind(tn);
        if (!kind || !CotWanted(ln)) return;
        CottonField cf;
        cf.f = f;
        cf.kind = kind;
        cf.isStatic = isStatic;
        double target = kind == 's' ? 32000.0 : kind == 'S' ? 65000.0 : kind == 'b' ? 250.0 : 1000000.0;
        bool known = false;
        for (auto &p : g_cotSaved) if (p.first.f == f && p.second.get() == obj) known = true;
        if (!known && g_cotSaved.size() < 200) {
            cf.oldV = CottonGet(cf, obj);
            Ref r;
            if (obj) r.set(obj);
            g_cotSaved.push_back({cf, r});
            ym::log("leos-menu: cottonmanager field %s (%s) was %.2f", ln.c_str(), tn.c_str(), cf.oldV);
        }
        CottonPut(cf, obj, target);
        changed++;
    };
    guard = 0;
    for (void *k = klass; k && guard < 6; k = p_classParent ? p_classParent(k) : nullptr, guard++) {
        auto *kc = (BNM::IL2CPP::Il2CppClass *)k;
        if (kc->namespaze && kc->namespaze[0]) break;
        void *iter = nullptr;
        while (void *f = p_classFields(k, &iter)) {
            const char *fn = p_fieldName(f);
            int fl = p_fieldFlags ? p_fieldFlags(f) : 0;
            if (!fn || (fl & 0x40)) continue;
            std::string tn = TypeName((const BNM::IL2CPP::Il2CppType *)p_fieldType(f)), ln = Lower(fn);
            if (fl & 0x10) doField(f, true, nullptr, tn, ln);
            else for (auto *o : objs) doField(f, false, o, tn, ln);
        }
    }
    if (!g_cotNoted) {
        g_cotNoted = true;
        ym::log("leos-menu: cottonmanager: %d objects, %d fields set", (int)objs.size(), changed);
        if (!changed) Notify("CottonManager found nothing to set (see log)");
    }
}

// ---- Visual: Bloom ----
// Uses the game's own post-processing (URP): a global Volume with a Bloom effect is created, and the cameras' post
// processing is switched on. If this build of the game has no URP post-processing (or its bloom shader was stripped
// out when the game was built), nothing can be shown and the toggle switches itself off with a message.
static Ref g_bloomGo;
static std::vector<std::pair<Ref, bool>> g_bloomCams;
static double g_nextBloom = 0;
static void *FindFieldIn(void *klass, const char *name) {
    int guard = 0;
    for (void *k = klass; k && guard < 8; k = p_classParent ? p_classParent(k) : nullptr, guard++) {
        void *iter = nullptr;
        while (void *f = p_classFields(k, &iter)) {
            const char *fn = p_fieldName(f);
            if (fn && !strcmp(fn, name)) return f;
        }
    }
    return nullptr;
}
static void BloomParam(Il2CppObject *comp, const char *field, float v) {
    void *pf = FindFieldIn(p_objClass(comp), field);
    if (!pf) return;
    Il2CppObject *param = nullptr;
    p_fieldGet(comp, pf, &param);
    if (!param) return;
    void *pk = p_objClass(param);
    void *mv = FindFieldIn(pk, "m_Value"), *mo = FindFieldIn(pk, "m_OverrideState");
    if (mv) p_fieldSet(param, mv, &v);
    bool t = true;
    if (mo) p_fieldSet(param, mo, &t);
}
static void BloomOff() {
    Kill(g_bloomGo);
    static Class data("UnityEngine.Rendering.Universal", "UniversalAdditionalCameraData");
    if (data.IsValid()) {
        static Method<void> setPP = data.GetMethod("set_renderPostProcessing", 1);
        for (auto &c : g_bloomCams)
            if (Alive(c.first.get()) && setPP.IsValid()) { try { setPP[c.first.get()](c.second); } catch (...) {} }
    }
    g_bloomCams.clear();
}
static bool BloomApply() {
    if (!EnsureFieldApi() || !p_fieldSet || !p_fieldGet) return false;
    Class vol("UnityEngine.Rendering", "Volume"), prof("UnityEngine.Rendering", "VolumeProfile"), bloom("UnityEngine.Rendering.Universal", "Bloom");
    if (!vol.IsValid() || !prof.IsValid() || !bloom.IsValid()) return false;
    try {
        Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("LeoBloom"));
        Il2CppObject *v = go ? G.goAddComponent[go]((Il2CppObject *)vol.GetMonoType()) : nullptr;
        if (!v) return false;
        Method<void> setGlobal, setPrio, setWeight;
        setGlobal = vol.GetMethod("set_isGlobal", 1);
        setPrio = vol.GetMethod("set_priority", 1);
        setWeight = vol.GetMethod("set_weight", 1);
        if (setGlobal.IsValid()) setGlobal[v](true);
        if (setPrio.IsValid()) setPrio[v](100.0f);
        if (setWeight.IsValid()) setWeight[v](1.0f);
        Method<Il2CppObject *> getProfile, add;
        getProfile = vol.GetMethod("get_profile", 0);
        add = prof.GetMethod("Add", 2);
        Il2CppObject *p = getProfile.IsValid() ? getProfile[v]() : nullptr;
        Il2CppObject *comp = (p && add.IsValid()) ? add[p]((Il2CppObject *)bloom.GetMonoType(), true) : nullptr;
        if (!comp) { G.destroy(go); return false; }
        BloomParam(comp, "intensity", 1.6f);
        BloomParam(comp, "threshold", 0.7f);
        BloomParam(comp, "scatter", 0.75f);
        g_bloomGo.set(go);
        // cameras need post processing on
        Class data("UnityEngine.Rendering.Universal", "UniversalAdditionalCameraData");
        Class cam("UnityEngine", "Camera");
        if (data.IsValid() && cam.IsValid()) {
            Method<bool> getPP;
            Method<void> setPP;
            getPP = data.GetMethod("get_renderPostProcessing", 0);
            setPP = data.GetMethod("set_renderPostProcessing", 1);
            uint8_t *cams = ScanAll(cam.GetMonoType());
            for (size_t i = 0; i < ArrLen(cams) && i < 8; i++) {
                Il2CppObject *c = ArrAt(cams, i);
                if (!Alive(c)) continue;
                Il2CppObject *cgo = G.compGameObject[c]();
                Il2CppObject *d = cgo ? G.goGetComponent[cgo]((Il2CppObject *)data.GetMonoType()) : nullptr;
                if (!d || !setPP.IsValid()) continue;
                bool old = getPP.IsValid() ? getPP[d]() : false;
                bool known = false;
                for (auto &e : g_bloomCams) if (e.first.get() == d) known = true;
                if (!known) { Ref r; r.set(d); g_bloomCams.push_back({r, old}); }
                setPP[d](true);
            }
        }
        ym::log("leos-menu: bloom volume created");
        return true;
    } catch (...) { return false; }
}
static bool g_bloomOk = false;
static void BloomToggled() {
    g_nextBloom = 0;
    if (!f_bloom) { BloomOff(); g_bloomOk = false; return; }
    g_bloomOk = BloomApply();
    Notify(g_bloomOk ? "Bloom on" : "Bloom: glow on lights only (no post-processing in this game)");
}
static void BloomTick() {
    if (!f_bloom || !g_bloomOk || g_time < g_nextBloom) return;
    g_nextBloom = g_time + 2.0;
    if (!Alive(g_bloomGo.get())) g_bloomOk = BloomApply();
}

static void ExtraRoomReset() {
    g_fall.clear();
    SkyboxRestore();
    g_stayTarget = -1;
    g_stayGripPrev = false;
    g_ptw.clear();                                // the old room's items are gone
    g_cotSaved.clear();
    g_bloomGo.clear();
    g_bloomCams.clear();
    Extra8Reset();
    g_sizeApplied = false;
    g_pixelApplied = false;
    g_flashL.clear();
    g_fbPend.clear();
    for (auto &b : g_fbLive) { b.it.clear(); b.live = false; }
    for (auto &l : g_lev) { l.it.clear(); l.live = false; }
    g_nextProj = 0;
    g_rainSpawned = 0;
    f_itemRain = false;
}

// ---- Menu types ---------------------------------------------------------------------------------------------------------------
// Settings > Menu Type: Hand Menu (the default), Corner Menu (the old "Pizza Menu": see-through board in the top
// corner, aimed at with your hand) and Pizza Menu (new: only text in the top corner, no pointer).
//
// Pizza Menu controls:  X = up,  Y = down,  B = select. The last two lines of every page are "Back page" and
// "Next page"; select one of them to change page. A slider cycles to its next value when selected.
// While it is on, X / Y / B are also seen by anything else that uses them (Legs jump uses X, Right Hand Menu uses B).
static bool *kMenuFlags[] = {&f_pizza, &f_textMenu, &f_longMenu, &f_pixelMenu, &f_phoneMenu};
static bool g_menuPrev[5];
static Ref g_pzGo, g_pzText;
static int g_pzSel = 0;
static bool g_pzPrev[3];
static double g_pzNext = 0;
static const char *kCatNames[] = {"Main", "Settings", "Themes", "Gun Color", "Menu Type", "Movement", "Spawning", "Overpowered",
                                  "Rig Mods", "Soundboard", "Visual", "Fun", "Enabled Mods", "Page Buttons", "Experimental", "Spawn Presets", "Text Colour", "Info", "Other Players", "Favourites", "Games", "Owner Code", "Owner", "Search", "Theme Presets", "Preset Name"};
static void MenuTypePicked() {
    Radio(kMenuFlags, g_menuPrev, 5);
    g_page = 0;
    PizzaChanged();
    g_pzSel = 0;
    g_pzNext = 0;
}
static bool *kPbFlags[] = {&f_pbUnder, &f_pbSides, &f_pbBottom, &f_pbModTop};
static bool g_pbPrev[4] = {false, true, false, false};
static void PageBtnPicked() {
    Radio(kPbFlags, g_pbPrev, 4);
    if (!f_pbUnder && !f_pbSides && !f_pbBottom && !f_pbModTop) { f_pbSides = true; g_pbPrev[1] = true; }   // nothing picked = the default
    g_dirty = true;
    g_dirty = true;
}
static void HandMenuPicked() {
    f_pizza = false;
    f_textMenu = false;
    f_longMenu = false;
    f_pixelMenu = false;
    f_phoneMenu = false;
    g_page = 0;
    g_menuPrev[0] = g_menuPrev[1] = g_menuPrev[2] = g_menuPrev[3] = g_menuPrev[4] = false;
    PizzaChanged();
    Notify("Hand Menu");
}
static void PizzaTextBuild() {
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("LeoPizzaText"));
    if (!go) return;
    g_pzGo.set(go);
    Il2CppObject *cv = Canvas(Tr(go), Vector3(0, 0, 0), 0.36f, 0.36f);
    if (!cv) return;
    Il2CppObject *lgo = Label(Tr(cv), "", Vector3(0, 0, 0), 0.36f, 0.36f, 22, &g_pzText);
    if (!lgo || !Alive(g_pzText.get())) return;
    G.textSetAlignment[g_pzText.get()](0);              // UpperLeft
    // No background, so a thin black outline keeps the text readable on any map.
    Class ol("UnityEngine.UI", "Outline"), shd("UnityEngine.UI", "Shadow");
    if (ol.IsValid() && shd.IsValid()) {
        Il2CppObject *o = G.goAddComponent[lgo](ol.GetMonoType());
        Method<void> setCol, setDist;
        setCol = shd.GetMethod("set_effectColor", 1);
        setDist = shd.GetMethod("set_effectDistance", 1);
        if (o && setCol.IsValid() && setDist.IsValid()) {
            setCol[o](Color(0, 0, 0, 1));
            setDist[o](Vector2(1.6f, -1.6f));
        }
    }
}
static void PizzaTextPlace() {
    Il2CppObject *go = g_pzGo.get(), *head = g_head.get();
    if (!Alive(go) || !head || !G.trGetRotation.IsValid()) return;
    Il2CppObject *t = Tr(go);
    G.trSetPosition[t](G.trTransformPoint[head](Vector3(f_rightHand ? 0.28f : -0.28f, 0.12f, 0.6f)));   // top corner of your view
    G.trSetRotation[t](G.trGetRotation[head]());
}
static int PizzaRowsOnPage() {
    int rows = (int)g_view.size() - g_page * kRows;
    return rows < 0 ? 0 : rows > kRows ? kRows : rows;
}
static void PizzaTextRender() {
    int pages = PageCount(), rows = PizzaRowsOnPage();
    std::string s;
    char b[240];
    snprintf(b, sizeof b, "<b>Leo's Menu %s</b>  <color=grey>%s  %d/%d</color>\n", kVersion, kCatNames[(int)g_cat], g_page + 1, pages);
    s += b;
    for (int r = 0; r < rows; r++) {
        MenuItem *it = g_view[g_page * kRows + r];
        bool sel = r == g_pzSel, on = it->kind == TOGGLE && *it->flag;
        char val[64] = "";
        if (it->kind == SLIDER) snprintf(val, sizeof val, ": %s", it->iname ? it->iname(*it->ival) : "");
        else if (it->kind == TOGGLE) snprintf(val, sizeof val, "  %s", on ? "ON" : "OFF");
        snprintf(b, sizeof b, "<color=%s>%s%s%s</color>\n", sel ? "#FFE74A" : on ? "#6BFF7B" : "#FFFFFF", sel ? "> " : "   ", it->label, val);
        s += b;
    }
    snprintf(b, sizeof b, "<color=%s>%sBack page</color>\n", g_pzSel == rows ? "#FFE74A" : "#BBBBBB", g_pzSel == rows ? "> " : "   ");
    s += b;
    snprintf(b, sizeof b, "<color=%s>%sNext page</color>\n", g_pzSel == rows + 1 ? "#FFE74A" : "#BBBBBB", g_pzSel == rows + 1 ? "> " : "   ");
    s += b;
    s += "<color=grey>X up   Y down   B select</color>";
    if (g_time < g_noteUntil) {
        snprintf(b, sizeof b, "\n<color=#FFB060>%s</color>", g_note);
        s += b;
    }
    SetText(g_pzText, s.c_str());
}
static void PizzaTextTick(const Buttons &in) {
    bool cur[3] = {in.x, in.y, in.b}, edge[3];
    for (int i = 0; i < 3; i++) { edge[i] = cur[i] && !g_pzPrev[i]; g_pzPrev[i] = cur[i]; }
    if (!Alive(g_pzGo.get())) { PizzaTextBuild(); if (!Alive(g_pzGo.get())) return; }
    PizzaTextPlace();
    BuildView();
    int rows = PizzaRowsOnPage(), n = rows + 2;          // the rows, then Back and Next
    bool changed = false;
    if (g_pzSel >= n) g_pzSel = n - 1;
    if (edge[0]) { g_pzSel = (g_pzSel + n - 1) % n; changed = true; Buzz(); }
    if (edge[1]) { g_pzSel = (g_pzSel + 1) % n; changed = true; Buzz(); }
    if (edge[2]) {
        Buzz();
        if (g_pzSel < rows) {
            MenuItem *it = g_view[g_page * kRows + g_pzSel];
            if (it->kind == SLIDER) {
                if (it->icount > 1) *it->ival = (*it->ival + (it->icount > 20 ? 5 : 1)) % it->icount;
            } else {
                Press(it);
                if (it->kind == OPEN) g_pzSel = 0;
            }
        } else {
            int pages = PageCount();
            if (pages > 1) g_page = (g_page + (g_pzSel == rows + 1 ? 1 : pages - 1)) % pages;
            else Notify("Only one page");
            g_pzSel = 0;
        }
        changed = true;
    }
    if (changed || g_dirty || g_time >= g_pzNext) {
        g_dirty = false;
        g_pzNext = g_time + 0.25;
        BuildView();
        PizzaTextRender();
    }
}

// ---- callbacks ---------------------------------------------------------------

static void OnRoomChange() {
    // The old room's objects are gone (or about to be): forget them, build again.
    AtmoRestore();
    BoundsRestore();
    ClearTrail();
    ClearSnow();
    ClearRain();
    ShaderRestore();
    SoundRoomReset();
    for (auto &w : g_web) { Kill(w.go); w.line.clear(); w.on = false; }
    for (auto &r : g_orbitItem) r.clear();
    g_orbitN = 0;
    g_orbitIds.clear();
    g_recording = g_rewinding = false;
    g_swimHave[0] = g_swimHave[1] = false;
    g_pzText.clear();
    g_nbHave = false;
    g_pizzaHover = g_pizzaTrig = g_cursorOn = false;
    for (int r = 0; r < kMaxRows; r++) { g_rowCube[r].clear(); g_rowLabel[r].clear(); g_rowText[r].clear(); g_knob[r].clear(); }
    for (auto &r : g_bgX) r.clear();
    for (auto &r : g_outX) r.clear();
    g_rowsBuilt = 0;
    for (Ref *r : {&g_prevLbl, &g_nextLbl, &g_verGo, &g_menuCanvas[0], &g_menuCanvas[1]}) r->clear();
    g_lastTrans = false;
    for (Ref *r : {&g_board, &g_bg, &g_outline, &g_title, &g_titleBar, &g_prev, &g_next, &g_notifyText, &g_tipText}) r->clear();
    for (Ref *r : {&g_menu, &g_pointer, &g_notifyGo, &g_notifyBar, &g_leftPlatform, &g_rightPlatform, &g_gunBall, &g_gunLineGo,
                   &g_legL, &g_legR, &g_audioGo, &g_pzGo}) Kill(*r);
    for (auto &r : g_stroke) Kill(r);
    for (auto &g : g_rig) { Kill(g.go); Kill(g.headGo); }
    for (auto &b : g_bullets) { Kill(b.obj); b.live = false; }
    for (auto &g : g_rig) { g.obj.clear(); g.head.clear(); g.lh.clear(); g.rh.clear(); g.line.clear(); }
    g_audioSrc.clear();
    g_curLine.clear();
    PauseTrackers(false);
    g_dancing = false;
    g_gunLine.clear();
    g_masksSaved = false; g_noclipOn = false; g_collOff = false;   // the old player is gone; the next frame sets the new one up
    g_noclipOffN = 0;
    g_boosted = nullptr;
    g_gravityOff = false;
    g_strokeN = 0; g_curPts = 0; g_drawing = false;
    g_espShown = false;
    g_armHave[0] = g_armHave[1] = false;
    g_armOwnsPause = false;                       // PauseTrackers(false) above already gave the trackers back
    g_armReady = false;
    g_armLegacy = false;
    g_armTries = 0;
    g_armK = 1.0f;
    for (auto &c : g_armCal) { c.driver.clear(); c.have = false; c.stable = 0; }
    for (auto &f : g_flame) Kill(f);
    for (auto &b : g_fb) { Kill(b.obj); b.live = false; }
    for (auto &r : g_shell) r.clear();
    g_sunGo.clear(); g_sunLight.clear();
    g_vHave = false;
    g_vfSpeed = 0;
    g_tw.clear();                                 // the old room's items are gone
    ExtraRoomReset();
    g_cottonApplied = false;
    for (Ref *r : {&g_player, &g_rigidbody, &g_leftHand, &g_rightHand, &g_head}) r->clear();
    g_built = false; g_menuShown = false; g_touching = -2;
}

// ============================================================================================
// ---- Update 7: Joystick Fly, Zero Gravity Fly, CS2 Strafe, Size Changer, Pixelated menu ------
// ============================================================================================
static std::string ObjName(Il2CppObject *go) {
    auto *n = go ? G.objGetName[go]() : nullptr;
    return n ? Lower(n->str()) : std::string();
}
static bool g_zeroHold = false;
static void KeepFloating() {
    Il2CppObject *player = g_player.get();
    if (player && G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.25f);
}
// Joystick Fly: left stick flies the way you look (forward / back / sideways), right stick goes up and down.
// Let go of the sticks and you hover.
static void JoyFlyTick(const Buttons &, float dt) {
    Il2CppObject *head = g_head.get();
    Vector3 v;
    if (!f_joyFly || !head || !GetVel(v)) return;
    Vector2 l(0, 0), r(0, 0);
    ReadStick(4, l);
    ReadStick(5, r);
    float sp = kNoclipSpeeds[g_joySpeed] * SizeMul();
    Vector3 f = G.trGetForward[head](), side = Cross(Vector3(0, 1, 0), Flat(f));
    Vector3 target = f * (l.y * sp) + side * (l.x * sp) + Vector3(0, r.y * sp, 0);
    float k = 1.0f - expf(-8.0f * dt);
    SetVel(v + (target - v) * k);
    KeepFloating();
}
// Zero Gravity Fly: hold A to fly where you look. Let go and you stay where you are in the air, not moving,
// until you turn the mod off or fly again.
static void ZeroFlyTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get();
    Vector3 v;
    if (!f_zeroFly || !head || !GetVel(v)) return;
    if (in.a && !MenuBusy()) {
        Vector3 target = G.trGetForward[head]() * (kNoclipSpeeds[g_zeroSpeed] * SizeMul());
        float k = 1.0f - expf(-8.0f * dt);
        SetVel(v + (target - v) * k);
    } else {
        SetVel(Vector3(0, 0, 0));
    }
    KeepFloating();
}
// CS2 Strafe: hold A and you run forward where you look (the left stick curves you left / right). On the ground it is a
// steady run; in the air you get faster and faster the longer you stay up, and you can steer the speed with your head.
// There is no jump button: the game's own jump is used.
static float g_strafeAir = 0;
static void StrafeTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get();
    Vector3 v, body;
    if (!f_strafe || !head || !GetVel(v) || !BodyPos(body) || !in.a || MenuBusy()) { g_strafeAir = 0; return; }
    bool grounded = false;
    if (G.raycast.IsValid()) {
        BNM::Structures::Unity::RaycastHit hit{};
        grounded = G.raycast(body, Vector3(0, -1, 0), &hit, 0.8f, -5) && v.y < 1.0f;
    }
    Vector2 st(0, 0);
    ReadStick(4, st);
    Vector3 f = Flat(G.trGetForward[head]()), side = Cross(Vector3(0, 1, 0), f);
    Vector3 wish = f + side * (st.x * 0.8f);
    float wl = sqrtf(Len2(wish));
    if (wl < 0.01f) return;
    wish = wish * (1.0f / wl);
    Vector3 hv(v.x, 0, v.z);
    float speed = sqrtf(Len2(hv)), base = 6.0f * SizeMul();
    Vector3 nv;
    if (grounded) {
        g_strafeAir = 0;
        float k = 1.0f - expf(-12.0f * dt);
        nv = hv + (wish * base - hv) * k;
    } else {
        g_strafeAir += dt;
        float accel = 4.0f * kStrafeCap[g_strafeLvl] * (1.0f + 0.6f * g_strafeAir);      // m/s per second, more the longer you are up
        Vector3 dir = speed > 0.5f ? hv * (1.0f / speed) : wish;
        float turn = 3.0f * dt;
        dir = dir + (wish - dir) * (turn > 1.0f ? 1.0f : turn);
        float dl = sqrtf(Len2(dir));
        if (dl > 0.01f) dir = dir * (1.0f / dl);
        float sp = (speed > base ? speed : base) + accel * dt;
        if (sp > 45.0f) sp = 45.0f;
        nv = dir * sp;
    }
    SetVel(Vector3(nv.x, v.y, nv.z));
}
// Size Changer (Experimental): right trigger = bigger, right grip = smaller. Your player object is scaled, and
// the walking speed and jump follow your size (a big you is faster, a small you is slower).
static float g_sizeBaseJump = 0, g_sizeBaseMul = 0;
static Ref g_sizePar[2];
static void SizeRestore() {
    Il2CppObject *t = PlayerTransform(), *player = g_player.get();
    if (t) G.trSetLocalScale[t](Vector3(1, 1, 1));
    for (auto &p : g_sizePar) { if (Alive(p.get())) G.trSetLocalScale[p.get()](Vector3(1, 1, 1)); p.clear(); }
    if (player && G.maxJumpSpeed.IsValid() && G.jumpMultiplier.IsValid() && g_sizeBaseJump > 0) {
        G.maxJumpSpeed[player] = g_sizeBaseJump;
        G.jumpMultiplier[player] = g_sizeBaseMul;
    }
    g_sizeApplied = false;
}
static void ResetSize() { g_size = 1.0f; Notify("Size reset"); }
static void SizeTick(const Buttons &in, float dt) {
    Il2CppObject *t = PlayerTransform(), *player = g_player.get();
    if (!f_sizeChanger) { if (g_sizeApplied) SizeRestore(); g_size = 1.0f; return; }
    if (!t || !player) return;
    bool ok = G.maxJumpSpeed.IsValid() && G.jumpMultiplier.IsValid();
    if (!g_sizeApplied) {
        if (ok) { g_sizeBaseJump = G.maxJumpSpeed[player](); g_sizeBaseMul = G.jumpMultiplier[player](); }
        g_sizeApplied = true;
    }
    static const float kSizeVals[3] = {0.4f, 1.0f, 2.5f};            // Small, Normal, Big
    g_size += (kSizeVals[g_sizeSel] - g_size) * (1.0f - expf(-4.0f * dt));
    if (g_size < 0.25f) g_size = 0.25f;
    if (g_size > 5.0f) g_size = 5.0f;
    G.trSetLocalScale[t](Vector3(g_size, g_size, g_size));
    // The space the hand trackers move in is scaled too when it is not already under your player object, so your real
    // hand reach grows and shrinks with you and the arms and hands stay connected.
    static Method<bool> isChild;
    static bool looked = false;
    if (!looked) { looked = true; isChild = Class("UnityEngine", "Transform").GetMethod("IsChildOf", 1); }
    for (int i = 0; i < 2; i++) {
        Il2CppObject *hand = i ? g_rightHand.get() : g_leftHand.get();
        Il2CppObject *drv = hand ? TrackerDriver(hand) : nullptr;
        Il2CppObject *par = drv ? G.trGetParent[drv]() : nullptr;
        if (!par || par == t) continue;
        bool child = false;
        try { child = isChild.IsValid() && isChild[par](t); } catch (...) {}
        static int logged = 0;
        if (logged++ < 2) ym::log("leos-menu: size: hand %d tracker parent %s, under the player object: %d", i, ObjName(G.compGameObject[par]()).c_str(), (int)child);
        if (child) continue;
        G.trSetLocalScale[par](Vector3(g_size, g_size, g_size));
        g_sizePar[i].set(par);
    }
    if (ok && g_sizeBaseJump > 0) {
        float boost = f_speedBoost ? 2.0f : 1.0f;
        G.maxJumpSpeed[player] = g_sizeBaseJump * powf(g_size, 0.8f) * boost;
        G.jumpMultiplier[player] = g_sizeBaseMul * powf(g_size, 0.5f);
    }
}
// Pixelated menu type: the menu text is drawn at a low resolution and its font texture is not smoothed, so it gets blocky.
static double g_nextPixel = 0;
static void PixelApply(bool on) {
    try {
        static Method<void> setSF = Class("UnityEngine", "Canvas").GetMethod("set_scaleFactor", 1);
        if (setSF.IsValid() && G.tCanvas)
            for (auto &cg : g_menuCanvas) {
                Il2CppObject *go = cg.get();
                Il2CppObject *cv = Alive(go) ? G.goGetComponent[go](G.tCanvas) : nullptr;
                if (cv) setSF[cv](on ? 0.7f : 1.0f);
            }
        Il2CppObject *font = G.font.get();
        if (font) {
            static Method<Il2CppObject *> getMat = Class("UnityEngine", "Font").GetMethod("get_material", 0);
            static Method<Il2CppObject *> getTex = Class("UnityEngine", "Material").GetMethod("get_mainTexture", 0);
            static Method<void> setFilter = Class("UnityEngine", "Texture").GetMethod("set_filterMode", 1);
            Il2CppObject *mat = getMat.IsValid() ? getMat[font]() : nullptr;
            Il2CppObject *tex = (mat && getTex.IsValid()) ? getTex[mat]() : nullptr;
            if (tex && setFilter.IsValid()) setFilter[tex](on ? 0 : 1);        // Point / Bilinear
        }
    } catch (...) {}
    g_dirty = true;
}
static void PixelTick() {
    if (f_pixelMenu != g_pixelApplied) { g_pixelApplied = f_pixelMenu; PixelApply(f_pixelMenu); g_nextPixel = g_time + 1.0; return; }
    if (f_pixelMenu && g_time >= g_nextPixel) { g_nextPixel = g_time + 1.0; PixelApply(true); }   // the font texture can be rebuilt by the game
}

// ============================================================================================
// ---- Update 8 -------------------------------------------------------------------------------
// ============================================================================================

// ---- Tracers / Box ESP ----
static Ref g_tracerGo[16], g_tracerLine[16], g_boxGo[16], g_boxLine[16];
static double g_nextTracer = 0;
static void TracerKill(int i) { Kill(g_tracerGo[i]); g_tracerLine[i].clear(); Kill(g_boxGo[i]); g_boxLine[i].clear(); }
static void TracerBoxTick() {
    static bool had = false;
    if (!f_tracers && !f_boxEsp) { if (had) { for (int i = 0; i < 16; i++) TracerKill(i); had = false; } return; }
    had = true;
    Il2CppObject *me = g_head.get();
    if (!Alive(me)) return;
    if (g_time >= g_nextTracer) { g_nextTracer = g_time + 1.0; EspRefresh(); }
    Vector3 from = G.trGetPosition[me]() + G.trGetForward[me]() * 0.4f + Vector3(0, -0.3f, 0);
    Color col = Rainbow(fmodf((float)g_time * 0.2f, 1.0f));
    for (int i = 0; i < 16; i++) {
        Il2CppObject *head = g_rig[i].head.get();
        if (!Alive(head)) { TracerKill(i); continue; }
        Vector3 h = G.trGetPosition[head]();
        if (f_tracers) {
            if (!Alive(g_tracerLine[i].get()) && MakeLine(col, 0.006f, "LeoTracer", g_tracerGo[i], g_tracerLine[i])) G.lrSetPositionCount[g_tracerLine[i].get()](2);
            if (Il2CppObject *ln = g_tracerLine[i].get()) { G.lrSetPosition[ln](0, from); G.lrSetPosition[ln](1, h + Vector3(0, -0.4f, 0)); }
        } else { Kill(g_tracerGo[i]); g_tracerLine[i].clear(); }
        if (f_boxEsp) {
            if (!Alive(g_boxLine[i].get()) && MakeLine(col, 0.008f, "LeoBox", g_boxGo[i], g_boxLine[i])) G.lrSetPositionCount[g_boxLine[i].get()](16);
            if (Il2CppObject *ln = g_boxLine[i].get()) {
                float y0 = h.y - 1.55f, y1 = h.y + 0.2f, hw = 0.35f;
                Vector3 b[4] = {Vector3(h.x - hw, y0, h.z - hw), Vector3(h.x + hw, y0, h.z - hw), Vector3(h.x + hw, y0, h.z + hw), Vector3(h.x - hw, y0, h.z + hw)};
                Vector3 t[4] = {Vector3(h.x - hw, y1, h.z - hw), Vector3(h.x + hw, y1, h.z - hw), Vector3(h.x + hw, y1, h.z + hw), Vector3(h.x - hw, y1, h.z + hw)};
                Vector3 path[16] = {b[0], b[1], b[2], b[3], b[0], t[0], t[1], t[2], t[3], t[0], t[1], b[1], b[2], t[2], t[3], b[3]};   // all 12 edges
                for (int k = 0; k < 16; k++) G.lrSetPosition[ln](k, path[k]);
            }
        } else { Kill(g_boxGo[i]); g_boxLine[i].clear(); }
    }
}

// ---- Rainbow Sky: the cameras' background color cycles smoothly through the rainbow ----
struct SkyCam { Ref cam; int flags = 1; Color bg = Color(0, 0, 0, 1); };
static std::vector<SkyCam> g_skyCams;
static double g_nextSky = 0;
static float g_skyHue = 0;
static void SkyRestore() {
    Method<void> setFlags, setBg;
    setFlags = Class("UnityEngine", "Camera").GetMethod("set_clearFlags", 1);
    setBg = Class("UnityEngine", "Camera").GetMethod("set_backgroundColor", 1);
    for (auto &c : g_skyCams) {
        Il2CppObject *cam = c.cam.get();
        if (Alive(cam)) {
            try { if (setFlags.IsValid()) setFlags[cam](c.flags); if (setBg.IsValid()) setBg[cam](c.bg); } catch (...) {}
        }
        c.cam.clear();
    }
    g_skyCams.clear();
}
static void SkyTick(float dt) {
    if (!f_rainbowSky) { if (!g_skyCams.empty()) SkyRestore(); return; }
    Class cc("UnityEngine", "Camera");
    Method<void> setFlags, setBg;
    Method<int> getFlags;
    Method<Color> getBg;
    setFlags = cc.GetMethod("set_clearFlags", 1);
    setBg = cc.GetMethod("set_backgroundColor", 1);
    getFlags = cc.GetMethod("get_clearFlags", 0);
    getBg = cc.GetMethod("get_backgroundColor", 0);
    if (!setFlags.IsValid() || !setBg.IsValid() || !getFlags.IsValid() || !getBg.IsValid()) { f_rainbowSky = false; Notify("Rainbow Sky isn't available here"); return; }
    if (g_skyCams.empty() || g_time >= g_nextSky) {
        g_nextSky = g_time + 3.0;
        uint8_t *arr = ScanAll(cc.GetMonoType());
        for (size_t i = 0; i < ArrLen(arr) && i < 8; i++) {
            Il2CppObject *cam = ArrAt(arr, i);
            if (!Alive(cam)) continue;
            bool known = false;
            for (auto &c : g_skyCams) if (c.cam.get() == cam) known = true;
            if (known) continue;
            SkyCam sc;
            sc.cam.set(cam);
            sc.flags = getFlags[cam]();
            sc.bg = getBg[cam]();
            g_skyCams.push_back(sc);
        }
    }
    g_skyHue = fmodf(g_skyHue + dt * 0.04f, 1.0f);
    Color c = Rainbow(g_skyHue);
    for (auto &sc : g_skyCams) {
        Il2CppObject *cam = sc.cam.get();
        if (!Alive(cam)) continue;
        try { setFlags[cam](2); setBg[cam](c); } catch (...) {}      // 2 = solid color
    }
}

// ---- Solid Players: a see-through solid box follows every other player, so you can stand on them ----
static Ref g_solid[16];
static double g_nextSolid = 0;
static void SolidTick() {
    if (!f_solidPlayers) { for (auto &r : g_solid) if (r.get()) Kill(r); return; }
    if (g_time >= g_nextSolid) { g_nextSolid = g_time + 1.0; EspRefresh(); }
    for (int i = 0; i < 16; i++) {
        Il2CppObject *head = g_rig[i].head.get();
        if (!Alive(head)) { Kill(g_solid[i]); continue; }
        Vector3 h = G.trGetPosition[head](), pos(h.x, h.y + 0.18f, h.z);
        if (!Alive(g_solid[i].get())) {
            if (Il2CppObject *p = NewPlatform(pos, Vector3(0.55f, 0.1f, 0.55f), Color(1, 1, 1, 0.15f))) { MatTransparent(p, true); g_solid[i].set(p); }
        } else G.trSetPosition[Tr(g_solid[i].get())](pos);
    }
}

// ---- Leaderboard Color: anything named leaderboard / scoreboard gets the menu's colors ----
static double g_nextLb = 0;
static void LeaderboardTick() {
    if (!f_lbColor || g_time < g_nextLb || !G.tRenderer || !G.objGetName.IsValid()) return;
    g_nextLb = g_time + 5.0;
    int hits = 0;
    uint8_t *arr = ScanAll(G.tRenderer);
    for (size_t i = 0; i < ArrLen(arr); i++) {
        Il2CppObject *r = ArrAt(arr, i);
        if (!Alive(r)) continue;
        Il2CppObject *go = G.compGameObject[r]();
        std::string n = ObjName(go);
        Il2CppObject *par = G.trGetParent[Tr(go)]();
        if (par) n += "/" + ObjName(G.compGameObject[par]());
        if (!HasAny(n, {"leaderboard", "scoreboard"})) continue;
        SetColor(go, T().bgB);
        if (hits++ < 5) ym::log("leos-menu: leaderboard colored: %s", n.c_str());
    }
    Class tmp("TMPro", "TMP_Text");
    if (tmp.IsValid()) {
        Method<void> setCol;
        setCol = tmp.GetMethod("set_color", 1);
        uint8_t *ta = setCol.IsValid() ? ScanAll(tmp.GetMonoType()) : nullptr;
        for (size_t i = 0; ta && i < ArrLen(ta); i++) {
            Il2CppObject *t = ArrAt(ta, i);
            if (!Alive(t)) continue;
            Il2CppObject *go = G.compGameObject[t]();
            std::string n = ObjName(go);
            Il2CppObject *par = G.trGetParent[Tr(go)]();
            if (par) n += "/" + ObjName(G.compGameObject[par]());
            if (!HasAny(n, {"leaderboard", "scoreboard"})) continue;
            try { setCol[t](T().title); } catch (...) {}
            if (hits++ < 5) ym::log("leos-menu: leaderboard text colored: %s", n.c_str());
        }
    }
}

// ---- Bloom, glow part: the strongest lights near you get soft see-through glow balls ----
struct Halo { Ref a, b; };
static Halo g_halo[20];
static Ref g_haloLight[20];
static double g_nextHalo = 0;
static void HaloKill() { for (int i = 0; i < 20; i++) { Kill(g_halo[i].a); Kill(g_halo[i].b); g_haloLight[i].clear(); } }
static void HaloTick() {
    static bool had = false;
    if (!f_bloom) { if (had) { HaloKill(); had = false; } return; }
    had = true;
    Il2CppObject *head = g_head.get();
    if (!Alive(head) || !G.tLight || !G.lightGetIntensity.IsValid()) return;
    Vector3 hp = G.trGetPosition[head]();
    if (g_time >= g_nextHalo) {
        g_nextHalo = g_time + 1.5;
        std::vector<std::pair<float, Il2CppObject *>> found;
        uint8_t *arr = ScanAll(G.tLight);
        for (size_t i = 0; i < ArrLen(arr); i++) {
            Il2CppObject *l = ArrAt(arr, i);
            if (!Alive(l)) continue;
            Il2CppObject *go = G.compGameObject[l]();
            if (!go || (G.goActiveInHierarchy.IsValid() && !G.goActiveInHierarchy[go]())) continue;
            if (G.lightGetIntensity[l]() < 0.3f) continue;
            float d = Len2(G.trGetPosition[G.compTransform[l]()]() - hp);
            if (d < 70.0f * 70.0f) found.push_back({d, l});
        }
        std::sort(found.begin(), found.end(), [](const std::pair<float, Il2CppObject *> &a, const std::pair<float, Il2CppObject *> &b) { return a.first < b.first; });
        for (int i = 0; i < 20; i++) {
            Il2CppObject *l = i < (int)found.size() ? found[(size_t)i].second : nullptr;
            if (l) g_haloLight[i].set(l); else { g_haloLight[i].clear(); Kill(g_halo[i].a); Kill(g_halo[i].b); }
        }
    }
    for (int i = 0; i < 20; i++) {
        Il2CppObject *l = g_haloLight[i].get();
        if (!Alive(l)) { Kill(g_halo[i].a); Kill(g_halo[i].b); continue; }
        Vector3 p = G.trGetPosition[G.compTransform[l]()]();
        float rad = 0.2f + 0.12f * G.lightGetIntensity[l]();
        if (rad < 0.25f) rad = 0.25f;
        if (rad > 1.2f) rad = 1.2f;
        bool tooClose = Len2(p - hp) < 0.9f * 0.9f;
        for (int k = 0; k < 2; k++) {
            Ref &r = k ? g_halo[i].b : g_halo[i].a;
            if (!Alive(r.get())) {
                Il2CppObject *o = Prim(0, k ? Color(1, 0.92f, 0.7f, 0.06f) : Color(1, 0.92f, 0.7f, 0.16f), false);
                if (o) { MatTransparent(o, true); r.set(o); }
            }
            Il2CppObject *o = r.get();
            if (!Alive(o)) continue;
            Show(r, !tooClose);
            G.trSetPosition[Tr(o)](p);
            float sc = rad * (k ? 4.0f : 2.0f);
            G.trSetLocalScale[Tr(o)](Vector3(sc, sc, sc));
        }
    }
}

// ---- Disable Boundary, part 2: game scripts that look after the map edge are switched off ----
static Ref g_bBeh[64];
static int g_bBehN = 0;
static double g_nextBeh = 0;
static void BoundBehTick() {
    static Method<void> setEn;
    static bool looked = false;
    if (!looked) { looked = true; setEn = Class("UnityEngine", "Behaviour").GetMethod("set_enabled", 1); }
    if (!setEn.IsValid()) return;
    if (!f_noBounds) {
        for (int i = 0; i < g_bBehN; i++) { if (Alive(g_bBeh[i].get())) { try { setEn[g_bBeh[i].get()](true); } catch (...) {} } g_bBeh[i].clear(); }
        g_bBehN = 0;
        return;
    }
    if (g_time < g_nextBeh || !EnsureFieldApi() || !p_objClass) return;
    g_nextBeh = g_time + 10.0;
    static BNM::MonoType *tMono = Class("UnityEngine", "MonoBehaviour").GetMonoType();
    uint8_t *arr = ScanAll(tMono);
    for (size_t i = 0; i < ArrLen(arr) && g_bBehN < 64; i++) {
        Il2CppObject *o = ArrAt(arr, i);
        if (!Alive(o)) continue;
        auto *kc = (BNM::IL2CPP::Il2CppClass *)p_objClass(o);
        if (!kc || (kc->namespaze && kc->namespaze[0]) || !kc->name) continue;
        std::string cn = Lower(kc->name);
        if (!HasAny(cn, {"outofbounds", "outofmap", "mapedge", "worldedge", "mapboundary", "worldboundary", "boundarycheck", "boundarykill", "playareabound"})) continue;
        if (HasAny(cn, {"collider", "player", "camera", "network", "photon", "input", "rig"})) continue;
        try { setEn[o](false); } catch (...) { continue; }
        g_bBeh[g_bBehN++].set(o);
        ym::log("leos-menu: boundary script off: %s", kc->name);
    }
}

// ---- Mic Changer: High Pitch / Low Pitch / Broken ----
// Your microphone is recorded into a loop, run through a pitch shifter (two crossfading delay lines), and the result is
// written into a second audio clip that the voice recorder is set to send (the same way the soundboard sends sounds).
static constexpr int kMicHz = 16000, kMicOut = 32000, kMicR = 8192, kMicW = 1024;
static Ref g_micIn, g_micOut;
static int g_micLast = 0, g_micWrite = 0, g_micRunMode = -1, g_micRw = 0, g_micDropLeft = 0;
static std::vector<float> g_micRing(kMicR, 0.0f);
static float g_micP = 0, g_micRate = 1.0f;
static double g_micJit = 0;
static Il2CppObject *NewFloats(int n) { return (Il2CppObject *)G.arrayNew(G.floatClass, (size_t)n); }
static void MicEnd() {
    static Method<void> end;
    static bool looked = false;
    if (!looked) { looked = true; end = Class("UnityEngine", "Microphone").GetMethod("End", 1); }
    try { if (end.IsValid()) end((BNM::Structures::Mono::String *)nullptr); } catch (...) {}
    g_micIn.clear();
    g_micOut.clear();
    g_micRunMode = -1;
    g_micRun = false;
}
static bool MicBegin() {
    Class mic("UnityEngine", "Microphone");
    Method<Il2CppObject *> start;
    start = mic.GetMethod("Start", 4);
    Il2CppObject *rec = VoiceRecorder();
    if (!start.IsValid() || !G.audioCreate.IsValid() || !G.audioSetData.IsValid() || !G.arrayNew || !rec || !InRoom()) return false;
    Il2CppObject *outc = G.audioCreate((BNM::Structures::Mono::String *)CreateMonoString("LeoMic"), kMicOut, 1, kMicHz, false);
    if (!outc) return false;
    g_micOut.set(outc);
    RecCall(rec, {"set_SourceType", "set_sourceType"}, 1);
    RecCall(rec, {"set_AudioClip", "set_audioClip"}, outc);
    RecCall(rec, {"set_LoopAudioClip", "set_loopAudioClip"}, true);
    RecCall(rec, {"set_TransmitEnabled"}, true);
    RecCall(rec, {"set_IsRecording"}, true);
    RecCall(rec, {"RestartRecording"}, true);
    RecCall(rec, {"StartRecording"});
    Il2CppObject *in = start((BNM::Structures::Mono::String *)nullptr, true, 1, kMicHz);
    if (!in) { g_micOut.clear(); RestoreMic(); return false; }
    g_micIn.set(in);
    g_micLast = 0;
    g_micWrite = kMicHz / 4;                                    // we write a quarter second ahead of where the recorder reads
    g_micRing.assign(kMicR, 0.0f);
    g_micRw = 0;
    g_micP = 0;
    g_micRun = true;
    return true;
}
static float MicSample(int mode, float x) {
    if (mode == 3) {                                            // Broken: crushed, wobbling, with drop-outs
        if (g_micDropLeft > 0) { g_micDropLeft--; x = 0; }
        else if ((rand() % 12000) == 0) g_micDropLeft = 600 + rand() % 1800;
        x = roundf(x * 6.0f) / 6.0f;
    }
    g_micRing[(size_t)g_micRw] = x;
    g_micRw = (g_micRw + 1) % kMicR;
    float r = mode == 1 ? 1.6f : mode == 2 ? 0.65f : g_micRate;
    g_micP -= (r - 1.0f) / (float)kMicW;
    g_micP -= floorf(g_micP);
    float p2 = g_micP + 0.5f;
    p2 -= floorf(p2);
    int d1 = (int)(g_micP * kMicW) + 32, d2 = (int)(p2 * kMicW) + 32;
    float w1 = 0.5f - 0.5f * cosf(6.2831853f * g_micP), w2 = 0.5f - 0.5f * cosf(6.2831853f * p2);
    float a = g_micRing[(size_t)((g_micRw - 1 - d1 + 2 * kMicR) % kMicR)], b = g_micRing[(size_t)((g_micRw - 1 - d2 + 2 * kMicR) % kMicR)];
    return a * w1 + b * w2;
}
static void MicTick() {
    int mode = g_micMode;
    if (!f_micFx || mode == 0) { if (g_micRunMode != -1) { MicEnd(); RestoreMic(); } return; }
    if (g_micRestoreAt > 0) { if (g_micRunMode != -1) MicEnd(); return; }       // a soundboard sound owns the recorder right now
    if (g_micRunMode != mode) {
        if (g_micRunMode == -1 && !MicBegin()) { f_micFx = false; Notify("Mic Changer isn't available here"); return; }
        g_micRunMode = mode;
    }
    if (mode == 3 && g_time >= g_micJit) { g_micJit = g_time + 0.15; g_micRate = 0.75f + (rand() % 1000) / 1000.0f * 0.75f; }
    static Method<int> getPos, getSamples, getHz;
    static Method<bool> getData;
    static bool looked = false;
    if (!looked) {
        looked = true;
        getPos = Class("UnityEngine", "Microphone").GetMethod("GetPosition", 1);
        getData = Class("UnityEngine", "AudioClip").GetMethod("GetData", 2);
        getSamples = Class("UnityEngine", "AudioClip").GetMethod("get_samples", 0);
        getHz = Class("UnityEngine", "AudioClip").GetMethod("get_frequency", 0);
    }
    Il2CppObject *in = g_micIn.get(), *out = g_micOut.get();
    if (!getPos.IsValid() || !getData.IsValid() || !getSamples.IsValid() || !getHz.IsValid() || !Alive(in) || !Alive(out)) return;
    int inLen = getSamples[in](), inHz = getHz[in]();                       // the device may not give exactly what was asked for
    if (inLen < 1000 || inHz < 4000 || inHz > 96000) return;
    int pos = getPos((BNM::Structures::Mono::String *)nullptr);
    if (pos < 0 || pos >= inLen) return;
    if (g_micLast >= inLen) g_micLast = 0;
    int n = (pos - g_micLast + inLen) % inLen;
    if (n <= 0) return;
    int maxN = inHz / 4;
    if (n > maxN) { g_micLast = (pos - maxN + inLen) % inLen; n = maxN; }
    std::vector<float> raw((size_t)n);
    int got = 0;
    while (got < n) {
        int off = (g_micLast + got) % inLen, len = std::min(n - got, inLen - off);
        Il2CppObject *arr = NewFloats(len);
        if (!arr) return;
        getData[in](arr, off);
        memcpy(&raw[(size_t)got], (uint8_t *)arr + 0x20, (size_t)len * 4);
        got += len;
    }
    int m = (int)((long long)n * kMicHz / inHz);                           // to the 16 kHz the effect runs at
    if (m < 1) { g_micLast = pos; return; }
    std::vector<float> samples((size_t)m);
    for (int i = 0; i < m; i++) {
        long long idx = (long long)i * inHz / kMicHz;
        samples[(size_t)i] = MicSample(mode, raw[(size_t)(idx < n ? idx : n - 1)]);
    }
    int done = 0;
    while (done < m) {
        int len = std::min(m - done, kMicOut - g_micWrite);
        Il2CppObject *arr = NewFloats(len);
        if (!arr) return;
        memcpy((uint8_t *)arr + 0x20, &samples[(size_t)done], (size_t)len * 4);
        G.audioSetData[out](arr, g_micWrite);
        g_micWrite = (g_micWrite + len) % kMicOut;
        done += len;
    }
    g_micLast = pos;
}

// ---- Ghost Monkey / Inv Monkey ----
// With the mod on in the menu, a button press switches it:
//   Ghost Monkey: X (A if the menu is on your right hand). Your real rig freezes for everyone else while you walk around
//                 freely; press again to unfreeze.
//   Inv Monkey:   left stick click. You turn invisible for everyone else while you still move around; click again to come back.
// Both stop your own PhotonViews from sending, so the room keeps showing the last thing it got. For Inv Monkey you are sent
// 4000 m up for 0.7 seconds first, so that last thing is far away in the sky. While either is active a ball in the menu's
// color sits on each of your hands.
struct SyncSave { Ref pv; int old = 0; };
static std::vector<SyncSave> g_syncSaved;
static bool g_ghostState = false, g_invState = false, g_gPrev = false, g_iPrev = false;
static int g_invPhase = 0;                                   // 0 not started, 1 going up, 2 invisible
static double g_nextGhost = 0, g_invAt = 0;
static Vector3 g_invBack;
static Ref g_orb[2];
static void SyncRestore() {
    Method<void> setSync;
    setSync = Class("Photon.Pun", "PhotonView").GetMethod("set_Synchronization", 1);
    for (auto &sv : g_syncSaved) {
        Il2CppObject *pv = sv.pv.get();
        if (Alive(pv) && setSync.IsValid()) { try { setSync[pv](sv.old); } catch (...) {} }
        sv.pv.clear();
    }
    g_syncSaved.clear();
}
static void GhostApply() {
    Class pc("Photon.Pun", "PhotonView");
    Method<bool> isMine;
    Method<int> getSync;
    Method<void> setSync;
    isMine = pc.GetMethod("get_IsMine", 0);
    getSync = pc.GetMethod("get_Synchronization", 0);
    setSync = pc.GetMethod("set_Synchronization", 1);
    if (!isMine.IsValid() || !getSync.IsValid() || !setSync.IsValid() || !G.tPhotonView) return;
    uint8_t *arr = ScanAll(G.tPhotonView);
    size_t n = ArrLen(arr);
    int changed = 0;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *pv = ArrAt(arr, i);
        if (!Alive(pv)) continue;
        try {
            if (!isMine[pv]()) continue;
            int cur = getSync[pv]();
            if (cur == 0) continue;
            SyncSave sv;
            sv.pv.set(pv);
            sv.old = cur;
            g_syncSaved.push_back(sv);
            setSync[pv](0);                                 // ViewSynchronization.Off
            changed++;
        } catch (...) {}
    }
    static int logged = 0;
    if (changed && logged++ < 6) ym::log("leos-menu: ghost: stopped %d of your network views", changed);
}
static void GhostTick(const Buttons &in, float dt) {
    (void)dt;
    bool gb = f_rightHand ? in.a : in.x, ib = in.lstick;
    bool gEdge = gb && !g_gPrev, iEdge = ib && !g_iPrev;
    g_gPrev = gb;
    g_iPrev = ib;
    Il2CppObject *player = g_player.get();
    if (!f_ghost) g_ghostState = false;
    if (!f_inv) { g_invState = false; g_invPhase = 0; }
    if (player && !MenuBusy()) {
        if (f_ghost && gEdge) {
            g_ghostState = !g_ghostState;
            if (g_ghostState) { g_invState = false; g_invPhase = 0; }
            Notify(g_ghostState ? "Ghost Monkey: frozen for others" : "Ghost Monkey off");
        }
        if (f_inv && iEdge) {
            g_invState = !g_invState;
            g_invPhase = 0;
            if (g_invState) g_ghostState = false;
            Notify(g_invState ? "Inv Monkey: invisible" : "Inv Monkey off");
        }
    }
    if (g_invState && g_invPhase == 0 && player) {          // going invisible: first to the sky, while still sending
        SyncRestore();
        Il2CppObject *t = PlayerTransform();
        if (t) {
            g_invBack = G.trGetPosition[t]();
            TeleportPlayer(g_invBack + Vector3(0, 4000, 0));
            g_invPhase = 1;
            g_invAt = g_time + 0.7;
        }
    }
    if (g_invState && g_invPhase == 1) {
        StopMoving();
        KeepFloating();
        if (g_time >= g_invAt) { GhostApply(); TeleportPlayer(g_invBack); StopMoving(); g_invPhase = 2; }
        return;
    }
    bool frozen = g_ghostState || (g_invState && g_invPhase == 2);
    if (frozen) {
        if (g_syncSaved.empty() || g_time >= g_nextGhost) { g_nextGhost = g_time + 1.0; GhostApply(); }   // new views get caught too
    } else if (!g_syncSaved.empty()) SyncRestore();         // sending again
    if (!frozen) { for (auto &o : g_orb) Kill(o); return; }
    Il2CppObject *hands[2] = {g_leftHand.get(), g_rightHand.get()};
    for (int i = 0; i < 2; i++) {
        if (!Alive(g_orb[i].get())) {
            Il2CppObject *o = Prim(0, T().bgB, false);
            if (o) { G.trSetLocalScale[Tr(o)](Vector3(0.12f, 0.12f, 0.12f)); g_orb[i].set(o); }
        }
        Il2CppObject *o = g_orb[i].get();
        if (!Alive(o) || !hands[i]) continue;
        G.trSetPosition[Tr(o)](G.trGetPosition[hands[i]]());
        SetColor(o, T().bgB);
    }
}

// ---- Camera screens: Mini Map and Drone Mode ----
// A second camera renders into a small texture; a quad stuck to your head (in a top corner of your view) shows that texture.
struct CamScreen { Ref camGo, cam, rt, screen, frame; };
static CamScreen g_mini, g_droneCam;
static void CamScreenKill(CamScreen &s) { Kill(s.camGo); s.cam.clear(); s.rt.clear(); Kill(s.screen); Kill(s.frame); }
static bool CamScreenBuild(CamScreen &s, Vector3 local, float w, float h, bool ortho, float size, float fov) {
    Il2CppObject *head = g_head.get();
    Class camC("UnityEngine", "Camera"), rtC("UnityEngine", "RenderTexture"), matC("UnityEngine", "Material");
    if (!Alive(head) || !camC.IsValid() || !rtC.IsValid() || !matC.IsValid() || !G.tRenderer) return false;
    try {
        Il2CppObject *rt = rtC.CreateNewObjectParameters(256, 256, 16);
        Il2CppObject *cgo = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("LeoCam"));
        Il2CppObject *cam = (rt && cgo) ? G.goAddComponent[cgo]((Il2CppObject *)camC.GetMonoType()) : nullptr;
        if (!cam) { if (cgo) G.destroy(cgo); return false; }
        Method<void> setTarget, setOrtho, setOrthoSize, setFov, setFlags, setBg, setNear, setFar;
        setTarget = camC.GetMethod("set_targetTexture", 1);
        setOrtho = camC.GetMethod("set_orthographic", 1);
        setOrthoSize = camC.GetMethod("set_orthographicSize", 1);
        setFov = camC.GetMethod("set_fieldOfView", 1);
        setFlags = camC.GetMethod("set_clearFlags", 1);
        setBg = camC.GetMethod("set_backgroundColor", 1);
        setNear = camC.GetMethod("set_nearClipPlane", 1);
        setFar = camC.GetMethod("set_farClipPlane", 1);
        if (!setTarget.IsValid()) { G.destroy(cgo); return false; }
        setTarget[cam](rt);
        if (setOrtho.IsValid()) setOrtho[cam](ortho);
        if (ortho && setOrthoSize.IsValid()) setOrthoSize[cam](size);
        if (!ortho && setFov.IsValid()) setFov[cam](fov);
        if (setFlags.IsValid()) setFlags[cam](2);
        if (setBg.IsValid()) setBg[cam](Color(0.05f, 0.07f, 0.1f, 1));
        if (setNear.IsValid()) setNear[cam](0.2f);
        if (setFar.IsValid()) setFar[cam](400.0f);
        s.camGo.set(cgo);
        s.cam.set(cam);
        s.rt.set(rt);
        Il2CppObject *q = Prim(5, Color(1, 1, 1, 1), false);        // Quad
        Il2CppObject *fr = Prim(5, Color(0.05f, 0.05f, 0.08f, 1), false);
        if (q) {
            Il2CppObject *t = Tr(q);
            G.trSetParent[t](head, false);
            G.trSetLocalPosition[t](local);
            G.trSetLocalScale[t](Vector3(w, h, 1));
            if (auto *r = G.goGetComponent[q](G.tRenderer))
                if (Il2CppObject *m = G.rendererGetMaterial[r]()) {
                    Method<void> setTex, setTexN;
                    setTex = matC.GetMethod("set_mainTexture", 1);
                    setTexN = matC.GetMethod("SetTexture", 2);
                    if (setTex.IsValid()) setTex[m](rt);
                    if (setTexN.IsValid()) setTexN[m](MS("_BaseMap"), rt);
                }
            s.screen.set(q);
        }
        if (fr) {
            Il2CppObject *t = Tr(fr);
            G.trSetParent[t](head, false);
            G.trSetLocalPosition[t](Vector3(local.x, local.y, local.z + 0.002f));
            G.trSetLocalScale[t](Vector3(w + 0.012f, h + 0.012f, 1));
            s.frame.set(fr);
        }
        return true;
    } catch (...) { return false; }
}
static void MiniMapTick() {
    static bool had = false;
    if (!f_miniMap) { if (had) { CamScreenKill(g_mini); had = false; } return; }
    Il2CppObject *head = g_head.get();
    if (!Alive(head)) return;
    had = true;
    if (!Alive(g_mini.camGo.get()) && !CamScreenBuild(g_mini, Vector3(0.24f, 0.13f, 0.6f), 0.2f, 0.2f, true, 16.0f, 60.0f)) {
        f_miniMap = false;
        had = false;
        CamScreenKill(g_mini);
        Notify("Mini Map isn't available here");
        return;
    }
    Vector3 f = Flat(G.trGetForward[head]());
    Il2CppObject *ct = Tr(g_mini.camGo.get());
    G.trSetPosition[ct](G.trGetPosition[head]() + Vector3(0, 40.0f, 0));
    G.trSetRotation[ct](Quaternion::LookRotation(Vector3(0, -1, 0), f));        // looking down, the top of the screen is where you face
}
// Drone Mode: the camera is a drone. Left stick moves it (forward / back / sideways), right stick turns it (left / right) and
// raises / lowers it. You stand still (your sideways walking is held at zero), the drone's view is on the screen in the top left.
static Vector3 g_dronePos;
static float g_droneYaw = 0;
static void DroneCamTick(const Buttons &, float dt) {
    static bool had = false;
    if (!f_drone) { if (had) { CamScreenKill(g_droneCam); had = false; } return; }
    Il2CppObject *head = g_head.get();
    if (!Alive(head)) return;
    if (!Alive(g_droneCam.camGo.get())) {
        if (!CamScreenBuild(g_droneCam, Vector3(-0.24f, 0.13f, 0.6f), 0.22f, 0.22f, false, 0, 80.0f)) {
            f_drone = false;
            had = false;
            CamScreenKill(g_droneCam);
            Notify("Freecam isn't available here");
            return;
        }
        Vector3 f = Flat(G.trGetForward[head]());
        g_dronePos = G.trGetPosition[head]() + f * 0.6f;
        g_droneYaw = atan2f(f.x, f.z);
    }
    had = true;
    Vector2 l(0, 0), r(0, 0);
    ReadStick(4, l);
    ReadStick(5, r);
    float sp = kNoclipSpeeds[g_droneSpeed];
    g_droneYaw += r.x * 1.6f * dt;
    Vector3 fwd(sinf(g_droneYaw), 0, cosf(g_droneYaw)), side(cosf(g_droneYaw), 0, -sinf(g_droneYaw));
    g_dronePos = g_dronePos + fwd * (l.y * sp * dt) + side * (l.x * sp * dt) + Vector3(0, r.y * sp * dt, 0);
    const float pitch = -0.15f;
    Vector3 look(sinf(g_droneYaw) * cosf(pitch), sinf(pitch), cosf(g_droneYaw) * cosf(pitch));
    Il2CppObject *ct = Tr(g_droneCam.camGo.get());
    G.trSetPosition[ct](g_dronePos);
    G.trSetRotation[ct](Quaternion::LookRotation(look, Vector3(0, 1, 0)));
    Vector3 v;
    if (GetVel(v)) SetVel(Vector3(0, v.y, 0));                // you stay put, only the drone moves
}

// ---- Reverse Velocity: press B (X with the menu on the right hand) and the way you are moving is reversed ----
static bool g_revPrev = false;
static void ReverseVelTick(const Buttons &in) {
    bool btn = f_rightHand ? in.x : in.b;
    bool edge = btn && !g_revPrev;
    g_revPrev = btn;
    Vector3 v;
    if (!f_revVel || !edge || MenuBusy() || !GetVel(v)) return;
    SetVel(v * -1.0f);
}

// ---- Potions: Potion Effect Gun and Give Self Potion ----
// The game's own effect methods are looked for by name on the MasterPlayer class: a method with one number (seconds) that has
// leg / visib / love / blind / small / big / slowfall / nogravity (levitation) in its name. Everything with potion / effect /
// duration in its name is written to the log ("leos-menu: potion method ...") so the names can be fixed if one is missing.
// The gun calls the method on the other player's object in your game; whether that reaches them depends on how the game
// shares effects, so it may only show on your side.
static Method<void> g_potM[8];
static bool g_potDone = false;
static void PotionResolve() {
    if (g_potDone) return;
    g_potDone = true;
    static const char *kKeys[8][5] = {{"legs", nullptr}, {"visib", "invis", nullptr}, {"love", "heart", "charm", nullptr}, {"blind", nullptr},
                                      {"small", "shrink", "tiny", nullptr}, {"big", "grow", "giant", "huge", nullptr},
                                      {"slowfall", "featherfall", "slowfalling", "feather", nullptr}, {"nogravity", "levitat", nullptr}};
    Class mp("", "MasterPlayer", Image("Assembly-CSharp.dll"));
    if (!mp.IsValid()) { ym::log("leos-menu: potion: no MasterPlayer class"); return; }
    int logged = 0;
    for (auto &m : mp.GetMethods(false)) {
        auto *mi = m.GetInfo();
        if (!mi || !mi->name) continue;
        std::string nm = Lower(mi->name);
        if (HasAny(nm, {"potion", "effect", "duration"}) && logged++ < 60) ym::log("leos-menu: potion method %s/%d", mi->name, (int)mi->parameters_count);
        if (mi->parameters_count != 1 || Has(nm, "glove")) continue;
        if (!HasAny(nm, {"apply", "give", "add", "duration", "effect"})) continue;
        for (int e = 0; e < 8; e++) {
            if (g_potM[e].IsValid()) continue;
            for (int k = 0; kKeys[e][k]; k++)
                if (Has(nm, kKeys[e][k])) { g_potM[e] = m; ym::log("leos-menu: potion %s uses %s", kPotionN[e], mi->name); break; }
        }
    }
    if (!g_potM[7].IsValid() && G.plNoGravityFor.IsValid()) g_potM[7] = G.plNoGravityFor;
}
static bool PotionApply(Il2CppObject *target, int fx, float secs) {
    PotionResolve();
    if (!Alive(target) || fx < 0 || fx > 7 || !g_potM[fx].IsValid()) return false;
    try { g_potM[fx][target](secs); return true; } catch (...) { return false; }
}
static void GiveSelfPotion() { GiveSelfPotionImpl(); }
static double g_nextPotion = 0;
static void PotionGunAim(Vector3 start, Vector3 dir) {
    if (g_time < g_nextPotion) return;
    g_nextPotion = g_time + 0.5;
    EspRefresh();
    int best = -1;
    float bestT = 1e30f;
    if (f_targetLock) best = LockTargetPersist(start, true);      // Target Lock: the closest player in the radius (or the kept one)
    else
        for (int i = 0; i < 16; i++) {
            Il2CppObject *head = g_rig[i].head.get();
            if (!Alive(head)) continue;
            Vector3 to = G.trGetPosition[head]() - Vector3(0, 0.5f, 0) - start;
            float t = Dot(to, dir);
            if (t < 0.2f || t > 60.0f) continue;
            Vector3 perp = to - dir * t;
            float rad = 0.6f + 0.02f * t;
            if (Len2(perp) > rad * rad || t >= bestT) continue;
            best = i;
            bestT = t;
        }
    if (best < 0) { Notify("No player there"); return; }
    PotionOnTarget(g_rig[best].obj.get(), g_rig[best].head.get(), g_potionGun);
}

// ---- Punch Fling: another player's hand moving fast into your head or chest flings you the way the hand was moving ----
static Vector3 g_punchPrev[16][2];
static bool g_punchHave[16][2];
static double g_punchStamp = 0, g_nextPunchRef = 0;
static void PunchTick() {
    Il2CppObject *me = g_head.get();
    if (!f_punch || !Alive(me)) return;
    if (g_time >= g_nextPunchRef) { g_nextPunchRef = g_time + 1.0; EspRefresh(); }
    if (g_time - g_punchStamp < 0.1) return;                 // hands are compared every 0.1 s (the room updates them less often than frames)
    float span = (float)(g_time - g_punchStamp);
    g_punchStamp = g_time;
    Vector3 hp = G.trGetPosition[me](), chest = hp + Vector3(0, -0.35f, 0);
    bool punched = false;
    Vector3 pv(0, 0, 0);
    for (int i = 0; i < 16; i++)
        for (int h = 0; h < 2; h++) {
            Il2CppObject *o = h ? g_rig[i].rh.get() : g_rig[i].lh.get();
            if (!Alive(o)) { g_punchHave[i][h] = false; continue; }
            Vector3 p = G.trGetPosition[o]();
            if (g_punchHave[i][h] && g_time >= g_nextPunch) {
                Vector3 vel = (p - g_punchPrev[i][h]) * (1.0f / span);
                if (Len2(vel) > 2.5f * 2.5f && (Len2(p - hp) < 0.45f * 0.45f || Len2(p - chest) < 0.45f * 0.45f)) { punched = true; pv = vel; }
            }
            g_punchPrev[i][h] = p;
            g_punchHave[i][h] = true;
        }
    if (!punched) return;
    Vector3 dn = Flat(pv);
    float power = kPunchPow[g_punchPower];
    SetVel(dn * power + Vector3(0, power * 0.35f, 0));
    g_nextPunch = g_time + 0.7;
    Notify("Punched! Flung");
}

// ---- Spawn Presets: mob spawn eggs ----
// ---- Spawn Presets: mob eggs. The real item is "mobSpawnEgg" with 3 string parameters (the mob's name, then its
// primary and secondary colors) sent through the same item message as SendItem, extended to carry those parameters.
static bool SendItemP(const char *key, Vector3 pos, Quaternion rot, Vector3 vel, const char *const *params, int nParams, char *idOut = nullptr) {
    auto *mi = G.rpcCreateItem;
    Il2CppObject *nm = G.netLocal.IsValid() ? G.netLocal() : nullptr;
    if (!mi || !nm || !G.pvRpc.IsValid() || !G.arrayNew || !G.valueBox || !G.tPhotonView) return false;
    Il2CppObject *go = G.compGameObject[nm]();
    Il2CppObject *pv = go ? G.goGetComponent[go]((Il2CppObject *)G.tPhotonView) : nullptr;
    if (!pv) return false;
    int count = mi->parameters_count;
    if (count > 0 && TypeName(mi->parameters[count - 1]) == "Photon.Pun.PhotonMessageInfo") count--;
    auto *args = (Il2CppObject *)G.arrayNew(G.objectClass, count);
    if (!args) return false;
    char id[8];
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 4; i++) id[i] = kChars[(int)(Rand01() * 35.99f)];
    id[4] = 0;
    if (idOut) memcpy(idOut, id, 5);
    int strings = 0, vectors = 0;
    for (int i = 0; i < count; i++) {
        std::string tn = TypeName(mi->parameters[i]);
        Class pc(mi->parameters[i]);
        auto *k = pc.IsValid() ? pc.GetClass() : nullptr;
        void *v = nullptr;
        if (tn == "System.String") {
            if (strings == 0) v = CreateMonoString(id);
            else if (strings == 1) v = CreateMonoString(key);
            strings++;
        } else if (tn == "UnityEngine.Vector3") {
            Vector3 vec = vectors == 0 ? pos : vectors == 1 ? vel : Vector3(0, 0, 0);
            vectors++;
            v = G.valueBox(k, &vec);
        } else if (tn == "UnityEngine.Quaternion") {
            v = G.valueBox(k, &rot);
        } else if (tn == "System.String[]") {
            int n = params && nParams > 0 ? nParams : 0;
            Il2CppObject *arr = (Il2CppObject *)G.arrayNew(G.stringClass, (size_t)n);
            for (int j = 0; j < n; j++) Put(arr, (size_t)j, CreateMonoString(params[j] ? params[j] : ""));
            v = arr;
        } else if (k && k->byval_arg.valuetype) {
            uint8_t raw[64] = {};
            raw[0] = 1;
            auto *boxAs = k->enumtype && k->element_class ? k->element_class : k;
            v = G.valueBox(boxAs, raw);
        }
        Put(args, i, v);
    }
    try { G.pvRpc[pv](CreateMonoString("RpcCreateItem"), 0, args); } catch (BNM::IL2CPP::Il2CppExceptionWrapper &w) {
        auto *ex = w.ex;
        auto *msg = ex ? (BNM::Structures::Mono::String *)ex->message : nullptr;
        ym::log("leos-menu: egg spawn error: %s", msg ? msg->str().c_str() : "?");
        return false;
    }
    return true;
}
struct EggColor { const char *mob, *primary, *secondary; };
static const EggColor kEggColors[] = {
        {"alienEye", "blue_turquoise", "guac"},
        {"alienHopper", "purple", "guac"},
        {"armadillo", "maroon_dark", "mocha_light"},
        {"bitingBat", "brown_dark", "red_dark"},
        {"crab", "red", "yellow_light"},
        {"eggSack", "yellow_light", "purple"},
        {"frogling", "green_light", "yellow_light"},
        {"grazingHog", "blue_turquoise", "olive_light"},
        {"grazingSheep", "white", "blue_light"},
        {"grazingYak", "brown_dark", "brown"},
        {"hermitCrab", "red", "blue_dark"},
        {"horrorBear", "mocha", "blue_dark"},
        {"horrorChild", "blue_dark", "black"},
        {"horrorChimp", "yellow", "red"},
        {"horrorEmpty", "black", "black"},
        {"horrorFather", "black", "red"},
        {"horrorFisherman", "green_dark", "pink"},
        {"horrorFrankenstein", "gray_dark", "blue_turquoise"},
        {"horrorGhost_boy", "gray_cool_light", "blue_light"},
        {"horrorGhost_girl", "gray_cool_light", "pink"},
        {"horrorGhost_pet", "gray_cool_light", "green_light"},
        {"horrorGrandma", "maroon", "orange_light"},
        {"horrorMother", "blue_light", "yellow_light"},
        {"horrorYeepingAngel", "purple_dark", "blue_dark"},
        {"insectExploder", "red", "white"},
        {"insectHatchling", "purple", "purple_dark"},
        {"insectQueen", "purple_dark", "purple"},
        {"insectSoldier", "purple", "periwinkle"},
        {"insectTank", "purple_dark", "gray"},
        {"rat", "red_dark", "brown"},
        {"ratKing", "red_dark", "yellow_light"},
        {"scorpion", "purple_dark", "yellow_light"},
        {"shark", "blue_dark", "gray"},
        {"spittingFrog", "green_light", "green"},
        {"spittingLizard", "red", "yellow"},
        {"spittingSnail", "blue_dark", "cyan"},
        {"wasp", "brown_dark", "yellow"},
        {"wolf", "blue_dark", "red"},
        {"yeti", "blue_light", "blue_dark"}
};
static bool EggColorsFor(const char *mob, const char **primary, const char **secondary) {
    for (auto &e : kEggColors)
        if (!strcmp(e.mob, mob)) { *primary = e.primary; *secondary = e.secondary; return true; }
    return false;
}
static bool g_eggInit = false;
static void SpawnEgg(const char *mob) {
    Il2CppObject *head = g_head.get();
    if (!head) return;
    const char *primary = "white", *secondary = "white";
    EggColorsFor(mob, &primary, &secondary);
    const char *params[3] = {mob, primary, secondary};
    Vector3 r, f = HeadFlat(head, r);
    Vector3 pos = G.trGetPosition[head]() + f * 1.0f + Vector3(0, -0.2f, 0);
    bool ok = SendItemP("mobSpawnEgg", pos, Quaternion(0, 0, 0, 1), Vector3(0, 0.4f, 0), params, 3);
    ym::log("leos-menu: egg spawn %s ok=%d", mob, ok ? 1 : 0);
    Notify(ok ? "Spawning %s" : "Couldn't spawn %s (see log)", mob);
}
static void EggEnsure() {
    if (g_eggInit) return;
    g_eggInit = true;
    for (auto &e : kEggColors) g_eggItems.push_back(MenuItem{SPAWNPRESETS, e.mob, ACTION, MAIN, nullptr, nullptr, nullptr, 0, nullptr, 0});
    for (size_t i = 0; i < g_eggItems.size(); i++) g_eggItems[i].arg = (int)i;
    for (auto &it : g_eggItems) it.fn = []() { if (g_pressArg >= 0 && g_pressArg < (int)(sizeof kEggColors / sizeof kEggColors[0])) SpawnEgg(kEggColors[g_pressArg].mob); };
}

// ============================================================================================
// ---- Update 10 ------------------------------------------------------------------------------
// ============================================================================================

// ---- Favourites ----
static std::string FavFile() { std::string p = PersistentPath(); return p.empty() ? std::string() : p + "/leos-favs.txt"; }
static void FavSave() {
    std::string f = FavFile();
    if (f.empty()) return;
    try { std::ofstream o(f); for (auto &x : g_favs) o << x << "\n"; } catch (...) {}
}
static void FavLoad() {
    static bool done = false;
    if (done) return;
    done = true;
    std::string f = FavFile();
    if (f.empty()) return;
    try { std::ifstream i(f); std::string l; while (std::getline(i, l)) if (!l.empty()) g_favs.push_back(l); } catch (...) {}
}
static bool IsFav(const char *label) { FavLoad(); for (auto &x : g_favs) if (x == label) return true; return false; }
static void FavToggle(const char *label) {
    FavLoad();
    for (size_t i = 0; i < g_favs.size(); i++)
        if (g_favs[i] == label) { g_favs.erase(g_favs.begin() + (long)i); FavSave(); Notify("Removed %s from favourites", label); return; }
    g_favs.push_back(label);
    FavSave();
    Notify("Added %s to favourites", label);
}

// ---- Search (the magnifying glass beside the menu name) and the keyboards ----
// Search puts the menu where it is and keeps it still, with a keyboard below it. What you type filters every mod by name.
// The Owner code page uses the same idea with a number pad.
struct KeyDef { std::string text; float x, y, w, h; };
static std::vector<KeyDef> g_keyDef[4];
static std::vector<Ref> g_keyCube[4], g_keyLbl[4];
static Ref g_srchParts[3];
static void KeyLayout(int mode) {
    auto &d = g_keyDef[mode];
    if (!d.empty()) return;
    if (mode == 1 || mode == 3) {
        const char *rows[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
        for (int r = 0; r < 4; r++) {
            int n = (int)strlen(rows[r]);
            for (int i = 0; i < n; i++) d.push_back({std::string(1, rows[r][i]), ((float)i - (float)(n - 1) / 2.0f) * 0.029f, -0.235f - r * 0.034f, 0.026f, 0.03f});
        }
        float y = -0.235f - 4 * 0.034f;
        d.push_back({"SPACE", -0.065f, y, 0.09f, 0.03f});
        d.push_back({"DEL", 0.01f, y, 0.05f, 0.03f});
        d.push_back({mode == 3 ? "OK" : "CLR", 0.07f, y, 0.05f, 0.03f});
        d.push_back({"BACK", 0.13f, y, 0.05f, 0.03f});
    } else {
        const char *rows[3] = {"123", "456", "789"};
        for (int r = 0; r < 3; r++)
            for (int i = 0; i < 3; i++) d.push_back({std::string(1, rows[r][i]), ((float)i - 1.0f) * 0.062f, -0.245f - r * 0.042f, 0.056f, 0.038f});
        float y = -0.245f - 3 * 0.042f;
        d.push_back({"DEL", -0.062f, y, 0.056f, 0.038f});
        d.push_back({"0", 0.0f, y, 0.056f, 0.038f});
        d.push_back({"ENT", 0.062f, y, 0.056f, 0.038f});
    }
}
static void KeyBuild(int mode) {
    KeyLayout(mode);
    if (!g_keyCube[mode].empty()) return;
    Il2CppObject *board = g_board.get(), *canvas = g_menuCanvas[0].get();
    if (!Alive(board) || !Alive(canvas)) return;
    Il2CppObject *bt = Tr(board), *ct = Tr(canvas);
    for (auto &k : g_keyDef[mode]) {
        Ref c, l;
        c.set(Part(bt, BtnCol(), Vector3(k.x, k.y, -0.005f), Vector3(k.w, k.h, 0.009f)));
        l.set(Label(ct, k.text.c_str(), Vector3(k.x, k.y, 0), k.w, k.h, k.text.size() > 1 ? 13 : 18, nullptr));
        g_keyCube[mode].push_back(c);
        g_keyLbl[mode].push_back(l);
        if (Trans()) MatTransparent(c.get(), true);
    }
}
static void SearchUi() {
    for (int m = 1; m <= 3; m++) {
        bool vis = g_kbMode == m;
        if (vis) KeyBuild(m);
        for (auto &r : g_keyCube[m]) Show(r, vis);
        for (auto &r : g_keyLbl[m]) Show(r, vis);
    }
    Il2CppObject *board = g_board.get();
    if (!Alive(board)) return;
    if (!Alive(g_srchParts[0].get())) {                  // the magnifying glass: a ring and a handle
        Il2CppObject *bt = Tr(board);
        Il2CppObject *ring = Prim(2, T().title, false), *hole = Prim(2, BtnOnCol(), false), *handle = Prim(3, T().title, false);
        float cx = kBtnW / 2 - 0.023f, cy = kTitleY + 0.004f;
        Quaternion qx(0.70710678f, 0, 0, 0.70710678f);
        if (ring) { Il2CppObject *t = Tr(ring); G.trSetParent[t](bt, false); G.trSetLocalPosition[t](Vector3(cx, cy, -0.0075f)); G.trSetLocalRotation[t](qx); G.trSetLocalScale[t](Vector3(0.026f, 0.002f, 0.026f)); g_srchParts[0].set(ring); }
        if (hole) { Il2CppObject *t = Tr(hole); G.trSetParent[t](bt, false); G.trSetLocalPosition[t](Vector3(cx, cy, -0.0095f)); G.trSetLocalRotation[t](qx); G.trSetLocalScale[t](Vector3(0.017f, 0.002f, 0.017f)); g_srchParts[1].set(hole); }
        if (handle) { Il2CppObject *t = Tr(handle); G.trSetParent[t](bt, false); G.trSetLocalPosition[t](Vector3(cx + 0.0125f, cy - 0.0125f, -0.0075f)); G.trSetLocalRotation[t](Quaternion(0, 0, -0.38268343f, 0.92387953f)); G.trSetLocalScale[t](Vector3(0.016f, 0.004f, 0.004f)); g_srchParts[2].set(handle); }
    }
    Show(g_pinLbl, true);
    if (Alive(g_pinLbl.get())) G.graphicSetColor[g_pinLbl.get()](f_floatMenu ? BtnOnCol() : Color(1, 1, 1, 0.55f));
    SetColor(g_srchParts[0].get(), T().title);
    SetColor(g_srchParts[1].get(), BtnOnCol());
    SetColor(g_srchParts[2].get(), T().title);
}
static int KeyAt(Vector3 p) {
    if (g_kbMode < 1 || g_kbMode > 3) return -1;
    auto &d = g_keyDef[g_kbMode];
    for (size_t k = 0; k < d.size(); k++)
        if (fabsf(p.x - d[k].x) < d[k].w / 2 && fabsf(p.y - d[k].y) < d[k].h / 2) return (int)k;
    return -1;
}
static void CloseKeyboard() { g_kbMode = 0; g_floatPlaced = f_floatMenu; g_dirty = true; }
static void OpenSearch() { g_cat = SEARCH; g_query.clear(); g_kbMode = 1; g_floatPlaced = true; g_page = 0; g_dirty = true; g_nextTitle = 0; }
static void OwnerOpen() {
    if (g_pinLocked) { Notify("Locked until you restart the game"); return; }
    g_cat = OWNERPIN; g_pin.clear(); g_kbMode = 2; g_floatPlaced = true; g_page = 0; g_dirty = true; g_nextTitle = 0;
}
static void PinEnter() {
    if (g_pin.size() != 6) { Notify("Type all 6 digits"); return; }
    if (g_pin == "152016") {
        g_ownerOk = true; g_pinFails = 0;
        CloseKeyboard();
        g_cat = OWNER; g_page = 0;
        Notify("Owner unlocked");
        return;
    }
    g_pinFails++;
    g_pin.clear();
    if (g_pinFails >= 3) {                               // stays locked until the game is restarted (this is only kept in memory)
        g_pinLocked = true;
        CloseKeyboard();
        g_cat = MAIN; g_page = 0;
        Notify("3 wrong codes: locked until you restart the game");
    } else Notify("Wrong code (%d of 3)", g_pinFails);
}
static void KeyPress(int k) {
    if (g_kbMode < 1 || g_kbMode > 3 || k < 0 || k >= (int)g_keyDef[g_kbMode].size()) return;
    std::string t = g_keyDef[g_kbMode][(size_t)k].text;
    if (g_kbMode == 3) {                                  // naming a preset
        if (t == "SPACE") { if (g_presetName.size() < 14) g_presetName += ' '; }
        else if (t == "DEL") { if (!g_presetName.empty()) g_presetName.pop_back(); }
        else if (t == "OK") { PresetSaveCurrent(g_presetName.empty() ? std::string("Custom") : g_presetName); CloseKeyboard(); g_cat = PRESETS; g_page = 0; }
        else if (t == "BACK") { CloseKeyboard(); g_cat = PRESETS; g_page = 0; }
        else if (g_presetName.size() < 14) g_presetName += t[0];
    } else if (g_kbMode == 1) {
        if (t == "SPACE") { if (g_query.size() < 16) g_query += ' '; }
        else if (t == "DEL") { if (!g_query.empty()) g_query.pop_back(); }
        else if (t == "CLR") g_query.clear();
        else if (t == "BACK") { CloseKeyboard(); g_cat = MAIN; }
        else if (g_query.size() < 16) g_query += (char)tolower((unsigned char)t[0]);
        g_page = 0;
    } else {
        if (t == "DEL") { if (!g_pin.empty()) g_pin.pop_back(); }
        else if (t == "ENT") PinEnter();
        else if (g_pin.size() < 6) g_pin += t[0];
    }
    g_dirty = true;
    g_nextTitle = 0;
}
static void SearchFill() {
    if (g_query.empty()) return;
    for (int _gi = 0; _gi < kItems; _gi++) {
        auto &it = g_items[_gi];
        if (it.cat == SEARCH || it.cat == OWNERPIN || it.kind == OPEN) continue;
        if (Has(Lower(it.label), g_query.c_str())) g_view.push_back(&it);
    }
}

// ---- Other players ----
struct PlayerEntry { std::string name; int slot; };
static std::vector<PlayerEntry> g_players;
static std::string g_nameCache[16];
static double g_nameAt[16];
static std::string PlayerName(int slot) {
    char buf[24];
    snprintf(buf, sizeof buf, "Player %d", slot + 1);
    if (g_time < g_nameAt[slot] && !g_nameCache[slot].empty()) return g_nameCache[slot];
    g_nameAt[slot] = g_time + 3.0;
    Il2CppObject *obj = g_rig[slot].obj.get();
    std::string out = buf;
    if (Alive(obj) && G.tPhotonView) {
        try {
            static Method<Il2CppObject *> getOwner = Class("Photon.Pun", "PhotonView").GetMethod("get_Owner", 0);
            static Method<BNM::Structures::Mono::String *> getNick = Class("Photon.Realtime", "Player").GetMethod("get_NickName", 0);
            Il2CppObject *go = G.compGameObject[obj](), *pv = nullptr;
            for (int up = 0; up < 5 && go && !pv; up++) {
                pv = G.goGetComponent[go]((Il2CppObject *)G.tPhotonView);
                if (pv) break;
                Il2CppObject *par = G.trGetParent[Tr(go)]();
                go = par ? G.compGameObject[par]() : nullptr;
            }
            Il2CppObject *owner = (pv && getOwner.IsValid()) ? getOwner[pv]() : nullptr;
            auto *nick = (owner && getNick.IsValid()) ? getNick[owner]() : nullptr;
            if (nick && !nick->str().empty()) out = nick->str();
        } catch (...) {}
    }
    g_nameCache[slot] = out;
    return out;
}
static void RefreshPlayers() {
    g_players.clear();
    g_playerItems.clear();
    EspRefresh();
    for (int i = 0; i < 16; i++) if (Alive(g_rig[i].head.get())) g_players.push_back({PlayerName(i), i});
    for (size_t k = 0; k < g_players.size(); k++)
        g_playerItems.push_back(MenuItem{OTHERPLAYERS, g_players[k].name.c_str(), ACTION, MAIN, nullptr, nullptr, nullptr, 0, nullptr, (int)k});
    for (auto &it : g_playerItems) it.fn = []() {
        int i = g_pressArg;
        if (i < 0 || i >= (int)g_players.size()) return;
        g_selPlayer = g_players[(size_t)i].slot;
        Notify("Selected %s", g_players[(size_t)i].name.c_str());
    };
    if (g_selPlayer >= 0 && !Alive(g_rig[g_selPlayer].head.get())) g_selPlayer = -1;
    g_dirty = true;
}
static void PlayersEnsure() { if (!g_playersInit) { g_playersInit = true; RefreshPlayers(); } }
static void RefreshPlayersBtn() { g_view.clear(); RefreshPlayers(); Notify("%d player(s) found", (int)g_players.size()); }

// ---- Potion effects: the game's own method when one was found, otherwise an effect we can make ourselves ----
static double g_fxUntil[8];
static float g_fxJumpBase = 0, g_fxMulBase = 0;
static Ref g_fxOrb[6], g_fxBlind;
struct TargetFx { Ref obj, head; int fx = 0; double until = 0; };
static std::vector<TargetFx> g_tfx;
static void FxSelfStart(int fx) {
    g_fxUntil[fx] = g_time + 30.0;
    Notify("%s for 30 seconds", kPotionN[fx]);
}
static void GiveSelfPotionImpl() {
    Il2CppObject *p = g_player.get();
    if (p) PotionApply(p, g_potionSelf, 30.0f);                 // try the game's own effect too, but never rely on it alone
    FxSelfStart(g_potionSelf);
}
static void PotionOnTarget(Il2CppObject *obj, Il2CppObject *head, int fx) {
    bool real = PotionApply(obj, fx, 30.0f);                    // try the game's own effect too, but never rely on it alone
    if (fx == 4 || fx == 5 || fx == 2) {                 // these we can show ourselves: small, big, love (only on your screen)
        for (auto &t : g_tfx) if (t.obj.get() == obj && t.fx == fx) { t.until = g_time + 30.0; Notify("%s on them", kPotionN[fx]); return; }
        if (g_tfx.size() < 8) {
            TargetFx t;
            t.obj.set(obj); t.head.set(head); t.fx = fx; t.until = g_time + 30.0;
            g_tfx.push_back(t);
        }
        Notify("%s on them%s", kPotionN[fx], real ? "" : " (only you see it)");
    } else Notify(real ? "%s sent" : "%s: no way to show this on another player from here", kPotionN[fx]);
}
static void FxTick(float dt) {
    (void)dt;
    Il2CppObject *player = g_player.get(), *head = g_head.get();
    // effects on yourself
    if (player && head) {
        Vector3 v;
        if (g_fxUntil[7] > g_time) KeepFloating();                                  // levitation
        if (g_fxUntil[6] > g_time && GetVel(v) && v.y < -1.2f) SetVel(Vector3(v.x, -1.2f, v.z));   // slow falling
        if (G.maxJumpSpeed.IsValid() && G.jumpMultiplier.IsValid() && !f_sizeChanger) {          // legs: a stronger jump
            bool on = g_fxUntil[0] > g_time;
            if (on && g_fxJumpBase <= 0) { g_fxJumpBase = G.maxJumpSpeed[player](); g_fxMulBase = G.jumpMultiplier[player](); }
            if (g_fxJumpBase > 0) {
                G.maxJumpSpeed[player] = g_fxJumpBase * (on ? 1.8f : 1.0f);
                G.jumpMultiplier[player] = g_fxMulBase * (on ? 1.5f : 1.0f);
                if (!on) g_fxJumpBase = 0;
            }
        }
        if (!f_sizeChanger) {                                                       // small / big
            static bool scaled = false;
            float s = g_fxUntil[4] > g_time ? 0.4f : g_fxUntil[5] > g_time ? 2.2f : 1.0f;
            if (Il2CppObject *t = PlayerTransform()) {
                if (s != 1.0f) { G.trSetLocalScale[t](Vector3(s, s, s)); scaled = true; }
                else if (scaled) { G.trSetLocalScale[t](Vector3(1, 1, 1)); scaled = false; }
            }
        }
        bool blind = g_fxUntil[3] > g_time;                                         // blindness: black in front of your eyes
        if (blind && !Alive(g_fxBlind.get())) {
            if (Il2CppObject *q = Prim(5, Color(0, 0, 0, 1), false)) {
                Il2CppObject *t = Tr(q);
                G.trSetParent[t](head, false);
                G.trSetLocalPosition[t](Vector3(0, 0, 0.12f));
                G.trSetLocalScale[t](Vector3(0.8f, 0.8f, 1));
                g_fxBlind.set(q);
            }
        } else if (!blind && Alive(g_fxBlind.get())) Kill(g_fxBlind);
        bool love = g_fxUntil[2] > g_time;                                          // love: pink hearts (balls) around your head
        Vector3 hp = G.trGetPosition[head]();
        for (int i = 0; i < 6; i++) {
            if (!love) { Kill(g_fxOrb[i]); continue; }
            if (!Alive(g_fxOrb[i].get())) {
                if (Il2CppObject *o = Prim(0, Color(1.0f, 0.3f, 0.6f, 1), false)) { G.trSetLocalScale[Tr(o)](Vector3(0.07f, 0.07f, 0.07f)); g_fxOrb[i].set(o); }
            }
            if (Alive(g_fxOrb[i].get())) {
                float a = (float)g_time * 2.0f + (float)i * 1.047f;
                G.trSetPosition[Tr(g_fxOrb[i].get())](hp + Vector3(cosf(a) * 0.45f, 0.15f + 0.08f * sinf(a * 2.0f), sinf(a) * 0.45f));
            }
        }
        static Ref glow[2];
        if (g_fxUntil[1] > g_time) {                                                // visibility: your hands glow
            Il2CppObject *hands[2] = {g_leftHand.get(), g_rightHand.get()};
            for (int i = 0; i < 2; i++) {
                if (!Alive(glow[i].get()))
                    if (Il2CppObject *o = Prim(0, Color(1.0f, 0.95f, 0.4f, 1), false)) { G.trSetLocalScale[Tr(o)](Vector3(0.16f, 0.16f, 0.16f)); glow[i].set(o); }
                if (Alive(glow[i].get()) && hands[i]) G.trSetPosition[Tr(glow[i].get())](G.trGetPosition[hands[i]]());
            }
        } else for (auto &g : glow) Kill(g);                                        // the glow balls go away once visibility ends
    }
    // effects we show on other players
    for (size_t i = 0; i < g_tfx.size();) {
        TargetFx &t = g_tfx[i];
        Il2CppObject *obj = t.obj.get();
        Il2CppObject *tr = Alive(obj) ? G.compTransform[obj]() : nullptr;
        bool over = g_time >= t.until || !tr;
        if (tr && (t.fx == 4 || t.fx == 5)) { float s = over ? 1.0f : (t.fx == 4 ? 0.4f : 2.2f); G.trSetLocalScale[tr](Vector3(s, s, s)); }
        if (over) { g_tfx.erase(g_tfx.begin() + (long)i); continue; }
        i++;
    }
}

// ---- Info: names, update rate and skin color of other players ----
// What the game really renders for them (their fps) is not readable, so the number shown is how many times a second their
// position changes on your side (net / s). The skin color is the color of the first renderer found on their object.
static float g_updHz[16];
static int g_updCnt[16];
static Vector3 g_updLast[16];
static double g_updT = 0, g_nextRigRef = 0;
static std::string g_hexCache[16];
static double g_hexAt[16];
static void RigRefresh() { if (g_time >= g_nextRigRef) { g_nextRigRef = g_time + 1.0; EspRefresh(); } }
static void RateTick() {
    for (int i = 0; i < 16; i++) {
        Il2CppObject *h = g_rig[i].head.get();
        if (!Alive(h)) { g_updCnt[i] = 0; continue; }
        Vector3 p = G.trGetPosition[h]();
        if (Len2(p - g_updLast[i]) > 1e-8f) g_updCnt[i]++;
        g_updLast[i] = p;
    }
    if (g_time - g_updT >= 1.0) {
        for (int i = 0; i < 16; i++) { g_updHz[i] = (float)g_updCnt[i] / (float)(g_time - g_updT); g_updCnt[i] = 0; }
        g_updT = g_time;
    }
}
static std::string SkinHex(int slot) {
    if (g_time < g_hexAt[slot] && !g_hexCache[slot].empty()) return g_hexCache[slot];
    g_hexAt[slot] = g_time + 3.0;
    std::string out = "??????";
    Il2CppObject *obj = g_rig[slot].obj.get();
    if (Alive(obj) && G.tRenderer && G.getComponentsInChildren.IsValid()) {
        try {
            static Method<Il2CppObject *> getShared = Class("UnityEngine", "Renderer").GetMethod("get_sharedMaterial", 0);
            static Method<Color> getColor = Class("UnityEngine", "Material").GetMethod("get_color", 0);
            Il2CppObject *go = G.compGameObject[obj]();
            auto *rs = go ? (uint8_t *)G.getComponentsInChildren[go]((Il2CppObject *)G.tRenderer, true) : nullptr;
            for (size_t k = 0; rs && k < ArrLen(rs) && k < 12; k++) {
                Il2CppObject *r = ArrAt(rs, k);
                Il2CppObject *m = (Alive(r) && getShared.IsValid()) ? getShared[r]() : nullptr;
                if (!m || !getColor.IsValid()) continue;
                Color c = getColor[m]();
                char b[16];
                snprintf(b, sizeof b, "%02X%02X%02X", Hex(c.r), Hex(c.g), Hex(c.b));
                out = b;
                break;
            }
        } catch (...) {}
    }
    g_hexCache[slot] = out;
    return out;
}
static std::string PlayerInfoText(int slot) {
    char b[200];
    snprintf(b, sizeof b, "%s\nnet %d/s\n#%s", PlayerName(slot).c_str(), (int)(g_updHz[slot] + 0.5f), SkinHex(slot).c_str());
    return b;
}
struct Tag { Ref go, text; std::string last; };
static Tag g_tag[17];                                      // 0..15 nametags, 16 the target info label
static void TagKill(Tag &t) { Kill(t.go); t.text.clear(); t.last.clear(); }
static void TagUpdate(Tag &t, Vector3 pos, const std::string &txt, Vector3 viewer) {
    if (!Alive(t.go.get())) {
        Il2CppObject *cv = Canvas(nullptr, Vector3(0, 0, 0), 0.5f, 0.25f);
        if (!cv) return;
        Il2CppObject *ct = Tr(cv);
        G.trSetLocalScale[ct](Vector3(0.0025f, 0.0025f, 0.0025f));
        Label(ct, "", Vector3(0, 0, 0), 0.5f, 0.25f, 34, &t.text);
        t.go.set(cv);
        t.last.clear();
    }
    Il2CppObject *ct = Tr(t.go.get());
    G.trSetPosition[ct](pos);
    Vector3 d = pos - viewer;
    if (Len2(d) > 0.0001f) G.trSetRotation[ct](Quaternion::LookRotation(d, Vector3(0, 1, 0)));
    if (txt != t.last) { t.last = txt; SetText(t.text, txt.c_str()); }
}
static int LockTarget(Vector3 from) {                    // Target Lock: the closest player inside the radius
    int best = -1;
    float bd = kLockR[g_lockRad] * kLockR[g_lockRad];
    for (int i = 0; i < 16; i++) {
        Il2CppObject *h = g_rig[i].head.get();
        if (!Alive(h)) continue;
        float d = Len2(G.trGetPosition[h]() - from);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
static bool AffectsOthersActive() { return f_potionGun || f_flingGun || f_pistol || f_shotgun || f_sniper || f_knife || f_revolver; }
static Ref g_tbGo, g_tbLine;
static void PlayerInfoTick() {
    static bool had = false;
    bool target = f_targetInfo && f_targetLock && AffectsOthersActive();
    if (!f_nametags && !target) {
        if (had) { for (auto &t : g_tag) TagKill(t); Kill(g_tbGo); g_tbLine.clear(); had = false; }
        return;
    }
    Il2CppObject *me = g_head.get();
    if (!Alive(me)) return;
    had = true;
    RigRefresh();
    RateTick();
    Vector3 mp = G.trGetPosition[me]();
    for (int i = 0; i < 16; i++) {
        Il2CppObject *h = g_rig[i].head.get();
        if (!f_nametags || !Alive(h)) { TagKill(g_tag[i]); continue; }
        TagUpdate(g_tag[i], G.trGetPosition[h]() + Vector3(0, 0.4f, 0), PlayerInfoText(i), mp);
    }
    int slot = target ? LockTarget(mp) : -1;
    Il2CppObject *th = slot >= 0 ? g_rig[slot].head.get() : nullptr;
    if (!Alive(th)) { TagKill(g_tag[16]); Kill(g_tbGo); g_tbLine.clear(); return; }
    Vector3 h = G.trGetPosition[th]();
    TagUpdate(g_tag[16], h + Vector3(0, 0.6f, 0), PlayerInfoText(slot), mp);
    if (!Alive(g_tbLine.get()) && MakeLine(T().bgA, 0.012f, "LeoTarget", g_tbGo, g_tbLine)) G.lrSetPositionCount[g_tbLine.get()](16);
    if (Il2CppObject *ln = g_tbLine.get()) {
        float y0 = h.y - 1.55f, y1 = h.y + 0.2f, hw = 0.35f;
        Vector3 b[4] = {Vector3(h.x - hw, y0, h.z - hw), Vector3(h.x + hw, y0, h.z - hw), Vector3(h.x + hw, y0, h.z + hw), Vector3(h.x - hw, y0, h.z + hw)};
        Vector3 t[4] = {Vector3(h.x - hw, y1, h.z - hw), Vector3(h.x + hw, y1, h.z - hw), Vector3(h.x + hw, y1, h.z + hw), Vector3(h.x - hw, y1, h.z + hw)};
        Vector3 path[16] = {b[0], b[1], b[2], b[3], b[0], t[0], t[1], t[2], t[3], t[0], t[1], b[1], b[2], t[2], t[3], b[3]};
        for (int k = 0; k < 16; k++) G.lrSetPosition[ln](k, path[k]);
    }
}

// ---- Selected player actions and Spectate ----
static void GiveSelectedEffect() {
    if (g_selPlayer < 0 || !Alive(g_rig[g_selPlayer].head.get())) { Notify("Select a player first"); return; }
    PotionOnTarget(g_rig[g_selPlayer].obj.get(), g_rig[g_selPlayer].head.get(), g_selEffect);
}
static void SpawnAtSelected() {
    if (g_selPlayer < 0 || !Alive(g_rig[g_selPlayer].head.get())) { Notify("Select a player first"); return; }
    if (!LeftHandItem()) return;                         // hold the item in your left hand first
    Il2CppObject *h = g_rig[g_selPlayer].head.get();
    Vector3 hp = G.trGetPosition[h]();
    PendingItem p{hp + Flat(G.trGetForward[h]()) * 0.9f + Vector3(0, -0.3f, 0), Vector3(0, 0, 0)};
    g_itemQueue.push_back(p);
    Notify("MenuItem spawned in front of them");
}
static CamScreen g_spec;
static void SpecTick() {
    static bool had = false;
    bool ok = f_spectate && g_selPlayer >= 0 && Alive(g_rig[g_selPlayer].head.get());
    if (!ok) {
        if (had) { CamScreenKill(g_spec); had = false; }
        static double nn = 0;
        if (f_spectate && g_time >= nn) { nn = g_time + 4.0; Notify("Select a player first (Other Players)"); }
        return;
    }
    if (!Alive(g_spec.camGo.get())) {
        if (!CamScreenBuild(g_spec, Vector3(0, 0.13f, 0.6f), 0.3f, 0.3f, false, 0, 75.0f)) { f_spectate = false; CamScreenKill(g_spec); Notify("Spectate isn't available here"); return; }
    }
    had = true;
    Il2CppObject *h = g_rig[g_selPlayer].head.get();
    Vector3 hp = G.trGetPosition[h](), f = G.trGetForward[h]();
    Il2CppObject *ct = Tr(g_spec.camGo.get());
    if (g_specView == 0) {
        G.trSetPosition[ct](hp + f * 0.1f);
        G.trSetRotation[ct](Quaternion::LookRotation(f, Vector3(0, 1, 0)));
    } else {
        Vector3 pos = hp - Flat(f) * 1.8f + Vector3(0, 0.8f, 0);
        G.trSetPosition[ct](pos);
        G.trSetRotation[ct](Quaternion::LookRotation(hp - pos, Vector3(0, 1, 0)));
    }
}

// ---- Trail ESP: a short trail behind every other player ----
static std::vector<Vector3> g_etPts[16];
static Ref g_etGo[16], g_etLine[16];
static double g_etNext = 0, g_etCol = 0;
static void TrailEspTick() {
    static bool had = false;
    if (!f_trailEsp) { if (had) { for (int i = 0; i < 16; i++) { Kill(g_etGo[i]); g_etLine[i].clear(); g_etPts[i].clear(); } had = false; } return; }
    had = true;
    RigRefresh();
    bool sample = g_time >= g_etNext;
    if (sample) g_etNext = g_time + 0.05;
    bool recolor = g_time >= g_etCol;
    if (recolor) g_etCol = g_time + 1.0;
    for (int i = 0; i < 16; i++) {
        Il2CppObject *h = g_rig[i].head.get();
        if (!Alive(h)) { Kill(g_etGo[i]); g_etLine[i].clear(); g_etPts[i].clear(); continue; }
        if (!sample) continue;
        auto &pts = g_etPts[i];
        pts.push_back(G.trGetPosition[h]() - Vector3(0, 0.4f, 0));
        if (pts.size() > 20) pts.erase(pts.begin());
        if (pts.size() < 2) continue;
        if (!Alive(g_etLine[i].get()) && !MakeLine(DrawColor(), 0.03f, "LeoEspTrail", g_etGo[i], g_etLine[i])) continue;
        Il2CppObject *ln = g_etLine[i].get();
        if (!ln) continue;
        G.lrSetPositionCount[ln]((int)pts.size());
        for (size_t k = 0; k < pts.size(); k++) G.lrSetPosition[ln]((int)k, pts[k]);
        if (recolor) if (Il2CppObject *m = G.rendererGetMaterial[ln]()) G.matSetColor[m](DrawColor());
    }
}

// ---- Ripple: water ripples where your hands or feet touch the ground ----
struct Ripple { Ref a, b; double born = 0; };
static std::vector<Ripple> g_ripples;
static double g_rippleNext[2];
static void RippleSpawn(Vector3 p) {
    if (g_ripples.size() >= 14) { Kill(g_ripples.front().a); Kill(g_ripples.front().b); g_ripples.erase(g_ripples.begin()); }
    Ripple r;
    for (int k = 0; k < 2; k++) {
        Il2CppObject *o = Prim(2, Color(0.55f, 0.85f, 1.0f, 0.5f), false);
        if (!o) continue;
        MatTransparent(o, true);
        G.trSetPosition[Tr(o)](p + Vector3(0, 0.015f + 0.003f * k, 0));
        G.trSetLocalScale[Tr(o)](Vector3(0.05f, 0.002f, 0.05f));
        (k ? r.b : r.a).set(o);
    }
    r.born = g_time;
    g_ripples.push_back(r);
}
static void RippleTick(float dt) {
    (void)dt;
    for (size_t i = 0; i < g_ripples.size();) {
        Ripple &r = g_ripples[i];
        float age = (float)(g_time - r.born);
        if (age > 1.1f) { Kill(r.a); Kill(r.b); g_ripples.erase(g_ripples.begin() + (long)i); continue; }
        float rad = 0.05f + age * 0.9f, al = 0.55f * (1.0f - age / 1.1f);
        if (Alive(r.a.get())) { G.trSetLocalScale[Tr(r.a.get())](Vector3(rad * 2, 0.002f, rad * 2)); SetColor(r.a.get(), Color(0.55f, 0.85f, 1.0f, al * 0.6f)); }
        if (Alive(r.b.get())) { G.trSetLocalScale[Tr(r.b.get())](Vector3(rad * 1.4f, 0.002f, rad * 1.4f)); SetColor(r.b.get(), Color(0.8f, 0.95f, 1.0f, al)); }
        i++;
    }
    if (!f_ripple || !G.raycast.IsValid()) return;
    Il2CppObject *hands[2] = {g_leftHand.get(), g_rightHand.get()};
    for (int h = 0; h < 2; h++) {
        if (!hands[h] || g_time < g_rippleNext[h]) continue;
        Vector3 hp = G.trGetPosition[hands[h]]();
        BNM::Structures::Unity::RaycastHit hit{};
        if (G.raycast(hp + Vector3(0, 0.05f, 0), Vector3(0, -1, 0), &hit, 0.17f, -5)) { RippleSpawn(hit.point); g_rippleNext[h] = g_time + 0.3; }
    }
    static bool wasAir = false;
    Vector3 body;
    if (BodyPos(body)) {
        BNM::Structures::Unity::RaycastHit hit{};
        bool ground = G.raycast(body, Vector3(0, -1, 0), &hit, 1.2f, -5);
        if (ground && wasAir) RippleSpawn(hit.point);        // landing
        wasAir = !ground;
    }
}

// ---- Blood (Games) ----
// Blood only comes from hits by the Games weapons: the game's own damage can't be seen from here.
struct Drop { Ref o; Vector3 pos, vel; double until = 0; };
struct Splat { Ref a, b; double until = 0; };
static std::vector<Drop> g_drops;
static std::vector<Splat> g_splats;
static void SplatAt(Vector3 g, bool soft) {
    if (g_splats.size() >= 40) { Kill(g_splats.front().a); Kill(g_splats.front().b); g_splats.erase(g_splats.begin()); }
    Splat s;
    s.until = g_time + 25.0;
    Il2CppObject *a = Prim(2, soft ? Color(0.55f, 0.0f, 0.0f, 0.35f) : Color(0.4f, 0.0f, 0.0f, 1), false);
    if (a && soft) MatTransparent(a, true);
    s.a.set(a);
    if (soft) {
        Il2CppObject *b = Prim(2, Color(0.7f, 0.0f, 0.0f, 0.6f), false);
        if (b) { MatTransparent(b, true); s.b.set(b); }
    }
    float r = 0.18f + Rand01() * 0.15f;
    if (a) { G.trSetPosition[Tr(a)](g + Vector3(0, 0.012f, 0)); G.trSetLocalScale[Tr(a)](Vector3(r * 2, 0.002f, r * 2)); }
    if (Il2CppObject *b = s.b.get()) { G.trSetPosition[Tr(b)](g + Vector3(0, 0.014f, 0)); G.trSetLocalScale[Tr(b)](Vector3(r * 1.1f, 0.002f, r * 1.1f)); }
    g_splats.push_back(s);
}
static void BloodAt(Vector3 p, Vector3 dir) {
    if (!f_blood1 && !f_blood2) return;
    bool soft = f_blood1;
    for (int k = 0; k < 10 && g_drops.size() < 80; k++) {
        Il2CppObject *o = Prim(0, soft ? Color(0.6f, 0.0f, 0.0f, 0.85f) : Color(0.45f, 0.0f, 0.0f, 1), false);
        if (!o) continue;
        if (soft) MatTransparent(o, true);
        float sc = 0.02f + Rand01() * 0.03f;
        G.trSetLocalScale[Tr(o)](Vector3(sc, sc, sc));
        Drop d;
        d.o.set(o);
        d.pos = p;
        d.vel = Vector3((Rand01() - 0.5f) * 2.5f, Rand01() * 1.5f, (Rand01() - 0.5f) * 2.5f) + dir * 1.5f;
        d.until = g_time + 0.9;
        g_drops.push_back(d);
    }
    if (G.raycast.IsValid()) {
        BNM::Structures::Unity::RaycastHit hit{};
        if (G.raycast(p + Vector3(0, 0.3f, 0), Vector3(0, -1, 0), &hit, 6.0f, -5)) SplatAt(hit.point, soft);   // blood on the ground below
    }
}
static void BloodTick(float dt) {
    for (size_t i = 0; i < g_drops.size();) {
        Drop &d = g_drops[i];
        if (g_time >= d.until || !Alive(d.o.get())) { Kill(d.o); g_drops.erase(g_drops.begin() + (long)i); continue; }
        d.vel = d.vel + Vector3(0, -9.8f * dt, 0);
        d.pos = d.pos + d.vel * dt;
        G.trSetPosition[Tr(d.o.get())](d.pos);
        i++;
    }
    for (size_t i = 0; i < g_splats.size();) {
        if (g_time >= g_splats[i].until) { Kill(g_splats[i].a); Kill(g_splats[i].b); g_splats.erase(g_splats.begin() + (long)i); continue; }
        i++;
    }
}

// ---- Games: pistol, shotgun, sniper, knife (right hand) ----
static bool *kWeaponFlags[] = {&f_pistol, &f_shotgun, &f_sniper, &f_knife, &f_revolver};
static bool g_weaponPrev[5];
static void WeaponPicked() { Radio(kWeaponFlags, g_weaponPrev, 5); }
static Ref g_wRoot;
static double g_spinUntil = 0;
static std::vector<Ref> g_wParts;
static int g_wBuilt = -1;
static void WeaponKill() { for (auto &r : g_wParts) Kill(r); g_wParts.clear(); Kill(g_wRoot); g_wBuilt = -1; }
static void WeaponBuild(int kind) {
    Il2CppObject *hand = g_rightHand.get();
    if (!Alive(hand)) { WeaponKill(); return; }
    WeaponKill();
    Il2CppObject *root = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("LeoWeapon"));
    if (!root) return;
    G.trSetParent[Tr(root)](hand, false);
    G.trSetLocalPosition[Tr(root)](Vector3(0, 0, 0));
    G.trSetLocalRotation[Tr(root)](Quaternion(0, 0, 0, 1));
    g_wRoot.set(root);
    auto add = [&](int prim, Color c, Vector3 pos, Vector3 scale, bool alongZ) {
        Il2CppObject *o = Prim(prim, c, false);
        if (!o) return;
        Il2CppObject *t = Tr(o);
        G.trSetParent[t](root, false);
        G.trSetLocalPosition[t](pos);
        G.trSetLocalRotation[t](alongZ ? Quaternion(0.70710678f, 0, 0, 0.70710678f) : Quaternion(0, 0, 0, 1));
        G.trSetLocalScale[t](scale);
        Ref r;
        r.set(o);
        g_wParts.push_back(r);
    };
    Color dark(0.12f, 0.12f, 0.14f, 1), steel(0.6f, 0.62f, 0.66f, 1), wood(0.38f, 0.22f, 0.1f, 1), black(0.04f, 0.04f, 0.05f, 1);
    if (kind == 0) {                                      // pistol
        add(3, steel, Vector3(0, 0.02f, 0.1f), Vector3(0.035f, 0.045f, 0.18f), false);
        add(3, dark, Vector3(0, -0.035f, 0.045f), Vector3(0.032f, 0.08f, 0.04f), false);
        add(2, black, Vector3(0, 0.02f, 0.2f), Vector3(0.016f, 0.01f, 0.016f), true);
    } else if (kind == 1) {                               // shotgun
        add(2, steel, Vector3(0, 0.02f, 0.3f), Vector3(0.028f, 0.25f, 0.028f), true);
        add(2, steel, Vector3(0, -0.012f, 0.26f), Vector3(0.026f, 0.15f, 0.026f), true);
        add(3, wood, Vector3(0, -0.01f, 0.2f), Vector3(0.04f, 0.04f, 0.14f), false);
        add(3, wood, Vector3(0, -0.02f, -0.06f), Vector3(0.04f, 0.075f, 0.22f), false);
    } else if (kind == 2) {                               // sniper
        add(2, dark, Vector3(0, 0.02f, 0.5f), Vector3(0.02f, 0.4f, 0.02f), true);
        add(3, dark, Vector3(0, 0.0f, 0.12f), Vector3(0.04f, 0.06f, 0.4f), false);
        add(2, black, Vector3(0, 0.06f, 0.15f), Vector3(0.04f, 0.09f, 0.04f), true);
        add(3, dark, Vector3(0, -0.04f, -0.14f), Vector3(0.04f, 0.1f, 0.12f), false);
    } else if (kind == 3) {                               // knife: the blade points up out of your fist
        add(3, steel, Vector3(0, 0.17f, 0), Vector3(0.01f, 0.22f, 0.032f), false);
        add(3, black, Vector3(0, 0.0f, 0), Vector3(0.026f, 0.1f, 0.026f), false);
        add(3, dark, Vector3(0, 0.055f, 0), Vector3(0.012f, 0.012f, 0.07f), false);
    } else {                                              // revolver
        add(2, steel, Vector3(0, 0.025f, 0.2f), Vector3(0.016f, 0.1f, 0.016f), true);
        add(3, steel, Vector3(0, 0.015f, 0.08f), Vector3(0.03f, 0.05f, 0.14f), false);
        add(2, dark, Vector3(0, 0.02f, 0.085f), Vector3(0.05f, 0.03f, 0.05f), true);
        add(3, wood, Vector3(0, -0.04f, 0.025f), Vector3(0.03f, 0.09f, 0.04f), false);
    }
    g_wBuilt = kind;
}
struct Bul { Ref o; Vector3 pos, vel; double until = 0; };
static std::vector<Bul> g_bullets9;
static void SpawnBullet(Vector3 pos, Vector3 vel, float life, Color c) {
    if (g_bullets9.size() >= 40) { Kill(g_bullets9.front().o); g_bullets9.erase(g_bullets9.begin()); }
    Il2CppObject *o = Prim(0, c, false);
    if (!o) return;
    G.trSetLocalScale[Tr(o)](Vector3(0.04f, 0.04f, 0.04f));
    G.trSetPosition[Tr(o)](pos);
    Bul b;
    b.o.set(o);
    b.pos = pos;
    b.vel = vel;
    b.until = g_time + life;
    g_bullets9.push_back(b);
}
static float SegPointDist2(Vector3 a, Vector3 sd, float sl, Vector3 c) {
    float t = Dot(c - a, sd);
    t = t < 0 ? 0 : t > sl ? sl : t;
    return Len2(a + sd * t - c);
}
static void ShotTick(float dt) {
    for (size_t i = 0; i < g_bullets9.size();) {
        Bul &b = g_bullets9[i];
        bool dead = g_time >= b.until || !Alive(b.o.get());
        if (!dead) {
            Vector3 np = b.pos + b.vel * dt, seg = np - b.pos;
            float sl = sqrtf(Len2(seg));
            if (sl > 0.0001f) {
                Vector3 sd = seg * (1.0f / sl);
                for (int k = 0; k < 16 && !dead; k++) {
                    Il2CppObject *h = g_rig[k].head.get();
                    if (!Alive(h)) continue;
                    Vector3 c = G.trGetPosition[h]() - Vector3(0, 0.45f, 0);
                    if (SegPointDist2(b.pos, sd, sl, c) < 0.42f * 0.42f) { BloodAt(c + Vector3(0, 0.25f, 0), sd); Notify("Hit"); dead = true; }
                }
                BNM::Structures::Unity::RaycastHit hit{};
                if (!dead && G.raycast.IsValid() && G.raycast(b.pos, sd, &hit, sl, -5)) dead = true;      // the world stops it
            }
            if (!dead) { b.pos = np; G.trSetPosition[Tr(b.o.get())](np); }
        }
        if (dead) { Kill(b.o); g_bullets9.erase(g_bullets9.begin() + (long)i); continue; }
        i++;
    }
}
static bool g_wTrig = false;
static double g_wNext = 0;
static Vector3 g_knifePrev;
static bool g_knifeInit = false;
static void WeaponTick(const Buttons &in, float dt) {
    ShotTick(dt);
    int kind = f_pistol ? 0 : f_shotgun ? 1 : f_sniper ? 2 : f_knife ? 3 : f_revolver ? 4 : -1;
    Il2CppObject *hand = g_rightHand.get();
    if (kind < 0 || !Alive(hand)) { if (g_wBuilt >= 0) WeaponKill(); g_wTrig = in.rtrig; g_knifeInit = false; return; }
    if (g_wBuilt != kind || g_wParts.empty() || !Alive(g_wParts[0].get()) || !Alive(g_wRoot.get())) WeaponBuild(kind);
    RigRefresh();
    Vector3 hp = G.trGetPosition[hand](), f = G.trGetForward[hand]();
    if (f_targetLock) {                                   // Target Lock: aim at the closest player (or the kept one with Stay Locked)
        int s = LockTargetPersist(hp, in.rgrip);
        if (s >= 0) { Vector3 d = G.trGetPosition[g_rig[s].head.get()]() - Vector3(0, 0.45f, 0) - hp; float l = sqrtf(Len2(d)); if (l > 0.1f) f = d * (1.0f / l); }
    }
    if (kind == 3) {                                      // knife: a fast swing into a player
        if (!g_knifeInit) { g_knifePrev = hp; g_knifeInit = true; }
        Vector3 hv = (hp - g_knifePrev) * (1.0f / (dt > 0.001f ? dt : 0.001f));
        g_knifePrev = hp;
        Vector3 tip = hp + G.trGetUp[hand]() * 0.3f;
        if (g_time >= g_wNext && Len2(hv) > 1.8f * 1.8f && !MenuBusy())
            for (int k = 0; k < 16; k++) {
                Il2CppObject *h = g_rig[k].head.get();
                if (!Alive(h)) continue;
                Vector3 c = G.trGetPosition[h]() - Vector3(0, 0.4f, 0);
                if (Len2(tip - c) < 0.5f * 0.5f) { BloodAt(tip, Flat(hv)); g_wNext = g_time + 0.35; Notify("Stab"); break; }
            }
        return;
    }
    if (kind == 4) {                                      // A spins the revolver on your finger
        static bool aPrev = false;
        if (in.a && !aPrev && !MenuBusy() && g_time >= g_spinUntil) g_spinUntil = g_time + 0.9;
        aPrev = in.a;
    }
    if (Alive(g_wRoot.get())) {
        Il2CppObject *rt = g_wRoot.get();
        float ang = 0;
        if (kind == 4 && g_time < g_spinUntil) ang = 4.0f * 3.14159265f * (1.0f - (float)(g_spinUntil - g_time) / 0.9f);
        G.trSetLocalRotation[Tr(rt)](Quaternion(sinf(ang / 2), 0, 0, cosf(ang / 2)));
    }
    if (kind == 4 && g_time < g_spinUntil) { g_wTrig = in.rtrig; return; }
    bool fire = in.rtrig && !g_wTrig && !in.rgrip && !MenuBusy();
    g_wTrig = in.rtrig;
    if (!fire || g_time < g_wNext) return;
    Vector3 muzzle = hp + f * (kind == 2 ? 0.7f : 0.3f);
    float recoil = 0;
    if (kind == 0 || kind == 4) {
        bool rv = kind == 4;
        SpawnBullet(muzzle, f * (rv ? 105.0f : 90.0f), 1.4f, rv ? Color(1.0f, 0.6f, 0.2f, 1) : Color(1.0f, 0.9f, 0.3f, 1));
        recoil = rv ? 4.0f : 2.5f;
        g_wNext = g_time + (rv ? 0.35 : 0.2);
    }
    else if (kind == 1) {
        for (int k = 0; k < 5; k++) {
            Vector3 d = f + Vector3((Rand01() - 0.5f) * 0.18f, (Rand01() - 0.5f) * 0.18f, (Rand01() - 0.5f) * 0.18f);
            float l = sqrtf(Len2(d));
            SpawnBullet(muzzle, d * (70.0f / l), 0.2f, Color(1.0f, 0.6f, 0.2f, 1));       // 14 m, then gone
        }
        recoil = 9.0f;
        g_wNext = g_time + 0.9;
    } else { SpawnBullet(muzzle, f * 220.0f, 1.8f, Color(1.0f, 1.0f, 1.0f, 1)); recoil = 12.0f; g_wNext = g_time + 1.5; }
    Vector3 v;
    if (GetVel(v)) SetVel(v - f * recoil);                // the shot pushes you back
}

static void Extra10Reset() {
    for (int m = 0; m < 4; m++) { g_keyCube[m].clear(); g_keyLbl[m].clear(); }
    for (auto &r : g_srchParts) r.clear();
    g_kbMode = 0;
    for (auto &t : g_tag) { t.go.clear(); t.text.clear(); t.last.clear(); }
    g_tbGo.clear(); g_tbLine.clear();
    CamScreenKill(g_spec);
    for (int i = 0; i < 16; i++) { g_etGo[i].clear(); g_etLine[i].clear(); g_etPts[i].clear(); }
    g_ripples.clear(); g_drops.clear(); g_splats.clear(); g_bullets9.clear(); g_wParts.clear(); g_wBuilt = -1;
    g_tfx.clear();
    for (auto &r : g_wParts) Kill(r);
    Kill(g_wRoot);
    g_wBuilt = -1;
    g_wTrig = false;
    g_knifeInit = false;
    for (auto &r : g_fxOrb) r.clear();
    g_fxBlind.clear();
    g_fxJumpBase = 0;
    g_playersInit = false;
    g_selPlayer = -1;
}

// ============================================================================================
// ---- Update 11: the Phone menu type (V2.0) --------------------------------------------------
// ============================================================================================
// The phone is built from cubes, quads and text on the same board the hand menu uses, so it is placed and touched the same
// way. Everything on its screen is a "widget" (a flat colored rectangle, a text, or a picture) that is re-assigned whenever
// the screen changes. Apps: Settings (every menu category except Soundboard, Exit Menu Type at the bottom), Camera, Photos,
// Music (the soundboard sounds), Maps, Notes. Photos, videos and notes are saved in the game's persistent data folder.
static bool PhoneActive() { return f_phoneMenu && !f_pizza && !f_textMenu; }
enum { PH_HOME, PH_SETTINGS, PH_CAT, PH_CAMERA, PH_PHOTOS, PH_VIEW, PH_MUSIC, PH_MAPS, PH_NOTES, PH_NOTE };
enum { A_NONE, A_APP, A_CAT, A_ITEM, A_EXIT, A_BACK, A_PHOTO, A_WALL, A_SHUTTER, A_FLIP, A_REC, A_GALLERY, A_MUSIC, A_STOPSND, A_NOTE, A_NEWNOTE, A_KEY };
static int g_phApp = PH_HOME;
static Cat g_phCat = MAIN;
static std::vector<Cat> g_phStack;
static float g_phScroll = 0, g_mapZoom = 0.4f;
static bool g_phDirty = true;
static std::vector<MenuItem *> g_phItems;
static constexpr float kPhW = 0.25f, kPhH = 0.515f, kScrW = 0.22f, kScrH = 0.385f, kScrCy = 0.005f, kPZ = -0.012f;
static constexpr float kViewTop = 0.148f, kViewBot = -0.172f;

// ---- pictures: icons from the five app pictures, photos from files ----
static Il2CppObject *TexFromBytes(const unsigned char *data, size_t n, Il2CppObject *reuse) {
    static Class tc("UnityEngine", "Texture2D"), ic("UnityEngine", "ImageConversion"), bc("System", "Byte");
    static Method<bool> load;
    static bool looked = false;
    if (!looked) { looked = true; load = ic.GetMethod("LoadImage", 2); }
    if (!load.IsValid() || !tc.IsValid() || !bc.IsValid() || !G.arrayNew || n < 16) return nullptr;
    try {
        Il2CppObject *tex = reuse ? reuse : tc.CreateNewObjectParameters(2, 2);
        if (!tex) return nullptr;
        Il2CppObject *arr = (Il2CppObject *)G.arrayNew(bc.GetClass(), n);
        if (!arr) return nullptr;
        memcpy((uint8_t *)arr + 0x20, data, n);
        load(tex, arr);
        return tex;
    } catch (...) { return nullptr; }
}
static Il2CppObject *TexFromFile(const std::string &path, Il2CppObject *reuse = nullptr) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return TexFromBytes(d.data(), d.size(), reuse);
}
static Ref g_iconTex[6];
static Il2CppObject *IconTex(int k) {                       // Settings, Camera, Photos, Music, Maps, Notes
    if (Alive(g_iconTex[k].get())) return g_iconTex[k].get();
    const unsigned char *d[6] = {kIconSettings, kIconCamera, kIconPhotos, kIconMusic, kIconMaps, kIconNotes};
    size_t n[6] = {sizeof kIconSettings, sizeof kIconCamera, sizeof kIconPhotos, sizeof kIconMusic, sizeof kIconMaps, sizeof kIconNotes};
    Il2CppObject *t = TexFromBytes(d[k], n[k], nullptr);
    if (t) g_iconTex[k].set(t);
    return t;
}
static std::unordered_map<std::string, Ref> g_thumb;
static Il2CppObject *ThumbFor(const std::string &path) {
    auto it = g_thumb.find(path);
    if (it != g_thumb.end() && Alive(it->second.get())) return it->second.get();
    if (g_thumb.size() >= 18) { for (auto &t : g_thumb) Kill(t.second); g_thumb.clear(); }
    Il2CppObject *t = TexFromFile(path);
    if (t) g_thumb[path].set(t);
    return t;
}
static Il2CppObject *TexShader() {                          // a shader that shows a picture: tried in this order
    static Il2CppObject *sh = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char *names[4] = {"Universal Render Pipeline/Unlit", "Unlit/Texture", "Sprites/Default", "UI/Default"};
        for (const char *n : names) {
            try {
                Il2CppObject *x = G.shaderFind.IsValid() ? G.shaderFind((BNM::Structures::Mono::String *)CreateMonoString(n)) : nullptr;
                if (x) { sh = x; ym::log("leos-menu: phone picture shader %s", n); break; }
            } catch (...) {}
        }
    }
    return sh;
}
static void SetQuadTex(Il2CppObject *q, Il2CppObject *tex) {
    static Method<void> setTex, setTexN;
    static bool looked = false;
    if (!looked) { looked = true; Class mc("UnityEngine", "Material"); setTex = mc.GetMethod("set_mainTexture", 1); setTexN = mc.GetMethod("SetTexture", 2); }
    if (!Alive(q) || !G.tRenderer) return;
    Il2CppObject *r = G.goGetComponent[q](G.tRenderer);
    Il2CppObject *m = r ? G.rendererGetMaterial[r]() : nullptr;
    if (!m) return;
    if (Il2CppObject *sh = TexShader()) { if (G.matSetShader.IsValid()) G.matSetShader[m](sh); MatTransparent(q, true); }
    try {
        if (setTex.IsValid()) setTex[m](tex);
        if (setTexN.IsValid()) setTexN[m](MS("_BaseMap"), tex);
    } catch (...) {}
    SetColor(q, Color(1, 1, 1, 1));
}

// ---- widgets ----
struct PW {
    Ref cube, lbl, txt, quad, quad2;
    std::string text, lastText;
    Color col = Color(0, 0, 0, 0), tcol = Color(1, 1, 1, 1), lastCol = Color(-1, -1, -1, -1), lastTcol = Color(-1, -1, -1, -1);
    float x = 0, y = 0, w = 0, h = 0;
    int act = 0, arg = 0, size = 13, align = 4, lastSize = -1;
    Il2CppObject *tex = nullptr, *lastTex = nullptr;
};
static std::vector<PW> g_pw;
static int g_pwN = 0;
static Ref g_phoneCanvas, g_wallQuad;
static std::vector<Ref> g_phBody;
static bool g_phBuilt = false;
static std::string g_wallPath;
static bool SameCol(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }
static void PWBegin() { g_pwN = 0; }
static int PWAdd(float x, float y, float w, float h, const std::string &text, Color col, Color tcol, int act, int arg, int size = 13, int align = 4) {
    if (g_pwN >= 100) return -1;
    if ((int)g_pw.size() <= g_pwN) g_pw.emplace_back();
    PW &p = g_pw[(size_t)g_pwN++];
    p.x = x; p.y = y; p.w = w; p.h = h; p.text = text; p.col = col; p.tcol = tcol; p.act = act; p.arg = arg; p.size = size; p.align = align; p.tex = nullptr;
    return g_pwN - 1;
}
static void PWTex(int i, Il2CppObject *tex) { if (i >= 0) g_pw[(size_t)i].tex = tex; }
static void PWFlush() {
    Il2CppObject *board = g_board.get(), *pc = g_phoneCanvas.get();
    if (!Alive(board) || !Alive(pc)) return;
    Il2CppObject *bt = Tr(board), *ct = Tr(pc);
    for (size_t i = 0; i < g_pw.size(); i++) {
        PW &p = g_pw[i];
        bool used = (int)i < g_pwN;
        bool wantCube = used && p.col.a > 0.001f, wantTxt = used && !p.text.empty(), wantQuad = used && p.tex;
        if (wantCube) {
            if (!Alive(p.cube.get())) { p.cube.set(Part(bt, p.col, Vector3(p.x, p.y, kPZ), Vector3(p.w, p.h, 0.003f))); p.lastCol = p.col; }
            else if (Il2CppObject *t = Tr(p.cube.get())) {
                G.trSetLocalPosition[t](Vector3(p.x, p.y, kPZ));
                G.trSetLocalScale[t](Vector3(p.w, p.h, 0.003f));
                if (!SameCol(p.col, p.lastCol)) { SetColor(p.cube.get(), p.col); p.lastCol = p.col; }
            }
            Show(p.cube, true);
        } else if (p.cube.get()) Show(p.cube, false);
        if (wantQuad) {
            if (!Alive(p.quad.get())) {
                if (Il2CppObject *q = Prim(5, Color(1, 1, 1, 1), false)) { MatTransparent(q, true); G.trSetParent[Tr(q)](bt, false); p.quad.set(q); p.lastTex = nullptr; }
            }
            if (!Alive(p.quad2.get())) {                  // a Quad shows only from one side; a second one facing the other way means it is visible either way the first one turned out
                if (Il2CppObject *q2 = Prim(5, Color(1, 1, 1, 1), false)) { MatTransparent(q2, true); G.trSetParent[Tr(q2)](bt, false); G.trSetLocalRotation[Tr(q2)](Quaternion(0, 1, 0, 0)); p.quad2.set(q2); }
            }
            Vector3 pos(p.x, p.y, kPZ - 0.0022f);          // in front of the cube behind it, behind the text
            if (Il2CppObject *q = p.quad.get()) {
                Il2CppObject *t = Tr(q);
                G.trSetLocalPosition[t](pos);
                G.trSetLocalScale[t](Vector3(p.w, p.h, 1));
                Show(p.quad, true);
            }
            if (Il2CppObject *q2 = p.quad2.get()) {
                Il2CppObject *t = Tr(q2);
                G.trSetLocalPosition[t](pos);
                G.trSetLocalScale[t](Vector3(p.w, p.h, 1));
                Show(p.quad2, true);
            }
            if (p.tex != p.lastTex) { SetQuadTex(p.quad.get(), p.tex); SetQuadTex(p.quad2.get(), p.tex); p.lastTex = p.tex; }
        } else { if (p.quad.get()) Show(p.quad, false); if (p.quad2.get()) Show(p.quad2, false); }
        if (wantTxt) {
            if (!Alive(p.lbl.get())) { p.lbl.set(Label(ct, "", Vector3(p.x, p.y, 0), p.w, p.h, p.size, &p.txt)); p.lastText.clear(); p.lastSize = -1; if (G.textSetFontStyle.IsValid() && p.txt.get()) G.textSetFontStyle[p.txt.get()](0); }
            if (Il2CppObject *l = p.lbl.get()) {
                Il2CppObject *t = Tr(l), *tx = p.txt.get();
                G.rtSetSizeDelta[t](Vector2(p.w * 1000, p.h * 1000));
                G.trSetLocalPosition[t](Vector3(p.x, p.y, 0) * 1000.0f);
                if (tx) {
                    if (p.size != p.lastSize) { G.textSetFontSize[tx](p.size); if (G.textSetMaxSize.IsValid()) G.textSetMaxSize[tx](p.size); p.lastSize = p.size; }
                    G.textSetAlignment[tx](p.align);
                    if (p.text != p.lastText) { SetText(p.txt, p.text.c_str()); p.lastText = p.text; }
                    if (!SameCol(p.tcol, p.lastTcol)) { G.graphicSetColor[tx](p.tcol); p.lastTcol = p.tcol; }
                }
                Show(p.lbl, true);
            }
        } else if (p.lbl.get()) Show(p.lbl, false);
    }
}

// ---- the phone body, built once: black case, white front, screen, speaker, camera dots, home button (like the picture) ----
static void PhoneBuild() {
    if (g_phBuilt || !Alive(g_board.get())) return;
    Il2CppObject *bt = Tr(g_board.get());
    auto keep = [&](Il2CppObject *o) { if (o) { Ref r; r.set(o); g_phBody.push_back(r); } };
    Quaternion qx(0.70710678f, 0, 0, 0.70710678f);
    auto roundRect = [&](Color c, float z, float w, float h, float th, float r) {          // a rectangle with rounded corners
        keep(Part(bt, c, Vector3(0, 0, z), Vector3(w - 2 * r, h, th)));
        keep(Part(bt, c, Vector3(0, 0, z), Vector3(w, h - 2 * r, th)));
        for (int i = 0; i < 4; i++) {
            Il2CppObject *o = Prim(2, c, false);
            if (!o) continue;
            Il2CppObject *t = Tr(o);
            G.trSetParent[t](bt, false);
            G.trSetLocalRotation[t](qx);
            G.trSetLocalPosition[t](Vector3(((i & 1) ? 1.0f : -1.0f) * (w / 2 - r), ((i & 2) ? 1.0f : -1.0f) * (h / 2 - r), z));
            G.trSetLocalScale[t](Vector3(2 * r, th / 2, 2 * r));
            keep(o);
        }
    };
    roundRect(Color(0.04f, 0.04f, 0.05f, 1), 0.003f, kPhW + 0.018f, kPhH + 0.018f, 0.014f, 0.045f);
    roundRect(Color(0.96f, 0.96f, 0.96f, 1), -0.0015f, kPhW, kPhH, 0.01f, 0.04f);
    keep(Part(bt, Color(0.03f, 0.03f, 0.05f, 1), Vector3(0, kScrCy, -0.0085f), Vector3(kScrW, kScrH, 0.004f)));
    keep(Part(bt, Color(0.15f, 0.15f, 0.17f, 1), Vector3(0, 0.232f, -0.0075f), Vector3(0.05f, 0.006f, 0.002f)));
    keep(Part(bt, Color(0.05f, 0.05f, 0.06f, 1), Vector3(0, 0.247f, -0.0075f), Vector3(0.008f, 0.008f, 0.002f)));
    keep(Part(bt, Color(0.05f, 0.05f, 0.06f, 1), Vector3(-0.06f, 0.229f, -0.0075f), Vector3(0.01f, 0.01f, 0.002f)));
    Il2CppObject *ring = Prim(2, Color(0.72f, 0.72f, 0.75f, 1), false), *btn = Prim(2, Color(0.04f, 0.04f, 0.05f, 1), false);
    if (ring) { Il2CppObject *t = Tr(ring); G.trSetParent[t](bt, false); G.trSetLocalPosition[t](Vector3(0, -0.2225f, -0.0075f)); G.trSetLocalRotation[t](qx); G.trSetLocalScale[t](Vector3(0.058f, 0.001f, 0.058f)); keep(ring); }
    if (btn) { Il2CppObject *t = Tr(btn); G.trSetParent[t](bt, false); G.trSetLocalPosition[t](Vector3(0, -0.2225f, -0.0085f)); G.trSetLocalRotation[t](qx); G.trSetLocalScale[t](Vector3(0.05f, 0.001f, 0.05f)); keep(btn); }
    if (Il2CppObject *q = Prim(5, Color(0.07f, 0.1f, 0.28f, 1), false)) {          // the wallpaper
        Il2CppObject *t = Tr(q);
        G.trSetParent[t](bt, false);
        G.trSetLocalPosition[t](Vector3(0, kScrCy, -0.0108f));
        G.trSetLocalScale[t](Vector3(kScrW, kScrH, 1));
        g_wallQuad.set(q);
    }
    if (Il2CppObject *cv = Canvas(bt, Vector3(0, 0, kPZ - 0.003f), 0.6f, 0.7f)) g_phoneCanvas.set(cv);
    std::string f = PersistentPath();
    if (!f.empty()) {
        try { std::ifstream i(f + "/leos-wall.txt"); std::getline(i, g_wallPath); } catch (...) {}
        if (!g_wallPath.empty()) if (Il2CppObject *t = TexFromFile(g_wallPath)) SetQuadTex(g_wallQuad.get(), t);
    }
    g_phBuilt = true;
}
static void PhoneBodyShow(bool on) {
    for (auto &r : g_phBody) Show(r, on);
    Show(g_wallQuad, on);
    Show(g_phoneCanvas, on);
    if (!on) { g_pwN = 0; PWFlush(); }
}
static void PhoneOldParts(bool show) {
    Show(g_bg, show); Show(g_outline, show); Show(g_titleBar, show); Show(g_menuCanvas[0], show);
    for (auto &r : g_srchParts) Show(r, show);
    if (!show) {
        Show(g_prev, false); Show(g_next, false);
        for (auto &r : g_bgX) Show(r, false);
        for (auto &r : g_outX) Show(r, false);
        for (auto &r : g_rowCube) Show(r, false);
        for (auto &r : g_knob) Show(r, false);
        for (int m = 1; m <= 3; m++) { for (auto &r : g_keyCube[m]) Show(r, false); for (auto &r : g_keyLbl[m]) Show(r, false); }
    } else g_dirty = true;                                   // the normal menu redraws itself
}

// ---- cameras ----
struct PCam { Ref go, cam, rt; };
static PCam g_camPC, g_mapPC;
static bool g_camFront = false;
static void PCamKill(PCam &c) { Kill(c.go); c.cam.clear(); Kill(c.rt); }
static bool PCamBuild(PCam &c, int w, int h, bool ortho, float size, float fov, Il2CppObject *parent) {
    Class camC("UnityEngine", "Camera"), rtC("UnityEngine", "RenderTexture");
    if (!camC.IsValid() || !rtC.IsValid()) return false;
    try {
        Il2CppObject *rt = rtC.CreateNewObjectParameters(w, h, 16);
        Il2CppObject *cgo = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("LeoPhoneCam"));
        Il2CppObject *cam = (rt && cgo) ? G.goAddComponent[cgo]((Il2CppObject *)camC.GetMonoType()) : nullptr;
        if (!cam) { if (cgo) G.destroy(cgo); return false; }
        Method<void> setTarget, setOrtho, setOrthoSize, setFov, setFlags, setBg, setNear, setFar;
        setTarget = camC.GetMethod("set_targetTexture", 1);
        setOrtho = camC.GetMethod("set_orthographic", 1);
        setOrthoSize = camC.GetMethod("set_orthographicSize", 1);
        setFov = camC.GetMethod("set_fieldOfView", 1);
        setFlags = camC.GetMethod("set_clearFlags", 1);
        setBg = camC.GetMethod("set_backgroundColor", 1);
        setNear = camC.GetMethod("set_nearClipPlane", 1);
        setFar = camC.GetMethod("set_farClipPlane", 1);
        if (!setTarget.IsValid()) { G.destroy(cgo); return false; }
        setTarget[cam](rt);
        if (setOrtho.IsValid()) setOrtho[cam](ortho);
        if (ortho && setOrthoSize.IsValid()) setOrthoSize[cam](size);
        if (!ortho && setFov.IsValid()) setFov[cam](fov);
        if (setFlags.IsValid()) setFlags[cam](2);
        if (setBg.IsValid()) setBg[cam](Color(0.05f, 0.07f, 0.1f, 1));
        if (setNear.IsValid()) setNear[cam](0.05f);
        if (setFar.IsValid()) setFar[cam](400.0f);
        if (parent) G.trSetParent[Tr(cgo)](parent, false);
        c.go.set(cgo); c.cam.set(cam); c.rt.set(rt);
        return true;
    } catch (...) { return false; }
}
struct Rect4 { float x, y, w, h; };
static bool CaptureJpg(Il2CppObject *rt, int w, int h, const std::string &path) {
    static Class tc("UnityEngine", "Texture2D"), rtC("UnityEngine", "RenderTexture"), ic("UnityEngine", "ImageConversion");
    static Method<void> setActive, readPixels, apply, reinit;
    static Method<Il2CppObject *> encode;
    static bool looked = false;
    if (!looked) {
        looked = true;
        setActive = rtC.GetMethod("set_active", 1);
        readPixels = tc.GetMethod("ReadPixels", 3);
        apply = tc.GetMethod("Apply", 0);
        reinit = tc.GetMethod("Reinitialize", 2);
        encode = ic.GetMethod("EncodeToJPG", 2);
    }
    if (!setActive.IsValid() || !readPixels.IsValid() || !apply.IsValid() || !reinit.IsValid() || !encode.IsValid() || !Alive(rt)) return false;
    try {
        Il2CppObject *tex = tc.CreateNewObjectParameters(2, 2);
        if (!tex) return false;
        reinit[tex](w, h);
        setActive((Il2CppObject *)rt);
        readPixels[tex](Rect4{0, 0, (float)w, (float)h}, 0, 0);
        apply[tex]();
        setActive((Il2CppObject *)nullptr);
        Il2CppObject *bytes = encode(tex, 85);
        G.destroy(tex);
        size_t n = bytes ? ArrLen((uint8_t *)bytes) : 0;
        if (!n) return false;
        std::ofstream o(path, std::ios::binary);
        o.write((const char *)((uint8_t *)bytes + 0x20), (std::streamsize)n);
        return o.good();
    } catch (...) { return false; }
}

// ---- saved photos, videos (a folder of frames) and notes ----
struct Media { std::string path; bool video; };
static std::vector<Media> g_media;
static int g_mediaSel = -1;
static std::vector<std::string> g_vidFrames;
static size_t g_vidIdx = 0;
static double g_vidNext = 0;
static Ref g_vidTex;
static bool g_recOn = false;
static std::string g_recDir;
static int g_recN = 0, g_stampN = 0;
static double g_recNext = 0;
static std::string PhoneDir(const char *sub) {
    std::string p = PersistentPath();
    if (p.empty()) return std::string();
    std::string d = p + "/" + sub;
    try { std::filesystem::create_directories(d); } catch (...) {}
    return d;
}
static std::string Stamp() {
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char b[40];
    strftime(b, sizeof b, "%Y%m%d_%H%M%S", &tmv);
    return std::string(b) + "_" + std::to_string((g_stampN++) % 100);
}
static void MediaRefresh() {
    g_media.clear();
    std::string pd = PhoneDir("LeoPhotos"), vd = PhoneDir("LeoVideos");
    try {
        if (!pd.empty()) for (auto &f : std::filesystem::directory_iterator(pd)) if (f.is_regular_file() && Lower(f.path().extension().string()) == ".jpg") g_media.push_back({f.path().string(), false});
        if (!vd.empty()) for (auto &f : std::filesystem::directory_iterator(vd)) if (f.is_directory()) g_media.push_back({f.path().string(), true});
    } catch (...) {}
    std::sort(g_media.begin(), g_media.end(), [](const Media &a, const Media &b) { return std::filesystem::path(a.path).filename().string() > std::filesystem::path(b.path).filename().string(); });
}
static std::string FirstFrame(const std::string &dir) {
    try { std::vector<std::string> f; for (auto &e : std::filesystem::directory_iterator(dir)) f.push_back(e.path().string()); if (!f.empty()) { std::sort(f.begin(), f.end()); return f[0]; } } catch (...) {}
    return std::string();
}
static std::vector<std::string> g_noteFiles;
static std::string g_noteText, g_notePath;
static void NotesRefresh() {
    g_noteFiles.clear();
    std::string nd = PhoneDir("LeoNotes");
    try { if (!nd.empty()) for (auto &f : std::filesystem::directory_iterator(nd)) if (f.is_regular_file()) g_noteFiles.push_back(f.path().string()); } catch (...) {}
    std::sort(g_noteFiles.begin(), g_noteFiles.end(), std::greater<std::string>());
}
static std::string ReadFile(const std::string &p) {
    std::ifstream f(p);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void NoteSave() { if (g_notePath.empty()) return; try { std::ofstream o(g_notePath); o << g_noteText; } catch (...) {} }

// ---- the apps ----
static void PhoneStopVideo() { g_vidFrames.clear(); g_vidIdx = 0; Kill(g_vidTex); }
static void PhoneOpen(int app) {
    g_phApp = app;
    g_phScroll = 0;
    g_phStack.clear();
    if (app == PH_PHOTOS) MediaRefresh();
    if (app == PH_MUSIC) SoundEnsure();
    if (app == PH_NOTES) NotesRefresh();
    g_phDirty = true;
}
static void PhoneGoHome() {
    if (g_recOn) g_recOn = false;
    PhoneStopVideo();
    g_phApp = PH_HOME;
    g_phScroll = 0;
    g_phDirty = true;
}
static void PhoneBack() {
    if (g_phApp == PH_CAT) {
        if (!g_phStack.empty()) { g_phCat = g_phStack.back(); g_phStack.pop_back(); }
        else g_phApp = PH_SETTINGS;
    } else if (g_phApp == PH_VIEW) { PhoneStopVideo(); g_phApp = PH_PHOTOS; }
    else if (g_phApp == PH_NOTE) { NoteSave(); NotesRefresh(); g_phApp = PH_NOTES; }
    else PhoneGoHome();
    g_phScroll = 0;
}
static void PhoneItemPress(MenuItem *it) {
    if (it->kind == OPEN) {
        if (it->target == MAIN || it->target == SOUNDBOARD || it->target == OWNER) return;
        g_phStack.push_back(g_phCat);
        g_phCat = it->target;
        g_phScroll = 0;
    } else if (it->kind == SLIDER) {
        if (it->ival && it->icount > 1) *it->ival = (*it->ival + (it->icount > 20 ? 5 : 1)) % it->icount;
    } else Press(it);
}
static void PhoneStartVideo(size_t idx) {
    g_vidFrames.clear();
    try { for (auto &e : std::filesystem::directory_iterator(g_media[idx].path)) g_vidFrames.push_back(e.path().string()); } catch (...) {}
    std::sort(g_vidFrames.begin(), g_vidFrames.end());
    g_vidIdx = 0;
    g_vidNext = 0;
    Kill(g_vidTex);
    if (!g_vidFrames.empty()) { if (Il2CppObject *t = TexFromFile(g_vidFrames[0])) g_vidTex.set(t); }
}
static void PhoneAct(int act, int arg) {
    switch (act) {
    case A_APP: PhoneOpen(arg); break;
    case A_CAT: if (arg >= 0 && arg < (int)g_phItems.size()) { g_phCat = g_phItems[(size_t)arg]->target; g_phApp = PH_CAT; g_phScroll = 0; g_phStack.clear(); } break;
    case A_ITEM: if (arg >= 0 && arg < (int)g_phItems.size()) PhoneItemPress(g_phItems[(size_t)arg]); break;
    case A_EXIT: f_phoneMenu = false; g_menuPrev[4] = false; g_dirty = true; Notify("Default menu type"); break;
    case A_BACK: PhoneBack(); break;
    case A_PHOTO:
        if (arg >= 0 && arg < (int)g_media.size()) { g_mediaSel = arg; g_phApp = PH_VIEW; g_phScroll = 0; if (g_media[(size_t)arg].video) PhoneStartVideo((size_t)arg); }
        break;
    case A_WALL:
        if (g_mediaSel >= 0 && g_mediaSel < (int)g_media.size() && !g_media[(size_t)g_mediaSel].video) {
            g_wallPath = g_media[(size_t)g_mediaSel].path;
            std::string f = PersistentPath();
            if (!f.empty()) { try { std::ofstream o(f + "/leos-wall.txt"); o << g_wallPath; } catch (...) {} }
            if (Il2CppObject *t = ThumbFor(g_wallPath)) SetQuadTex(g_wallQuad.get(), t);
            Notify("Wallpaper set");
        }
        break;
    case A_SHUTTER: {
        std::string d = PhoneDir("LeoPhotos");
        if (!d.empty() && Alive(g_camPC.rt.get()) && CaptureJpg(g_camPC.rt.get(), 256, 320, d + "/IMG_" + Stamp() + ".jpg")) Notify("Photo saved");
        else Notify("Couldn't save the photo");
        break;
    }
    case A_FLIP: g_camFront = !g_camFront; break;
    case A_REC:
        if (!g_recOn) { std::string d = PhoneDir("LeoVideos"); if (!d.empty()) { g_recDir = d + "/VID_" + Stamp(); try { std::filesystem::create_directories(g_recDir); } catch (...) {} g_recN = 0; g_recNext = 0; g_recOn = true; Notify("Recording"); } }
        else { g_recOn = false; Notify("Video saved"); }
        break;
    case A_GALLERY: PhoneOpen(PH_PHOTOS); break;
    case A_MUSIC: if (arg >= 0 && arg < (int)g_sounds.size()) PlaySoundIdx(arg); break;
    case A_STOPSND: StopSound(); break;
    case A_NEWNOTE: { std::string d = PhoneDir("LeoNotes"); if (!d.empty()) { g_notePath = d + "/note_" + Stamp() + ".txt"; g_noteText.clear(); NoteSave(); g_phApp = PH_NOTE; g_phScroll = 0; } break; }
    case A_NOTE: if (arg >= 0 && arg < (int)g_noteFiles.size()) { g_notePath = g_noteFiles[(size_t)arg]; g_noteText = ReadFile(g_notePath); g_phApp = PH_NOTE; g_phScroll = 0; } break;
    case A_KEY:
        if (arg == 8) { if (!g_noteText.empty()) g_noteText.pop_back(); }
        else if (g_noteText.size() < 600) g_noteText += (char)arg;
        NoteSave();
        g_phScroll = 1.0f;                                    // typing follows the end of the note
        break;
    }
    g_phDirty = true;
}

// ---- drawing the screens ----
static const Color kPhGray(0.22f, 0.22f, 0.28f, 1), kPhBar(0.12f, 0.12f, 0.18f, 1), kPhWhite(1, 1, 1, 1), kPhNone(0, 0, 0, 0);
static float PhoneOffset(int rows, float pitch) {
    float total = (float)rows * pitch, view = kViewTop - kViewBot, mx = total > view ? total - view : 0;
    return g_phScroll * mx;
}
static void PhoneHeader(const char *title, bool back) {
    PWAdd(0, 0.188f, 0.21f, 0.016f, std::string("Leo's Menu ") + kVersion, kPhNone, Color(1, 1, 1, 0.55f), A_NONE, 0, 9);
    PWAdd(0, 0.167f, 0.22f, 0.026f, "", kPhBar, kPhWhite, A_NONE, 0);
    if (back) PWAdd(-0.072f, 0.167f, 0.07f, 0.022f, "< Back", kPhGray, kPhWhite, A_BACK, 0, 11);
    PWAdd(back ? 0.04f : 0.0f, 0.167f, 0.12f, 0.022f, title, kPhNone, kPhWhite, A_NONE, 0, 13);
}
static void PhoneScrollBar() {
    PWAdd(0.168f, kScrCy, 0.012f, 0.37f, "", Color(0.25f, 0.25f, 0.3f, 1), kPhWhite, A_NONE, 0);
    float ky = 0.1625f - g_phScroll * (0.365f - 0.05f);
    PWAdd(0.168f, ky, 0.026f, 0.05f, "", Color(0.92f, 0.92f, 0.95f, 1), kPhWhite, A_NONE, 0);
}
static void PhoneItemsFor(Cat c, std::vector<MenuItem *> &out) {
    if (c == OTHERPLAYERS) PlayersEnsure();
    if (c == ENABLED) { for (int _gi = 0; _gi < kItems; _gi++) { auto &it = g_items[_gi]; if (it.kind == TOGGLE && it.flag && *it.flag && !IsMenuFlag(it.flag)) out.push_back(&it); } return; }
    for (int _gi = 0; _gi < kItems; _gi++) {
        auto &it = g_items[_gi];
        if (it.cat != c) continue;
        if (it.kind == OPEN && (!strncmp(it.label, "Exit", 4) || !strncmp(it.label, "Cancel", 6))) continue;
        out.push_back(&it);
    }
    if (c == OTHERPLAYERS) for (auto &it : g_playerItems) out.push_back(&it);
    if (c == PRESETS) { PresetsEnsure(); for (auto &it : g_presetItems) out.push_back(&it); }
    if (c == SPAWNPRESETS) { EggEnsure(); for (auto &it : g_eggItems) out.push_back(&it); }
    if (c == FAVOURITES) for (int _gi = 0; _gi < kItems; _gi++) { auto &it = g_items[_gi]; if (it.cat != FAVOURITES && it.kind != OPEN && IsFav(it.label)) out.push_back(&it); }
}
static void PhoneModRows() {
    float pitch = 0.036f, off = PhoneOffset((int)g_phItems.size(), pitch);
    for (size_t i = 0; i < g_phItems.size(); i++) {
        float y = kViewTop - pitch * ((float)i + 0.5f) + off;
        if (y - 0.017f < kViewBot || y + 0.017f > kViewTop) continue;
        MenuItem *it = g_phItems[i];
        bool on = it->kind == TOGGLE && it->flag && *it->flag;
        std::string st;
        Color sc = kPhGray;
        if (it->kind == TOGGLE) { st = on ? "ON" : "OFF"; sc = on ? Color(0.1f, 0.55f, 0.2f, 1) : Color(0.35f, 0.35f, 0.4f, 1); }
        else if (it->kind == SLIDER) { st = it->iname && it->ival ? it->iname(*it->ival) : ""; sc = Color(0.2f, 0.3f, 0.55f, 1); }
        else if (it->kind == OPEN) { st = ">"; sc = Color(0.3f, 0.3f, 0.36f, 1); }
        else { st = "RUN"; sc = Color(0.5f, 0.3f, 0.15f, 1); }
        PWAdd(-0.03f, y, 0.15f, 0.03f, it->label, kPhGray, kPhWhite, A_ITEM, (int)i, 12);
        PWAdd(0.0775f, y, 0.062f, 0.03f, st, sc, kPhWhite, A_ITEM, (int)i, 11);
    }
}
static void PhoneRender() {
    PWBegin();
    g_phItems.clear();
    switch (g_phApp) {
    case PH_HOME: {
        PWAdd(0, 0.188f, 0.21f, 0.016f, std::string("Leo's Menu ") + kVersion, kPhNone, Color(1, 1, 1, 0.8f), A_NONE, 0, 10);
        const char *names[6] = {"Settings", "Camera", "Photos", "Music", "Maps", "Notes"};
        int apps[6] = {PH_SETTINGS, PH_CAMERA, PH_PHOTOS, PH_MUSIC, PH_MAPS, PH_NOTES};
        for (int k = 0; k < 6; k++) {
            float x = (float)(k % 3 - 1) * 0.075f, y = 0.1f - (float)(k / 3) * 0.105f;
            const Color base[6] = {Color(0.45f, 0.48f, 0.56f, 1), Color(0.8f, 0.8f, 0.84f, 1), Color(0.9f, 0.3f, 0.5f, 1), Color(0.85f, 0.2f, 0.2f, 1), Color(0.92f, 0.92f, 0.92f, 1), Color(0.85f, 0.25f, 0.2f, 1)};
            PWAdd(x, y, 0.056f, 0.056f, "", base[k], kPhWhite, A_APP, apps[k]);          // a colored square, so the icon is there even without its picture
            int id = PWAdd(x, y, 0.056f, 0.056f, "", kPhNone, kPhWhite, A_APP, apps[k]);
            PWTex(id, IconTex(k));
            PWAdd(x, y - 0.038f, 0.07f, 0.016f, names[k], kPhNone, kPhWhite, A_APP, apps[k], 10);
        }
        break;
    }
    case PH_SETTINGS: {
        PhoneHeader("Settings", true);
        for (int _gi = 0; _gi < kItems; _gi++) { auto &it = g_items[_gi]; if (it.cat == MAIN && it.kind == OPEN && it.target != SOUNDBOARD && it.target != OWNER) g_phItems.push_back(&it); }
        float pitch = 0.036f;
        int rows = (int)g_phItems.size() + 1;
        float off = PhoneOffset(rows, pitch);
        for (int i = 0; i < rows; i++) {
            float y = kViewTop - pitch * ((float)i + 0.5f) + off;
            if (y - 0.017f < kViewBot || y + 0.017f > kViewTop) continue;
            if (i < (int)g_phItems.size()) PWAdd(0, y, 0.2f, 0.03f, g_phItems[(size_t)i]->label, kPhGray, kPhWhite, A_CAT, i, 13);
            else PWAdd(0, y, 0.2f, 0.03f, "Exit Menu Type", kPhGray, Color(1.0f, 0.2f, 0.2f, 1), A_EXIT, 0, 13);       // red: back to the default menu
        }
        PhoneScrollBar();
        break;
    }
    case PH_CAT:
        PhoneHeader(kCatNames[(int)g_phCat], true);
        PhoneItemsFor(g_phCat, g_phItems);
        PhoneModRows();
        PhoneScrollBar();
        break;
    case PH_CAMERA: {
        PhoneHeader("Camera", true);
        PWAdd(0.07f, 0.167f, 0.075f, 0.022f, g_camFront ? "Front" : "Back", Color(0.3f, 0.3f, 0.5f, 1), kPhWhite, A_FLIP, 0, 11);
        int id = PWAdd(0, 0.015f, 0.208f, 0.26f, "", Color(0, 0, 0, 1), kPhWhite, A_NONE, 0);
        (void)id;
        int q = PWAdd(0, 0.015f, 0.2f, 0.25f, "", kPhNone, kPhWhite, A_NONE, 0);
        PWTex(q, g_camPC.rt.get());
        PWAdd(0, -0.155f, 0.046f, 0.046f, "", Color(1, 1, 1, 1), Color(0, 0, 0, 1), A_SHUTTER, 0);
        PWAdd(-0.08f, -0.155f, 0.042f, 0.034f, g_recOn ? "STOP" : "REC", g_recOn ? Color(0.9f, 0.1f, 0.1f, 1) : Color(0.55f, 0.1f, 0.1f, 1), kPhWhite, A_REC, 0, 10);
        PWAdd(0.08f, -0.155f, 0.042f, 0.034f, "Pics", kPhGray, kPhWhite, A_GALLERY, 0, 10);
        break;
    }
    case PH_PHOTOS: {
        PhoneHeader("Photos", true);
        float pitch = 0.088f;
        int rows = ((int)g_media.size() + 2) / 3;
        float off = PhoneOffset(rows, pitch);
        if (g_media.empty()) PWAdd(0, 0.05f, 0.2f, 0.03f, "No photos or videos yet", kPhNone, Color(1, 1, 1, 0.7f), A_NONE, 0, 11);
        for (int i = 0; i < (int)g_media.size(); i++) {
            float y = kViewTop - pitch * ((float)(i / 3) + 0.5f) + off, x = (float)(i % 3 - 1) * 0.072f;
            if (y - 0.043f < kViewBot || y + 0.043f > kViewTop) continue;
            const Media &m = g_media[(size_t)i];
            int id = PWAdd(x, y, 0.066f, 0.082f, m.video ? "VIDEO" : "", Color(0.1f, 0.1f, 0.14f, 1), Color(1, 1, 1, 0.9f), A_PHOTO, i, 9);
            (void)id;
            int q = PWAdd(x, y, 0.066f, 0.082f, "", kPhNone, kPhWhite, A_PHOTO, i);
            PWTex(q, ThumbFor(m.video ? FirstFrame(m.path) : m.path));
        }
        PhoneScrollBar();
        break;
    }
    case PH_VIEW: {
        PhoneHeader("Photo", true);
        bool video = g_mediaSel >= 0 && g_mediaSel < (int)g_media.size() && g_media[(size_t)g_mediaSel].video;
        int q = PWAdd(0, 0.0f, 0.208f, 0.26f, "", Color(0, 0, 0, 1), kPhWhite, A_NONE, 0);
        (void)q;
        int t = PWAdd(0, 0.0f, 0.2f, 0.25f, "", kPhNone, kPhWhite, A_NONE, 0);
        if (g_mediaSel >= 0 && g_mediaSel < (int)g_media.size()) PWTex(t, video ? g_vidTex.get() : ThumbFor(g_media[(size_t)g_mediaSel].path));
        if (!video) PWAdd(0, -0.16f, 0.2f, 0.028f, "Set as wallpaper", Color(0.2f, 0.35f, 0.7f, 1), kPhWhite, A_WALL, 0, 12);
        else PWAdd(0, -0.16f, 0.2f, 0.028f, "Playing video", kPhNone, Color(1, 1, 1, 0.7f), A_NONE, 0, 11);
        break;
    }
    case PH_MUSIC: {
        PhoneHeader("Music", true);
        float pitch = 0.036f;
        int rows = (int)g_sounds.size() + 1;
        float off = PhoneOffset(rows, pitch);
        for (int i = 0; i < rows; i++) {
            float y = kViewTop - pitch * ((float)i + 0.5f) + off;
            if (y - 0.017f < kViewBot || y + 0.017f > kViewTop) continue;
            if (i == 0) PWAdd(0, y, 0.2f, 0.03f, "Stop", Color(0.55f, 0.1f, 0.1f, 1), kPhWhite, A_STOPSND, 0, 13);
            else PWAdd(0, y, 0.2f, 0.03f, g_sounds[(size_t)i - 1].name, kPhGray, kPhWhite, A_MUSIC, i - 1, 12);
        }
        if (g_sounds.empty()) PWAdd(0, 0.05f, 0.2f, 0.05f, "No sounds. Put files in the HSounds folder.", kPhNone, Color(1, 1, 1, 0.7f), A_NONE, 0, 10);
        PhoneScrollBar();
        break;
    }
    case PH_MAPS: {
        PhoneHeader("Maps", true);
        PWAdd(0, 0.015f, 0.208f, 0.26f, "", Color(0, 0, 0, 1), kPhWhite, A_NONE, 0);
        int q = PWAdd(0, 0.015f, 0.2f, 0.25f, "", kPhNone, kPhWhite, A_NONE, 0);
        PWTex(q, g_mapPC.rt.get());
        PWAdd(0, 0.015f, 0.01f, 0.01f, "", Color(1, 0.1f, 0.1f, 1), kPhWhite, A_NONE, 0);                  // you
        PWAdd(0, -0.16f, 0.17f, 0.008f, "", Color(0.35f, 0.35f, 0.4f, 1), kPhWhite, A_NONE, 0);
        PWAdd(-0.085f + 0.17f * g_mapZoom, -0.16f, 0.016f, 0.024f, "", Color(0.95f, 0.95f, 0.98f, 1), kPhWhite, A_NONE, 0);
        PWAdd(-0.095f, -0.175f, 0.03f, 0.012f, "Zoom", kPhNone, Color(1, 1, 1, 0.7f), A_NONE, 0, 9);
        break;
    }
    case PH_NOTES: {
        PhoneHeader("Notes", true);
        float pitch = 0.036f;
        int rows = (int)g_noteFiles.size() + 1;
        float off = PhoneOffset(rows, pitch);
        for (int i = 0; i < rows; i++) {
            float y = kViewTop - pitch * ((float)i + 0.5f) + off;
            if (y - 0.017f < kViewBot || y + 0.017f > kViewTop) continue;
            if (i == 0) { PWAdd(0, y, 0.2f, 0.03f, "+ New Note", Color(0.15f, 0.45f, 0.25f, 1), kPhWhite, A_NEWNOTE, 0, 13); continue; }
            std::string t = ReadFile(g_noteFiles[(size_t)i - 1]);
            size_t nl = t.find('\n');
            if (nl != std::string::npos) t = t.substr(0, nl);
            if (t.size() > 26) t = t.substr(0, 26);
            if (t.empty()) t = "(empty)";
            PWAdd(0, y, 0.2f, 0.03f, t, kPhGray, kPhWhite, A_NOTE, i - 1, 11);
        }
        PhoneScrollBar();
        break;
    }
    case PH_NOTE: {
        PhoneHeader("Note", true);
        std::string shown;                                   // the text wrapped at 26 letters, the last 8 lines
        std::vector<std::string> lines;
        std::string cur;
        for (char ch : g_noteText) { if (ch == '\n' || cur.size() >= 26) { lines.push_back(cur); cur.clear(); if (ch == '\n') continue; } cur += ch; }
        lines.push_back(cur + "_");
        size_t maxStart = lines.size() > 8 ? lines.size() - 8 : 0;
        size_t from = (size_t)lroundf(g_phScroll * (float)maxStart);                          // the slider beside the phone scrolls the text
        size_t to = std::min(lines.size(), from + 8);
        for (size_t i = from; i < to; i++) shown += lines[i] + (i + 1 < to ? "\n" : "");
        PhoneScrollBar();
        PWAdd(0, 0.075f, 0.21f, 0.14f, shown, Color(0.12f, 0.12f, 0.16f, 1), kPhWhite, A_NONE, 0, 11, 0);
        const char *rowsK[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
        for (int r = 0; r < 4; r++) {
            int n = (int)strlen(rowsK[r]);
            for (int i = 0; i < n; i++) PWAdd(((float)i - (float)(n - 1) / 2.0f) * 0.0215f, -0.03f - (float)r * 0.0285f, 0.0195f, 0.025f, std::string(1, rowsK[r][i]), kPhGray, kPhWhite, A_KEY, (int)tolower((unsigned char)rowsK[r][i]), 12);
        }
        float yb = -0.03f - 4 * 0.0285f;
        PWAdd(-0.06f, yb, 0.075f, 0.025f, "SPACE", kPhGray, kPhWhite, A_KEY, ' ', 10);
        PWAdd(-0.0075f, yb, 0.02f, 0.025f, ".", kPhGray, kPhWhite, A_KEY, '.', 12);
        PWAdd(0.0165f, yb, 0.02f, 0.025f, ",", kPhGray, kPhWhite, A_KEY, ',', 12);
        PWAdd(0.0535f, yb, 0.045f, 0.025f, "DEL", Color(0.5f, 0.2f, 0.2f, 1), kPhWhite, A_KEY, 8, 10);
        PWAdd(0.1f, yb, 0.045f, 0.025f, "ENT", Color(0.2f, 0.4f, 0.25f, 1), kPhWhite, A_KEY, '\n', 10);
        break;
    }
    }
    PWFlush();
    g_phDirty = false;
}

// ---- touching it ----
static int g_phTouch = -2;
static double g_phNextPress = 0;
static void PhoneInput() {
    Il2CppObject *board = g_board.get(), *pointer = g_pointer.get();
    if (!Alive(board) || !Alive(pointer)) return;
    Vector3 p = G.trInverseTransformPoint[Tr(board)](G.trGetPosition[Tr(pointer)]());
    int hit = -2;
    if (p.z >= -0.035f && p.z <= 0.015f) {
        if (fabsf(p.x) < 0.03f && fabsf(p.y + 0.2225f) < 0.03f) hit = -3;                           // the home button
        else if (p.x > 0.145f && p.x < 0.195f && p.y > -0.19f && p.y < 0.2f) hit = -4;            // the scroll slider beside the phone
        else if (g_phApp == PH_MAPS && fabsf(p.x) < 0.105f && p.y < -0.145f && p.y > -0.178f) hit = -5;   // the map zoom slider
        else
            for (int i = g_pwN - 1; i >= 0; i--) {
                PW &w = g_pw[(size_t)i];
                if (w.act != A_NONE && fabsf(p.x - w.x) < w.w / 2 && fabsf(p.y - w.y) < w.h / 2) { hit = i; break; }
            }
    }
    bool press = hit != -2 && hit != g_phTouch;
    g_phTouch = hit;
    if (hit == -4) { float f = (0.1875f - p.y) / 0.365f; g_phScroll = f < 0 ? 0 : f > 1 ? 1 : f; g_phDirty = true; return; }
    if (hit == -5) { float f = (p.x + 0.085f) / 0.17f; g_mapZoom = f < 0 ? 0 : f > 1 ? 1 : f; g_phDirty = true; return; }
    if (!press || g_time < g_phNextPress) return;
    g_phNextPress = g_time + 0.18;
    Buzz();
    if (hit == -3) { PhoneGoHome(); return; }
    if (hit >= 0) PhoneAct(g_pw[(size_t)hit].act, g_pw[(size_t)hit].arg);
}

// ---- per frame ----
static bool g_phoneWas = false;
static void PhoneOff() {
    PCamKill(g_camPC);
    PCamKill(g_mapPC);
    g_recOn = false;
    PhoneStopVideo();
}
static void PhoneStateTick() {
    bool on = PhoneActive();
    if (on && !g_phoneWas) {
        PhoneBuild();
        if (!g_phBuilt) { f_phoneMenu = false; return; }
        PhoneOldParts(false);
        PhoneBodyShow(true);
        g_phApp = PH_HOME;
        g_phDirty = true;
        g_phoneWas = true;
    } else if (!on && g_phoneWas) {
        PhoneBodyShow(false);
        PhoneOldParts(true);
        PhoneOff();
        g_phoneWas = false;
    }
    if (g_phoneWas && !g_menuShown) PhoneOff();
}
static void PhoneFrame() {
    if (!(f_floatMenu && g_floatPlaced)) { PlaceMenu(); g_floatPlaced = f_floatMenu; }
    PhoneInput();
    Il2CppObject *board = g_board.get(), *head = g_head.get();
    if (!Alive(board)) return;
    Il2CppObject *bt = Tr(board);
    // camera app: a camera in the phone, looking out of its back (or its front)
    if (g_phApp == PH_CAMERA) {
        if (!Alive(g_camPC.go.get()) && !PCamBuild(g_camPC, 256, 320, false, 0, 70.0f, bt)) { Notify("Camera isn't available here"); g_phApp = PH_HOME; g_phDirty = true; }
        if (Il2CppObject *cg = g_camPC.go.get()) {
            Il2CppObject *t = Tr(cg);
            G.trSetLocalPosition[t](Vector3(0, 0, g_camFront ? -0.03f : 0.03f));
            G.trSetLocalRotation[t](g_camFront ? Quaternion(0, 1, 0, 0) : Quaternion(0, 0, 0, 1));    // front: turned around to face you
        }
        if (g_recOn && g_time >= g_recNext) {
            g_recNext = g_time + 0.125;
            char nm[32];
            snprintf(nm, sizeof nm, "/f%04d.jpg", ++g_recN);
            CaptureJpg(g_camPC.rt.get(), 256, 320, g_recDir + nm);
            if (g_recN >= 80) { g_recOn = false; Notify("Video saved"); g_phDirty = true; }
        }
    } else if (Alive(g_camPC.go.get())) { PCamKill(g_camPC); g_recOn = false; }
    // maps app: a camera high above you looking down; the slider zooms
    if (g_phApp == PH_MAPS && Alive(head)) {
        if (!Alive(g_mapPC.go.get()) && !PCamBuild(g_mapPC, 256, 320, true, 20.0f, 60.0f, nullptr)) { Notify("Maps isn't available here"); g_phApp = PH_HOME; g_phDirty = true; }
        if (Il2CppObject *cg = g_mapPC.go.get()) {
            static Method<void> setSize = Class("UnityEngine", "Camera").GetMethod("set_orthographicSize", 1);
            if (setSize.IsValid() && g_mapPC.cam.get()) setSize[g_mapPC.cam.get()](5.0f + g_mapZoom * g_mapZoom * 75.0f);
            Il2CppObject *t = Tr(cg);
            G.trSetPosition[t](G.trGetPosition[head]() + Vector3(0, 80.0f, 0));
            G.trSetRotation[t](Quaternion::LookRotation(Vector3(0, -1, 0), Flat(G.trGetForward[head]())));
        }
    } else if (Alive(g_mapPC.go.get())) PCamKill(g_mapPC);
    // a video in the photo viewer: the frames one after another, 8 a second
    if (g_phApp == PH_VIEW && !g_vidFrames.empty() && g_time >= g_vidNext) {
        g_vidNext = g_time + 0.125;
        g_vidIdx = (g_vidIdx + 1) % g_vidFrames.size();
        if (Il2CppObject *t = TexFromFile(g_vidFrames[g_vidIdx], g_vidTex.get())) { if (!g_vidTex.get()) g_vidTex.set(t); }
        g_phDirty = true;
    }
    if (g_phDirty || g_phApp == PH_CAMERA || g_phApp == PH_MAPS) PhoneRender();
}
static void Extra11Reset() {
    g_phBuilt = false;
    g_phBody.clear();
    g_wallQuad.clear();
    g_phoneCanvas.clear();
    for (auto &w : g_pw) { w.cube.clear(); w.lbl.clear(); w.txt.clear(); w.quad.clear(); w.quad2.clear(); w.lastText.clear(); w.lastTex = nullptr; w.lastCol = Color(-1, -1, -1, -1); w.lastTcol = Color(-1, -1, -1, -1); w.lastSize = -1; }
    g_pwN = 0;
    g_camPC.go.clear(); g_camPC.cam.clear(); g_camPC.rt.clear();
    g_mapPC.go.clear(); g_mapPC.cam.clear(); g_mapPC.rt.clear();
    g_thumb.clear();
    for (auto &t : g_iconTex) t.clear();
    g_vidTex.clear();
    g_vidFrames.clear();
    g_recOn = false;
    g_phoneWas = false;
    g_phApp = PH_HOME;
    g_ballL.clear(); g_ballR.clear();
}

// ============================================================================================
// ---- Update 12: Wide Menu, Theme Presets ----------------------------------------------------
// ============================================================================================
static void MenuRebuild() {                              // the menu objects are destroyed and made again next frame
    for (Ref *r : {&g_menu, &g_pointer, &g_notifyGo, &g_notifyBar}) Kill(*r);
    for (Ref *r : {&g_board, &g_bg, &g_outline, &g_title, &g_titleBar, &g_prev, &g_next, &g_notifyText, &g_tipText}) r->clear();
    for (Ref *r : {&g_prevLbl, &g_nextLbl, &g_verGo, &g_menuCanvas[0], &g_menuCanvas[1]}) r->clear();
    for (auto &r : g_bgX) r.clear();
    for (auto &r : g_outX) r.clear();
    for (int r = 0; r < kMaxRows; r++) { g_rowCube[r].clear(); g_rowLabel[r].clear(); g_rowText[r].clear(); g_knob[r].clear(); }
    g_rowsBuilt = 0;
    for (int m = 0; m < 4; m++) { g_keyCube[m].clear(); g_keyLbl[m].clear(); }
    for (auto &r : g_srchParts) r.clear();
    Extra11Reset();
    g_kbMode = 0;
    g_lastTrans = false;
    g_built = false;
    g_menuShown = false;
    g_touching = -2;
}
static void WideChanged() {
    kBoardW = f_wideMenu ? 0.42f : 0.30f;
    kBtnW = f_wideMenu ? 0.39f : 0.27f;
    kSideX = f_wideMenu ? 0.26f : 0.2f;
    MenuRebuild();
    Notify(f_wideMenu ? "Wide menu" : "Normal width");
}

struct Preset { std::string name; int theme = 0, textCol = 0, pb = 0, mtype = 0, gunFx = 0, gunColor = 0, round = 0, trans = 3, wide = 0, outline = 0, btnOv = 0; Color btn = Color(0,0,0,0), btnOn = Color(0,0,0,0); };
static std::vector<Preset> g_customs;
static bool g_presetsLoaded = false;
static std::string PresetFile() { std::string p = PersistentPath(); return p.empty() ? std::string() : p + "/leos-presets.txt"; }
static void PresetsSave() {
    std::string f = PresetFile();
    if (f.empty()) return;
    try {
        std::ofstream o(f);
        for (auto &p : g_customs)
            o << p.name << "|" << p.theme << "|" << p.textCol << "|" << p.pb << "|" << p.mtype << "|" << p.gunFx << "|" << p.gunColor << "|" << p.round << "|" << p.trans << "|" << p.wide
              << "|" << p.outline << "|" << p.btnOv << "|" << p.btn.r << "|" << p.btn.g << "|" << p.btn.b << "|" << p.btnOn.r << "|" << p.btnOn.g << "|" << p.btnOn.b << "\n";
    } catch (...) {}
}
static void PresetsLoad() {
    std::string f = PresetFile();
    if (f.empty()) return;
    try {
        std::ifstream i(f);
        std::string l;
        while (std::getline(i, l)) {
            std::vector<std::string> v;
            size_t a = 0, b;
            while ((b = l.find('|', a)) != std::string::npos) { v.push_back(l.substr(a, b - a)); a = b + 1; }
            v.push_back(l.substr(a));
            if (v.size() < 10) continue;
            Preset p;
            p.name = v[0]; p.theme = atoi(v[1].c_str()); p.textCol = atoi(v[2].c_str()); p.pb = atoi(v[3].c_str()); p.mtype = atoi(v[4].c_str());
            p.gunFx = atoi(v[5].c_str()); p.gunColor = atoi(v[6].c_str()); p.round = atoi(v[7].c_str()); p.trans = atoi(v[8].c_str()); p.wide = atoi(v[9].c_str());
            if (v.size() >= 17) {
                p.outline = atoi(v[10].c_str()); p.btnOv = atoi(v[11].c_str());
                p.btn = Color((float)atof(v[12].c_str()), (float)atof(v[13].c_str()), (float)atof(v[14].c_str()), 1);
                p.btnOn = Color((float)atof(v[15].c_str()), (float)atof(v[16].c_str()), (float)atof(v[17].c_str()), 1);
            }
            g_customs.push_back(p);
        }
    } catch (...) {}
}
static Preset PresetNow(const std::string &name) {        // what the menu looks like right now
    Preset p;
    p.name = name;
    for (auto &ch : p.name) if (ch == '|') ch = ' ';
    p.theme = g_theme; p.textCol = g_textCol; p.pb = f_pbUnder ? 1 : f_pbBottom ? 2 : f_pbModTop ? 3 : 0;
    p.mtype = f_pizza ? 1 : f_longMenu ? 2 : f_pixelMenu ? 3 : f_phoneMenu ? 4 : f_textMenu ? 5 : 0;
    p.gunFx = g_gunFx; p.gunColor = g_gunColor; p.round = g_roundLvl; p.trans = g_transLvl; p.wide = f_wideMenu ? 1 : 0;
    p.outline = f_noOutline ? 1 : 0;
    p.btnOv = g_btnOverride ? 1 : 0; p.btn = g_btnCol; p.btnOn = g_btnOnCol;
    return p;
}
static void PresetApply(const Preset &p) {
    g_theme = p.theme; g_textCol = p.textCol; g_gunFx = p.gunFx; g_gunColor = p.gunColor; g_roundLvl = p.round; g_transLvl = p.trans;
    f_pbUnder = p.pb == 1; f_pbBottom = p.pb == 2; f_pbModTop = p.pb == 3; f_pbSides = p.pb == 0;
    g_pbPrev[0] = f_pbUnder; g_pbPrev[1] = f_pbSides; g_pbPrev[2] = f_pbBottom; g_pbPrev[3] = f_pbModTop;
    f_noOutline = p.outline != 0;
    g_btnOverride = p.btnOv != 0; g_btnCol = p.btn; g_btnOnCol = p.btnOn;
    f_pizza = p.mtype == 1; f_longMenu = p.mtype == 2; f_pixelMenu = p.mtype == 3; f_phoneMenu = p.mtype == 4; f_textMenu = p.mtype == 5;
    g_menuPrev[0] = f_pizza; g_menuPrev[1] = f_textMenu; g_menuPrev[2] = f_longMenu; g_menuPrev[3] = f_pixelMenu; g_menuPrev[4] = f_phoneMenu;
    g_page = 0;
    PizzaChanged();
    g_dirty = true;
    if ((p.wide != 0) != f_wideMenu) { f_wideMenu = p.wide != 0; WideChanged(); }
    Notify("Preset: %s", p.name.c_str());
}
static void PresetsRebuildItems() {
    g_view.clear();
    g_presetItems.clear();
    for (size_t k = 0; k < g_customs.size(); k++) g_presetItems.push_back(MenuItem{PRESETS, g_customs[k].name.c_str(), ACTION, MAIN, nullptr, nullptr, nullptr, 0, nullptr, (int)k});
    for (auto &it : g_presetItems) it.fn = []() { if (g_pressArg >= 0 && g_pressArg < (int)g_customs.size()) PresetApply(g_customs[(size_t)g_pressArg]); };
    g_dirty = true;
}
static void PresetsEnsure() { if (!g_presetsLoaded) { g_presetsLoaded = true; PresetsLoad(); PresetsRebuildItems(); } }
static void PresetSaveCurrent(const std::string &name) {
    PresetsEnsure();
    g_customs.push_back(PresetNow(name));
    PresetsSave();
    PresetsRebuildItems();
    Notify("Saved preset %s", name.c_str());
}
static void PresetLeo() { Preset p; p.name = "Leo's Menu"; p.theme = 0; p.gunColor = 0; PresetApply(p); }                        // blue, normal text, side buttons, normal menu, blue pointer
static void PresetStupid() { Preset p; p.name = "Stupid Menu"; p.theme = 1; p.gunColor = 1; PresetApply(p); }                  // orange, orange pointer
static void PresetModder() { Preset p; p.name = "Modder 3x Menu"; p.theme = 8; p.textCol = 3; p.gunColor = 0; p.wide = 1; p.outline = 1; p.pb = 3; PresetApply(p); }   // black, purple text, wide, no outline, mod-button switcher, blue pointer
static void PresetMalachi() {
    Preset p;
    p.name = "Malachi's Theme";
    p.theme = 9; p.pb = 3;                                   // dark blue, mod-button page switcher
    p.btnOv = 1; p.btn = Color(0.95f, 0.85f, 0.15f, 1); p.btnOn = Color(0.6f, 0.2f, 0.9f, 1);   // yellow buttons, purple when on
    PresetApply(p);
}
static void PresetSeralyth() {
    Preset p;
    p.name = "Seralyth's Theme";
    p.theme = 5; p.textCol = 0; p.pb = 0;                    // purple theme, white text, default (side) page switcher
    PresetApply(p);
}
static void PresetAdd() {
    PresetsEnsure();
    if (PhoneActive()) { PresetSaveCurrent("Custom " + std::to_string(g_customs.size() + 1)); return; }     // no keyboard on the phone: it gets a number
    g_cat = PRESETNAME; g_presetName.clear(); g_kbMode = 3; g_floatPlaced = true; g_page = 0; g_dirty = true; g_nextTitle = 0;
}
static void PresetDelete() {
    PresetsEnsure();
    if (g_customs.empty()) { Notify("No custom presets"); return; }
    g_customs.pop_back();
    PresetsSave();
    PresetsRebuildItems();
    Notify("Deleted the last custom preset");
}

// ============================================================================================
// ---- Update 13: Stay Locked, Space Stars / Galaxy Sky, Fling Gun, egg fix, crash guards -----
// ============================================================================================

// ---- Stay Locked: once a gun that affects other players picks a target, it keeps that target
// (instead of re-picking the closest every frame) until you let go of the grip. ----
static int LockTargetPersist(Vector3 from, bool gripHeld) {
    if (!f_stayLocked) return LockTarget(from);
    if (!gripHeld) { g_stayTarget = -1; g_stayGripPrev = false; return LockTarget(from); }
    if (!g_stayGripPrev) { g_stayTarget = LockTarget(from); g_stayGripPrev = true; }
    if (g_stayTarget < 0 || !Alive(g_rig[g_stayTarget].head.get())) g_stayTarget = LockTarget(from);
    return g_stayTarget;
}

// ---- Space Stars / Galaxy Sky: the game's skybox is swapped for a generated starfield or
// nebula picture (a flat equirectangular image on a Skybox/Panoramic material). ----
static Ref g_skyboxMat, g_skyboxOldMat, g_skyTex;
static bool g_skyboxSaved = false;
static int g_skyboxTexId = -1;                      // which picture is loaded: 0 stars, 1 galaxy
static Il2CppObject *SkyShader() {
    static Il2CppObject *sh = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char *names[3] = {"Skybox/Panoramic", "Skybox/Cubemap", "Skybox/6 Sided"};
        for (const char *n : names) {
            try {
                Il2CppObject *x = G.shaderFind.IsValid() ? G.shaderFind((BNM::Structures::Mono::String *)CreateMonoString(n)) : nullptr;
                if (x) { sh = x; ym::log("leos-menu: skybox shader %s", n); break; }
            } catch (...) {}
        }
    }
    return sh;
}
static bool SkyboxApply(int which) {
    static Class rsc("UnityEngine", "RenderSettings"), matC("UnityEngine", "Material");
    static Method<Il2CppObject *> getSkybox;
    static Method<void> setSkybox, setTex, setTexN;
    static bool looked = false;
    if (!looked) {
        looked = true;
        getSkybox = rsc.GetMethod("get_skybox", 0);
        setSkybox = rsc.GetMethod("set_skybox", 1);
        setTex = matC.GetMethod("set_mainTexture", 1);
        setTexN = matC.GetMethod("SetTexture", 2);
    }
    Il2CppObject *sh = SkyShader();
    if (!setSkybox.IsValid() || !getSkybox.IsValid() || !sh) return false;
    try {
        if (!g_skyboxSaved) { g_skyboxOldMat.set(getSkybox()); g_skyboxSaved = true; }
        if (!Alive(g_skyboxMat.get())) {
            Il2CppObject *m = matC.CreateNewObjectParameters(sh);
            if (!m) return false;
            g_skyboxMat.set(m);
        }
        if (g_skyboxTexId != which) {
            const unsigned char *d = which ? kSkyGalaxy : kSkyStars;
            size_t n = which ? sizeof kSkyGalaxy : sizeof kSkyStars;
            Il2CppObject *t = TexFromBytes(d, n, g_skyTex.get());
            if (t) { g_skyTex.set(t); g_skyboxTexId = which; }
        }
        Il2CppObject *m = g_skyboxMat.get();
        if (setTex.IsValid()) setTex[m](g_skyTex.get());
        if (setTexN.IsValid()) setTexN[m](MS("_Tex"), g_skyTex.get());
        setSkybox(m);
        return true;
    } catch (...) { return false; }
}
static void SkyboxRestore() {
    static Class rsc("UnityEngine", "RenderSettings");
    static Method<void> setSkybox;
    if (!setSkybox.IsValid()) setSkybox = rsc.GetMethod("set_skybox", 1);
    if (g_skyboxSaved && setSkybox.IsValid()) { try { setSkybox(g_skyboxOldMat.get()); } catch (...) {} }
    g_skyboxSaved = false;
    g_skyboxOldMat.clear();
    Kill(g_skyboxMat);
    g_skyTex.clear();
    g_skyboxTexId = -1;
}
static void SkyboxTick() {
    static bool had = false;
    bool want = f_spaceStars || f_galaxySky;
    if (!want) { if (had) { SkyboxRestore(); had = false; } return; }
    had = true;
    static double next = 0;
    if (g_time < next && g_skyboxTexId >= 0) return;
    next = g_time + 2.0;
    if (!SkyboxApply(f_galaxySky ? 1 : 0)) { f_spaceStars = f_galaxySky = false; SkyboxRestore(); had = false; Notify("Sky pictures aren't available here"); }
}

// ---- Fling Gun (Owner): points at a player and flings them with the game's own knockback
// message (RpcApplyPlayerKnockback / SendApplyPlayerKnockback), with a slider for how hard. ----
static const float kFlingSpeeds[] = {12.0f, 20.0f, 32.0f};
static const char *kFlingN[] = {"Light", "Medium", "Hard"};
static const char *FlingName(int i) { return kFlingN[i]; }
static double g_nextFling2 = 0;
static bool SendKnockbackRpc(Il2CppObject *target, Vector3 force) {
    static Method<void> plSend;
    static BNM::IL2CPP::MethodInfo *rpcKb = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        Class p("", "Player", Image("Assembly-CSharp.dll"));
        if (p.IsValid()) plSend = p.GetMethod("SendApplyKnockback", 2);
        Class net("", "NetworkMessenger", Image("Assembly-CSharp.dll"));
        if (net.IsValid())
            for (auto &m : net.GetMethods(false)) {
                auto *mi = m.GetInfo();
                if (mi && mi->name && !strcmp(mi->name, "RpcApplyPlayerKnockback")) rpcKb = mi;
            }
    }
    if (plSend.IsValid() && Alive(target)) {
        try { plSend[target](force, (int32_t)3); return true; } catch (...) {}
    }
    if (!rpcKb || !G.pvRpc.IsValid() || !G.arrayNew || !G.valueBox || !G.objectClass || !G.tPhotonView || !G.netLocal.IsValid()) return false;
    Il2CppObject *nm = G.netLocal();
    Il2CppObject *go = nm ? G.compGameObject[nm]() : nullptr;
    Il2CppObject *pv = go ? G.goGetComponent[go]((Il2CppObject *)G.tPhotonView) : nullptr;
    if (!pv) return false;
    static Method<Il2CppObject *> getUid;
    static bool lookedUid = false;
    if (!lookedUid) { lookedUid = true; Class p("", "Player", Image("Assembly-CSharp.dll")); if (p.IsValid()) getUid = p.GetMethod("get_userID", 0); }
    Il2CppObject *uid = getUid.IsValid() && Alive(target) ? getUid[target]() : nullptr;
    if (!uid) return false;
    int count = rpcKb->parameters_count;
    if (count > 0 && TypeName(rpcKb->parameters[count - 1]) == "Photon.Pun.PhotonMessageInfo") count--;
    auto *args = (Il2CppObject *)G.arrayNew(G.objectClass, count);
    if (!args) return false;
    int strings = 0, vectors = 0, ints = 0;
    for (int i = 0; i < count; i++) {
        std::string tn = TypeName(rpcKb->parameters[i]);
        Class pc(rpcKb->parameters[i]);
        auto *k = pc.IsValid() ? pc.GetClass() : nullptr;
        void *v = nullptr;
        if (tn == "System.String") { if (strings == 0) v = uid; strings++; }
        else if (tn == "UnityEngine.Vector3") { v = G.valueBox(k, &force); vectors++; }
        else if (k && k->byval_arg.valuetype) {
            uint8_t raw[16] = {};
            raw[0] = ints == 0 ? 3 : 0;
            auto *boxAs = k->enumtype && k->element_class ? k->element_class : k;
            v = G.valueBox(boxAs, raw);
            ints++;
        }
        Put(args, i, v);
    }
    try { G.pvRpc[pv](CreateMonoString("RpcApplyPlayerKnockback"), 0, args); return true; } catch (...) { return false; }
}
static void FlingGunAim(Vector3 start, Vector3 dir) {
    if (g_time < g_nextFling2) return;
    g_nextFling2 = g_time + 0.4;
    EspRefresh();
    int slot = f_targetLock ? LockTargetPersist(start, true) : -1;
    if (slot < 0)
        for (int i = 0; i < 16; i++) {
            Il2CppObject *head = g_rig[i].head.get();
            if (!Alive(head)) continue;
            Vector3 to = G.trGetPosition[head]() - Vector3(0, 0.5f, 0) - start;
            float t = Dot(to, dir);
            if (t < 0.2f || t > 60.0f) continue;
            Vector3 perp = to - dir * t;
            if (Len2(perp) < (0.6f + 0.02f * t) * (0.6f + 0.02f * t)) { slot = i; break; }
        }
    if (slot < 0) { Notify("No player there"); return; }
    Vector3 force = dir * kFlingSpeeds[g_flingPower];
    force.y += 4.0f;
    bool any = false;
    for (int i = 0; i < 6; i++) {
        Vector3 f = force;
        f.y += (float)i * 0.4f;
        if (SendKnockbackRpc(g_rig[slot].obj.get(), f)) any = true;
    }
    Notify(any ? "Flung!" : "No route to fling them (see log)");
}

static void Extra8Reset() {
    Extra10Reset();
    Extra11Reset();
    g_syncSaved.clear(); g_invPhase = 0; for (auto &o : g_orb) o.clear();
    g_ghostState = g_invState = false;
    CamScreenKill(g_mini);
    CamScreenKill(g_droneCam);
    for (auto &p : g_sizePar) p.clear();
    for (int i = 0; i < 16; i++) { g_tracerGo[i].clear(); g_tracerLine[i].clear(); g_boxGo[i].clear(); g_boxLine[i].clear(); g_solid[i].clear(); }
    g_skyCams.clear();
    for (auto &h : g_halo) { h.a.clear(); h.b.clear(); }
    for (auto &r : g_haloLight) r.clear();
    g_sticky.clear();
    g_bBehN = 0;
    g_micIn.clear(); g_micOut.clear(); g_micRunMode = -1; g_micRun = false;
    f_ghost = f_inv = false;                      // a new room starts with you visible
    f_miniMap = f_drone = false;
}

static void OnUpdate(float dt) {
    g_time += dt;
    g_frames++;
    static double fpsStart = 0;
    if (g_time - fpsStart >= 1.0) { g_fps = (float)(g_frames / (g_time - fpsStart)); g_frames = 0; fpsStart = g_time; }
    if (!Lookups()) return;
    auto *player = (Il2CppObject *)ym::api()->localPlayer();
    if (!player) return;
    if (player != g_player.get()) {                 // first frame in this room
        OnRoomChange();
        g_player.set(player);
        g_leftHand.set(G.leftHand[player]());
        g_rightHand.set(G.rightHand[player]());
        g_head.set(G.headTransform[player]());
        Il2CppObject *pgo = G.compGameObject[player]();
        if (pgo) g_rigidbody.set(G.goGetComponent[pgo](G.tRigidbody));
    }
    if (!g_leftHand.get() || !g_rightHand.get() || !g_head.get()) return;
    if (!g_built) BuildMenu();

    Buttons in = ReadButtons();
    VelStart();                     // what the game did to your velocity since last frame (Bounce, No Knockback)
    // Pizza Menu: always on screen, aimed at with your hand. Otherwise hold Y (B on the right hand).
    bool menuBtn = f_rightHand ? in.b : in.y;
    static bool menuBtnPrev = false;
    if (f_toggleMenu && menuBtn && !menuBtnPrev) g_menuToggled = !g_menuToggled;      // Toggle Menu: press to open, press to close
    menuBtnPrev = menuBtn;
    PhoneStateTick();
    ShowMenu(f_textMenu ? false : f_pizza ? true : (f_toggleMenu ? g_menuToggled : menuBtn));
    if (g_menuShown) {
        if (f_pizza) { PlacePizza(); PizzaPress(in); }
        else if (PhoneActive()) PhoneFrame();
        else {
            if (!(g_kbMode != 0 || (f_floatMenu && g_floatPlaced))) { PlaceMenu(); g_floatPlaced = f_floatMenu; }     // Floating Menu (and the keyboards): placed once
            CheckPress();
        }
        if (!PhoneActive()) { if (g_dirty) RenderPage(); UpdateTitle(); AnimateMenu(); }
    } else g_pizzaHover = false;
    if (f_textMenu) PizzaTextTick(in);
    else if (g_pzGo.get()) { Kill(g_pzGo); g_pzText.clear(); }
    if (g_notifyUntil > 0 && g_time >= g_notifyUntil) { Show(g_notifyGo, false); Show(g_notifyBar, false); g_notifyUntil = 0; }
    GravityTick(dt);
    SpeedBoostTick(dt);
    PlatformFor(in.lgrip, g_leftPlatform, g_leftHand);
    PlatformFor(in.rgrip, g_rightPlatform, g_rightHand);
    ApplySpeedBoost();
    ApplyGravity();
    NoclipTick(in);
    NoclipFlyTick(in, dt);
    VelFlyTick(in, dt);
    SmoothFlyTick(in, dt);
    JoyFlyTick(in, dt);
    ZeroFlyTick(in, dt);
    DroneCamTick(in, dt);
    MiniMapTick();
    ReverseVelTick(in);
    PunchTick();
    StrafeTick(in, dt);
    SizeTick(in, dt);
    PixelTick();
    IronManTick(in, dt);
    SpiderTick(in, dt);
    DashTick(in);
    RewindTick(in, dt);
    AirSwimTick(in, dt);
    TeleportGunTick(in);
    NoBoundsTick(dt);
    try {
        LegsTick(in, dt);
        BulletTick(in, dt);
        DrawTick(in);
        TrailTick();
        EspTick();
        SoundLoadTick();
        ShaderPassTick();
        AtmoTick();
        SnowTick(dt);
        RainTick(dt);
        LagTick();
    } catch (...) {
        static int errors = 0;
        if (errors++ < 3) ym::log("leos-menu: effects code error");
    }
    try { SpawnOnHandTick(in); FireballTick(in, dt); ItemQueueTick(); OrbitTick(dt); ItemRainTick(); LevTick(dt); }
    catch (...) { g_itemQueue.clear(); ym::log("leos-menu: item code error"); }
    auto safe = [](const char *name, auto fn) {
        try { fn(); } catch (...) { static int errors = 0; if (errors++ < 12) ym::log("leos-menu: %s code error", name); }
    };
    safe("item tweak", [&] { ItemTweakTick(); });
    safe("cotton", [&] { CottonTick(); });
    safe("projectile", [&] { ProjectileTick(); });
    safe("infinite cotton", [&] { InfCottonTick(); });
    safe("bloom", [&] { BloomTick(); HaloTick(); });
    safe("tracers", [&] { TracerBoxTick(); });
    safe("sky", [&] { SkyTick(dt); });
    safe("solid players", [&] { SolidTick(); });
    safe("leaderboard", [&] { LeaderboardTick(); });
    safe("boundary scripts", [&] { BoundBehTick(); });
    safe("mic", [&] { MicTick(); });
    safe("ghost", [&] { GhostTick(in, dt); });
    safe("info", [&] { PlayerInfoTick(); });
    safe("spectate", [&] { SpecTick(); });
    safe("trail esp", [&] { TrailEspTick(); });
    safe("ripple", [&] { RippleTick(dt); });
    safe("blood", [&] { BloodTick(dt); });
    safe("weapons", [&] { WeaponTick(in, dt); });
    safe("effects", [&] { FxTick(dt); });
    safe("sky", [&] { SkyboxTick(); });
    safe("potion", [&] { static bool once = false; if (!once && g_time > 8.0) { once = true; PotionResolve(); } });
    safe("dump", [&] { DumpTick(); });
    LongArmsTick(in, dt);
    DanceTick(in, dt);              // these two move your hands after everything else
    VelEnd();                       // the velocity the next frame is compared with
}

YM_EXPORT int YeepsMod_Init(const YeepsModApi *api) {
    if (!ym::init(api)) return -1;
    ym::onUpdate(OnUpdate);
    ym::log("Leo's Menu loaded on %s (%s)", api->gameVersion, api->buildId);
    return 0;
}

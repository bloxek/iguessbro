// ClassicCombatLog - ESPless xNVSE plugin (no .esp needed)
// Recreates the Fallout 1/2 message log:
//   • You see a Boxes.
//   • Ghoul was hit for 10 hit points. Ghoul was stabbed to death.
//   • Ghoul missed.
//
// Build: MSVC, 32-bit (Win32), Release, with the xNVSE SDK headers (nvse/*.h).
// Output: ClassicCombatLog.dll -> Data/NVSE/Plugins/
//
// NOTE: written against the xNVSE 6.x plugin API from memory and NOT compiled/tested.
// Lines marked [VERIFY] depend on exact header names/signatures in your SDK version.

#include "nvse/PluginAPI.h"
#include "nvse/GameAPI.h"
#include "nvse/GameObjects.h"
#include "nvse/GameForms.h"
#include "nvse/GameUI.h"
#include <deque>
#include <string>
#include <unordered_map>
#include <cstdio>

static IDebugLog gLog("ClassicCombatLog.log");
static NVSEInterface*             g_nvse   = nullptr;
static NVSEEventManagerInterface* g_events = nullptr;
static NVSEMessagingInterface*    g_msg    = nullptr;

// ---------------------------------------------------------------- log buffer
static std::deque<std::string> g_lines;      // newest at the back
static const size_t kMaxLines = 50;
static const char*  kBullet   = "\x95 ";     // classic bullet; swap for "- " if the font lacks it

static void Push(const std::string& text)
{
    g_lines.push_back(std::string(kBullet) + text);
    if (g_lines.size() > kMaxLines) g_lines.pop_front();
    // Display layer. Default: HUD ticker. For the real green box, render g_lines
    // from an overlay (see README) instead of calling this.
    QueueUIMessage(g_lines.back().c_str(), 0, nullptr, nullptr, 4.0f, false);   // [VERIFY signature]
}

// ---------------------------------------------------------------- helpers
static std::unordered_map<UInt32, float> g_lastHealth;  // refID -> health seen last time

static const char* NameOf(TESObjectREFR* r)
{
    if (!r) return "Something";
    if (r == (TESObjectREFR*)*g_thePlayer) return "You";
    const char* n = r->GetTheName();                    // [VERIFY]
    return (n && *n) ? n : "Something";
}

static float HealthOf(Actor* a) { return a->avOwner.GetActorValue(kAVCode_Health); } // [VERIFY]

// Death flavour text by weapon skill (extend freely)
static const char* DeathText(TESObjectWEAP* w)
{
    if (!w) return "was killed.";
    switch (w->weaponSkill) {                            // [VERIFY field name]
        case kAVCode_MeleeWeapons: return "was stabbed to death.";
        case kAVCode_Unarmed:      return "was beaten to death.";
        case kAVCode_Explosives:   return "was blown apart.";
        case kAVCode_EnergyWeapons:return "was burned to a crisp.";
        default:                   return "was shot to death.";
    }
}

// ---------------------------------------------------------------- events
// OnHit: thisObj = the actor that was hit. [VERIFY] parameter order for your SDK:
// params[0] = attacker, params[1] = weapon (for OnHitWith). Adjust if it differs.
static void OnHit(TESObjectREFR* thisObj, void* params)
{
    if (!thisObj || !thisObj->IsActor()) return;
    Actor* victim = (Actor*)thisObj;

    float now    = HealthOf(victim);
    float before = g_lastHealth.count(victim->refID) ? g_lastHealth[victim->refID] : now;
    g_lastHealth[victim->refID] = now;

    int dmg = (int)(before - now + 0.5f);
    if (dmg <= 0) return;                                // ignore 0-damage ticks

    char buf[128];
    snprintf(buf, sizeof buf, "%s %s hit for %d hit points.",
             NameOf(victim), victim == (Actor*)*g_thePlayer ? "were" : "was", dmg);
    Push(buf);

    if (now <= 0.0f) {
        TESObjectWEAP* w = nullptr;                      // fill from OnHitWith if you want per-weapon text
        snprintf(buf, sizeof buf, "%s %s", NameOf(victim), DeathText(w));
        Push(buf);
        g_lastHealth.erase(victim->refID);
    }
}

// "You see a Boxes." - fired when the crosshair target changes.
static UInt32 g_lastLooked = 0;
static void OnFrame()
{
    TESObjectREFR* ref = InterfaceManager::GetSingleton()->crosshairRef;   // [VERIFY]
    UInt32 id = ref ? ref->refID : 0;
    if (id == g_lastLooked) return;
    g_lastLooked = id;
    if (!ref) return;

    const char* n = ref->GetTheName();
    if (!n || !*n) return;
    char buf[160];
    snprintf(buf, sizeof buf, "You see a %s.", n);
    Push(buf);
    // Classic flavour line on the next row could be read from an INI keyed by base form ID.
}

// ---------------------------------------------------------------- public API
// For YOUR hit-chance system (the % to-hit part): call this when the roll fails.
extern "C" __declspec(dllexport) void CCL_LogMiss(TESObjectREFR* target)
{
    char buf[96];
    snprintf(buf, sizeof buf, "%s missed.", NameOf(target));   // "Ghoul missed."
    Push(buf);
}

// ---------------------------------------------------------------- plugin entry
static void MsgHandler(NVSEMessagingInterface::Message* m)
{
    if (m->type == NVSEMessagingInterface::kMessage_MainGameLoop) OnFrame();
    if (m->type == NVSEMessagingInterface::kMessage_PostLoadGame ||
        m->type == NVSEMessagingInterface::kMessage_NewGame) { g_lines.clear(); g_lastHealth.clear(); }
}

extern "C" {
__declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info)
{
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name        = "ClassicCombatLog";
    info->version     = 1;
    return !nvse->isEditor;
}

__declspec(dllexport) bool NVSEPlugin_Load(NVSEInterface* nvse)
{
    g_nvse   = nvse;
    g_msg    = (NVSEMessagingInterface*)nvse->QueryInterface(kInterface_Messaging);
    g_events = (NVSEEventManagerInterface*)nvse->QueryInterface(kInterface_EventManager);
    if (!g_msg || !g_events) return false;

    g_msg->RegisterListener(nvse->GetPluginHandle(), "NVSE", MsgHandler);
    g_events->SetNativeEventHandler("OnHit", (NVSEEventManagerInterface::NativeEventHandler)OnHit); // [VERIFY]
    return true;
}
}

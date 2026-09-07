#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#include <dlfcn.h>
#include <link.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(AML64)
    #error This 2.11.277 port supports arm64-v8a only.
#endif

#include "GTASA_STRUCTS_211277.h"
#include "AArch64_ModHelper/ARMv8_ASMHelper.h"
using namespace ARMv8;

#define MAX_NODE_POINTS         50000
#define MAX_RADAR_TRACES        250
#define MAX_PICKUPS             620

#define GPS_LINE_R              235
#define GPS_LINE_G              212
#define GPS_LINE_B              0
#define GPS_LINE_A              255

MYMODCFG(net.dk22pac.rusjj.gps, GTA:SA GPS, 1.5.1, DK22Pac & JuniorDjjr & juicermv & RusJJ)
NEEDGAME(com.rockstargames.gtasager)

BEGIN_DEPLIST()
ADD_DEPENDENCY_VER(net.rusjj.aml, 1.2.1)
END_DEPLIST()

namespace Game211277 {
constexpr uintptr_t kHudDrawGot = 0x82FE40;
constexpr uintptr_t kInitialise3DGot = 0x82FF98;
constexpr uintptr_t kLoadScenePathNodesCall = 0x39F4BC;
constexpr uintptr_t kExtraPathPos = 0x968F74;
constexpr uintptr_t kExtraPathsNeeded = 0x968F80;

constexpr uintptr_t kPathArrayFirstAdrp = 0x3E7E24;
constexpr uintptr_t kPathArrayFirstStore = 0x3E7E40;
constexpr uintptr_t kPathArrayFirstAdrp2 = 0x3E7E44;
constexpr uintptr_t kPathMaxMinusOne = 0x3E7E5C;
constexpr uintptr_t kPathArrayFirstAdd = 0x3E7E64;
constexpr uintptr_t kPathMaxMinusFifty = 0x3E80AC;
constexpr uintptr_t kPathArraySecondAdrp = 0x3E80C8;
constexpr uintptr_t kPathArraySecondAdd = 0x3E80D0;
constexpr uintptr_t kPathArrayThirdAdrp = 0x3E8264;
constexpr uintptr_t kPathArrayThirdAdd = 0x3E826C;
}

CVector2D g_vecUnderRadar(0.0, -1.05); // 0
CVector2D g_vecAboveRadar(0.0, 1.05); // 1
CVector2D g_vecLeftRadar(-1.05, 0.0); // 2
CVector2D g_vecRightRadar(1.05, 0.0); // 3
eFontAlignment g_nTextAlignment;

// Patched
CNodeAddress aNodesToBeCleared_NEW[MAX_NODE_POINTS];

// Savings
uintptr_t pGTASA;
void* hGTASA;
CNodeAddress resultNodes[MAX_NODE_POINTS];
CVector2D nodePoints[MAX_NODE_POINTS];
RwOpenGLVertex lineVerts[MAX_NODE_POINTS * 4] {0};
float gpsDistance;
CVector2D gpsDistanceTextPos;
unsigned int gpsLineColor = RWOPENGLCOLOR(GPS_LINE_R, GPS_LINE_G, GPS_LINE_B, GPS_LINE_A);
unsigned int maxLoadedPathNodes;
float lineWidth = 3.5f, textOffset, textScale, flMenuMapScaling;
CVector2D vecTextOffset;
tRadarTrace* pRedMarkerBlip;
float NearScreenZ_Cache = -1;
float RecipNearClip_Cache = -1;
bool gPortEnabled = false;
char gGameLibraryPath[512] {0};

// Config
ConfigEntry* pCfgClosestMaxGPSDistance;
ConfigEntry* pCfgGPSMaxPathNodesLoaded;
ConfigEntry* pCfgGPSLineColorRGB;
ConfigEntry* pCfgGPSDrawDistance;
ConfigEntry* pCfgGPSDrawDistancePosition;
ConfigEntry* pCfgGPSDrawDistanceTextScale;
ConfigEntry* pCfgGPSDrawDistanceTextOffset;
ConfigEntry* pCfgGPSTextAlignment;
bool bAllowBMX, bAllowBoat, bAllowMission, bImperialUnits, bRespectLanesDirection, bDisableFarFromRoadText, bUseMarkerColors;

// Game Vars
RsGlobalType* RsGlobal;
tRadarTrace* pRadarTrace;
MobileMenu* gMobileMenu;
CPathFind* ThePaths;
ScriptHandle TargetBlip {0};
float* NearScreenZ;
float* RecipNearClip;
bool *m_UserPause, *m_CodePause;
CWidget** aWidgets;
CPickup* aPickUps;
uint32_t *m_snTimeInMilliseconds, *m_snPreviousTimeInMilliseconds;
bool *ExtraPathsNeeded;
CVector *ExtraPathPos;
uint8_t *ToBeStreamed;

// Game Funcs
CPlayerPed* (*FindPlayerPed)(int);
float (*FindGroundZForCoord)(float, float);
int (*DoPathSearch)(CPathFind*, unsigned char, CVector, CNodeAddress, CVector, CNodeAddress*, short*, int, float*, float, CNodeAddress*, float, bool, CNodeAddress, bool, bool);
void (*TransformRealWorldPointToRadarSpace)(CVector2D& out, CVector2D const& in);
void (*TransformRadarPointToScreenSpace)(CVector2D& out, CVector2D const& in);
float (*LimitRadarPoint)(CVector2D& in);
void (*RwRenderStateSet)(RwRenderState, void*);
void (*RwIm2DRenderPrimitive)(RwPrimitiveType, RwOpenGLVertex*, int);
void (*ClearRadarBlip)(uint32_t);
bool (*IsOnAMission)();
CPed* (*GetPoolPed)(int);
CVehicle* (*GetPoolVeh)(int);
CObject* (*GetPoolObj)(int);
void (*RequestModel)(int, int);
void (*RemoveModel)(int);
void (*LoadAllRequestedModels)(bool);
void (*MarkRegionsForCoors)(CPathFind*, CVector, float);
void (*RequestCollision)(CVector*, int);

void (*FontSetOrientation)(unsigned char);
void (*FontSetColor)(CRGBA*);
void (*FontSetBackground)(unsigned char, unsigned char);
void (*FontSetWrapx)(float);
void (*FontSetStyle)(unsigned char);
void (*FontSetScale)(float);
void (*FontSetProportional)(unsigned char);
void (*FontSetDropShadowPosition)(char);
void (*FontSetEdge)(char);
void (*FontPrintString)(float, float, short*);
void (*AsciiToGxtChar)(const char* txt, short* ret);
void (*RenderFontBuffer)(void);



inline bool IsRadarVisible()
{
    CWidget* radar = aWidgets[161];
    return (radar != NULL && radar->enabled);
}
inline uint8_t GetRadarVisibility()
{
    CWidget* radar = aWidgets[161];
    return (radar) ? radar->color.a : 0;
}
inline bool IsGamePaused() { return *m_CodePause || *m_UserPause; };
inline bool IsRGBValue(int value) { return value >= 0 && value <= 255; }

int FindGameLibraryCallback(struct dl_phdr_info* info, size_t, void* userData)
{
    if(!info || !info->dlpi_name || !strstr(info->dlpi_name, "libGame.so")) return 0;

    auto* address = reinterpret_cast<uintptr_t*>(userData);
    *address = static_cast<uintptr_t>(info->dlpi_addr);
    strncpy(gGameLibraryPath, info->dlpi_name, sizeof(gGameLibraryPath) - 1);
    gGameLibraryPath[sizeof(gGameLibraryPath) - 1] = '\0';
    return 1;
}

uintptr_t FindGameLibraryBase()
{
    uintptr_t address = 0;
    dl_iterate_phdr(FindGameLibraryCallback, &address);
    return address;
}

uintptr_t ResolveGameSymbol(const char* symbol)
{
    uintptr_t address = 0;
    if(hGTASA) address = aml->GetSym(hGTASA, symbol);
    if(!address && pGTASA) address = aml->GetSym(pGTASA, symbol);
    if(!address && hGTASA) address = reinterpret_cast<uintptr_t>(dlsym(hGTASA, symbol));
    return address;
}

template <typename T>
bool ResolveRequired(T& destination, const char* symbol)
{
    uintptr_t address = ResolveGameSymbol(symbol);
    if(!address)
    {
        logger->Error("Required 2.11.277 symbol is missing: %s", symbol);
        return false;
    }
    destination = reinterpret_cast<T>(address);
    return true;
}

bool VerifyInstruction(uintptr_t offset, uint32_t expected)
{
    uint32_t actual = *reinterpret_cast<const uint32_t*>(pGTASA + offset);
    if(actual != expected)
    {
        logger->Error("Unsupported libGame.so at +0x%lX: expected %08X, found %08X", offset, expected, actual);
        return false;
    }
    return true;
}

bool EncodeAdrp(uintptr_t instructionAddress, uintptr_t targetAddress, unsigned reg, uint32_t& encoded)
{
    int64_t pageDelta = static_cast<int64_t>(targetAddress >> 12) - static_cast<int64_t>(instructionAddress >> 12);
    if(pageDelta < -(1LL << 20) || pageDelta >= (1LL << 20)) return false;

    uint64_t immediate = static_cast<uint64_t>(pageDelta) & 0x1FFFFF;
    encoded = 0x90000000u |
              static_cast<uint32_t>((immediate & 3u) << 29) |
              static_cast<uint32_t>(((immediate >> 2) & 0x7FFFFu) << 5) |
              (reg & 31u);
    return true;
}

uint32_t EncodeAddImmediate(unsigned destinationReg, unsigned sourceReg, unsigned immediate)
{
    return 0x91000000u | ((immediate & 0xFFFu) << 10) | ((sourceReg & 31u) << 5) | (destinationReg & 31u);
}

uint32_t EncodeStoreWordImmediate(unsigned valueReg, unsigned baseReg, unsigned byteOffset)
{
    return 0xB9000000u | (((byteOffset >> 2) & 0xFFFu) << 10) | ((baseReg & 31u) << 5) | (valueReg & 31u);
}

CPathNode* TryGetPathNode(CNodeAddress address)
{
    // Node addresses returned by CPathFind::DoPathSearch are engine-owned and
    // already range checked by the game.  The per-area node-count table is not
    // ABI-stable across the 64-bit mobile releases, so reading it here can
    // reject every valid route (as 2.11.277 does).  Validate the stable area
    // and loaded-area pointer before dereferencing the returned node instead.
    if(address.m_nAreaId >= 72 || !ThePaths->pNodes[address.m_nAreaId]) return nullptr;
    return &ThePaths->pNodes[address.m_nAreaId][address.m_nNodeId];
}

#ifndef MAP_FIXED_NOREPLACE
    #define MAP_FIXED_NOREPLACE 0x100000
#endif

void* AllocateBranchIsland(uintptr_t callAddress)
{
    const size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const uintptr_t granularity = 0x10000;
    const uintptr_t center = callAddress & ~(granularity - 1);
    const uintptr_t minimum = callAddress > 0x07000000 ? callAddress - 0x07000000 : granularity;
    const uintptr_t maximum = callAddress + 0x07000000;

    for(uintptr_t distance = 0x01000000; distance < 0x07000000; distance += granularity)
    {
        const uintptr_t candidates[2] = {center + distance, center - distance};
        for(uintptr_t candidate : candidates)
        {
            if(candidate < minimum || candidate > maximum) continue;
            void* memory = mmap(reinterpret_cast<void*>(candidate), pageSize,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                                -1, 0);
            if(memory != MAP_FAILED) return memory;
        }
    }
    return nullptr;
}

bool InstallBranchLinkHook(uintptr_t callAddress, uintptr_t hookAddress, void** originalAddress)
{
    const uint32_t instruction = *reinterpret_cast<const uint32_t*>(callAddress);
    if((instruction & 0xFC000000u) != 0x94000000u) return false;

    int64_t originalImmediate = static_cast<int64_t>(instruction & 0x03FFFFFFu);
    if(originalImmediate & 0x02000000) originalImmediate |= ~0x03FFFFFFLL;
    *originalAddress = reinterpret_cast<void*>(callAddress + (originalImmediate << 2));

    auto* island = reinterpret_cast<uint32_t*>(AllocateBranchIsland(callAddress));
    if(!island) return false;

    // ldr x16, #8; br x16; .quad hookAddress
    island[0] = 0x58000050u;
    island[1] = 0xD61F0200u;
    *reinterpret_cast<uintptr_t*>(&island[2]) = hookAddress;
    __builtin___clear_cache(reinterpret_cast<char*>(island), reinterpret_cast<char*>(island) + 16);

    const size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    if(mprotect(island, pageSize, PROT_READ | PROT_EXEC) != 0)
    {
        munmap(island, pageSize);
        return false;
    }

    const int64_t branchDistance = reinterpret_cast<uintptr_t>(island) - callAddress;
    if((branchDistance & 3) || branchDistance < -(1LL << 27) || branchDistance >= (1LL << 27))
    {
        munmap(island, pageSize);
        return false;
    }

    const uint32_t branchInstruction = 0x94000000u |
        (static_cast<uint32_t>(branchDistance >> 2) & 0x03FFFFFFu);
    aml->Write32(callAddress, branchInstruction);
    return true;
}

void SetDistanceTextValues();

void InitializeConfigValues()
{
    textOffset = (8.0f * (float)RsGlobal->maximumHeight) / 448.0f;
    textScale = (0.4f * ((float)RsGlobal->maximumWidth) / 640.0f) * pCfgGPSDrawDistanceTextScale->GetFloat();
    flMenuMapScaling = (float)RsGlobal->maximumHeight / 448.0f;

    if(sscanf(pCfgGPSDrawDistanceTextOffset->GetString(), "%f %f", &vecTextOffset.x, &vecTextOffset.y) != 2)
    {
        vecTextOffset.x = vecTextOffset.y = 0;
    }

    // The minimap path is drawn before the distance text on a frame. Prepare
    // its clip rectangle here so the first active route is visible as well.
    SetDistanceTextValues();
}

RwUInt32 GetTraceColor(eBlipColour clr, bool friendly = false)
{
    switch(clr)
    {
        case BLIP_COLOUR_RED:
            return RWOPENGLCOLOR(255,   0,   0, 255);
        case BLIP_COLOUR_GREEN:
            return RWOPENGLCOLOR(  0, 255,   0, 255);
        case BLIP_COLOUR_BLUE:
            return RWOPENGLCOLOR(  0, 128, 255, 255);
        case BLIP_COLOUR_WHITE:
            return RWOPENGLCOLOR(255, 255, 255, 255);
        case BLIP_COLOUR_YELLOW:
            return RWOPENGLCOLOR(255, 220,   0, 255);
        case BLIP_COLOUR_PURPLE:
            return RWOPENGLCOLOR(190,   0, 255, 255);
        case BLIP_COLOUR_CYAN:
            return RWOPENGLCOLOR(  0, 220, 255, 255);
        case BLIP_COLOUR_THREAT:
            return friendly ? RWOPENGLCOLOR(0, 128, 255, 255) : RWOPENGLCOLOR(255, 0, 0, 255);
        case BLIP_COLOUR_DESTINATION:
            return RWOPENGLCOLOR(255, 220,   0, 255);
            
        default:
            CRGBA a((int)clr);
            return RWOPENGLCOLOR(a.r, a.g, a.b, 255);
    }
}

CRGBA rgbclr;
CRGBA& GetTraceTextColor(eBlipColour clr, bool friendly = false)
{
    switch(clr)
    {
        case BLIP_COLOUR_RED:
            return rgbclr = CRGBA(127,0,0,255);
        case BLIP_COLOUR_GREEN:
            return rgbclr = CRGBA(0,127,0,255);
        case BLIP_COLOUR_BLUE:
            return rgbclr = CRGBA(0,0,127,255);
        case BLIP_COLOUR_WHITE:
            return rgbclr = CRGBA(127,127,127,255);
        case BLIP_COLOUR_YELLOW:
            return rgbclr = CRGBA(200,200,0,255);
        case BLIP_COLOUR_PURPLE:
            return rgbclr = CRGBA(127,0,127,255);
        case BLIP_COLOUR_CYAN:
            return rgbclr = CRGBA(0,127,127,255);
        case BLIP_COLOUR_THREAT:
            return friendly ? rgbclr = CRGBA(0,0,127,255) : rgbclr = CRGBA(127,0,0,255);
        case BLIP_COLOUR_DESTINATION:
            return rgbclr = CRGBA(200,200,0,255);
            
        default:
            rgbclr = CRGBA((int)clr);
            rgbclr.a = 255;
            return rgbclr;
    }
}

void SetDistanceTextValues()
{
    switch(pCfgGPSDrawDistancePosition->GetInt())
    {
        default:
        case 0: // Under
            g_nTextAlignment = ALIGN_CENTER;
            TransformRadarPointToScreenSpace(gpsDistanceTextPos, g_vecUnderRadar);
            gpsDistanceTextPos += vecTextOffset;
            gpsDistanceTextPos.y += textOffset;
            break;

        case 1: // Above
            g_nTextAlignment = ALIGN_CENTER;
            TransformRadarPointToScreenSpace(gpsDistanceTextPos, g_vecAboveRadar);
            gpsDistanceTextPos += vecTextOffset;
            gpsDistanceTextPos.y -= textOffset;
            break;

        case 2: // Left
            g_nTextAlignment = ALIGN_RIGHT;
            TransformRadarPointToScreenSpace(gpsDistanceTextPos, g_vecLeftRadar);
            gpsDistanceTextPos += vecTextOffset;
            gpsDistanceTextPos.x -= textOffset;
            break;

        case 3: // Right
            g_nTextAlignment = ALIGN_LEFT;
            TransformRadarPointToScreenSpace(gpsDistanceTextPos, g_vecRightRadar);
            gpsDistanceTextPos += vecTextOffset;
            gpsDistanceTextPos.x += textOffset;
            break;
            
        case 4: // Custom
            gpsDistanceTextPos = vecTextOffset;
            break;
    }

    int newAlign = pCfgGPSTextAlignment->GetInt();
    if(newAlign >= 0 && newAlign < 3)
    {
        g_nTextAlignment = (eFontAlignment)newAlign;
    }
}

inline void PreSetup2DVertexNearClip(RwOpenGLVertex &vertex, float nearClip)
{
    vertex.pos.z = nearClip;
}

inline void PreSetup2DVertexRecipClip(RwOpenGLVertex &vertex, float recipClip)
{
    vertex.rhw = recipClip;
}

inline void Setup2DVertex(RwOpenGLVertex &vertex, float x, float y, RwUInt32 color)
{
    vertex.pos.x = x;
    vertex.pos.y = y;
    vertex.color = color;
}

inline bool IsBMXNaviAllowed(CPlayerPed* player)
{
    CVehicle* vehicle = GetPedVehicle(player);
    return bAllowBMX ||
           (!bAllowBMX && GetVehicleSubType(vehicle) != VEHICLE_TYPE_BMX);
}

bool IsUsableRoutePosition(const CVector& position)
{
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           std::fabs(position.x) < 100000.0f && std::fabs(position.y) < 100000.0f &&
           std::fabs(position.z) < 100000.0f;
}

bool TryResolveTracePosition(const tRadarTrace& trace, CVector& position)
{
    CEntity* entity = nullptr;
    switch(trace.m_nBlipType)
    {
        case BLIP_CAR:
            if(trace.m_nEntityHandle < 0) return false;
            entity = GetPoolVeh(trace.m_nEntityHandle);
            break;

        case BLIP_CHAR:
            if(trace.m_nEntityHandle < 0) return false;
            entity = GetPoolPed(trace.m_nEntityHandle);
            break;

        case BLIP_OBJECT:
            if(trace.m_nEntityHandle < 0) return false;
            entity = GetPoolObj(trace.m_nEntityHandle);
            break;

        case BLIP_PICKUP:
            if(trace.m_ScriptHandle.m_nId >= MAX_PICKUPS) return false;
            position = UncompressLargeVector(aPickUps[trace.m_ScriptHandle.m_nId].m_vecPos);
            return IsUsableRoutePosition(position);

        case BLIP_COORD:
        case BLIP_CONTACT_POINT:
        case BLIP_SPOTLIGHT:
        case BLIP_AIRSTRIP:
            position = trace.m_vecWorldPosition;
            return IsUsableRoutePosition(position);

        case BLIP_NONE:
        default:
            return false;
    }

    if(!entity) return false;
    position = GetEntityPosition(entity);
    return IsUsableRoutePosition(position);
}

RwUInt32 GetRouteColor(const tRadarTrace& trace, bool playerTarget = false)
{
    if(!bUseMarkerColors) return gpsLineColor;
    // The pause-map waypoint is always the game's red custom marker even on
    // builds that internally label coordinate blips as DESTINATION.
    if(playerTarget) return GetTraceColor(BLIP_COLOUR_RED, false);
    return GetTraceColor(trace.m_nColour, trace.m_bFriendly);
}

inline bool IsInSupportedVehicle(CPlayerPed* player)
{
    if(!player) return false;
    CVehicle* vehicle = GetPedVehicle(player);
    return (vehicle &&
            IsPedInVehicle(player) &&
            GetVehicleSubType(vehicle) != VEHICLE_TYPE_PLANE &&
            GetVehicleSubType(vehicle) != VEHICLE_TYPE_HELI &&
            IsBMXNaviAllowed(player));
}

inline bool LaneDirectionRespected()
{
    return bRespectLanesDirection;
}

inline bool IsBoatNaviAllowed()
{
    return bAllowBoat;
}

char text[24];
short* textGxt = new short[0xFF];
DECL_HOOK(void, PreRenderEnd)
{
    PreRenderEnd();

    if(!gPortEnabled)
    {
        gpsDistance = 0.0f;
        return;
    }

    if(gpsDistance > 0.0f && !IsGamePaused() && IsRadarVisible())
    {
        static bool bInit = false;
        if(!bInit)
        {
            bInit = true;
            SetDistanceTextValues();
        }

        if(pCfgGPSDrawDistance->GetBool())
        {
            if(gpsDistance == 100000.0f)
            {
                if(bDisableFarFromRoadText) return;
                sprintf(text, "Far from the road!");
            }
            else
            {
                if(!bImperialUnits)
                {
                    if (gpsDistance >= 1000.0f) sprintf(text, "%.2fkm", 0.001f * gpsDistance);
                    else sprintf(text, "%dm", (int)gpsDistance);
                }
                else
                {
                    if (gpsDistance > 1609.344f) sprintf(text, "%.2fmil", (float)(0.000621371192237334 * (double)gpsDistance));
                    else sprintf(text, "%dyrd", (int)(gpsDistance * 1.094f));
                }
            }
            AsciiToGxtChar(text, textGxt);

            static CRGBA drawingColor;
            drawingColor = (!TargetBlip.m_nHandleIndex && pRedMarkerBlip) ? GetTraceTextColor(pRedMarkerBlip->m_nColour, pRedMarkerBlip->m_bFriendly) : rgbaWhite;
            if(!IsGamePaused()) drawingColor.a = GetRadarVisibility();

            FontSetOrientation(g_nTextAlignment);
            FontSetColor(&drawingColor);
            FontSetBackground(false, false);
            FontSetWrapx(500.0f);
            FontSetScale(textScale);
            FontSetStyle(FONT_SUBTITLES);
            FontSetProportional(true);
            FontSetDropShadowPosition(1);
            FontPrintString(gpsDistanceTextPos.x, gpsDistanceTextPos.y, textGxt);
            RenderFontBuffer();
        }
    }
    gpsDistance = 0;
}

DECL_HOOK(int, InitRenderWare, void* app)
{
    int result = InitRenderWare(app);
    if(result && gPortEnabled) InitializeConfigValues();
    return result;
}

short nodesCount = 0;
float trashVar;
void DoPathDraw(CVector to, RwUInt32 color, bool isTargetBlip = false, float* dist = NULL)
{
    CPlayerPed* player = FindPlayerPed(-1);
    if(!IsInSupportedVehicle(player)) return;
    
    const bool isGamePaused = IsGamePaused();
    const bool isMenuMap = isGamePaused && gMobileMenu->m_bDrawMenuMap;
    if(isGamePaused && !isMenuMap) return;
    
    CVehicle* vehicle = GetPedVehicle(player);
    CVector& playerPos = GetEntityPosition(player);
    DoPathSearch(ThePaths, LaneDirectionRespected() && GetVehicleSubType(vehicle) != VEHICLE_TYPE_BOAT, playerPos,
                 CNodeAddress(), to, resultNodes, &nodesCount, maxLoadedPathNodes, dist ? dist : &trashVar, 1000000.0f, NULL, 1000000.0f, false,
                 CNodeAddress(), false, GetVehicleSubType(vehicle) == VEHICLE_TYPE_BOAT && IsBoatNaviAllowed());

    if(nodesCount > 0)
    {
        if(dist)
        {
            CPathNode* node = TryGetPathNode(resultNodes[0]);
            if(!node) return;
            *dist += (node->GetPosition() - playerPos).Magnitude();
        }

        if(isTargetBlip && !isMenuMap &&
           gpsDistance < pCfgClosestMaxGPSDistance->GetFloat())
        {
            ClearRadarBlip(TargetBlip.m_nHandleIndex);
            gMobileMenu->m_TargetBlipHandle.m_nHandleIndex = 0;
            TargetBlip.m_nHandleIndex = 0;
            return;
        }

        int drawPointCount = 0;
        CPathNode* node;
        CVector2D nodePos;
        if (isGamePaused)
        {
            for (int i = 0; i < nodesCount; ++i)
            {
                node = TryGetPathNode(resultNodes[i]);
                if(!node) break;
                nodePos = node->GetPosition2D();
                TransformRealWorldPointToRadarSpace(nodePos, nodePos);
                LimitRadarPoint(nodePos);
                TransformRadarPointToScreenSpace(nodePoints[drawPointCount], nodePos);
                nodePoints[drawPointCount] *= flMenuMapScaling;
                ++drawPointCount;
            }
        }
        else
        {
            for (int i = 0; i < nodesCount; ++i)
            {
                node = TryGetPathNode(resultNodes[i]);
                if(!node) break;
                nodePos = node->GetPosition2D();
                TransformRealWorldPointToRadarSpace(nodePos, nodePos);
                const float radarDistance = LimitRadarPoint(nodePos);
                TransformRadarPointToScreenSpace(nodePoints[drawPointCount], nodePos);
                ++drawPointCount;
                // Keep the first boundary point and stop. This geometrically
                // clips the route to the circular minimap on mobile builds
                // where the legacy CWidget scissor call has no effect.
                if(radarDistance > 1.0f) break;
            }
        }

        const int segmentCount = drawPointCount - 1;
        if(segmentCount > 0 && (IsRadarVisible() || isGamePaused))
        {
            if(*NearScreenZ != NearScreenZ_Cache)
            {
                NearScreenZ_Cache = *NearScreenZ;
                float nearScreen = NearScreenZ_Cache + 0.0001f;
                for(int i = 0; i < MAX_NODE_POINTS * 4; ++i)
                {
                    PreSetup2DVertexNearClip(lineVerts[i], nearScreen);
                }
            }
            if(*RecipNearClip != RecipNearClip_Cache)
            {
                RecipNearClip_Cache = *RecipNearClip;
                float recipClip = RecipNearClip_Cache;
                for(int i = 0; i < MAX_NODE_POINTS * 4; ++i)
                {
                    PreSetup2DVertexRecipClip(lineVerts[i], recipClip);
                }
            }

            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);

            unsigned int vertIndex = 0;

            CVector2D point[4], shift[2], dir;
            float angle;
            if (isGamePaused)
            {
                float mp = gMobileMenu->m_fMapZoom - 140.0f;
                mp = clampint(140.0f, 960.0f, mp) / 960.0f + 0.4f;
                mp *= lineWidth;

                for (int i = 0; i < segmentCount; i++)
                {
                    dir = CVector2D::Diff(nodePoints[i + 1], nodePoints[i]);
                    angle = atan2(dir.y, dir.x);

                    sincosf(angle - 1.5707963f, &shift[0].y, &shift[0].x); shift[0] *= mp;
                    sincosf(angle + 1.5707963f, &shift[1].y, &shift[1].x); shift[1] *= mp;

                    Setup2DVertex(lineVerts[vertIndex], nodePoints[i].x + shift[0].x, nodePoints[i].y + shift[0].y, color);
                    Setup2DVertex(lineVerts[++vertIndex], nodePoints[i + 1].x + shift[0].x, nodePoints[i + 1].y + shift[0].y, color);
                    Setup2DVertex(lineVerts[++vertIndex], nodePoints[i].x + shift[1].x, nodePoints[i].y + shift[1].y, color);
                    Setup2DVertex(lineVerts[++vertIndex], nodePoints[i + 1].x + shift[1].x, nodePoints[i + 1].y + shift[1].y, color);
                    ++vertIndex;
                }
            }
            else
            {
                for (int i = 0; i < segmentCount; i++)
                {
                    dir = CVector2D::Diff(nodePoints[i + 1], nodePoints[i]);
                    angle = atan2(dir.y, dir.x);

                    sincosf(angle - 1.5707963f, &shift[0].y, &shift[0].x); shift[0] *= lineWidth;
                    sincosf(angle + 1.5707963f, &shift[1].y, &shift[1].x); shift[1] *= lineWidth;

                    Setup2DVertex(lineVerts[vertIndex], nodePoints[i].x + shift[0].x, nodePoints[i].y + shift[0].y, color);
                    Setup2DVertex(lineVerts[++vertIndex], nodePoints[i + 1].x + shift[0].x, nodePoints[i + 1].y + shift[0].y, color);
                    Setup2DVertex(lineVerts[++vertIndex], nodePoints[i].x + shift[1].x, nodePoints[i].y + shift[1].y, color);
                    Setup2DVertex(lineVerts[++vertIndex], nodePoints[i + 1].x + shift[1].x, nodePoints[i + 1].y + shift[1].y, color);
                    ++vertIndex;
                }
            }
            RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, lineVerts, 4 * segmentCount);
        }
    }
}

DECL_HOOKv(PostRadarDraw, bool b)
{
    PostRadarDraw(b);

    if(!gPortEnabled) return;

    if(gMobileMenu->m_TargetBlipHandle.m_nHandleIndex)
    {
        bool isGamePaused = IsGamePaused();
        if(TargetBlip.m_nHandleIndex != gMobileMenu->m_TargetBlipHandle.m_nHandleIndex && !isGamePaused && IsRadarVisible())
        {
            TargetBlip = gMobileMenu->m_TargetBlipHandle;
        }

        const std::uint16_t targetId = gMobileMenu->m_TargetBlipHandle.m_nId;
        CVector bpos;
        if(targetId < MAX_RADAR_TRACES && TryResolveTracePosition(pRadarTrace[targetId], bpos))
        {
            if(bpos.z == 0)
            {
                RequestCollision(&bpos, -1);
                LoadAllRequestedModels(false);
                bpos.z = FindGroundZForCoord(bpos.x, bpos.y) + 5.0f;
            }
            DoPathDraw(bpos, GetRouteColor(pRadarTrace[targetId], true), true, &gpsDistance);
        }
    }
    else
    {
        TargetBlip.m_nHandleIndex = 0;
    }
        
    pRedMarkerBlip = NULL;
    if(bAllowMission && IsOnAMission())
    {
        CPlayerPed* player = FindPlayerPed(-1);
        if(!player) return;

        std::uint16_t count = 0, maxi = 0;
        float distances[MAX_RADAR_TRACES], maxdist;
        CVector positions[MAX_RADAR_TRACES], selectedPosition;
        tRadarTrace* traces[MAX_RADAR_TRACES];
        bool hasSelectedPosition = false;
        const CVector playerPosition = GetEntityPosition(player);

        for(std::uint16_t i = 0; i < MAX_RADAR_TRACES; ++i)
        {
            tRadarTrace& trace = pRadarTrace[i];
            if(trace.m_nBlipSprite == RADAR_SPRITE_NONE &&
               trace.m_nBlipDisplayFlag > 1)
            {
                CVector position;
                if(!TryResolveTracePosition(trace, position)) continue;

                if(trace.m_nColour == BLIP_COLOUR_DESTINATION)
                {
                    pRedMarkerBlip = &trace;
                    selectedPosition = position;
                    hasSelectedPosition = true;
                    break;
                }
                traces[count] = &trace;
                positions[count] = position;
                distances[count] = DistanceBetweenPoints(playerPosition, position);
                ++count;
            }
        }

        if(count > 0)
        {
            maxdist = distances[0];
            for(unsigned char i = 1; i < count; ++i)
            {
                if(distances[i] > maxdist)
                {
                    maxi = i;
                    maxdist = distances[i];
                }
            }
            pRedMarkerBlip = traces[maxi];
            selectedPosition = positions[maxi];
            hasSelectedPosition = true;
        }

        if(pRedMarkerBlip && hasSelectedPosition)
        {
            DoPathDraw(selectedPosition, GetRouteColor(*pRedMarkerBlip), false,
                       !TargetBlip.m_nHandleIndex ? &gpsDistance : NULL);
        }
    }
}

DECL_HOOKv(LoadSceneForPathNodes, CPathFind* self, CVector center)
{
    if(!gPortEnabled)
    {
        LoadSceneForPathNodes(self, center);
        return;
    }

    memset(ToBeStreamed, 0, 64);
    CPlayerPed* player = FindPlayerPed(-1);

    if(player)
    {
        MarkRegionsForCoors(self, center, 4300.0f);
        MarkRegionsForCoors(self, GetEntityPosition(player), 4300.0f);
    }
    else
    {
        MarkRegionsForCoors(self, center, 4300.0f);
    }
    for(int i = 0; i < 64; ++i)
    {
        if(ToBeStreamed[i] != 0) RequestModel(25511 + i, STREAMING_NONE);
    }
}

static CVector mapcorner1 = { -3000.0f, -3000.0f, 20.0f };
static CVector mapcorner2 = { -3000.0f,  3000.0f, 20.0f };
static CVector mapcorner3 = {  3000.0f, -3000.0f, 20.0f };
static CVector mapcorner4 = {  3000.0f,  3000.0f, 20.0f };
DECL_HOOKv(UpdateStreaming, CPathFind *self, bool forceLoad)
{
    if(!gPortEnabled)
    {
        UpdateStreaming(self, forceLoad);
        return;
    }

    if(!forceLoad && !*ExtraPathsNeeded && (*m_snTimeInMilliseconds ^ *m_snPreviousTimeInMilliseconds) < 512) return;
    memset(ToBeStreamed, 0, 64);

    MarkRegionsForCoors(self, mapcorner1, 4300.0f);
    MarkRegionsForCoors(self, mapcorner2, 4300.0f);
    MarkRegionsForCoors(self, mapcorner3, 4300.0f);
    MarkRegionsForCoors(self, mapcorner4, 4300.0f);

    CPlayerPed* player = FindPlayerPed(-1);
    if(player) MarkRegionsForCoors(self, GetEntityPosition(player), 4300.0f);
    if(*ExtraPathsNeeded)
    {
        MarkRegionsForCoors(self, *ExtraPathPos, 4300.0f);
        *ExtraPathsNeeded = false;
    }
    for(int i = 0; i < 64; ++i)
    {
        if(ToBeStreamed[i] != 0)
        {
            if(self->pNodes[i] == NULL) RequestModel(25511 + i, STREAMING_KEEP_IN_MEMORY);
        }
        else if(self->pNodes[i] != NULL) RemoveModel(25511 + i);
    }
}

extern "C" EXPORT void OnModLoad()
{
    logger->SetTag("GPS 2.11.277");
    pGTASA = aml->GetLib("libGame.so");
    hGTASA = aml->GetLibHandle("libGame.so");
    if(!pGTASA)
    {
        pGTASA = FindGameLibraryBase();
        if(pGTASA) logger->Info("Located libGame.so with dl_iterate_phdr at %p", reinterpret_cast<void*>(pGTASA));
    }
    if(!hGTASA)
    {
        hGTASA = dlopen("libGame.so", RTLD_NOW | RTLD_NOLOAD);
        if(!hGTASA && gGameLibraryPath[0]) hGTASA = dlopen(gGameLibraryPath, RTLD_NOW | RTLD_NOLOAD);
    }
    if(!pGTASA)
    {
        logger->Error("libGame.so is not loaded; GPS was not enabled");
        return;
    }

    bool supportedBuild =
        VerifyInstruction(Game211277::kLoadScenePathNodesCall, 0x94112335) &&
        VerifyInstruction(Game211277::kPathArrayFirstAdrp, 0xB0002BE1) &&
        VerifyInstruction(Game211277::kPathArrayFirstStore, 0xB9005022) &&
        VerifyInstruction(Game211277::kPathArrayFirstAdrp2, 0xB0002BE2) &&
        VerifyInstruction(Game211277::kPathMaxMinusOne, 0x528270E0) &&
        VerifyInstruction(Game211277::kPathArrayFirstAdd, 0x91014042) &&
        VerifyInstruction(Game211277::kPathMaxMinusFifty, 0x52826ACE) &&
        VerifyInstruction(Game211277::kPathArraySecondAdrp, 0x90002BE9) &&
        VerifyInstruction(Game211277::kPathArraySecondAdd, 0x91014129) &&
        VerifyInstruction(Game211277::kPathArrayThirdAdrp, 0x90002BE9) &&
        VerifyInstruction(Game211277::kPathArrayThirdAdd, 0x91014129);
    if(!supportedBuild)
    {
        logger->Error("GPS supports only the verified GTA:SA 2.11.277 ARM64 build");
        return;
    }

    pCfgClosestMaxGPSDistance = cfg->Bind("ClosestMaxGPSDistance", 40.0f);
    pCfgClosestMaxGPSDistance->Clamp(0.0f, 200.0f);

    pCfgGPSMaxPathNodesLoaded = cfg->Bind("GPSMaxPathNodesLoaded", 50000);
    pCfgGPSMaxPathNodesLoaded->Clamp(5000, MAX_NODE_POINTS);
    maxLoadedPathNodes = pCfgGPSMaxPathNodesLoaded->GetInt();
    delete pCfgGPSMaxPathNodesLoaded;
    
    pCfgGPSLineColorRGB = cfg->Bind("GPSLineColorRGB", STRINGIFY(GPS_LINE_R)" " STRINGIFY(GPS_LINE_G)" " STRINGIFY(GPS_LINE_B)" " STRINGIFY(GPS_LINE_A));
    lineWidth = cfg->GetFloat("GPSLineWidth", 4.0f);
    pCfgGPSDrawDistance = cfg->Bind("GPSDrawDistance", true);
    pCfgGPSDrawDistancePosition = cfg->Bind("GPSDrawDistancePos", 0); // 0 under, 1 above, 2 left, 3 right, 4 custom
    pCfgGPSDrawDistanceTextScale = cfg->Bind("GPSDrawDistanceTextScale", 1.0f);
    pCfgGPSDrawDistanceTextOffset = cfg->Bind("GPSDrawDistanceTextOffset", "0.0 0.0");
    pCfgGPSTextAlignment = cfg->Bind("GPSTextAlignment", "-1");
    bAllowBMX = cfg->GetBool("AllowBMX", false);
    bAllowBoat = cfg->GetBool("AllowBoatNavi", true);
    bAllowMission = cfg->GetBool("MissionRoutes", true);
    bImperialUnits = cfg->GetBool("UseImperialUnits", false); // People need this. https://github.com/juicermv/GTA-GPS-Redux/issues/13
    bRespectLanesDirection = cfg->GetBool("RespectLanesDirection", false);
    bDisableFarFromRoadText = cfg->GetBool("DisableFarFromRoadText", false);
    bUseMarkerColors = cfg->GetBool("UseMarkerColors", true);
    
    int r, g, b, a, sscanfed = sscanf(pCfgGPSLineColorRGB->GetString(), "%d %d %d %d", &r, &g, &b, &a);
    if(sscanfed == 4 && IsRGBValue(r) && IsRGBValue(g) && IsRGBValue(b) && IsRGBValue(a))
    {
        gpsLineColor = RWOPENGLCOLOR(r, g, b, a);
    }
    else if(sscanfed == 3 && IsRGBValue(r) && IsRGBValue(g) && IsRGBValue(b))
    {
        gpsLineColor = RWOPENGLCOLOR(r, g, b, 255);
    }
    else
    {
        pCfgGPSLineColorRGB->SetString(STRINGIFY(GPS_LINE_R)" " STRINGIFY(GPS_LINE_G)" " STRINGIFY(GPS_LINE_B)" " STRINGIFY(GPS_LINE_A));
        cfg->Save();
    }
    delete pCfgGPSLineColorRGB;
    
    cfg->Bind("Author", "", "About")->SetString("[-=KILL MAN=-]"); cfg->ClearLast();
    cfg->Bind("IdeasFrom", "", "About")->SetString("DK22Pac, JuniorDjjr, juicermv"); cfg->ClearLast();
    cfg->Bind("Discord", "", "About")->SetString("https://discord.gg/2MY7W39kBg"); cfg->ClearLast();
    cfg->Bind("GitHub", "", "About")->SetString("https://github.com/AndroidModLoader/GTASA_GPS"); cfg->ClearLast();
    cfg->Save();

    // Init

    for(int i = 0; i < maxLoadedPathNodes; ++i) *(uint32_t*)(&aNodesToBeCleared_NEW[i]) = 0xFFFFFFFF;

    // Resolve every public symbol before changing game memory.
    bool resolved = true;
    resolved &= ResolveRequired(ThePaths,                       "ThePaths");
    resolved &= ResolveRequired(gMobileMenu,                    "gMobileMenu");
    resolved &= ResolveRequired(RsGlobal,                       "RsGlobal");
    resolved &= ResolveRequired(NearScreenZ,                    "_ZN9CSprite2d11NearScreenZE");
    resolved &= ResolveRequired(RecipNearClip,                  "_ZN9CSprite2d13RecipNearClipE");
    resolved &= ResolveRequired(pRadarTrace,                    "_ZN6CRadar13ms_RadarTraceE");
    resolved &= ResolveRequired(m_UserPause,                    "_ZN6CTimer11m_UserPauseE");
    resolved &= ResolveRequired(m_CodePause,                    "_ZN6CTimer11m_CodePauseE");
    resolved &= ResolveRequired(aWidgets,                       "_ZN15CTouchInterface10m_pWidgetsE");
    resolved &= ResolveRequired(aPickUps,                       "_ZN8CPickups8aPickUpsE");
    resolved &= ResolveRequired(ToBeStreamed,                   "ToBeStreamed");
    resolved &= ResolveRequired(m_snTimeInMilliseconds,         "_ZN6CTimer22m_snTimeInMillisecondsE");
    resolved &= ResolveRequired(m_snPreviousTimeInMilliseconds, "_ZN6CTimer30m_snPreviousTimeInMillisecondsE");

    resolved &= ResolveRequired(FindPlayerPed,                  "_Z13FindPlayerPedi");
    resolved &= ResolveRequired(FindGroundZForCoord,            "_ZN6CWorld19FindGroundZForCoordEff");
    resolved &= ResolveRequired(DoPathSearch,                   "_ZN9CPathFind12DoPathSearchEh7CVector12CNodeAddressS0_PS1_PsiPffS2_fbS1_bb");
    resolved &= ResolveRequired(TransformRealWorldPointToRadarSpace, "_ZN6CRadar35TransformRealWorldPointToRadarSpaceER9CVector2DRKS0_");
    resolved &= ResolveRequired(TransformRadarPointToScreenSpace,    "_ZN6CRadar32TransformRadarPointToScreenSpaceER9CVector2DRKS0_");
    resolved &= ResolveRequired(LimitRadarPoint,                "_ZN6CRadar15LimitRadarPointER9CVector2D");
    resolved &= ResolveRequired(RwRenderStateSet,               "_Z16RwRenderStateSet13RwRenderStatePv");
    resolved &= ResolveRequired(RwIm2DRenderPrimitive,          "_Z28RwIm2DRenderPrimitive_BUGFIX15RwPrimitiveTypeP14RwOpenGLVertexi");
    resolved &= ResolveRequired(ClearRadarBlip,                 "_ZN6CRadar9ClearBlipEi");
    resolved &= ResolveRequired(FontSetOrientation,             "_ZN5CFont14SetOrientationEh");
    resolved &= ResolveRequired(FontSetColor,                   "_ZN5CFont8SetColorE5CRGBA");
    resolved &= ResolveRequired(FontSetBackground,              "_ZN5CFont13SetBackgroundEhh");
    resolved &= ResolveRequired(FontSetWrapx,                   "_ZN5CFont8SetWrapxEf");
    resolved &= ResolveRequired(FontSetStyle,                   "_ZN5CFont12SetFontStyleEh");
    resolved &= ResolveRequired(FontSetScale,                   "_ZN5CFont8SetScaleEf");
    resolved &= ResolveRequired(FontSetEdge,                    "_ZN5CFont7SetEdgeEa");
    resolved &= ResolveRequired(FontSetProportional,            "_ZN5CFont15SetProportionalEh");
    resolved &= ResolveRequired(FontSetDropShadowPosition,      "_ZN5CFont21SetDropShadowPositionEa");
    resolved &= ResolveRequired(FontPrintString,                "_ZN5CFont11PrintStringEffPs");
    resolved &= ResolveRequired(AsciiToGxtChar,                 "_Z14AsciiToGxtCharPKcPs");
    resolved &= ResolveRequired(RenderFontBuffer,               "_ZN5CFont16RenderFontBufferEv");
    resolved &= ResolveRequired(IsOnAMission,                   "_ZN11CTheScripts18IsPlayerOnAMissionEv");
    resolved &= ResolveRequired(GetPoolPed,                     "_ZN6CPools6GetPedEi");
    resolved &= ResolveRequired(GetPoolVeh,                     "_ZN6CPools10GetVehicleEi");
    resolved &= ResolveRequired(GetPoolObj,                     "_ZN6CPools9GetObjectEi");
    resolved &= ResolveRequired(RequestModel,                   "_ZN10CStreaming12RequestModelEii");
    resolved &= ResolveRequired(RemoveModel,                    "_ZN10CStreaming11RemoveModelEi");
    resolved &= ResolveRequired(LoadAllRequestedModels,         "_ZN10CStreaming22LoadAllRequestedModelsEb");
    resolved &= ResolveRequired(MarkRegionsForCoors,            "_ZN9CPathFind19MarkRegionsForCoorsE7CVectorf");
    resolved &= ResolveRequired(RequestCollision,               "_ZN9CColStore16RequestCollisionERK7CVectori");
    if(!resolved)
    {
        logger->Error("GPS was not enabled because libGame.so did not match the verified symbol set");
        return;
    }

    ExtraPathsNeeded = reinterpret_cast<bool*>(pGTASA + Game211277::kExtraPathsNeeded);
    ExtraPathPos = reinterpret_cast<CVector*>(pGTASA + Game211277::kExtraPathPos);

    // Retarget all three references from the original 5,000-entry work array
    // to the mod's 50,000-entry array. ADRP has a +/-4 GiB reach.
    const uintptr_t expandedArray = reinterpret_cast<uintptr_t>(&aNodesToBeCleared_NEW[0]);
    const unsigned pageOffset = static_cast<unsigned>(expandedArray & 0xFFFu);
    uint32_t firstAdrp, firstAdrp2, secondAdrp, thirdAdrp;
    bool arrayReachable = (pageOffset & 3u) == 0 &&
        EncodeAdrp(pGTASA + Game211277::kPathArrayFirstAdrp, expandedArray, 1, firstAdrp) &&
        EncodeAdrp(pGTASA + Game211277::kPathArrayFirstAdrp2, expandedArray, 2, firstAdrp2) &&
        EncodeAdrp(pGTASA + Game211277::kPathArraySecondAdrp, expandedArray, 9, secondAdrp) &&
        EncodeAdrp(pGTASA + Game211277::kPathArrayThirdAdrp, expandedArray, 9, thirdAdrp);
    if(!arrayReachable)
    {
        logger->Error("Expanded path array is outside ARM64 ADRP reach; GPS was not enabled");
        return;
    }

    uintptr_t postRadarDraw = ResolveGameSymbol("_ZN6CRadar20DrawRadarGangOverlayEb");
    uintptr_t updateStreaming = ResolveGameSymbol("_ZN9CPathFind15UpdateStreamingEb");
    bool hooksInstalled = postRadarDraw && updateStreaming &&
        aml->HookPLT(reinterpret_cast<void*>(pGTASA + Game211277::kHudDrawGot), reinterpret_cast<void*>(&HookOf_PreRenderEnd), reinterpret_cast<void**>(&PreRenderEnd)) &&
        aml->HookPLT(reinterpret_cast<void*>(pGTASA + Game211277::kInitialise3DGot), reinterpret_cast<void*>(&HookOf_InitRenderWare), reinterpret_cast<void**>(&InitRenderWare)) &&
        aml->Hook(reinterpret_cast<void*>(postRadarDraw), reinterpret_cast<void*>(&HookOf_PostRadarDraw), reinterpret_cast<void**>(&PostRadarDraw)) &&
        InstallBranchLinkHook(pGTASA + Game211277::kLoadScenePathNodesCall,
                              reinterpret_cast<uintptr_t>(&HookOf_LoadSceneForPathNodes),
                              reinterpret_cast<void**>(&LoadSceneForPathNodes)) &&
        aml->Hook(reinterpret_cast<void*>(updateStreaming), reinterpret_cast<void*>(&HookOf_UpdateStreaming), reinterpret_cast<void**>(&UpdateStreaming));
    if(!hooksInstalled)
    {
        logger->Error("One or more GPS hooks could not be installed");
        return;
    }

    aml->Write32(pGTASA + Game211277::kPathMaxMinusOne, MOVBits::Create(maxLoadedPathNodes - 1, 0, false));
    aml->Write32(pGTASA + Game211277::kPathMaxMinusFifty, MOVBits::Create(maxLoadedPathNodes - 50, 14, false));
    aml->Write32(pGTASA + Game211277::kPathArrayFirstAdrp, firstAdrp);
    aml->Write32(pGTASA + Game211277::kPathArrayFirstStore, EncodeStoreWordImmediate(2, 1, pageOffset));
    aml->Write32(pGTASA + Game211277::kPathArrayFirstAdrp2, firstAdrp2);
    aml->Write32(pGTASA + Game211277::kPathArrayFirstAdd, EncodeAddImmediate(2, 2, pageOffset));
    aml->Write32(pGTASA + Game211277::kPathArraySecondAdrp, secondAdrp);
    aml->Write32(pGTASA + Game211277::kPathArraySecondAdd, EncodeAddImmediate(9, 9, pageOffset));
    aml->Write32(pGTASA + Game211277::kPathArrayThirdAdrp, thirdAdrp);
    aml->Write32(pGTASA + Game211277::kPathArrayThirdAdd, EncodeAddImmediate(9, 9, pageOffset));

    gPortEnabled = true;

    logger->Info("GTA:SA GPS 1.5.1 enabled for 2.11.277 ARM64");
}

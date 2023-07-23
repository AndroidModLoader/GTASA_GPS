#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#ifdef AML32
    #include "GTASA_STRUCTS.h"
    #define BYVER(__for32, __for64) (__for32)
#else
    #include "GTASA_STRUCTS_210.h"
    #include "AArch64_ModHelper/ARMv8_ASMHelper.h"
    #define BYVER(__for32, __for64) (__for64)
#endif

#define MAX_PATH_NODES      50000
#define TOBESTREAM_NODES    128 // def 8
#define STREAM_RADIUS       8000.0f
#define STREAM_RADIUS_LIMIT STREAM_RADIUS*1.01f
#define MAX_NODE_POINTS     10000

#define GPS_LINE_R          235
#define GPS_LINE_G          212
#define GPS_LINE_B          0
#define GPS_LINE_A          255

MYMODCFG(net.dk22pac.rusjj.gps, GTA:SA GPS, 1.3.1, DK22Pac & JuniorDjjr & juicermv & RusJJ)
NEEDGAME(com.rockstargames.gtasa)

CVector2D g_vecUnderRadar(0.0, -1.05); // 0
CVector2D g_vecAboveRadar(0.0, 1.05); // 1
CVector2D g_vecLeftRadar(-1.05, 0.0); // 2
CVector2D g_vecRightRadar(1.05, 0.0); // 3
eFontAlignment g_nTextAlignment;

// Patched
CNodeAddress aPathNodes[MAX_PATH_NODES];
uint8_t aStreamablePathNodes[8 * TOBESTREAM_NODES];

// Savings
uintptr_t pGTASA;
void* hGTASA;
CNodeAddress resultNodes[MAX_NODE_POINTS];
CVector2D nodePoints[MAX_NODE_POINTS];
RwOpenGLVertex lineVerts[MAX_NODE_POINTS * 4] {0};
float gpsDistance;
CVector2D gpsDistanceTextPos;
CRect emptyRect, radarRect;
unsigned int gpsLineColor = RWRGBALONG(GPS_LINE_R, GPS_LINE_G, GPS_LINE_B, GPS_LINE_A);
float lineWidth = 3.5f, textOffset, textScale, flMenuMapScaling;
CVector2D vecTextOffset;
tRadarTrace* pTrace;

// Config
ConfigEntry* pCfgClosestMaxGPSDistance;
ConfigEntry* pCfgGPSLineColorRGB;
ConfigEntry* pCfgGPSLineWidth;
ConfigEntry* pCfgGPSDrawDistance;
ConfigEntry* pCfgGPSDrawDistancePosition;
ConfigEntry* pCfgGPSDrawDistanceTextScale;
ConfigEntry* pCfgGPSDrawDistanceTextOffset;
bool bAllowBMX, bAllowBoat, bAllowMission, bImperialUnits;

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

// Game Funcs
CPlayerPed* (*FindPlayerPed)(int);
CVector& (*FindPlayerCoors)(CVector*, int);
float (*FindGroundZForCoord)(float, float);
int (*DoPathSearch)(CPathFind*, unsigned char, CVector, CNodeAddress, CVector, CNodeAddress*, short*, int, float*, float, CNodeAddress*, float, bool, CNodeAddress, bool, bool);
void (*TransformRadarPointToRealWorldSpace)(CVector2D& out, CVector2D const& in);
void (*TransformRealWorldPointToRadarSpace)(CVector2D& out, CVector2D const& in);
void (*TransformRadarPointToScreenSpace)(CVector2D& out, CVector2D const& in);
void (*LimitRadarPoint)(CVector2D& in);
void (*LimitToMap)(float*, float*);
void (*RwRenderStateSet)(RwRenderState, void*);
void (*RwIm2DRenderPrimitive)(RwPrimitiveType, RwOpenGLVertex*, int);
void (*SetScissorRect)(CRect&);
void (*ClearRadarBlip)(uint32_t);
bool (*IsOnAMission)();
CPed* (*GetPoolPed)(int);
CVehicle* (*GetPoolVeh)(int);
CObject* (*GetPoolObj)(int);

void (*FontSetOrientation)(unsigned char);
void (*FontSetColor)(CRGBA*);
void (*FontSetBackground)(unsigned char, unsigned char);
void (*FontSetWrapx)(float);
void (*FontSetStyle)(unsigned char);
void (*FontSetScale)(float);
void (*FontSetProportional)(unsigned char);
void (*FontSetDropShadowPosition)(char);
void (*FontSetEdge)(char);
void (*FontSetDropColor)(CRGBA*);
void (*FontPrintString)(float, float, unsigned short*);
void (*AsciiToGxtChar)(const char* txt, unsigned short* ret);
void (*RenderFontBuffer)(void);



inline bool IsRadarVisible()
{
    CWidget* radar = aWidgets[161];
    return (radar != NULL && radar->enabled);
}
inline bool IsGamePaused() { return *m_CodePause || *m_UserPause; };
inline bool IsRGBValue(int value) { return value >= 0 && value <= 255; }
void InitializeConfigValues()
{
    textOffset = (8.0f * (float)RsGlobal->maximumHeight) / 448.0f;
    textScale = (0.4f * ((float)RsGlobal->maximumWidth) / 640.0f) * pCfgGPSDrawDistanceTextScale->GetFloat();
    flMenuMapScaling = (float)RsGlobal->maximumHeight / 448.0f;

    if(sscanf(pCfgGPSDrawDistanceTextOffset->GetString(), "%f %f", &vecTextOffset.x, &vecTextOffset.y) != 2)
    {
        vecTextOffset.x = vecTextOffset.y = 0;
    }
}

RwUInt32 GetTraceColor(eBlipColour clr, bool friendly = false)
{
    switch(clr)
    {
        case BLIP_COLOUR_RED:
            return RWRGBALONG(127,0,0,255);
        case BLIP_COLOUR_GREEN:
            return RWRGBALONG(0,127,0,255);
        case BLIP_COLOUR_BLUE:
            return RWRGBALONG(0,0,127,255);
        case BLIP_COLOUR_WHITE:
            return RWRGBALONG(127,127,127,255);
        case BLIP_COLOUR_YELLOW:
            return RWRGBALONG(200,200,0,255);
        case BLIP_COLOUR_PURPLE:
            return RWRGBALONG(127,0,127,255);
        case BLIP_COLOUR_CYAN:
            return RWRGBALONG(0,127,127,255);
        case BLIP_COLOUR_THREAT:
            return friendly ? RWRGBALONG(0,0,127,255) : RWRGBALONG(127,0,0,255);
        case BLIP_COLOUR_DESTINATION:
            return RWRGBALONG(200,200,0,255);
            
        default:
            CRGBA a((int)clr);
            return RWRGBALONG(a.r, a.g, a.b, 255);
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
    CVector2D posn;
    TransformRadarPointToScreenSpace(posn, CVector2D(-1.0f, -1.0f));
    radarRect.left = posn.x + 2.0f;
    radarRect.bottom = posn.y - 2.0f;
    TransformRadarPointToScreenSpace(posn, CVector2D(1.0f, 1.0f));
    radarRect.right = posn.x - 2.0f;
    radarRect.top = posn.y + 2.0f;
            
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
}

inline void Setup2DVertex(RwOpenGLVertex &vertex, float x, float y, RwUInt32 color)
{
    vertex.pos.x = x;
    vertex.pos.y = y;
    vertex.pos.z = *NearScreenZ + 0.0001f;
    vertex.rhw = *RecipNearClip;
    vertex.color = color;
}

inline bool IsBMXNaviAllowed(CPlayerPed* player)
{
    return bAllowBMX ||
           (!bAllowBMX && player->m_pVehicle->m_nVehicleSubType != VEHICLE_TYPE_BMX);
}

inline bool IsInSupportedVehicle(CPlayerPed* player)
{
    return (player && 
            player->m_pVehicle &&
            player->m_PedFlags.bInVehicle &&
            player->m_pVehicle->m_nVehicleSubType != VEHICLE_TYPE_PLANE &&
            player->m_pVehicle->m_nVehicleSubType != VEHICLE_TYPE_HELI &&
            IsBMXNaviAllowed(player));
}

inline bool LaneDirectionRespected()
{
    return false;
}

inline bool IsBoatNaviAllowed()
{
    return bAllowBoat;
}

char text[24];
unsigned short* textGxt = new unsigned short[0xFF];
DECL_HOOK(void, PreRenderEnd, void* self)
{
    PreRenderEnd(self);
    if(gpsDistance > 0.0f && !IsGamePaused() && IsRadarVisible() && pCfgGPSDrawDistance->GetBool())
    {
        static bool bInit = false;
        if(!bInit)
        {
            bInit = true;
            SetDistanceTextValues();
        }
        
        if(!bImperialUnits)
        {
            if(gpsDistance == 100000.0f) sprintf(text, "Far from the road!");
            else if (gpsDistance >= 1000.0f) sprintf(text, "%.2fkm", 0.001f * gpsDistance);
            else sprintf(text, "%dm", (int)gpsDistance);
        }
        else
        {
            if(gpsDistance == 100000.0f) sprintf(text, "Far from the road!");
            else if (gpsDistance > 1609.344f) sprintf(text, "%.2fmil", 0.000621371192237334f * gpsDistance);
            else sprintf(text, "%dyrd", (int)(gpsDistance * 1.094f));
        }
        AsciiToGxtChar(text, textGxt);

        FontSetOrientation(g_nTextAlignment);
        if(!TargetBlip.m_nHandleIndex && pTrace) FontSetColor((CRGBA*)&GetTraceTextColor(pTrace->m_nColour, pTrace->m_bFriendly));
        else FontSetColor((CRGBA*)&rgbaWhite);
        FontSetBackground(false, false);
        FontSetWrapx(500.0f);
        FontSetScale(textScale);
        FontSetStyle(FONT_SUBTITLES);
        FontSetProportional(true);
        FontSetDropShadowPosition(1);
        FontPrintString(gpsDistanceTextPos.x, gpsDistanceTextPos.y, textGxt);
        RenderFontBuffer();
    }
    gpsDistance = 0;
}

DECL_HOOKv(InitRenderWare)
{
    InitRenderWare();
    InitializeConfigValues();
}

void DoPathDraw(CVector to, RwUInt32 color, bool isTargetBlip = false, float* dist = NULL)
{
    CPlayerPed* player = FindPlayerPed(-1);
    if(!IsInSupportedVehicle(player)) return;
    
    short nodesCount = 0;
    float trashVar;
    bool isGamePaused = IsGamePaused(), bScissors = !isGamePaused || !gMobileMenu->m_bDrawMenuMap;
    
    DoPathSearch(ThePaths, LaneDirectionRespected() && player->m_pVehicle->m_nVehicleSubType != VEHICLE_TYPE_BOAT, player->GetPosition(), 
                 CNodeAddress(), to, resultNodes, &nodesCount, MAX_NODE_POINTS, dist ? dist : &trashVar, 1000000.0f, NULL, 1000000.0f, false,
                 CNodeAddress(), false, player->m_pVehicle->m_nVehicleSubType == VEHICLE_TYPE_BOAT && IsBoatNaviAllowed());

    if(nodesCount > 0)
    {
        if(isTargetBlip && bScissors &&
           gpsDistance < pCfgClosestMaxGPSDistance->GetFloat())
        {
            ClearRadarBlip(TargetBlip.m_nHandleIndex);
            gMobileMenu->m_TargetBlipHandle.m_nHandleIndex = 0;
            TargetBlip.m_nHandleIndex = 0;
            return;
        }
        for (short i = 0; i < nodesCount; ++i)
        {
            CPathNode* node = &ThePaths->pNodes[resultNodes[i].m_nAreaId][resultNodes[i].m_nNodeId];
            CVector2D nodePos = node->GetPosition2D();
            TransformRealWorldPointToRadarSpace(nodePos, nodePos);
            if (!isGamePaused)
            {
                TransformRadarPointToScreenSpace(nodePoints[i], nodePos);
            }
            else
            {
                LimitRadarPoint(nodePos);
                TransformRadarPointToScreenSpace(nodePoints[i], nodePos);
                nodePoints[i].x *= flMenuMapScaling;
                nodePoints[i].y *= flMenuMapScaling;
            }
        }

        if(IsRadarVisible() || isGamePaused)
        {
            if (bScissors) SetScissorRect(radarRect); // Scissor
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);

            unsigned int vertIndex = 0;
            --nodesCount;
            for (short i = 0; i < nodesCount; i++)
            {
                CVector2D point[4], shift[2];
                CVector2D dir = CVector2D::Diff(nodePoints[i + 1], nodePoints[i]);
                float angle = atan2(dir.y, dir.x);
                if (!isGamePaused)
                {
                    shift[0].x = cosf(angle - 1.5707963f) * lineWidth;
                    shift[0].y = sinf(angle - 1.5707963f) * lineWidth;
                    shift[1].x = cosf(angle + 1.5707963f) * lineWidth;
                    shift[1].y = sinf(angle + 1.5707963f) * lineWidth;
                }
                else
                {
                    float mp = gMobileMenu->m_fMapZoom - 140.0f;
                    if (mp < 140.0f) mp = 140.0f;
                    else if (mp > 960.0f) mp = 960.0f;
                    mp = mp / 960.0f + 0.4f;

                    shift[0].x = cosf(angle - 1.5707963f) * lineWidth * mp;
                    shift[0].y = sinf(angle - 1.5707963f) * lineWidth * mp;
                    shift[1].x = cosf(angle + 1.5707963f) * lineWidth * mp;
                    shift[1].y = sinf(angle + 1.5707963f) * lineWidth * mp;
                }
                Setup2DVertex(lineVerts[vertIndex], nodePoints[i].x + shift[0].x, nodePoints[i].y + shift[0].y, color);
                Setup2DVertex(lineVerts[++vertIndex], nodePoints[i + 1].x + shift[0].x, nodePoints[i + 1].y + shift[0].y, color);
                Setup2DVertex(lineVerts[++vertIndex], nodePoints[i].x + shift[1].x, nodePoints[i].y + shift[1].y, color);
                Setup2DVertex(lineVerts[++vertIndex], nodePoints[i + 1].x + shift[1].x, nodePoints[i + 1].y + shift[1].y, color);
                ++vertIndex;
            }
            RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, lineVerts, 4 * nodesCount);
            if (bScissors) SetScissorRect(emptyRect); // Scissor
        }
    }
}

DECL_HOOKv(PostRadarDraw, bool b)
{
    PostRadarDraw(b);

    if(gMobileMenu->m_TargetBlipHandle.m_nHandleIndex)
    {
        bool isGamePaused = IsGamePaused();
        if(TargetBlip.m_nHandleIndex != gMobileMenu->m_TargetBlipHandle.m_nHandleIndex && !isGamePaused && IsRadarVisible())
        {
            TargetBlip = gMobileMenu->m_TargetBlipHandle;
        }

        CVector& bpos = pRadarTrace[gMobileMenu->m_TargetBlipHandle.m_nId].m_vecWorldPosition;
        if(bpos.z == 0) bpos.z = FindGroundZForCoord(bpos.x, bpos.y) + 5.0f;
        DoPathDraw(bpos, gpsLineColor, true, &gpsDistance);
    }
    else
    {
        TargetBlip.m_nHandleIndex = 0;
    }
        
    pTrace = NULL;
    if(bAllowMission && IsOnAMission())
    {
        CPlayerPed* player = FindPlayerPed(-1);
        unsigned char count = 0, maxi = 0;
        float distances[175], maxdist;
        tRadarTrace* traces[175];
            
        for(unsigned char i = 0; i < 175; ++i)
        {
            tRadarTrace& trace = pRadarTrace[i];
            if(trace.m_nBlipSprite == RADAR_SPRITE_NONE &&
               trace.m_nBlipDisplayFlag > 1)
            {
                if(trace.m_nColour == BLIP_COLOUR_DESTINATION)
                {
                    pTrace = &trace;
                    break;
                }
                traces[count] = &trace;
                distances[count] = DistanceBetweenPoints(player->GetPosition(), trace.m_vecWorldPosition);
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
            pTrace = traces[maxi];
        }
                
        if(pTrace)
        {
            switch(pTrace->m_nBlipType)
            {
                case BLIP_CAR:
                    DoPathDraw(GetPoolVeh(pTrace->m_nEntityHandle)->GetPosition(), GetTraceColor(pTrace->m_nColour, pTrace->m_bFriendly), false, !TargetBlip.m_nHandleIndex ? &gpsDistance : NULL);
                    break;
                        
                case BLIP_CHAR:
                    DoPathDraw(GetPoolPed(pTrace->m_nEntityHandle)->GetPosition(), GetTraceColor(pTrace->m_nColour, pTrace->m_bFriendly), false, !TargetBlip.m_nHandleIndex ? &gpsDistance : NULL);
                    break;
                        
                case BLIP_OBJECT:
                    DoPathDraw(GetPoolObj(pTrace->m_nEntityHandle)->GetPosition(), GetTraceColor(pTrace->m_nColour, pTrace->m_bFriendly), false, !TargetBlip.m_nHandleIndex ? &gpsDistance : NULL);
                    break;
                    
                case BLIP_PICKUP:
                {
                    CPickup* p = &aPickUps[pTrace->m_ScriptHandle.m_nId];
                    DoPathDraw(UncompressLargeVector(p->m_vecPos), GetTraceColor(pTrace->m_nColour, pTrace->m_bFriendly), false, !TargetBlip.m_nHandleIndex ? &gpsDistance : NULL);
                    break;
                }
                        
                case BLIP_COORD:
                case BLIP_CONTACT_POINT:
                    DoPathDraw(pTrace->m_vecWorldPosition, GetTraceColor(pTrace->m_nColour, pTrace->m_bFriendly), false, !TargetBlip.m_nHandleIndex ? &gpsDistance : NULL);
                    break;
                    
                default:
                    break;
            }
        }
    }
}

#ifdef AML64
float *StreamableDist1, *StreamableDist2, *StreamableDist3;
DECL_HOOKv(LoadSceneForPathNodes, CPathFind* self, CVector center)
{
    *StreamableDist1 = STREAM_RADIUS_LIMIT;
    *StreamableDist2 = STREAM_RADIUS;
    *StreamableDist3 = -STREAM_RADIUS;
    LoadSceneForPathNodes(self, center);
    *StreamableDist1 = 3000.0f;
    *StreamableDist2 = 350.0f;
    *StreamableDist3 = -350.0f;
}
DECL_HOOKv(UpdateStreaming, CPathFind* self, bool bForceLoad)
{
    *StreamableDist1 = STREAM_RADIUS_LIMIT;
    *StreamableDist2 = STREAM_RADIUS;
    *StreamableDist3 = -STREAM_RADIUS;
    UpdateStreaming(self, bForceLoad);
    *StreamableDist1 = 3000.0f;
    *StreamableDist2 = 350.0f;
    *StreamableDist3 = -350.0f;
}
DECL_HOOKv(MakeRequestForNodesToBeLoaded, CPathFind* self, float MinX, float MaxX, float MinY, float MaxY)
{
    self->bActiveRequestForRegions = true;
    self->RequestMinX = MinX;
    self->RequestMaxX = MaxX;
    self->RequestMinY = MinY;
    self->RequestMaxY = MaxY;
    HookOf_UpdateStreaming(self, true);
}

uintptr_t DoPathFind_BackTo1, DoPathFind_BackTo2, DoPathFind_BackTo3;
__attribute__((optnone)) __attribute__((naked)) void DoPathFind_Inject1(void)
{
    asm("MOV W9, WZR\nEOR W12, W8, #1");
    asm volatile("MOV X8, %0" :: "r"(&aPathNodes));
    asm volatile("STR W1, [X8]");
    asm volatile("MOV X1, X8");
    asm volatile("MOV X16, %0\n" :: "r"(DoPathFind_BackTo1));
    asm("BR X16");
}
__attribute__((optnone)) __attribute__((naked)) void DoPathFind_Inject2(void)
{
    asm("MOV W8, W8\nSTR X8, [SP, #-16]!\nMOV W10, #0x28");
    asm volatile("MOV X9, %0" :: "r"(&aPathNodes));
    asm volatile("MOV X16, %0\n" :: "r"(DoPathFind_BackTo2));
    asm("LDR X8, [SP], #16\nBR X16");
}
__attribute__((optnone)) __attribute__((naked)) void DoPathFind_Inject3(void)
{
    asm("MOV W8, W8\nSTR X8, [SP, #-16]!\nMOV W10, #0x28");
    asm volatile("MOV X9, %0" :: "r"(&aPathNodes));
    asm volatile("MOV X16, %0\n" :: "r"(DoPathFind_BackTo3));
    asm("LDR X8, [SP], #16\nBR X16");
}
#endif

extern "C" void OnModLoad()
{
    for(int i = 0; i < 8 * TOBESTREAM_NODES; ++i) aStreamablePathNodes[i] = 1;
    for(int i = 0; i < MAX_PATH_NODES; ++i) *(uint32_t*)(&aPathNodes[i]) = 0xFFFF;

    logger->SetTag("GPS AML");
    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = dlopen("libGTASA.so", RTLD_LAZY);

    pCfgClosestMaxGPSDistance = cfg->Bind("ClosestMaxGPSDistance", 50.0f);
    pCfgClosestMaxGPSDistance->Clamp(0.0f, 200.0f);
    
    pCfgGPSLineColorRGB = cfg->Bind("GPSLineColorRGB", STRINGIFY(GPS_LINE_R)" " STRINGIFY(GPS_LINE_G)" " STRINGIFY(GPS_LINE_B)" " STRINGIFY(GPS_LINE_A));
    pCfgGPSLineWidth = cfg->Bind("GPSLineWidth", 4.0f); lineWidth = pCfgGPSLineWidth->GetFloat();
    pCfgGPSDrawDistance = cfg->Bind("GPSDrawDistance", true);
    pCfgGPSDrawDistancePosition = cfg->Bind("GPSDrawDistancePos", 0); // 0 under, 1 above, 2 left, 3 right, 4 custom
    pCfgGPSDrawDistanceTextScale = cfg->Bind("GPSDrawDistanceTextScale", 1.0f);
    pCfgGPSDrawDistanceTextOffset = cfg->Bind("GPSDrawDistanceTextOffset", "0.0 0.0");
    bAllowBMX = cfg->Bind("AllowBMX", false)->GetBool();
    bAllowBoat = cfg->Bind("AllowBoatNavi", true)->GetBool();
    bAllowMission = cfg->Bind("MissionRoutes", true)->GetBool();
    // People need this. https://github.com/juicermv/GTA-GPS-Redux/issues/13
    bImperialUnits = cfg->Bind("UseImperialUnits", false)->GetBool();
    
    int r, g, b, a, sscanfed = sscanf(pCfgGPSLineColorRGB->GetString(), "%d %d %d %d", &r, &g, &b, &a);
    if(sscanfed == 4 && IsRGBValue(r) && IsRGBValue(g) && IsRGBValue(b) && IsRGBValue(a))
    {
        gpsLineColor = RWRGBALONG(r, g, b, a);
    }
    else if(sscanfed == 3 && IsRGBValue(r) && IsRGBValue(g) && IsRGBValue(b))
    {
        gpsLineColor = RWRGBALONG(r, g, b, 255);
    }
    else
    {
        pCfgGPSLineColorRGB->SetString(STRINGIFY(GPS_LINE_R)" " STRINGIFY(GPS_LINE_G)" " STRINGIFY(GPS_LINE_B)" " STRINGIFY(GPS_LINE_A));
        cfg->Save();
    }
    
    cfg->Bind("Author", "", "About")->SetString("[-=KILL MAN=-]"); cfg->ClearLast();
    cfg->Bind("IdeasFrom", "", "About")->SetString("DK22Pac, JuniorDjjr, juicermv"); cfg->ClearLast();
    cfg->Bind("Discord", "", "About")->SetString("https://discord.gg/2MY7W39kBg"); cfg->ClearLast();
    cfg->Bind("GitHub", "", "About")->SetString("https://github.com/AndroidModLoader/GTASA_GPS"); cfg->ClearLast();
    cfg->Save();

    SET_TO(ThePaths,                            aml->GetSym(hGTASA, "ThePaths"));
    SET_TO(gMobileMenu,                         aml->GetSym(hGTASA, "gMobileMenu"));
    SET_TO(RsGlobal,                            aml->GetSym(hGTASA, "RsGlobal"));
    SET_TO(NearScreenZ,                         aml->GetSym(hGTASA, "_ZN9CSprite2d11NearScreenZE"));
    SET_TO(RecipNearClip,                       aml->GetSym(hGTASA, "_ZN9CSprite2d13RecipNearClipE"));
    SET_TO(pRadarTrace,                         aml->GetSym(hGTASA, "_ZN6CRadar13ms_RadarTraceE"));
    SET_TO(m_UserPause,                         aml->GetSym(hGTASA, "_ZN6CTimer11m_UserPauseE"));
    SET_TO(m_CodePause,                         aml->GetSym(hGTASA, "_ZN6CTimer11m_CodePauseE"));

    SET_TO(FindPlayerPed,                       aml->GetSym(hGTASA, "_Z13FindPlayerPedi"));
    SET_TO(FindPlayerCoors,                     aml->GetSym(hGTASA, "_Z15FindPlayerCoorsi"));
    SET_TO(FindGroundZForCoord,                 aml->GetSym(hGTASA, "_ZN6CWorld19FindGroundZForCoordEff"));
    SET_TO(DoPathSearch,                        aml->GetSym(hGTASA, "_ZN9CPathFind12DoPathSearchEh7CVector12CNodeAddressS0_PS1_PsiPffS2_fbS1_bb"));
    SET_TO(TransformRadarPointToRealWorldSpace, aml->GetSym(hGTASA, "_ZN6CRadar35TransformRadarPointToRealWorldSpaceER9CVector2DRKS0_"));
    SET_TO(TransformRealWorldPointToRadarSpace, aml->GetSym(hGTASA, "_ZN6CRadar35TransformRealWorldPointToRadarSpaceER9CVector2DRKS0_"));
    SET_TO(TransformRadarPointToScreenSpace,    aml->GetSym(hGTASA, "_ZN6CRadar32TransformRadarPointToScreenSpaceER9CVector2DRKS0_"));
    SET_TO(LimitRadarPoint,                     aml->GetSym(hGTASA, "_ZN6CRadar15LimitRadarPointER9CVector2D"));
    SET_TO(RwRenderStateSet,                    aml->GetSym(hGTASA, "_Z16RwRenderStateSet13RwRenderStatePv"));
    SET_TO(RwIm2DRenderPrimitive,               aml->GetSym(hGTASA, "_Z28RwIm2DRenderPrimitive_BUGFIX15RwPrimitiveTypeP14RwOpenGLVertexi"));
    SET_TO(SetScissorRect,                      aml->GetSym(hGTASA, "_ZN7CWidget10SetScissorER5CRect"));
    SET_TO(ClearRadarBlip,                      aml->GetSym(hGTASA, "_ZN6CRadar9ClearBlipEi"));
    SET_TO(FontSetOrientation,                  aml->GetSym(hGTASA, "_ZN5CFont14SetOrientationEh"));
    SET_TO(FontSetColor,                        aml->GetSym(hGTASA, "_ZN5CFont8SetColorE5CRGBA"));
    SET_TO(FontSetBackground,                   aml->GetSym(hGTASA, "_ZN5CFont13SetBackgroundEhh"));
    SET_TO(FontSetWrapx,                        aml->GetSym(hGTASA, "_ZN5CFont8SetWrapxEf"));
    SET_TO(FontSetStyle,                        aml->GetSym(hGTASA, "_ZN5CFont12SetFontStyleEh"));
    SET_TO(FontSetScale,                        aml->GetSym(hGTASA, "_ZN5CFont8SetScaleEf"));
    SET_TO(FontSetEdge,                         aml->GetSym(hGTASA, "_ZN5CFont7SetEdgeEa"));
    SET_TO(FontSetProportional,                 aml->GetSym(hGTASA, "_ZN5CFont15SetProportionalEh"));
    SET_TO(FontSetDropShadowPosition,           aml->GetSym(hGTASA, "_ZN5CFont21SetDropShadowPositionEa"));
    SET_TO(FontSetDropColor,                    aml->GetSym(hGTASA, "_ZN5CFont12SetDropColorE5CRGBA"));
    SET_TO(FontPrintString,                     aml->GetSym(hGTASA, "_ZN5CFont11PrintStringEffPt"));
    SET_TO(AsciiToGxtChar,                      aml->GetSym(hGTASA, "_Z14AsciiToGxtCharPKcPt"));
    SET_TO(RenderFontBuffer,                    aml->GetSym(hGTASA, "_ZN5CFont16RenderFontBufferEv"));
    SET_TO(IsOnAMission,                        aml->GetSym(hGTASA, "_ZN11CTheScripts18IsPlayerOnAMissionEv"));
    SET_TO(GetPoolPed,                          aml->GetSym(hGTASA, "_ZN6CPools6GetPedEi"));
    SET_TO(GetPoolVeh,                          aml->GetSym(hGTASA, "_ZN6CPools10GetVehicleEi"));
    SET_TO(GetPoolObj,                          aml->GetSym(hGTASA, "_ZN6CPools9GetObjectEi"));

    HOOKPLT(PreRenderEnd,                       pGTASA + BYVER(0x674188, 0x846E90));
    HOOKPLT(InitRenderWare,                     pGTASA + BYVER(0x66F2D0, 0x8432F0));
    HOOK(PostRadarDraw,                         aml->GetSym(hGTASA, "_ZN6CRadar20DrawRadarGangOverlayEb"));
    SET_TO(aWidgets,                            *(void**)(pGTASA + BYVER(0x67947C, 0x850910)));
    SET_TO(aPickUps,                            aml->GetSym(hGTASA, "_ZN8CPickups8aPickUpsE"));

    // Patches

    aml->Write(pGTASA + BYVER(0x315B06, 0x3DBE58), (uintptr_t)BYVER("\x4C\xF2\x50\x32", "\x12\x6A\x98\x52"), 4); // 4999 -> 50000
    aml->Write(pGTASA + BYVER(0x315BC4, 0x3DBE48), (uintptr_t)BYVER("\x4C\xF2\x1E\x32", "\xCE\x63\x98\x52"), 4); // 4950 -> 49950
    aml->Unprot(pGTASA + BYVER(0x67899C, 0x84F358), sizeof(void*)); *(uintptr_t*)(pGTASA + BYVER(0x67899C, 0x84F358)) = (uintptr_t)aStreamablePathNodes;

    #ifdef AML32
    aml->Unprot(pGTASA + 0x315D30, sizeof(void*)); *(uintptr_t*)(pGTASA + 0x315D30) = (uintptr_t)aPathNodes - 0x31598A - pGTASA;
    aml->Unprot(pGTASA + 0x315D34, sizeof(void*)); *(uintptr_t*)(pGTASA + 0x315D34) = (uintptr_t)aPathNodes - 0x315BE2 - pGTASA;
    aml->Unprot(pGTASA + 0x315D38, sizeof(void*)); *(uintptr_t*)(pGTASA + 0x315D38) = (uintptr_t)aPathNodes - 0x315D08 - pGTASA;
    aml->Unprot(pGTASA + 0x315D3C, sizeof(void*)); *(uintptr_t*)(pGTASA + 0x315D3C) = (uintptr_t)aPathNodes - 0x315B20 - pGTASA;
    
    aml->Unprot(pGTASA + 0x31A04C, sizeof(float)); *(float*)(pGTASA + 0x31A04C) = -STREAM_RADIUS;
    aml->Unprot(pGTASA + 0x31A050, sizeof(float)); *(float*)(pGTASA + 0x31A050) = STREAM_RADIUS;
    aml->Unprot(pGTASA + 0x31A054, sizeof(float)); *(float*)(pGTASA + 0x31A054) = STREAM_RADIUS_LIMIT;
    aml->Unprot(pGTASA + 0x31A058, sizeof(float)); *(float*)(pGTASA + 0x31A058) = 750.0f;
    
    aml->Unprot(pGTASA + 0x319E6C + 0x2, sizeof(char)); *(unsigned char*)(pGTASA + 0x319E6C + 0x2) = TOBESTREAM_NODES-1;
    aml->Unprot(pGTASA + 0x319EC2 + 0x0, sizeof(char)); *(unsigned char*)(pGTASA + 0x319EC2 + 0x0) = TOBESTREAM_NODES-1;
    aml->Unprot(pGTASA + 0x319EE4 + 0x2, sizeof(char)); *(unsigned char*)(pGTASA + 0x319EE4 + 0x2) = TOBESTREAM_NODES-1;
    aml->Unprot(pGTASA + 0x319EF2 + 0x0, sizeof(char)); *(unsigned char*)(pGTASA + 0x319EF2 + 0x0) = TOBESTREAM_NODES-1;
    aml->Unprot(pGTASA + 0x319EFE + 0x0, sizeof(char)); *(unsigned char*)(pGTASA + 0x319EFE + 0x0) = TOBESTREAM_NODES-1;
    aml->Unprot(pGTASA + 0x319F0A + 0x0, sizeof(char)); *(unsigned char*)(pGTASA + 0x319F0A + 0x0) = TOBESTREAM_NODES-1;
    
    aml->Unprot(pGTASA + 0x319F28 + 0x2, sizeof(char)); *(unsigned char*)(pGTASA + 0x319F28 + 0x2) = TOBESTREAM_NODES;
    aml->Unprot(pGTASA + 0x319F32 + 0x2, sizeof(char)); *(unsigned char*)(pGTASA + 0x319F32 + 0x2) = TOBESTREAM_NODES;
    
    aml->Unprot(pGTASA + 0x3199A8, sizeof(float)); *(float*)(pGTASA + 0x3199A8) = -STREAM_RADIUS;
    aml->Unprot(pGTASA + 0x3199AC, sizeof(float)); *(float*)(pGTASA + 0x3199AC) = STREAM_RADIUS;
    aml->Unprot(pGTASA + 0x3199B0, sizeof(float)); *(float*)(pGTASA + 0x3199B0) = STREAM_RADIUS_LIMIT;
    aml->Unprot(pGTASA + 0x3199B4, sizeof(float)); *(float*)(pGTASA + 0x3199B4) = 750.0f;
    #else
    // LoadSceneForPathNodes
    aml->Write32(pGTASA + 0x3E0B0C, MOVBits::Create(TOBESTREAM_NODES-1, 11, false));
    aml->Write32(pGTASA + 0x3E0B38, CMPBits::Create(TOBESTREAM_NODES-1, 13, false));
    aml->Write32(pGTASA + 0x3E0B48, CMPBits::Create(TOBESTREAM_NODES-1, 14, false));
    aml->Write32(pGTASA + 0x3E0B54, CMPBits::Create(TOBESTREAM_NODES-1,  8, false));
    aml->Write32(pGTASA + 0x3E0B5C, CMPBits::Create(TOBESTREAM_NODES-1, 12, false));
    aml->Write32(pGTASA + 0x3E0CA4, CMPBits::Create(TOBESTREAM_NODES,   19, true));
    
    // UpdateStreaming
    aml->Write32(pGTASA + 0x3E0300, MOVBits::Create(TOBESTREAM_NODES-1, 11, false));
    aml->Write32(pGTASA + 0x3E0310, CMPBits::Create(TOBESTREAM_NODES-1, 13, false));
    aml->Write32(pGTASA + 0x3E0320, CMPBits::Create(TOBESTREAM_NODES-1, 14, false));
    aml->Write32(pGTASA + 0x3E032C, CMPBits::Create(TOBESTREAM_NODES-1,  8, false));
    aml->Write32(pGTASA + 0x3E0334, CMPBits::Create(TOBESTREAM_NODES-1, 12, false));

    aml->Write32(pGTASA + 0x3E0418, MOVBits::Create(TOBESTREAM_NODES-1, 12, false));
    aml->Write32(pGTASA + 0x3E0428, CMPBits::Create(TOBESTREAM_NODES-1, 13, false));
    aml->Write32(pGTASA + 0x3E0438, CMPBits::Create(TOBESTREAM_NODES-1, 14, false));
    aml->Write32(pGTASA + 0x3E0444, CMPBits::Create(TOBESTREAM_NODES-1,  8, false));
    aml->Write32(pGTASA + 0x3E044C, CMPBits::Create(TOBESTREAM_NODES-1, 11, false));

    aml->Write32(pGTASA + 0x3E051C, MOVBits::Create(TOBESTREAM_NODES-1, 14, false));
    aml->Write32(pGTASA + 0x3E0574, CMPBits::Create(TOBESTREAM_NODES-1, 12, false));
    aml->Write32(pGTASA + 0x3E0584, CMPBits::Create(TOBESTREAM_NODES-1, 13, false));
    aml->Write32(pGTASA + 0x3E0590, CMPBits::Create(TOBESTREAM_NODES-1,  8, false));
    aml->Write32(pGTASA + 0x3E0598, CMPBits::Create(TOBESTREAM_NODES-1, 11, false));

    aml->Write32(pGTASA + 0x3E06C4, MOVBits::Create(TOBESTREAM_NODES-1, 11, false));
    aml->Write32(pGTASA + 0x3E06D0, CMPBits::Create(TOBESTREAM_NODES-1, 13, false));
    aml->Write32(pGTASA + 0x3E06E0, CMPBits::Create(TOBESTREAM_NODES-1,  9, false));
    aml->Write32(pGTASA + 0x3E06EC, CMPBits::Create(TOBESTREAM_NODES-1,  8, false));
    aml->Write32(pGTASA + 0x3E06F4, CMPBits::Create(TOBESTREAM_NODES-1, 12, false));

    aml->Write32(pGTASA + 0x3E0718, MOVBits::Create(-TOBESTREAM_NODES,  12, false));
    aml->Write32(pGTASA + 0x3E0714, CMNBits::Create(TOBESTREAM_NODES,   12, false));

    HOOKPLT(UpdateStreaming, pGTASA + 0x843A40); // Faster code than patching static values
    HOOKPLT(LoadSceneForPathNodes, pGTASA + 0x848840); // Faster code than patching static values
    HOOKPLT(MakeRequestForNodesToBeLoaded, pGTASA + 0x845490); // Faster code than patching static values

    SET_TO(StreamableDist1, pGTASA + 0x73B3FC); aml->Unprot((uintptr_t)StreamableDist1);
    SET_TO(StreamableDist2, pGTASA + 0x740AFC); aml->Unprot((uintptr_t)StreamableDist2);
    SET_TO(StreamableDist3, pGTASA + 0x740AF8); aml->Unprot((uintptr_t)StreamableDist3);

    aml->PlaceNOP(pGTASA + 0x3DBE1C, 1);
    aml->PlaceNOP(pGTASA + 0x3DBE60, 1);
    DoPathFind_BackTo1 = pGTASA + 0x3DBE44; aml->Redirect(pGTASA + 0x3DBE34, (uintptr_t)DoPathFind_Inject1);
    DoPathFind_BackTo2 = pGTASA + 0x3DC0C0; aml->Redirect(pGTASA + 0x3DC0B0, (uintptr_t)DoPathFind_Inject2);
    DoPathFind_BackTo3 = pGTASA + 0x3DC248; aml->Redirect(pGTASA + 0x3DC238, (uintptr_t)DoPathFind_Inject3);
    #endif
}
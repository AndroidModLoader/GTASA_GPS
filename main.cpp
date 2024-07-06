#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#ifdef AML32
    #include "GTASA_STRUCTS.h"
    #include "AArch64_ModHelper/Thumbv7_ASMHelper.h"
    using namespace ThumbV7;
#else
    #include "GTASA_STRUCTS_210.h"
    #include "AArch64_ModHelper/ARMv8_ASMHelper.h"
    using namespace ARMv8;
#endif

#define MAX_NODE_POINTS         50000

#define GPS_LINE_R              235
#define GPS_LINE_G              212
#define GPS_LINE_B              0
#define GPS_LINE_A              255

MYMODCFG(net.dk22pac.rusjj.gps, GTA:SA GPS, 1.4, DK22Pac & JuniorDjjr & juicermv & RusJJ)
NEEDGAME(com.rockstargames.gtasa)

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
CRect emptyRect, radarRect;
unsigned int gpsLineColor = RWRGBALONG(GPS_LINE_R, GPS_LINE_G, GPS_LINE_B, GPS_LINE_A);
unsigned int maxLoadedPathNodes;
float lineWidth = 3.5f, textOffset, textScale, flMenuMapScaling;
CVector2D vecTextOffset;
tRadarTrace* pTrace;
bool *ExtraPathsNeeded;
CVector *ExtraPathPos;
uint8_t *ToBeStreamed;

// Config
ConfigEntry* pCfgClosestMaxGPSDistance;
ConfigEntry* pCfgGPSMaxPathNodesLoaded;
ConfigEntry* pCfgGPSLineColorRGB;
ConfigEntry* pCfgGPSDrawDistance;
ConfigEntry* pCfgGPSDrawDistancePosition;
ConfigEntry* pCfgGPSDrawDistanceTextScale;
ConfigEntry* pCfgGPSDrawDistanceTextOffset;
bool bAllowBMX, bAllowBoat, bAllowMission, bImperialUnits, bRespectLanesDirection;

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
void (*FontSetDropColor)(CRGBA*);
void (*FontPrintString)(float, float, unsigned short*);
void (*AsciiToGxtChar)(const char* txt, unsigned short* ret);
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
            return RWRGBALONG(127,   0,   0, 255);
        case BLIP_COLOUR_GREEN:
            return RWRGBALONG(  0, 127,   0, 255);
        case BLIP_COLOUR_BLUE:
            return RWRGBALONG(  0,   0, 127, 255);
        case BLIP_COLOUR_WHITE:
            return RWRGBALONG(127, 127, 127, 255);
        case BLIP_COLOUR_YELLOW:
            return RWRGBALONG(200, 200,   0, 255);
        case BLIP_COLOUR_PURPLE:
            return RWRGBALONG(127,   0, 127, 255);
        case BLIP_COLOUR_CYAN:
            return RWRGBALONG(  0, 127, 127, 255);
        case BLIP_COLOUR_THREAT:
            return friendly ? RWRGBALONG(0, 0, 127, 255) : RWRGBALONG(127, 0, 0, 255);
        case BLIP_COLOUR_DESTINATION:
            return RWRGBALONG(200, 200,   0, 255);
            
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
    return bRespectLanesDirection;
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
            if(!bImperialUnits)
            {
                if(gpsDistance == 100000.0f) sprintf(text, "Far from the road!");
                else if (gpsDistance >= 1000.0f) sprintf(text, "%.2fkm", 0.001f * gpsDistance);
                else sprintf(text, "%dm", (int)gpsDistance);
            }
            else
            {
                if(gpsDistance == 100000.0f) sprintf(text, "Far from the road!");
                else if (gpsDistance > 1609.344f) sprintf(text, "%.2fmil", (float)(0.000621371192237334 * (double)gpsDistance));
                else sprintf(text, "%dyrd", (int)(gpsDistance * 1.094f));
            }
            AsciiToGxtChar(text, textGxt);

            static CRGBA drawingColor;
            drawingColor = (!TargetBlip.m_nHandleIndex && pTrace) ? GetTraceTextColor(pTrace->m_nColour, pTrace->m_bFriendly) : rgbaWhite;
            //drawingColor.a = GetRadarVisibility();

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

DECL_HOOKv(InitRenderWare)
{
    InitRenderWare();
    InitializeConfigValues();
}

short nodesCount = 0;
float trashVar;
void DoPathDraw(CVector to, RwUInt32 color, bool isTargetBlip = false, float* dist = NULL)
{
    CPlayerPed* player = FindPlayerPed(-1);
    if(!IsInSupportedVehicle(player)) return;
    
    bool isGamePaused = IsGamePaused(), bScissors = (!isGamePaused || !gMobileMenu->m_bDrawMenuMap);
    
    //*ExtraPathsNeeded = true;
    //*ExtraPathPos = to;
    DoPathSearch(ThePaths, LaneDirectionRespected() && player->m_pVehicle->m_nVehicleSubType != VEHICLE_TYPE_BOAT, player->GetPosition(), 
                 CNodeAddress(), to, resultNodes, &nodesCount, maxLoadedPathNodes, dist ? dist : &trashVar, 1000000.0f, NULL, 1000000.0f, false,
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

        CPathNode* node;
        CVector2D nodePos;
        if (isGamePaused)
        {
            for (int i = 0; i < nodesCount; ++i)
            {
                node = &ThePaths->pNodes[resultNodes[i].m_nAreaId][resultNodes[i].m_nNodeId];
                nodePos = node->GetPosition2D();
                TransformRealWorldPointToRadarSpace(nodePos, nodePos);
                LimitRadarPoint(nodePos);
                TransformRadarPointToScreenSpace(nodePoints[i], nodePos);
                nodePoints[i] *= flMenuMapScaling;
            }
        }
        else
        {
            for (int i = 0; i < nodesCount; ++i)
            {
                node = &ThePaths->pNodes[resultNodes[i].m_nAreaId][resultNodes[i].m_nNodeId];
                nodePos = node->GetPosition2D();
                TransformRealWorldPointToRadarSpace(nodePos, nodePos);
                TransformRadarPointToScreenSpace(nodePoints[i], nodePos);
            }
        }

        if(IsRadarVisible() || isGamePaused)
        {
            if (bScissors) SetScissorRect(radarRect); // Scissors
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);

            unsigned int vertIndex = 0;
            --nodesCount;

            CVector2D point[4], shift[2], dir;
            float angle;
            if (isGamePaused)
            {
                float mp = gMobileMenu->m_fMapZoom - 140.0f;
                if (mp < 140.0f) mp = 140.0f;
                else if (mp > 960.0f) mp = 960.0f;
                mp = mp / 960.0f + 0.4f;
                mp *= lineWidth;

                for (int i = 0; i < nodesCount; i++)
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
                for (int i = 0; i < nodesCount; i++)
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
            RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, lineVerts, 4 * nodesCount);
            if (bScissors) SetScissorRect(emptyRect); // Scissors
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
        if(bpos.z == 0)
        {
            RequestCollision(&bpos, -1);
            LoadAllRequestedModels(false);
            bpos.z = FindGroundZForCoord(bpos.x, bpos.y) + 5.0f;
        }
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
        float distances[250], maxdist;
        tRadarTrace* traces[250];
            
        for(unsigned char i = 0; i < 250; ++i)
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
            CEntity *handle;
            switch(pTrace->m_nBlipType)
            {
                case BLIP_CAR:
                    if((handle = GetPoolVeh(pTrace->m_nEntityHandle)) != NULL)
                        DoPathDraw(handle->GetPosition(), GetTraceColor(pTrace->m_nColour, pTrace->m_bFriendly), false, !TargetBlip.m_nHandleIndex ? &gpsDistance : NULL);
                    break;
                        
                case BLIP_CHAR:
                    if((handle = GetPoolPed(pTrace->m_nEntityHandle)) != NULL)
                        DoPathDraw(handle->GetPosition(), GetTraceColor(pTrace->m_nColour, pTrace->m_bFriendly), false, !TargetBlip.m_nHandleIndex ? &gpsDistance : NULL);
                    break;
                        
                case BLIP_OBJECT:
                    if((handle = GetPoolObj(pTrace->m_nEntityHandle)) != NULL)
                        DoPathDraw(handle->GetPosition(), GetTraceColor(pTrace->m_nColour, pTrace->m_bFriendly), false, !TargetBlip.m_nHandleIndex ? &gpsDistance : NULL);
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

DECL_HOOKv(LoadSceneForPathNodes, CPathFind* self, CVector center)
{
    memset(ToBeStreamed, 0, 64);
    CPlayerPed* player = FindPlayerPed(-1);

    if(player)
    {
        MarkRegionsForCoors(self, center, 300.0f);
        MarkRegionsForCoors(self, player->GetPosition(), 4300.0f);
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

DECL_HOOKv(UpdateStreaming, CPathFind *self, bool forceLoad)
{
    if(!forceLoad && !*ExtraPathsNeeded && (*m_snTimeInMilliseconds ^ *m_snPreviousTimeInMilliseconds) < 512) return;
    memset(ToBeStreamed, 0, 64);

    CPlayerPed* player = FindPlayerPed(-1);
    if(player) MarkRegionsForCoors(self, player->GetPosition(), 4300.0f);
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

#ifdef AML64
uintptr_t DoPathFind_BackTo;
__attribute__((optnone)) __attribute__((naked)) void DoPathFind_Inject(void)
{
    asm("MOV W2, WZR\nMOV W9, WZR\nEOR W12, W8, #1");
    asm volatile("MOV X8, %0" :: "r"(&aNodesToBeCleared_NEW[0]));
    asm volatile("STR W1, [X8]");
    asm volatile("MOV X16, %0\n" :: "r"(DoPathFind_BackTo));
    asm("BR X16");
}
#endif

extern "C" void OnModLoad()
{
    logger->SetTag("GPS AML");
    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");

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
    bAllowBMX = cfg->GetBool("AllowBMX", false);
    bAllowBoat = cfg->GetBool("AllowBoatNavi", true);
    bAllowMission = cfg->GetBool("MissionRoutes", true);
    bImperialUnits = cfg->GetBool("UseImperialUnits", false); // People need this. https://github.com/juicermv/GTA-GPS-Redux/issues/13
    bRespectLanesDirection = cfg->GetBool("RespectLanesDirection", false);
    
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
    delete pCfgGPSLineColorRGB;
    
    cfg->Bind("Author", "", "About")->SetString("[-=KILL MAN=-]"); cfg->ClearLast();
    cfg->Bind("IdeasFrom", "", "About")->SetString("DK22Pac, JuniorDjjr, juicermv"); cfg->ClearLast();
    cfg->Bind("Discord", "", "About")->SetString("https://discord.gg/2MY7W39kBg"); cfg->ClearLast();
    cfg->Bind("GitHub", "", "About")->SetString("https://github.com/AndroidModLoader/GTASA_GPS"); cfg->ClearLast();
    cfg->Save();

    // Init

    for(int i = 0; i < maxLoadedPathNodes; ++i) *(uint32_t*)(&aNodesToBeCleared_NEW[i]) = 0xFFFFFFFF;

    // Hooks

    HOOKPLT(PreRenderEnd,                       pGTASA + BYBIT(0x674188, 0x846E90));
    HOOKPLT(InitRenderWare,                     pGTASA + BYBIT(0x66F2D0, 0x8432F0));
    HOOK(PostRadarDraw,                         aml->GetSym(hGTASA, "_ZN6CRadar20DrawRadarGangOverlayEb"));
    HOOKBLX(LoadSceneForPathNodes,              pGTASA + BYBIT(0x2D4AFE + 0x1, 0x3970DC));
    HOOK(UpdateStreaming,                       aml->GetSym(hGTASA, "_ZN9CPathFind15UpdateStreamingEb"));

    // Vars

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
    SET_TO(RequestModel,                        aml->GetSym(hGTASA, "_ZN10CStreaming12RequestModelEii"));
    SET_TO(RemoveModel,                         aml->GetSym(hGTASA, "_ZN10CStreaming11RemoveModelEi"));
    SET_TO(LoadAllRequestedModels,              aml->GetSym(hGTASA, "_ZN10CStreaming22LoadAllRequestedModelsEb"));
    SET_TO(MarkRegionsForCoors,                 aml->GetSym(hGTASA, "_ZN9CPathFind19MarkRegionsForCoorsE7CVectorf"));
    SET_TO(RequestCollision,                    aml->GetSym(hGTASA, "_ZN9CColStore16RequestCollisionERK7CVectori"));

    SET_TO(aWidgets,                            *(void**)(pGTASA + BYBIT(0x67947C, 0x850910)));
    SET_TO(aPickUps,                            *(void**)(pGTASA + BYBIT(0x678BF8, 0x84F818)));
    SET_TO(ExtraPathsNeeded,                    pGTASA + BYBIT(0x7AEE04, 0x9905C8));
    SET_TO(ExtraPathPos,                        pGTASA + BYBIT(0x7AEDF8, 0x9905BC));
    SET_TO(ToBeStreamed,                        aml->GetSym(hGTASA, "ToBeStreamed"));
    SET_TO(m_snTimeInMilliseconds,              aml->GetSym(hGTASA, "_ZN6CTimer22m_snTimeInMillisecondsE"));
    SET_TO(m_snPreviousTimeInMilliseconds,      aml->GetSym(hGTASA, "_ZN6CTimer30m_snPreviousTimeInMillisecondsE"));

    // Patches

    aml->Write32(pGTASA + BYBIT(0x315B06, 0x3DBE58), BYBIT(MOVWBits::Create(maxLoadedPathNodes-1, 2), MOVBits::Create(maxLoadedPathNodes-1, 18, false))); // 4999 -> maxLoadedPathNodes-1
    aml->Write32(pGTASA + BYBIT(0x315BC4, 0x3DBE48), BYBIT(MOVWBits::Create(maxLoadedPathNodes-50, 2), MOVBits::Create(maxLoadedPathNodes-50, 14, false))); // 4950 -> maxLoadedPathNodes-50

  #ifdef AML32
    aml->WriteAddr(pGTASA + 0x315D30, (uintptr_t)aNodesToBeCleared_NEW - 0x31598A - pGTASA);
    aml->WriteAddr(pGTASA + 0x315D34, (uintptr_t)aNodesToBeCleared_NEW - 0x315BE2 - pGTASA);
    aml->WriteAddr(pGTASA + 0x315D38, (uintptr_t)aNodesToBeCleared_NEW - 0x315D08 - pGTASA);
    aml->WriteAddr(pGTASA + 0x315D3C, (uintptr_t)aNodesToBeCleared_NEW - 0x315B20 - pGTASA);
  #else // AML64
    aml->WriteAddr(pGTASA + 0x9904B0, (uintptr_t)&aNodesToBeCleared_NEW[0]); // using original aNodesToBeCleared as a mapping
    aml->Redirect(pGTASA + 0x3DBE30, (uintptr_t)DoPathFind_Inject); DoPathFind_BackTo = pGTASA + 0x3DBE40;
    aml->Write32(pGTASA + 0x3DBE40, 0xB0002DA1); aml->Write32(pGTASA + 0x3DBE60, 0xF9425821);
    aml->Write32(pGTASA + 0x3DC0B0, 0x90002DA9); aml->Write32(pGTASA + 0x3DC0B8, 0xF9425929);
    aml->Write32(pGTASA + 0x3DC238, 0x90002DA9); aml->Write32(pGTASA + 0x3DC240, 0xF9425929);
  #endif
}
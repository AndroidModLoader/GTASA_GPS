#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

// Minimal public ABI declarations for the ARM64 2.11.277 mobile engine.
// Offsets were verified against the user's libGame.so and the 2.10 ARM64
// debug layout. Keeping this file small makes every layout dependency explicit.

using RwUInt32 = std::uint32_t;

struct CVector2D {
    float x;
    float y;

    constexpr CVector2D(float x_ = 0.0f, float y_ = 0.0f) : x(x_), y(y_) {}

    CVector2D& operator+=(const CVector2D& rhs) { x += rhs.x; y += rhs.y; return *this; }
    CVector2D& operator*=(float rhs) { x *= rhs; y *= rhs; return *this; }
    static CVector2D Diff(const CVector2D& a, const CVector2D& b) { return {a.x - b.x, a.y - b.y}; }
};
static_assert(sizeof(CVector2D) == 8);

struct CVector {
    float x;
    float y;
    float z;

    constexpr CVector(float x_ = 0.0f, float y_ = 0.0f, float z_ = 0.0f) : x(x_), y(y_), z(z_) {}
    CVector operator-(const CVector& rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    float Magnitude() const { return std::sqrt(x * x + y * y + z * z); }
};
static_assert(sizeof(CVector) == 12);

inline float DistanceBetweenPoints(const CVector& a, const CVector& b) { return (a - b).Magnitude(); }

struct CRGBA {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;

    constexpr CRGBA() : r(0), g(0), b(0), a(0) {}
    constexpr CRGBA(std::uint8_t r_, std::uint8_t g_, std::uint8_t b_, std::uint8_t a_)
        : r(r_), g(g_), b(b_), a(a_) {}
    explicit constexpr CRGBA(std::uint32_t packed)
        : r(static_cast<std::uint8_t>(packed >> 16)),
          g(static_cast<std::uint8_t>(packed >> 8)),
          b(static_cast<std::uint8_t>(packed)),
          a(static_cast<std::uint8_t>(packed >> 24)) {}
};
static_assert(sizeof(CRGBA) == 4);

inline constexpr CRGBA rgbaWhite{255, 255, 255, 255};

#define RWRGBALONG(r, g, b, a) \
    ((RwUInt32)((((RwUInt32)(a)) << 24) | (((RwUInt32)(r)) << 16) | (((RwUInt32)(g)) << 8) | ((RwUInt32)(b))))

// The mobile OpenGL immediate-mode vertex consumes the little-endian byte
// sequence R, G, B, A rather than the desktop D3D packed-color order.
#define RWOPENGLCOLOR(r, g, b, a) \
    ((RwUInt32)((((RwUInt32)(a)) << 24) | (((RwUInt32)(b)) << 16) | (((RwUInt32)(g)) << 8) | ((RwUInt32)(r))))

#define GPS_STRINGIFY_INNER(value) #value
#define STRINGIFY(value) GPS_STRINGIFY_INNER(value)

struct CRect {
    float left{};
    float top{};
    float right{};
    float bottom{};
};
static_assert(sizeof(CRect) == 16);

struct RwV3d {
    float x;
    float y;
    float z;
};
static_assert(sizeof(RwV3d) == 12);

struct RwOpenGLVertex {
    union {
        struct { float x, y, z; };
        RwV3d pos;
    };
    float rhw;
    union {
        struct { std::uint8_t b, g, r, a; };
        std::uint32_t color;
    };
    float s;
    float t;
};
static_assert(sizeof(RwOpenGLVertex) == 28);

enum RwRenderState : int {
    rwRENDERSTATENARENDERSTATE = 0,
    rwRENDERSTATETEXTURERASTER = 1,
};

enum RwPrimitiveType : int {
    rwPRIMTYPENAPRIMTYPE = 0,
    rwPRIMTYPELINELIST = 1,
    rwPRIMTYPEPOLYLINE = 2,
    rwPRIMTYPETRILIST = 3,
    rwPRIMTYPETRISTRIP = 4,
};

enum eFontAlignment : std::uint8_t {
    ALIGN_CENTER = 0,
    ALIGN_LEFT = 1,
    ALIGN_RIGHT = 2,
};

enum eFontStyle : std::uint8_t {
    FONT_GOTHIC = 0,
    FONT_SUBTITLES = 1,
};

enum eVehicleType : int {
    VEHICLE_TYPE_NONE = -1,
    VEHICLE_TYPE_CAR = 0,
    VEHICLE_TYPE_MONSTERTRUCK = 1,
    VEHICLE_TYPE_QUADBIKE = 2,
    VEHICLE_TYPE_HELI = 3,
    VEHICLE_TYPE_PLANE = 4,
    VEHICLE_TYPE_BOAT = 5,
    VEHICLE_TYPE_TRAIN = 6,
    VEHICLE_TYPE_NOT_USED = 7,
    VEHICLE_TYPE_FAKE_PLANE = 8,
    VEHICLE_TYPE_BIKE = 9,
    VEHICLE_TYPE_BMX = 10,
    VEHICLE_TYPE_TRAILER = 11,
};

enum eBlipType : std::uint8_t {
    BLIP_NONE = 0,
    BLIP_CAR = 1,
    BLIP_CHAR = 2,
    BLIP_OBJECT = 3,
    BLIP_COORD = 4,
    BLIP_CONTACT_POINT = 5,
    BLIP_SPOTLIGHT = 6,
    BLIP_PICKUP = 7,
    BLIP_AIRSTRIP = 8,
};

enum eBlipColour : std::uint32_t {
    BLIP_COLOUR_RED = 0,
    BLIP_COLOUR_GREEN = 1,
    BLIP_COLOUR_BLUE = 2,
    BLIP_COLOUR_WHITE = 3,
    BLIP_COLOUR_YELLOW = 4,
    BLIP_COLOUR_PURPLE = 5,
    BLIP_COLOUR_CYAN = 6,
    BLIP_COLOUR_THREAT = 7,
    BLIP_COLOUR_DESTINATION = 8,
};

constexpr std::int8_t RADAR_SPRITE_NONE = 0;
constexpr int STREAMING_NONE = 0;
constexpr int STREAMING_KEEP_IN_MEMORY = 8;

union ScriptHandle {
    std::uint32_t m_nHandleIndex;
    struct {
        std::uint16_t m_nId;
        std::uint16_t m_nReference;
    };

    constexpr ScriptHandle(std::uint32_t value = 0) : m_nHandleIndex(value) {}
};
static_assert(sizeof(ScriptHandle) == 4);

struct tRadarTrace {
    eBlipColour m_nColour;
    union {
        std::int32_t m_nEntityHandle;
        ScriptHandle m_ScriptHandle;
    };
    CVector m_vecWorldPosition;
    std::uint16_t m_nCounter;
    std::uint16_t _pad16;
    float m_fBlipMarkerScale;
    std::int16_t m_nBlipScale;
    std::uint16_t _pad30;
    void* m_pEntryExit;
    std::int8_t m_nBlipSprite;
    std::uint8_t m_bBright : 1;
    std::uint8_t m_bTrackingBlip : 1;
    std::uint8_t m_bShortRange : 1;
    std::uint8_t m_bFriendly : 1;
    std::uint8_t m_bBlipRemain : 1;
    std::uint8_t m_bBlipFade : 1;
    std::uint8_t m_nCoordBlipAppearance : 2;
    std::uint8_t m_nBlipDisplayFlag : 2;
    std::uint8_t m_nBlipType : 4;
    std::uint8_t m_nAppearance : 2;
    std::uint8_t _pad43[5];
};
static_assert(sizeof(tRadarTrace) == 48);
static_assert(offsetof(tRadarTrace, m_vecWorldPosition) == 8);
static_assert(offsetof(tRadarTrace, m_nBlipSprite) == 40);

struct CNodeAddress {
    union { std::uint16_t Region; std::uint16_t m_nAreaId; };
    union { std::uint16_t Index; std::uint16_t m_nNodeId; };

    constexpr CNodeAddress() : Region(0xFFFF), Index(0xFFFF) {}
    constexpr CNodeAddress(std::uint16_t region, std::uint16_t index) : Region(region), Index(index) {}
};
static_assert(sizeof(CNodeAddress) == 4);

struct CPathNode {
    CPathNode* pNext;
    CPathNode* pPrevious;
    std::int16_t CoorsX;
    std::int16_t CoorsY;
    std::int16_t CoorsZ;
    std::int16_t DistanceToTarget;
    std::int16_t IndexAdjacentNodes;
    CNodeAddress Address;
    std::uint8_t Width;
    std::uint8_t Group;
    std::uint8_t flags[3];
    std::uint8_t _pad35[5];

    CVector GetPosition() const { return {CoorsX / 8.0f, CoorsY / 8.0f, CoorsZ / 8.0f}; }
    CVector2D GetPosition2D() const { return {CoorsX / 8.0f, CoorsY / 8.0f}; }
};
static_assert(sizeof(CPathNode) == 40);
static_assert(offsetof(CPathNode, CoorsX) == 16);

struct CPathFind {
    std::uint8_t _pad0[4104];
    CPathNode* pNodes[72];
};
static_assert(offsetof(CPathFind, pNodes) == 4104);

struct CMatrix {
    float right[4];
    float up[4];
    float at[4];
    union {
        float pos4[4];
        struct { CVector pos; float posPad; };
    };
    void* m_pRwMat;
    std::uint32_t m_owner;
    std::uint32_t _pad76;
};
static_assert(sizeof(CMatrix) == 80);
static_assert(offsetof(CMatrix, pos) == 48);

struct CSimpleTransform {
    CVector position;
    float heading;
};
static_assert(sizeof(CSimpleTransform) == 16);

struct CPlaceableView {
    void* vtable;
    CSimpleTransform transform;
    CMatrix* matrix;
};
static_assert(sizeof(CPlaceableView) == 32);
static_assert(offsetof(CPlaceableView, matrix) == 24);

class CEntity {};
class CObject : public CEntity {};
class CPed : public CEntity {};
class CPlayerPed : public CPed {};
class CVehicle : public CEntity {};

inline CVector& GetEntityPosition(void* entity) {
    auto* placeable = reinterpret_cast<CPlaceableView*>(entity);
    return placeable->matrix ? placeable->matrix->pos : placeable->transform.position;
}

inline CVehicle* GetPedVehicle(CPlayerPed* ped) {
    return *reinterpret_cast<CVehicle**>(reinterpret_cast<std::uint8_t*>(ped) + 1800);
}

inline bool IsPedInVehicle(CPlayerPed* ped) {
    const auto firstFlags = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(ped) + 1412);
    return (firstFlags & (1u << 8)) != 0;
}

inline eVehicleType GetVehicleSubType(const CVehicle* vehicle) {
    return static_cast<eVehicleType>(*reinterpret_cast<const std::int32_t*>(reinterpret_cast<const std::uint8_t*>(vehicle) + 1848));
}

struct CompressedLargeVector {
    std::int16_t x;
    std::int16_t y;
    std::int16_t z;
};
static_assert(sizeof(CompressedLargeVector) == 6);

inline CVector UncompressLargeVector(const CompressedLargeVector& value) {
    return {value.x / 8.0f, value.y / 8.0f, value.z / 8.0f};
}

struct CPickup {
    std::uint8_t _pad0[20];
    CompressedLargeVector m_vecPos;
    std::uint8_t _pad26[10];
};
static_assert(sizeof(CPickup) == 36);
static_assert(offsetof(CPickup, m_vecPos) == 20);

struct CWidget {
    std::uint8_t _pad0[85];
    CRGBA color;
    bool enabled;
};
static_assert(offsetof(CWidget, color) == 85);
static_assert(offsetof(CWidget, enabled) == 89);

struct MobileMenu {
    std::uint8_t _pad0[104];
    ScriptHandle m_TargetBlipHandle;
    std::uint8_t _pad108[12];
    float m_fMapZoom;
    std::uint8_t _pad124[16];
    bool m_bDrawMenuMap;
};
static_assert(offsetof(MobileMenu, m_TargetBlipHandle) == 104);
static_assert(offsetof(MobileMenu, m_fMapZoom) == 120);
static_assert(offsetof(MobileMenu, m_bDrawMenuMap) == 140);

struct RsGlobalType {
    const char* appName;
    union { std::int32_t screenWidth; std::int32_t maximumWidth; };
    union { std::int32_t screenHeight; std::int32_t maximumHeight; };
    std::int32_t maxFPS;
    std::int32_t quit;
    void* ps;
    std::uint8_t inputDevices[48];
};
static_assert(sizeof(RsGlobalType) == 80);
static_assert(offsetof(RsGlobalType, maximumWidth) == 8);
static_assert(offsetof(RsGlobalType, maximumHeight) == 12);

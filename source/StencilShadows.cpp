#include <plugin.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "CCamera.h"
#include "CCollisionData.h"
#include "CGame.h"
#include "CObject.h"
#include "CSprite2d.h"
#include "CTimeCycle.h"
#include "CWorld.h"
#include "RenderWare.h"

// PC stencil shadows are a finished slot list wrapped around an unfinished
// composite. Volumes are built from each model's collision shadow mesh, then
// one fullscreen rectangle is supposed to darken the pixels inside them.
// The rectangle tests stencil <= 1 and never clears the buffer, so unmarked
// pixels are darkened and overlapping volumes are skipped. There is also no
// per-shadow alpha: the cutoff is a pop at 50 units, evaluated one frame in
// eight, and building volumes are rebuilt one frame in four.
//
// This keeps the game's volume builders and replaces the parts around them.

namespace
{
    // Was 64 slots and a hard pop at 50 units past the collision sphere.
    constexpr int kPoolSize = 192;
    constexpr float kDrawDistance = 120.0f;
    // Wider than the cutoff so a large collision sphere is still visited.
    constexpr float kScanRadius = 200.0f;
    constexpr float kFadeFraction = 0.25f;
    // Sky-top tint, same source as Junior_Djjr's Dynamic Shadow Color.
    // Dividing pulls the sky down toward black; 100 is the full-strength
    // opacity that mod used in place of the game's 50.
    constexpr float kShadowColorDivisor = 8.0f;
    constexpr float kShadowOpacity = 100.0f;
    constexpr unsigned kWorkBufferBytes = 0x6000;

    // The collision shadow flag is bit 2 of CCollisionData+0x7. Plugin-SDK
    // names that bit b03 and puts bHasShadow on bit 4, which this code does
    // not test.
    constexpr unsigned char kHasShadowMeshFlag = 0x04;

    constexpr std::uintptr_t kInit = 0x70F9E0;
    constexpr std::uintptr_t kRenderStencilShadows = 0x7113B0;
    constexpr std::uintptr_t kRenderBuffer = 0x710B50;
    constexpr std::uintptr_t kRenderVolumes = 0x710D50;
    constexpr std::uintptr_t kGraphicsHighQuality = 0x70F9B0;
    constexpr std::uintptr_t kCreateShadow = 0x711280;
    constexpr std::uintptr_t kDestroyShadow = 0x711310;
    constexpr std::uintptr_t kRegisterCall = 0x711DCD;

    constexpr std::uintptr_t kFirstAvailable = 0xC6A168;
    constexpr std::uintptr_t kFirstActive = 0xC6A16C;
    constexpr std::uintptr_t kSunDirection = 0x8D5244;

    enum class ShadowType : unsigned char
    {
        None = 0,
        Object = 1,
        Vehicle = 2,
    };

    struct StencilShadow
    {
        CEntity* owner;
        std::int16_t numFaces;
        unsigned char type;
        unsigned char pad;
        unsigned int faceId;
        unsigned int faceDataSize;
        CVector* faces;
        StencilShadow* next;
        StencilShadow* prev;
    };
    static_assert(sizeof(StencilShadow) == 0x1C);

    StencilShadow gPool[kPoolSize];
    float gDrawDistanceSquared = kDrawDistance * kDrawDistance;
    float gScanRadius = kScanRadius;

    alignas(16) unsigned char gWorkBuffers[3][kWorkBufferBytes];
    int gWorkBufferIndex = 0;

    using Register_t = bool(__cdecl*)(CVector*, bool);
    Register_t gOriginalRegister = nullptr;

    StencilShadow*& FirstAvailable()
    {
        return *reinterpret_cast<StencilShadow**>(kFirstAvailable);
    }

    StencilShadow*& FirstActive()
    {
        return *reinterpret_cast<StencilShadow**>(kFirstActive);
    }

    CVector& SunDirection()
    {
        return *reinterpret_cast<CVector*>(kSunDirection);
    }

    bool GraphicsHighQuality()
    {
        return reinterpret_cast<bool(__cdecl*)()>(kGraphicsHighQuality)();
    }

    void DestroyShadow(StencilShadow* shadow)
    {
        plugin::CallMethod<kDestroyShadow>(shadow);
    }

    StencilShadow* CreateShadow(StencilShadow* slot, CEntity* entity, ShadowType type)
    {
        return plugin::CallMethodAndReturn<StencilShadow*, kCreateShadow>(
            slot, entity, static_cast<unsigned char>(type));
    }

    bool HasShadowMesh(CEntity* entity)
    {
        auto* model = entity->GetColModel();
        auto* data = model ? model->m_pColData : nullptr;
        if (!data || data->m_nNumShadowTriangles == 0 || !data->m_pShadowVertices || !data->m_pShadowTriangles)
            return false;

        const auto flags = *reinterpret_cast<unsigned char*>(reinterpret_cast<unsigned char*>(data) + 0x7);
        return (flags & kHasShadowMeshFlag) != 0;
    }

    float SurfaceDistance(CEntity* entity, const CVector& camera)
    {
        if (!entity)
            return kDrawDistance;

        const float radius = entity->GetBoundRadius();
        return entity->GetBoundCentre().Distance(camera) - radius;
    }

    unsigned char ShadowAlpha(CEntity* entity, const CVector& camera)
    {
        const float surface = SurfaceDistance(entity, camera);
        const float fadeStart = kDrawDistance * (1.0f - kFadeFraction);
        float scale = 1.0f;
        if (surface >= kDrawDistance)
            scale = 0.0f;
        else if (surface > fadeStart)
            scale = (kDrawDistance - surface) / (kDrawDistance - fadeStart);

        // Timecycle strength still scales the darkness, then the distance fade.
        const int strength = CTimeCycle::m_CurrentColours.m_nShadowStrength;
        const int alpha = static_cast<int>(kShadowOpacity * static_cast<float>(strength) / 256.0f * scale);
        return static_cast<unsigned char>(std::clamp(alpha, 0, 255));
    }

    unsigned char SkyChannel(unsigned short value)
    {
        const int color = static_cast<int>(static_cast<float>(value) / kShadowColorDivisor + 0.5f);
        return static_cast<unsigned char>(std::clamp(color, 0, 255));
    }

    // A shadow is skylight, not a black decal. Noon is a dark blue, sunset
    // follows the sky, and night falls back to black because the sky does.
    CRGBA ShadowColor(unsigned char alpha)
    {
        const auto& colours = CTimeCycle::m_CurrentColours;
        return CRGBA{
            SkyChannel(colours.m_nSkyTopRed),
            SkyChannel(colours.m_nSkyTopGreen),
            SkyChannel(colours.m_nSkyTopBlue),
            alpha,
        };
    }

    bool AlreadyRegistered(CEntity* entity)
    {
        for (auto* shadow = FirstActive(); shadow; shadow = shadow->next)
        {
            if (shadow->owner == entity)
                return true;
        }
        return false;
    }

    void TryRegisterObject(CEntity* entity, const CVector& camera)
    {
        if (!entity || entity->m_nType != ENTITY_TYPE_OBJECT)
            return;
        if (!entity->bIsVisible || entity->bRemoveFromWorld || entity->bIsProcObject)
            return;
        if (entity->m_nAreaCode != CGame::currArea && entity->m_nAreaCode != 13)
            return;

        auto* object = static_cast<CObject*>(entity);
        if (object->m_nObjectFlags.bIsExploded || object->m_nObjectFlags.bIsBroken)
            return;
        if (AlreadyRegistered(entity) || !HasShadowMesh(entity))
            return;
        if (SurfaceDistance(entity, camera) > kDrawDistance)
            return;

        auto* slot = FirstAvailable();
        if (!slot)
            return;

        CreateShadow(slot, entity, ShadowType::Object);
    }

    void RegisterDynamicObjects(const CVector& camera)
    {
        const int x0 = std::clamp(CWorld::GetSectorIndexX(camera.x - kScanRadius), 0, NUMSECTORS_X - 1);
        const int x1 = std::clamp(CWorld::GetSectorIndexX(camera.x + kScanRadius), 0, NUMSECTORS_X - 1);
        const int y0 = std::clamp(CWorld::GetSectorIndexY(camera.y - kScanRadius), 0, NUMSECTORS_Y - 1);
        const int y1 = std::clamp(CWorld::GetSectorIndexY(camera.y + kScanRadius), 0, NUMSECTORS_Y - 1);

        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                auto* sector = CWorld::GetRepeatSector(x & 15, y & 15);
                for (auto* node = sector->m_lists[REPEATSECTOR_OBJECTS].GetNode(); node; node = node->pNext)
                    TryRegisterObject(static_cast<CEntity*>(node->pItem), camera);
            }
        }
    }

    bool __cdecl RegisterAndExtend(CVector* camera, bool doNotCreateNew)
    {
        const bool registered = gOriginalRegister(camera, doNotCreateNew);
        if (camera && !doNotCreateNew)
            RegisterDynamicObjects(*camera);
        return registered;
    }

    void __cdecl Init()
    {
        RwD3D9SetStencilClear(0);

        while (FirstActive())
            DestroyShadow(FirstActive());

        FirstAvailable() = gPool;
        FirstActive() = nullptr;

        for (int i = 0; i < kPoolSize; ++i)
        {
            auto& slot = gPool[i];
            slot.owner = nullptr;
            slot.numFaces = 0;
            slot.type = static_cast<unsigned char>(ShadowType::None);
            slot.pad = 0;
            slot.faceId = 0;
            slot.faceDataSize = 0;
            slot.faces = nullptr;
            slot.prev = i > 0 ? &gPool[i - 1] : nullptr;
            slot.next = i + 1 < kPoolSize ? &gPool[i + 1] : nullptr;
        }
    }

    void State(RwRenderState state, unsigned value)
    {
        RwRenderStateSet(state, reinterpret_cast<void*>(static_cast<std::uintptr_t>(value)));
    }

    bool ClearStencil()
    {
        auto* device = RwD3D9GetCurrentD3DDevice();
        if (!device)
            return false;

        using Clear_t = long(__stdcall*)(void*, unsigned long, const void*, unsigned long, unsigned long, float, unsigned long);
        auto** vtable = *reinterpret_cast<void***>(device);
        const auto result = reinterpret_cast<Clear_t>(vtable[43])(device, 0, nullptr, 0x4, 0, 1.0f, 0);
        return result >= 0;
    }

    void SetVolumeStates()
    {
        State(rwRENDERSTATEZWRITEENABLE, 0);
        State(rwRENDERSTATESTENCILENABLE, 1);
        State(rwRENDERSTATESHADEMODE, rwSHADEMODEFLAT);
        State(rwRENDERSTATEFOGENABLE, 0);
        State(rwRENDERSTATETEXTURERASTER, 0);
        State(rwRENDERSTATEVERTEXALPHAENABLE, 1);
        State(rwRENDERSTATESRCBLEND, rwBLENDZERO);
        State(rwRENDERSTATEDESTBLEND, rwBLENDONE);
        State(rwRENDERSTATESTENCILFUNCTIONMASK, 0xFFFFFFFFu);
        State(rwRENDERSTATESTENCILFUNCTIONWRITEMASK, 0xFFFFFFFFu);
        State(rwRENDERSTATESTENCILFUNCTION, rwSTENCILFUNCTIONALWAYS);
        State(rwRENDERSTATESTENCILFAIL, rwSTENCILOPERATIONKEEP);
        State(rwRENDERSTATESTENCILZFAIL, rwSTENCILOPERATIONKEEP);
        State(rwRENDERSTATESTENCILPASS, rwSTENCILOPERATIONKEEP);
        State(rwRENDERSTATESTENCILFUNCTIONREF, 0);
        State(rwRENDERSTATEZTESTENABLE, 1);
    }

    void DrawShadowVolume(StencilShadow* shadow)
    {
        auto* savedNext = shadow->next;
        auto* savedPrev = shadow->prev;
        auto* savedHead = FirstActive();
        shadow->next = nullptr;
        shadow->prev = nullptr;
        FirstActive() = shadow;

        SetVolumeStates();
        State(rwRENDERSTATESTENCILZFAIL, rwSTENCILOPERATIONINCR);
        State(rwRENDERSTATECULLMODE, rwCULLMODECULLFRONT);

        CRGBA hidden{ 0, 0, 0, 255 };
        reinterpret_cast<void(__cdecl*)(CRGBA*)>(kRenderVolumes)(&hidden);

        State(rwRENDERSTATESTENCILZFAIL, rwSTENCILOPERATIONDECR);
        State(rwRENDERSTATECULLMODE, rwCULLMODECULLBACK);
        reinterpret_cast<void(__cdecl*)(CRGBA*)>(kRenderVolumes)(&hidden);

        shadow->next = savedNext;
        shadow->prev = savedPrev;
        FirstActive() = savedHead;
    }

    void Darken(const CRGBA& color)
    {
        State(rwRENDERSTATEZWRITEENABLE, 0);
        State(rwRENDERSTATEZTESTENABLE, 0);
        State(rwRENDERSTATESTENCILENABLE, 1);
        State(rwRENDERSTATEFOGENABLE, 0);
        State(rwRENDERSTATEVERTEXALPHAENABLE, 1);
        State(rwRENDERSTATESRCBLEND, rwBLENDSRCALPHA);
        State(rwRENDERSTATEDESTBLEND, rwBLENDINVSRCALPHA);
        State(rwRENDERSTATESHADEMODE, rwSHADEMODEFLAT);
        State(rwRENDERSTATETEXTURERASTER, 0);
        State(rwRENDERSTATECULLMODE, rwCULLMODECULLNONE);
        // Inside the volume the count is non-zero. The original test was
        // <= 1, which also darkened every unmarked pixel.
        State(rwRENDERSTATESTENCILFUNCTIONREF, 0);
        State(rwRENDERSTATESTENCILFUNCTION, rwSTENCILFUNCTIONNOTEQUAL);
        State(rwRENDERSTATESTENCILFAIL, rwSTENCILOPERATIONKEEP);
        State(rwRENDERSTATESTENCILZFAIL, rwSTENCILOPERATIONKEEP);
        State(rwRENDERSTATESTENCILPASS, rwSTENCILOPERATIONKEEP);

        CSprite2d::InitPerFrame();
        CSprite2d::DrawRect(
            CRect{
                0.0f,
                0.0f,
                static_cast<float>(RsGlobal.maximumWidth),
                static_cast<float>(RsGlobal.maximumHeight),
            },
            color);
    }

    void RestoreStates()
    {
        State(rwRENDERSTATESTENCILENABLE, 0);
        State(rwRENDERSTATEZWRITEENABLE, 1);
        State(rwRENDERSTATEZTESTENABLE, 1);
        State(rwRENDERSTATEVERTEXALPHAENABLE, 0);
        State(rwRENDERSTATESHADEMODE, rwSHADEMODEGOURAUD);
        State(rwRENDERSTATECULLMODE, rwCULLMODECULLBACK);
    }

    void __cdecl RenderStencilShadows()
    {
        if (!GraphicsHighQuality())
            return;

        const CVector camera = TheCamera.GetPosition();
        bool cleared = false;

        for (auto* shadow = FirstActive(); shadow; shadow = shadow->next)
        {
            if (!shadow->owner || !shadow->faces || shadow->numFaces <= 0)
                continue;

            const unsigned char alpha = ShadowAlpha(shadow->owner, camera);
            if (alpha == 0)
                continue;

            if (!ClearStencil())
            {
                if (!cleared)
                    OutputDebugStringA("ImprovedShadowsSA: stencil clear failed; shadows skipped.\n");
                break;
            }
            cleared = true;

            DrawShadowVolume(shadow);
            Darken(ShadowColor(alpha));
        }

        RestoreStates();
    }

    bool __cdecl RenderBuffer(const CVector*)
    {
        const int sample = CTimeCycle::m_CurrentStoredValue & 15;
        const float frontX = CTimeCycle::m_fShadowFrontX[sample];
        const float frontY = CTimeCycle::m_fShadowFrontY[sample];
        const float horizontal = std::sqrt(frontX * frontX + frontY * frontY);
        // Keep at least a 0.25 downward component after normalize, so a low
        // sun still reaches the ground without a near-horizontal streak.
        const float downward = std::max(horizontal * 0.26f, 1.0f);
        CVector light{ frontX, frontY, -downward };
        light.Normalize();
        SunDirection() = light;

        // Vertical reach stays about 8 units for cars and 20 for aircraft
        // until the length cap. The cap is what stops an open shadow mesh.
        const float slope = std::max(std::fabs(light.z), 0.25f);
        const float carLength = std::clamp(8.0f / slope, 8.0f, 36.0f);
        const float airLength = std::clamp(20.0f / slope, 40.0f, 80.0f);
        plugin::patch::SetFloat(0x70FB24, airLength);
        plugin::patch::SetFloat(0x70FB2E, carLength);
        return true;
    }

    void* __cdecl WorkBuffer(unsigned, unsigned)
    {
        auto* buffer = gWorkBuffers[gWorkBufferIndex];
        gWorkBufferIndex = (gWorkBufferIndex + 1) % 3;
        return buffer;
    }

    void __cdecl IgnoreFree(void*)
    {
    }

    void Emit(unsigned char*& cursor, std::initializer_list<unsigned char> bytes)
    {
        for (auto byte : bytes)
            *cursor++ = byte;
    }

    void EmitRelative(unsigned char*& cursor, std::uintptr_t target)
    {
        const auto next = reinterpret_cast<std::uintptr_t>(cursor) + 4;
        const auto relative = static_cast<std::int32_t>(target - next);
        std::memcpy(cursor, &relative, 4);
        cursor += 4;
    }

    void EmitJumpEqual(unsigned char*& cursor, unsigned char value, std::uintptr_t target)
    {
        Emit(cursor, { 0x83, 0xF8, value, 0x0F, 0x84 });
        EmitRelative(cursor, target);
    }

    // RenderForVehicle gives the 40-unit extrusion to heli and plane only.
    // Forgotten subclasses (hunter, hydra, and so on) and BMX stay on the
    // short path. These caves add them without moving the length writes.
    void InstallExtrusionCaves()
    {
        alignas(16) static unsigned char cave[128];
        DWORD previous = 0;
        VirtualProtect(cave, sizeof(cave), PAGE_EXECUTE_READWRITE, &previous);

        auto* cursor = cave;
        EmitJumpEqual(cursor, 3, 0x70FB20);
        EmitJumpEqual(cursor, 4, 0x70FB20);
        EmitJumpEqual(cursor, 7, 0x70FB20);
        EmitJumpEqual(cursor, 8, 0x70FB20);
        Emit(cursor, { 0xE9 });
        EmitRelative(cursor, 0x70FB2A);
        plugin::patch::RedirectJump(0x70FB16, cave);
        plugin::patch::Nop(0x70FB1B, 5);

        auto* bike = cursor;
        Emit(bike, { 0x83, 0xBF, 0x90, 0x05, 0x00, 0x00, 0x09, 0x0F, 0x84 });
        EmitRelative(bike, 0x70FB89);
        Emit(bike, { 0x83, 0xBF, 0x90, 0x05, 0x00, 0x00, 0x0A, 0x0F, 0x84 });
        EmitRelative(bike, 0x70FB89);
        Emit(bike, { 0xE9 });
        EmitRelative(bike, 0x70FB9A);
        plugin::patch::RedirectJump(0x70FB80, cursor);
        plugin::patch::Nop(0x70FB85, 4);
        FlushInstructionCache(GetCurrentProcess(), cave, sizeof(cave));
    }

    bool IsFloatMemoryOperand(std::uintptr_t operand)
    {
        const auto* opcode = reinterpret_cast<unsigned char*>(operand - 2);
        return opcode[0] == 0xD8 && (opcode[1] == 0x1D || opcode[1] == 0x25 || opcode[1] == 0x05);
    }

    void RetargetFloat(std::uintptr_t operand, const float* value)
    {
        if (!IsFloatMemoryOperand(operand))
        {
            OutputDebugStringA("ImprovedShadowsSA: distance site did not match; left unchanged.\n");
            return;
        }
        plugin::patch::SetUInt(operand, reinterpret_cast<std::uintptr_t>(value));
    }

    bool ExpectMovImmediate(std::uintptr_t instruction)
    {
        const auto* bytes = reinterpret_cast<unsigned char*>(instruction);
        return bytes[0] == 0xC7 && bytes[1] == 0x44 && bytes[2] == 0x24 && bytes[3] == 0x18;
    }
}

void InstallImprovedStencilShadows()
{
    if (!ExpectMovImmediate(0x70FB20) || !ExpectMovImmediate(0x70FB2A))
    {
        OutputDebugStringA("ImprovedShadowsSA: extrusion sites did not match; patch skipped.\n");
        return;
    }

    plugin::patch::ReplaceFunction(kInit, reinterpret_cast<void*>(Init));
    plugin::patch::ReplaceFunction(kRenderStencilShadows, reinterpret_cast<void*>(RenderStencilShadows));
    plugin::patch::ReplaceFunction(kRenderBuffer, reinterpret_cast<void*>(RenderBuffer));
    Init();

    // Registration compared against the shared 2500 and 50 constants. Retarget
    // only the stencil sites so the rest of the game keeps those values.
    for (auto site : { 0x711827u, 0x71183Bu, 0x7118DCu, 0x711C05u, 0x711D22u })
        RetargetFloat(site, &gDrawDistanceSquared);
    for (auto site : { 0x711943u, 0x711954u, 0x711969u, 0x711977u })
        RetargetFloat(site, &gScanRadius);

    // doNotCreateNew was true for 7 of 8 frames. Always take the full pass.
    plugin::patch::Nop(0x711DC4, 2);
    // Building volumes were rebuilt only when index % 4 matched.
    plugin::patch::Nop(0x711E3D, 2);

    for (auto site : { 0x711DDCu, 0x711DEDu, 0x711DFEu })
        plugin::patch::RedirectCall(site, WorkBuffer);
    for (auto site : { 0x711E65u, 0x711E7Bu, 0x711E91u })
        plugin::patch::RedirectCall(site, IgnoreFree);

    gOriginalRegister = plugin::patch::TranslateCallOffset<Register_t>(kRegisterCall);
    plugin::patch::RedirectCall(kRegisterCall, RegisterAndExtend);

    InstallExtrusionCaves();
}

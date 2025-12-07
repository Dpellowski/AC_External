#include "stdafx.h"
#include <iostream>
#include <windows.h>
#include <vector>
#include <conio.h>
#include <iomanip>
#include <cfloat>
#include <cmath>
#include "proc.h"
#include "overlay.h"

//=============================================================================
// CONSTANTS AND CONFIGURATION
//=============================================================================

// Math constants
namespace MathConstants {
    const float PI = 3.14159265358979323846f;
    const float DEG_TO_RAD = PI / 180.0f;
    const float RAD_TO_DEG = 180.0f / PI;
}

// AssaultCube memory offsets
namespace GameOffsets {
    const uintptr_t LOCAL_PLAYER_OFFSET = 0x18AC00;
    const uintptr_t ENTITY_LIST_OFFSET = 0x18AC04;
    const uintptr_t VIEW_MATRIX_OFFSET = 0x17DFD0;
    const int MAX_ENTITIES = 32;
    const int MAX_HEALTH = 1000;
}

namespace PlayerOffsets {
    const unsigned int ASSAULT_RIFLE_AMMO = 0x140;
    const unsigned int HEALTH = 0xEC;
    const unsigned int PLAYERX = 0x28;
    const unsigned int PLAYERY = 0x2C;
    const unsigned int PLAYERZ = 0x30;
    const unsigned int HEAD_X = 0x4;
    const unsigned int HEAD_Y = 0x8;
    const unsigned int HEAD_Z = 0xC;
    const unsigned int TEAM = 0x30C;
    const unsigned int IS_DEAD = 0x318;
    const unsigned int YAW = 0x34;
    const unsigned int PITCH = 0x38;
}

//=============================================================================
// OPTIMIZED STRUCTURES
//=============================================================================

struct Position {
    float x, y, z;

    Position() : x(0.0f), y(0.0f), z(0.0f) {}
    Position(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 ToVec3() const { return Vec3(x, y, z); }

    // Optimized distance calculation
    float DistanceSquared(const Position& other) const {
        const float dx = x - other.x;
        const float dy = y - other.y;
        const float dz = z - other.z;
        return dx * dx + dy * dy + dz * dz;
    }
};

struct Entity {
    uintptr_t address;
    Position position;
    Position headPosition;
    int health;
    int team;
    bool isDead;
    bool isValid;

    // Cache for performance
    float lastDistanceSquared;
    uint64_t lastUpdateFrame;

    Entity() : 
        address(0), 
        health(0), 
        team(-1), 
        isDead(true), 
        isValid(false),
        lastDistanceSquared(FLT_MAX),
        lastUpdateFrame(0) {}
};

struct AimbotSettings {
    float smoothness;
    float maxDistance;
    float maxDistanceSquared; // Cache squared distance
    float fov;
    bool enableFOVCheck;
    bool enabled;

    AimbotSettings() :
        smoothness(1.0f),
        maxDistance(100.0f),
        maxDistanceSquared(10000.0f),
        fov(60.0f),
        enableFOVCheck(true),
        enabled(false) {}

    void SetMaxDistance(float distance) {
        maxDistance = distance;
        maxDistanceSquared = distance * distance;
    }
};

// Pre-allocated memory pools for performance
struct MemoryPools {
    std::vector<Entity> entityPool;
    std::vector<uintptr_t> addressPool;

    MemoryPools() {
        entityPool.reserve(GameOffsets::MAX_ENTITIES);
        addressPool.reserve(GameOffsets::MAX_ENTITIES);
    }
};

// Optimized GameData with proper initialization
struct GameData {
    float viewMatrix[16]; // Initialize in constructor
    bool viewMatrixValid;

    Position localPlayerPosition;
    float localPlayerYaw;
    float localPlayerPitch;
    int localPlayerTeam;
    int localPlayerHealth;
    bool localPlayerIsDead;

    uintptr_t entityListAddr;
    std::vector<Entity> entities;
    std::vector<uintptr_t> entityPointers;

    // Frame tracking for cache optimization
    uint64_t currentFrame;

    GameData() : 
        viewMatrixValid(false),
        localPlayerYaw(0.0f),
        localPlayerPitch(0.0f),
        localPlayerTeam(-1),
        localPlayerHealth(0),
        localPlayerIsDead(false),
        entityListAddr(0),
        currentFrame(0) {
        // Initialize view matrix
        memset(viewMatrix, 0, sizeof(viewMatrix));
    }

    void Clear() {
        memset(viewMatrix, 0, sizeof(viewMatrix));
        viewMatrixValid = false;
        localPlayerPosition = Position();
        localPlayerYaw = 0.0f;
        localPlayerPitch = 0.0f;
        localPlayerTeam = -1;
        localPlayerHealth = 0;
        localPlayerIsDead = false;
        entityListAddr = 0;
        entities.clear();
        entityPointers.clear();
        ++currentFrame;
    }
};

//=============================================================================
// HIGH-PERFORMANCE HACK CLASS
//=============================================================================

class AssaultCubeHack {
private:
    HANDLE hProcess;
    DWORD processId;
    uintptr_t moduleBase;
    uintptr_t localPlayerAddr;
    GameOverlay overlay;
    HWND gameWindowHandle;

    GameData gameData;
    AimbotSettings aimbotSettings;
    Entity* lockedTarget;

    MemoryPools memPools;

    // Performance monitoring
    uint64_t totalFrames;
    LARGE_INTEGER perfFreq, lastPerfTime;

    // Cached addresses for frequently accessed memory
    struct CachedAddresses {
        uintptr_t playerX, playerY, playerZ;
        uintptr_t playerYaw, playerPitch;
        uintptr_t playerHealth, playerTeam, playerDead;
        bool valid;

        CachedAddresses() : 
            playerX(0), playerY(0), playerZ(0),
            playerYaw(0), playerPitch(0),
            playerHealth(0), playerTeam(0), playerDead(0),
            valid(false) {}
    } cachedAddrs;

public:
    AssaultCubeHack() :
        hProcess(nullptr),
        processId(0),
        moduleBase(0),
        localPlayerAddr(0),
        gameWindowHandle(nullptr),
        lockedTarget(nullptr),
        totalFrames(0) {

        QueryPerformanceFrequency(&perfFreq);
        QueryPerformanceCounter(&lastPerfTime);
    }

    ~AssaultCubeHack() {
        if (hProcess) {
            CloseHandle(hProcess);
        }
    }

    //=========================================================================
    // FAST INITIALIZATION
    //=========================================================================

    bool Initialize() {
        if (!InitializeProcess()) return false;
        if (!InitializeOverlay()) return false;

        // Pre-cache frequently used addresses
        CachePlayerAddresses();

        std::cout << "Successfully initialized AssaultCube hack!" << std::endl;
        return true;
    }

private:
    void CachePlayerAddresses() {
        // ✅ FIXED: Don't cache addresses during initialization
        // We'll cache them in ReadLocalPlayerDataFast() after we get the real player pointer
        cachedAddrs.valid = false;
    }

    bool InitializeProcess() {
        processId = GetProcId(L"ac_client.exe");
        if (processId == 0) {
            std::cout << "Failed to find AssaultCube process!" << std::endl;
            return false;
        }

        moduleBase = GetModuleBaseAddress(processId, L"ac_client.exe");
        if (moduleBase == 0) {
            std::cout << "Failed to get module base address!" << std::endl;
            return false;
        }

        hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
            FALSE, processId);
        if (!hProcess) {
            std::cout << "Failed to open process!" << std::endl;
            return false;
        }

        // ✅ FIXED: Don't set localPlayerAddr here - it will be set in ReadLocalPlayerDataFast()
        localPlayerAddr = 0;  // Will be updated when we read the actual pointer
        return true;
    }

    bool InitializeOverlay() {
        gameWindowHandle = FindWindow(nullptr, L"AssaultCube");
        if (!gameWindowHandle) {
            std::cout << "Failed to find AssaultCube window!" << std::endl;
            return false;
        }

        if (!overlay.Initialize(gameWindowHandle)) {
            std::cout << "Failed to initialize overlay!" << std::endl;
            return false;
        }
        return true;
    }

    //=========================================================================
    // ULTRA-FAST MEMORY READING
    //=========================================================================

public:
    bool ReadAllGameData() {
        gameData.Clear();

        if (!hProcess || !moduleBase) {
            return false;
        }

        // Always try to read data - the functions will handle invalid states internally
        ReadViewMatrixFast();
        ReadLocalPlayerDataFast();  // This will set cachedAddrs.valid if successful

        // Only read entities if we have valid player data
        if (cachedAddrs.valid) {
            ReadAllEntityDataFast();
        }

        return cachedAddrs.valid;  // Return true only if we successfully read player data
    }

private:
    void ReadViewMatrixFast() {
        const uintptr_t viewMatrixAddr = moduleBase + GameOffsets::VIEW_MATRIX_OFFSET;
        SIZE_T bytesRead = 0;
        gameData.viewMatrixValid = ReadProcessMemory(hProcess,
            reinterpret_cast<LPCVOID>(viewMatrixAddr),
            gameData.viewMatrix, sizeof(gameData.viewMatrix), &bytesRead) &&
            (bytesRead == sizeof(gameData.viewMatrix));
    }

    void ReadLocalPlayerDataFast() {
        uintptr_t actualPlayerAddr = 0;
        SIZE_T bytesRead = 0;
        
        uintptr_t playerPtrAddress = moduleBase + GameOffsets::LOCAL_PLAYER_OFFSET;
        
        if (ReadProcessMemory(hProcess, 
            reinterpret_cast<LPCVOID>(playerPtrAddress),
            &actualPlayerAddr, sizeof(actualPlayerAddr), &bytesRead)) {
            
            if (actualPlayerAddr > 0x10000) {
                if (localPlayerAddr != actualPlayerAddr) {
                    localPlayerAddr = actualPlayerAddr;
                    UpdateCachedAddresses(actualPlayerAddr);
                }
                
                // Read position using correct addresses
                float x = ReadFloatDirect(cachedAddrs.playerX);
                float y = ReadFloatDirect(cachedAddrs.playerY);
                float z = ReadFloatDirect(cachedAddrs.playerZ);
                
                gameData.localPlayerPosition.x = x;
                gameData.localPlayerPosition.y = y;
                gameData.localPlayerPosition.z = z;
                
                gameData.localPlayerHealth = ReadIntDirect(cachedAddrs.playerHealth);
                gameData.localPlayerTeam = ReadIntDirect(cachedAddrs.playerTeam);
                gameData.localPlayerIsDead = ReadIntDirect(cachedAddrs.playerDead) != 0;
                gameData.localPlayerYaw = ReadFloatDirect(cachedAddrs.playerYaw);
                gameData.localPlayerPitch = ReadFloatDirect(cachedAddrs.playerPitch);
                
                cachedAddrs.valid = true;
            } else {
                gameData.localPlayerPosition = Position(0.0f, 0.0f, 0.0f);
                cachedAddrs.valid = false;
            }
        } else {
            gameData.localPlayerPosition = Position(0.0f, 0.0f, 0.0f);
            cachedAddrs.valid = false;
        }
    }

    // ✅ Add this function to update cached addresses
    void UpdateCachedAddresses(uintptr_t playerAddr) {
        cachedAddrs.playerX = playerAddr + PlayerOffsets::PLAYERX;
        cachedAddrs.playerY = playerAddr + PlayerOffsets::PLAYERY;
        cachedAddrs.playerZ = playerAddr + PlayerOffsets::PLAYERZ;
        cachedAddrs.playerYaw = playerAddr + PlayerOffsets::YAW;
        cachedAddrs.playerPitch = playerAddr + PlayerOffsets::PITCH;
        cachedAddrs.playerHealth = playerAddr + PlayerOffsets::HEALTH;
        cachedAddrs.playerTeam = playerAddr + PlayerOffsets::TEAM;
        cachedAddrs.playerDead = playerAddr + PlayerOffsets::IS_DEAD;
        cachedAddrs.valid = true;
    }

    void ReadAllEntityDataFast() {
        const uintptr_t entityListAddr = moduleBase + GameOffsets::ENTITY_LIST_OFFSET;
        if (!ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityListAddr),
            &gameData.entityListAddr, sizeof(gameData.entityListAddr), nullptr) ||
            gameData.entityListAddr == 0) {
            return;
        }

        // Batch read all entity pointers at once
        memPools.addressPool.resize(GameOffsets::MAX_ENTITIES);
        const SIZE_T totalSize = GameOffsets::MAX_ENTITIES * sizeof(uintptr_t);
        SIZE_T bytesRead = 0;

        if (!ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(gameData.entityListAddr),
            memPools.addressPool.data(), totalSize, &bytesRead)) {
            return;
        }

        // Process entities efficiently
        gameData.entities.clear();
        gameData.entities.reserve(GameOffsets::MAX_ENTITIES);

        // Add local player first
        AddLocalPlayerEntity();

        // Process other entities - C++14 compatible version
        for (int i = 1; i < GameOffsets::MAX_ENTITIES; ++i) {
            const uintptr_t entityPtr = memPools.addressPool[i];
            if (entityPtr == 0 || entityPtr < 0x10000) continue;

            Entity entity = ReadEntityFast(entityPtr);
            if (entity.isValid) {
                entity.lastUpdateFrame = gameData.currentFrame;
                gameData.entities.push_back(entity);
            }
        }
    }

    void AddLocalPlayerEntity() {
        Entity localPlayer;
        localPlayer.address = localPlayerAddr;
        localPlayer.position = gameData.localPlayerPosition;
        localPlayer.health = gameData.localPlayerHealth;
        localPlayer.team = gameData.localPlayerTeam;
        localPlayer.isDead = gameData.localPlayerIsDead;
        localPlayer.isValid = true;
        localPlayer.headPosition = ReadHeadPositionFast(localPlayerAddr, true);
        localPlayer.lastUpdateFrame = gameData.currentFrame;
        gameData.entities.push_back(localPlayer);
    }

    Entity ReadEntityFast(uintptr_t entityPtr) {
        Entity entity;
        entity.address = entityPtr;

        // Read all data in minimal memory operations
        struct EntityData {
            float x, y, z;
            int health, team, isDead;
            float headX, headY, headZ;

            EntityData() : x(0), y(0), z(0), health(0), team(0), isDead(0), headX(0), headY(0), headZ(0) {}
        };

        EntityData data;

        // Try direct read first (most common case)
        bool success =
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::PLAYERX),
                &data.x, sizeof(float), nullptr) &&
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::PLAYERY),
                &data.y, sizeof(float), nullptr) &&
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::PLAYERZ),
                &data.z, sizeof(float), nullptr) &&
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::HEALTH),
                &data.health, sizeof(int), nullptr) &&
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::TEAM),
                &data.team, sizeof(int), nullptr) &&
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::IS_DEAD),
                &data.isDead, sizeof(int), nullptr) &&
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::HEAD_X),
                &data.headX, sizeof(float), nullptr) &&
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::HEAD_Y),
                &data.headY, sizeof(float), nullptr) &&
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::HEAD_Z),
                &data.headZ, sizeof(float), nullptr);

        if (success) {
            entity.position = Position(data.x, data.y, data.z);
            entity.headPosition = Position(data.headX, data.headY, data.headZ);
            entity.health = data.health;
            entity.team = data.team;
            entity.isDead = data.isDead != 0;

            // Fast validation with early exit
            entity.isValid = ValidateEntityFast(entity);
        }

        return entity;
    }

    bool ValidateEntityFast(const Entity& entity) const {
        return entity.health > 0 && entity.health <= GameOffsets::MAX_HEALTH &&
            entity.team >= 0 && entity.team <= 2 &&
            !entity.isDead &&
            abs(entity.position.x) < 10000 &&
            abs(entity.position.y) < 10000 &&
            abs(entity.position.z) < 1000;
    }

    Position ReadHeadPositionFast(uintptr_t entityPtr, bool isLocalPlayer) {
        Position headPos;

        if (isLocalPlayer) {
            // Use cached addresses for local player
            headPos.x = ReadFloatDirect(entityPtr + PlayerOffsets::HEAD_X);
            headPos.y = ReadFloatDirect(entityPtr + PlayerOffsets::HEAD_Y);
            headPos.z = ReadFloatDirect(entityPtr + PlayerOffsets::HEAD_Z);
        }
        else {
            // Direct read for other entities
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::HEAD_X),
                &headPos.x, sizeof(float), nullptr);
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::HEAD_Y),
                &headPos.y, sizeof(float), nullptr);
            ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(entityPtr + PlayerOffsets::HEAD_Z),
                &headPos.z, sizeof(float), nullptr);
        }

        return headPos;
    }

    // Optimized memory readers
    int ReadIntDirect(uintptr_t address) const {
        int value = 0;
        ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address), &value, sizeof(int), nullptr);
        return value;
    }

    float ReadFloatDirect(uintptr_t address) const {
        float value = 0.0f;
        ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address), &value, sizeof(float), nullptr);
        return value;
    }

    //=========================================================================
    // OPTIMIZED AIMBOT FUNCTIONS
    //=========================================================================

public:
    void ToggleAimbot() {
        aimbotSettings.enabled = !aimbotSettings.enabled;
        if (!aimbotSettings.enabled) {
            lockedTarget = nullptr;
        }
        std::cout << "Aimbot " << (aimbotSettings.enabled ? "ENABLED" : "DISABLED") << std::endl;
    }

    void SetAimbotSettings(float smoothness, float maxDistance, float fov = 60.0f) {
        aimbotSettings.smoothness = smoothness;
        aimbotSettings.SetMaxDistance(maxDistance);
        aimbotSettings.fov = fov;
    }

    void LockCameraToTargetFast() {
        if (!aimbotSettings.enabled || !hProcess || !moduleBase) {
            return;
        }

        Entity* bestTarget = FindBestTargetFast();
        if (bestTarget && IsValidTargetForAimingFast(*bestTarget)) {
            lockedTarget = bestTarget;
            AimAtTargetFast(*bestTarget);
        }
        else {
            lockedTarget = nullptr;
        }
    }

private:
    Entity* FindBestTargetFast() {
        Entity* bestTarget = nullptr;
        float closestDistanceSquared = aimbotSettings.maxDistanceSquared;

        for (auto& entity : gameData.entities) {
            if (entity.address == localPlayerAddr) continue;

            // ✅ FIXED: Calculate distance first, THEN validate
            const Position& targetPos = entity.headPosition.x != 0 ?
                entity.headPosition : entity.position;
            float distanceSquared = gameData.localPlayerPosition.DistanceSquared(targetPos);
            float distance = sqrt(distanceSquared);
            
            // Update cache
            entity.lastDistanceSquared = distanceSquared;
            entity.lastUpdateFrame = gameData.currentFrame;

            // Debug output (optional)
            // std::cout << "Entity Health=" << entity.health << " Team=" << entity.team 
            //           << " Distance=" << distance << " MaxDist=" << aimbotSettings.maxDistance << std::endl;

            // Validate
            if (!entity.isValid || entity.isDead || entity.health <= 0 || 
                entity.team == gameData.localPlayerTeam || 
                distanceSquared > aimbotSettings.maxDistanceSquared ||
                distanceSquared < 1.0f) {
                continue;
            }

            if (distanceSquared < closestDistanceSquared) {
                closestDistanceSquared = distanceSquared;
                bestTarget = &entity;
            }
        }

        return bestTarget;
    }

    bool IsValidTargetCandidateFast(const Entity& entity) const {
        return entity.isValid &&
            !entity.isDead &&
            entity.health > 0 &&
            entity.team != gameData.localPlayerTeam &&
            entity.lastDistanceSquared <= aimbotSettings.maxDistanceSquared;
    }

    bool IsValidTargetForAimingFast(const Entity& target) const {
        return IsValidTargetCandidateFast(target) &&
            target.lastDistanceSquared > 1.0f; // Not too close
    }

    void AimAtTargetFast(const Entity& target) {
        if (!gameData.viewMatrixValid) return;

        const Position& targetHeadPos = target.headPosition.x != 0 ?
            target.headPosition : target.position;

        // Get window dimensions
        RECT gameRect;
        GetWindowRect(gameWindowHandle, &gameRect);
        const int windowWidth = gameRect.right - gameRect.left;
        const int windowHeight = gameRect.bottom - gameRect.top;

        Vec2 targetScreenPos;
        if (!overlay.WorldToScreen(targetHeadPos.ToVec3(), targetScreenPos,
            gameData.viewMatrix, windowWidth, windowHeight)) {
            return;
        }

        // Calculate screen center and deltas
        const float screenCenterX = windowWidth * 0.5f;
        const float screenCenterY = windowHeight * 0.5f;
        const float deltaScreenX = targetScreenPos.x - screenCenterX;
        const float deltaScreenY = targetScreenPos.y - screenCenterY;

        // Convert to angle adjustments
        const float mouseSensitivity = 0.022f;
        const float yawAdjustment = deltaScreenX * mouseSensitivity;
        const float pitchAdjustment = -deltaScreenY * mouseSensitivity;

        const float targetYaw = gameData.localPlayerYaw + yawAdjustment;
        const float targetPitch = gameData.localPlayerPitch + pitchAdjustment;

        SetCameraAnglesFast(targetYaw, targetPitch);
    }

    void SetCameraAnglesFast(float yaw, float pitch) {
        // Clamp angles
        while (yaw > 180.0f) yaw -= 360.0f;
        while (yaw < -180.0f) yaw += 360.0f;
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        // Write angles directly using cached addresses
        WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(cachedAddrs.playerYaw),
            &yaw, sizeof(yaw), nullptr);
        WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(cachedAddrs.playerPitch),
            &pitch, sizeof(pitch), nullptr);
    }

    //=========================================================================
    // OPTIMIZED MEMORY MODIFICATION
    //=========================================================================

public:
    void ModifyAmmo() {
        if (cachedAddrs.valid && localPlayerAddr != 0) {
            const int newAmmo = 999;
            const uintptr_t ammoAddr = localPlayerAddr + PlayerOffsets::ASSAULT_RIFLE_AMMO;
            WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(ammoAddr),
                &newAmmo, sizeof(newAmmo), nullptr);
        }
    }

    void ModifyHealth() {
        if (cachedAddrs.valid && cachedAddrs.playerHealth != 0) {
            const int newHealth = 100;  // Use 100 instead of 999 for more realistic value
            SIZE_T bytesWritten = 0;
            bool success = WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(cachedAddrs.playerHealth),
                &newHealth, sizeof(newHealth), &bytesWritten);

            if (!success) {
                std::cout << "Failed to modify health! Error: " << GetLastError() << std::endl;
            }
        }
    }

    //=========================================================================
    // OPTIMIZED RENDERING
    //=========================================================================

    void RenderEntities() {
        if (!gameData.viewMatrixValid) return;

        overlay.UpdateOverlay();
        overlay.BeginRender();

        for (const auto& entity : gameData.entities) {
            if (entity.address != localPlayerAddr && entity.isValid) {
                overlay.DrawEntityBox(entity.position.ToVec3(),
                    entity.headPosition.ToVec3(),
                    entity.health, entity.isValid,
                    gameData.viewMatrix);
            }
        }

        overlay.EndRender();
    }

    void EnumerateEntities() {
        int validEntityCount = 0;

        std::cout << "Entity[0]: Local Player | Health: " << gameData.localPlayerHealth
            << " | Pos: (" << std::fixed << std::setprecision(2)
            << gameData.localPlayerPosition.x << ", "
            << gameData.localPlayerPosition.y << ", "
            << gameData.localPlayerPosition.z << ")" << std::endl;

        for (size_t i = 0; i < gameData.entities.size(); i++) {
            const auto& entity = gameData.entities[i];
            if (entity.address != localPlayerAddr) {
                std::cout << "Entity[" << i + 1 << "]: 0x" << std::hex << entity.address
                    << " | Health: " << std::dec << entity.health
                    << " | Pos: (" << std::fixed << std::setprecision(2)
                    << entity.position.x << ", " << entity.position.y << ", "
                    << entity.position.z << ")"
                    << (entity.isValid ? " (Valid)" : " (Invalid)") << std::endl;
                if (entity.isValid) validEntityCount++;
            }
        }

        std::cout << "Found " << validEntityCount << " valid entities" << std::endl;
    }

    void PrintTargetInfo() {
        if (aimbotSettings.enabled) {
            std::cout << "Aimbot Status: ENABLED ";
            if (lockedTarget) {
                std::cout << "| TARGET LOCKED";
            }
            else {
                std::cout << "| SEARCHING...";
            }
            std::cout << " | Max Distance: " << aimbotSettings.maxDistance << std::endl;
        }
    }
};

//=============================================================================
// OPTIMIZED MAIN FUNCTION
//=============================================================================

int main() {
    // Set high priority for better performance
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    AssaultCubeHack hack;

    if (!hack.Initialize()) {
        std::cout << "Failed to initialize hack!" << std::endl;
        std::cin.get();
        return -1;
    }

    // Apply initial modifications
    hack.ModifyAmmo();
    hack.ModifyHealth();
    hack.SetAimbotSettings(1.0f, 150.0f, 60.0f);

    std::cout << "\nStarting entity visualization..." << std::endl;
    std::cout << "Press 'q' to quit, 'F' to toggle aimbot" << std::endl;

    // High-performance main loop
    const int DISPLAY_UPDATE_INTERVAL = 15; // Update display every 15 frames
    const int RENDER_UPDATE_INTERVAL = 3;   // Update render every 3 frames

    int frameCount = 0;

    while (true) {
        // Ultra-fast memory reading test
        if (!hack.ReadAllGameData()) {
            Sleep(1);
            continue;
        }

        // Process Windows messages (minimal)
        MSG msg;
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // High-priority aimbot execution
        hack.LockCameraToTargetFast();

        // Lower priority updates
        if (++frameCount >= DISPLAY_UPDATE_INTERVAL) {
            frameCount = 0;

            system("cls");
            std::cout << "=== AssaultCube Entity Monitor [OPTIMIZED] ===" << std::endl;
            std::cout << "Press 'q' to quit | 'F' to toggle aimbot" << std::endl;

            hack.EnumerateEntities();
            hack.PrintTargetInfo();
            std::cout << "\n[Press 'q' to quit, 'F' to toggle aimbot] extra" << std::endl;
        }

        // Render updates
        if (frameCount % RENDER_UPDATE_INTERVAL == 0) {
            hack.RenderEntities();
        }

        // Handle input
        if (_kbhit()) {
            const char key = _getch();
            if (key == 'q' || key == 'Q') {
                break;
            }
            if (key == 'f' || key == 'F') {
                hack.ToggleAimbot();
            }
        }

        // Minimal delay for maximum performance
        Sleep(1);
    }

    std::cout << "\nExiting..." << std::endl;
    return 0;
}
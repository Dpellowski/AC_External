#include "stdafx.h"
#include "overlay.h"
#include <cmath>

GameOverlay::GameOverlay() : gameWindow(nullptr), overlayWindow(nullptr),
hInstance(GetModuleHandle(nullptr)), overlayDC(nullptr) {

    memset(&gameRect, 0, sizeof(RECT));
}

GameOverlay::~GameOverlay() {
    Shutdown();
}

bool GameOverlay::Initialize(HWND gameWnd) {
    gameWindow = gameWnd;

    if (!gameWindow) {
        return false;
    }

    // Register window class for overlay
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = OverlayProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = L"AC_Overlay";
    wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);

    if (!RegisterClassEx(&wcex)) {
        return false;
    }

    GetWindowRect(gameWindow, &gameRect);

    // Create transparent overlay window
    overlayWindow = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        L"AC_Overlay",
        L"Overlay",
        WS_POPUP | WS_VISIBLE,
        gameRect.left, gameRect.top,
        gameRect.right - gameRect.left,
        gameRect.bottom - gameRect.top,
        nullptr, nullptr, hInstance, this
    );

    if (!overlayWindow) {
        return false;
    }

    SetLayeredWindowAttributes(overlayWindow, RGB(0, 0, 0), 0, LWA_COLORKEY);
    overlayDC = GetDC(overlayWindow);

    return true;
}

void GameOverlay::Shutdown() {
    if (overlayDC) {
        ReleaseDC(overlayWindow, overlayDC);
        overlayDC = nullptr;
    }

    if (overlayWindow) {
        DestroyWindow(overlayWindow);
        overlayWindow = nullptr;
    }

    UnregisterClass(L"AC_Overlay", hInstance);
}

void GameOverlay::UpdateOverlay() {
    UpdateWindowPosition();
    // Remove ReadViewMatrix and ReadGameDimensions calls
}

void GameOverlay::UpdateWindowPosition() {
    RECT newGameRect;
    GetWindowRect(gameWindow, &newGameRect);

    if (memcmp(&gameRect, &newGameRect, sizeof(RECT)) != 0) {
        gameRect = newGameRect;
        SetWindowPos(overlayWindow, HWND_TOPMOST,
            gameRect.left, gameRect.top,
            gameRect.right - gameRect.left,
            gameRect.bottom - gameRect.top,
            SWP_NOACTIVATE);
    }
}

bool GameOverlay::WorldToScreen(const Vec3& worldPos, Vec2& screenPos, float matrix[16], int windowWidth, int windowHeight) {
    if (windowWidth <= 0 || windowHeight <= 0) {
        return false;
    }

    // Transform world position to clip coordinates using view-projection matrix
    Vec4 clipCoords;
    clipCoords.x = worldPos.x * matrix[0] + worldPos.y * matrix[4] + worldPos.z * matrix[8] + matrix[12];
    clipCoords.y = worldPos.x * matrix[1] + worldPos.y * matrix[5] + worldPos.z * matrix[9] + matrix[13];
    clipCoords.z = worldPos.x * matrix[2] + worldPos.y * matrix[6] + worldPos.z * matrix[10] + matrix[14];
    clipCoords.w = worldPos.x * matrix[3] + worldPos.y * matrix[7] + worldPos.z * matrix[11] + matrix[15];

    // Check if point is behind camera
    if (clipCoords.w < 0.1f) {
        return false;
    }

    // Convert to normalized device coordinates
    Vec3 NDC;
    NDC.x = clipCoords.x / clipCoords.w;
    NDC.y = clipCoords.y / clipCoords.w;
    NDC.z = clipCoords.z / clipCoords.w;

    // Convert to screen coordinates
    screenPos.x = (windowWidth / 2.0f * NDC.x) + (windowWidth / 2.0f);
    screenPos.y = -(windowHeight / 2.0f * NDC.y) + (windowHeight / 2.0f);

    // Check if within screen bounds
    return (screenPos.x >= 0 && screenPos.x <= windowWidth &&
        screenPos.y >= 0 && screenPos.y <= windowHeight);
}

// NEW: Updated method to draw box around both feet and head positions
void GameOverlay::DrawEntityBox(const Vec3& feetPos, const Vec3& headPos, int health, bool isValid, float viewMatrix[16]) {
    int windowWidth = gameRect.right - gameRect.left;
    int windowHeight = gameRect.bottom - gameRect.top;

    // Convert both feet and head positions to screen coordinates
    Vec2 feetScreenPos;
    Vec2 headScreenPos;

    bool feetOnScreen = WorldToScreen(feetPos, feetScreenPos, viewMatrix, windowWidth, windowHeight);
    bool headOnScreen = WorldToScreen(headPos, headScreenPos, viewMatrix, windowWidth, windowHeight);

    // If neither feet nor head are on screen, don't draw anything
    if (!feetOnScreen && !headOnScreen) {
        return;
    }

    // Use head position if it's valid, otherwise fall back to feet position
    Vec3 targetHeadPos = headPos;
    if (headPos.x == 0 && headPos.y == 0 && headPos.z == 0) {
        targetHeadPos = feetPos;
        targetHeadPos.z += 1.8f; // Estimate head height (approximately 1.8 units above feet)
        headOnScreen = WorldToScreen(targetHeadPos, headScreenPos, viewMatrix, windowWidth, windowHeight);
    }

    // Calculate bounding box dimensions based on both positions
    float minX, maxX, minY, maxY;
    
    if (feetOnScreen && headOnScreen) {
        // Both positions are visible - create box around both
        minX = min(feetScreenPos.x, headScreenPos.x);
        maxX = max(feetScreenPos.x, headScreenPos.x);
        minY = min(feetScreenPos.y, headScreenPos.y);
        maxY = max(feetScreenPos.y, headScreenPos.y);
    } else if (feetOnScreen) {
        // Only feet visible - estimate head position
        minX = feetScreenPos.x;
        maxX = feetScreenPos.x;
        minY = feetScreenPos.y - 80; // Estimate head above feet
        maxY = feetScreenPos.y;
    } else {
        // Only head visible - estimate feet position
        minX = headScreenPos.x;
        maxX = headScreenPos.x;
        minY = headScreenPos.y;
        maxY = headScreenPos.y + 80; // Estimate feet below head
    }

    // Add some padding and ensure minimum box width
    const float padding = 20.0f;
    const float minWidth = 40.0f;
    
    float boxWidth = max(maxX - minX + padding * 2, minWidth);
    float boxHeight = maxY - minY + padding;
    
    // Center the box
    float centerX = (minX + maxX) * 0.5f;
    float centerY = (minY + maxY) * 0.5f;
    
    int x = (int)(centerX - boxWidth * 0.5f);
    int y = (int)(centerY - boxHeight * 0.5f);

    // Choose box color based on validity
    COLORREF boxColor = isValid ? RGB(0, 255, 0) : RGB(255, 0, 0);
    
    // Draw the bounding box
    DrawBox(x, y, (int)boxWidth, (int)boxHeight, boxColor);

    // Draw health text above the box
    char healthText[32];
    sprintf_s(healthText, "HP: %d", health);
    DrawText(x, y - 25, healthText, RGB(255, 255, 255));

    // Optional: Draw markers for feet and head positions
    if (feetOnScreen) {
        // Draw small marker at feet position
        DrawBox((int)feetScreenPos.x - 2, (int)feetScreenPos.y - 2, 4, 4, RGB(255, 255, 0)); // Yellow for feet
    }
    
    if (headOnScreen) {
        // Draw small marker at head position  
        DrawBox((int)headScreenPos.x - 2, (int)headScreenPos.y - 2, 4, 4, RGB(0, 255, 255)); // Cyan for head
    }
}

void GameOverlay::BeginRender() {
    RECT clientRect;
    GetClientRect(overlayWindow, &clientRect);

    HBRUSH transparentBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(overlayDC, &clientRect, transparentBrush);
    DeleteObject(transparentBrush);
}

void GameOverlay::EndRender() {
    InvalidateRect(overlayWindow, nullptr, FALSE);
}

void GameOverlay::DrawBox(int x, int y, int width, int height, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HPEN oldPen = (HPEN)SelectObject(overlayDC, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(overlayDC, GetStockObject(NULL_BRUSH));

    Rectangle(overlayDC, x, y, x + width, y + height);

    SelectObject(overlayDC, oldBrush);
    SelectObject(overlayDC, oldPen);
    DeleteObject(pen);
}

void GameOverlay::DrawText(int x, int y, const char* text, COLORREF color) {
    SetTextColor(overlayDC, color);
    SetBkMode(overlayDC, TRANSPARENT);

    wchar_t wideText[256];
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wideText, 256);
    TextOut(overlayDC, x, y, wideText, (int)wcslen(wideText));
}

LRESULT CALLBACK GameOverlay::OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}
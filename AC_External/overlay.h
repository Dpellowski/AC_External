#pragma once  
#include <windows.h>  

// Vector structures for 3D calculations  
struct Vec2 {  
    float x, y;  
    Vec2() : x(0), y(0) {}  
    Vec2(float x, float y) : x(x), y(y) {}  
};  

struct Vec3 {  
    float x, y, z;  
    Vec3() : x(0), y(0), z(0) {}  
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}  
};  

struct Vec4 {  
    float x, y, z, w;  
    Vec4() : x(0), y(0), z(0), w(0) {}  
    Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}  
};  

// Game overlay for rendering entity boxes on top of the game window  
class GameOverlay {  
private:  
    // Window handles and graphics context  
    HWND gameWindow;  
    HWND overlayWindow;  
    HINSTANCE hInstance;  
    HDC overlayDC;  
    RECT gameRect;  

    // Game dimensions and view matrix  
    int gameWidth = 0;  
    int gameHeight = 0;  

public:  
    GameOverlay();  
    ~GameOverlay();  

    // Initialization and cleanup  
    bool Initialize(HWND gameWnd);  
    void Shutdown();  

    // Rendering functions  
    void UpdateOverlay();  
    void BeginRender();  
    void EndRender();  
    
    // Updated function to draw box around both feet and head
    void DrawEntityBox(const Vec3& feetPos, const Vec3& headPos, int health, bool isValid, float viewMatrix[16]);

    // Make WorldToScreen public to resolve access issues  
    bool WorldToScreen(const Vec3& worldPos, Vec2& screenPos, float matrix[16], int windowWidth, int windowHeight);  

private:  
    // Window management  
    static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);  
    void UpdateWindowPosition();  

    // 3D math and rendering helpers  
    void DrawBox(int x, int y, int width, int height, COLORREF color);  
    void DrawText(int x, int y, const char* text, COLORREF color);  
};
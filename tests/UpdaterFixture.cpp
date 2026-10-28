#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
#else
int main(int argc, char **) {
    const bool arguments = argc > 1;
#endif
#ifdef EXIT_EARLY
    return 1;
#else
#ifdef _WIN32
    if (arguments && *arguments)
        return 0;
    WNDCLASSW windowClass{};
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"FARecorderUpdaterFixture";
    windowClass.lpfnWndProc = [](HWND window, UINT message, WPARAM wparam,
                                 LPARAM lparam) -> LRESULT {
        if (message == WM_TIMER) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    };
    RegisterClassW(&windowClass);
    const auto window = CreateWindowW(windowClass.lpszClassName, L"Updater fixture", 0, 0, 0, 100,
                                      100, nullptr, nullptr, windowClass.hInstance, nullptr);
    SetTimer(window, 1, 6500, nullptr);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DestroyWindow(window);
    return 0;
#else
    if (arguments)
        return 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(6500));
    return 0;
#endif
#endif
}

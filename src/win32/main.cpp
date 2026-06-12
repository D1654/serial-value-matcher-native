#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>

#include "win32/main_window.h"
#include "win32/ui_text.h"

namespace {

bool hasSelfTestArgument() {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        return false;
    }
    bool found = false;
    for (int index = 1; index < argumentCount; ++index) {
        if (lstrcmpiW(arguments[index], L"--self-test") == 0) {
            found = true;
            break;
        }
    }
    LocalFree(arguments);
    return found;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow) {
    if (hasSelfTestArgument()) {
        return svm::win32::NativeMainWindow::runSelfTest() ? 0 : 2;
    }

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    svm::win32::NativeMainWindow window;
    if (!window.create(instance)) {
        MessageBoxW(
            nullptr,
            svm::win32::uiText(svm::win32::TextId::CreateWindowError),
            svm::win32::uiText(svm::win32::TextId::SelfTestText),
            MB_ICONERROR | MB_OK);
        return 1;
    }

    window.show(commandShow);
    return window.runMessageLoop();
}

#endif

#include "App.h"

#ifdef __SWITCH__
#include <switch.h>
#include <cstdio>
#endif

#ifdef __SWITCH__
namespace {
void ShowFatalError(const std::string& message)
{
    consoleInit(nullptr);
    std::printf("\n EhViewer Switch startup failed\n\n %s\n\n Press + to exit.\n",
                message.c_str());
    consoleUpdate(nullptr);
    PadState pad;
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&pad);
    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}
}
#endif

int main(int argc, char** argv)
{
    App app;
    if (argc > 0 && argv[0] != nullptr)
        app.SetSelfPath(argv[0]);
    const bool initialized = app.Init();
    if (initialized)
        app.Run();
    else
    {
#ifdef __SWITCH__
        ShowFatalError("SDL2 UI initialization failed");
#endif
    }
    app.Uninit();
    return initialized ? 0 : 1;
}

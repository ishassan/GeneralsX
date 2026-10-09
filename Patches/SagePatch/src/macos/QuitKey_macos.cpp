// GeneralsX @feature ishassan 09/10/2026 Cmd+Q does not quit the game. It is next
// to Cmd+1 (the control groups), so it is easy to press by mistake, and the game
// then quits at once and the progress since the last save is lost. The Quit item
// of the app menu, Quit in the Dock and the game's own Exit still quit.
//
// SDL makes the app menu with Cocoa when it opens the video. Its Quit item has the
// key Cmd+Q, which macOS handles before SDL sees the key. removeQuitKey() takes the
// key off that item (through the Objective-C runtime, so this file stays C++).
// The interposed SDL_PollEvent calls it until the item is found, and
// handleKeyDown() eats the Cmd+Q key, so the game does not get it either.

#include <string.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include "SagePatch/Hooks.h"
#include "SagePatch/Logger.h"

namespace sagepatch {

void removeQuitKey() {
    static bool done = false;
    if (done) return;

    typedef id (*SendId)(id, SEL);
    typedef id (*SendIdLong)(id, SEL, long);
    typedef long (*SendLong)(id, SEL);
    typedef SEL (*SendSel)(id, SEL);
    typedef void (*SendVoidId)(id, SEL, id);

    // Only when SDL has opened the video with Cocoa (then NSApp exists).
    const char* driver = SDL_GetCurrentVideoDriver();
    if (!driver || strcmp(driver, "cocoa") != 0) return;
    id app = ((SendId)objc_msgSend)((id)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    if (!app) return;
    id menu = ((SendId)objc_msgSend)(app, sel_registerName("mainMenu"));
    if (!menu) return;

    // [[NSString alloc] init]: the empty string, no key
    id noKey = ((SendId)objc_msgSend)(((SendId)objc_msgSend)((id)objc_getClass("NSString"), sel_registerName("alloc")), sel_registerName("init"));
    SEL terminate = sel_registerName("terminate:");
    long topCount = ((SendLong)objc_msgSend)(menu, sel_registerName("numberOfItems"));
    for (long i = 0; i < topCount; i++) {
        id top = ((SendIdLong)objc_msgSend)(menu, sel_registerName("itemAtIndex:"), i);
        id submenu = ((SendId)objc_msgSend)(top, sel_registerName("submenu"));
        if (!submenu) continue;
        long count = ((SendLong)objc_msgSend)(submenu, sel_registerName("numberOfItems"));
        for (long j = 0; j < count; j++) {
            id item = ((SendIdLong)objc_msgSend)(submenu, sel_registerName("itemAtIndex:"), j);
            if (((SendSel)objc_msgSend)(item, sel_registerName("action")) == terminate) {
                ((SendVoidId)objc_msgSend)(item, sel_registerName("setKeyEquivalent:"), noKey);
                done = true;
            }
        }
    }
    if (done) SAGEPATCH_LOG("Cmd+Q taken off the Quit menu item");
}

}

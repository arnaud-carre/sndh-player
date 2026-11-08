/* Cocoa backend (macOS 10.13+). NSWindow + NSView with NSTextInputClient
 * for text input, NSOpenGLContext for OpenGL 3.3+ (falls back to a layer
 * backed software path), Retina scaling, pasteboard and cursors.
 * Compile with -fobjc-arc. */
#include "cui_platform.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <dlfcn.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@class CUIView;

@interface CUIPlat : NSObject <NSWindowDelegate>
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) CUIView *view;
@property (nonatomic, strong) NSOpenGLContext *gl;
@property (nonatomic, assign) cui_app *app;
@property (nonatomic, assign) cui_cursor cursor;
@end

static void *cui__mac_libgl;

double cui__plat_now(void) { return [[NSProcessInfo processInfo] systemUptime]; }
const char *cui__plat_name(void) { return "cocoa"; }

int cui__plat_system_dark(void) {
    if (@available(macOS 10.14, *)) {
        NSAppearance *a = [NSApp effectiveAppearance];
        if (!a) a = [NSAppearance currentAppearance];
        NSAppearanceName n = [a bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
        return [n isEqualToString:NSAppearanceNameDarkAqua] ? 1 : 0;
    }
    return -1;
}

static int cui__mac_mods(NSEventModifierFlags f) {
    int m = 0;
    if (f & NSEventModifierFlagShift) m |= CUI_MOD_SHIFT;
    if (f & NSEventModifierFlagControl) m |= CUI_MOD_CTRL;
    if (f & NSEventModifierFlagOption) m |= CUI_MOD_ALT;
    if (f & NSEventModifierFlagCommand) m |= CUI_MOD_SUPER;
    return m;
}

static cui_key cui__mac_key(NSEvent *e) {
    switch (e.keyCode) {
    case 0x24: case 0x4C: return CUI_KEY_ENTER;
    case 0x30: return CUI_KEY_TAB;
    case 0x31: return CUI_KEY_SPACE;
    case 0x33: return CUI_KEY_BACKSPACE;
    case 0x35: return CUI_KEY_ESCAPE;
    case 0x75: return CUI_KEY_DELETE;
    case 0x7B: return CUI_KEY_LEFT;
    case 0x7C: return CUI_KEY_RIGHT;
    case 0x7D: return CUI_KEY_DOWN;
    case 0x7E: return CUI_KEY_UP;
    case 0x73: return CUI_KEY_HOME;
    case 0x77: return CUI_KEY_END;
    case 0x74: return CUI_KEY_PAGE_UP;
    case 0x79: return CUI_KEY_PAGE_DOWN;
    case 0x7A: return CUI_KEY_F1; case 0x78: return CUI_KEY_F2; case 0x63: return CUI_KEY_F3;
    case 0x76: return CUI_KEY_F4; case 0x60: return CUI_KEY_F5; case 0x61: return CUI_KEY_F6;
    case 0x62: return CUI_KEY_F7; case 0x64: return CUI_KEY_F8; case 0x65: return CUI_KEY_F9;
    case 0x6D: return CUI_KEY_F10; case 0x67: return CUI_KEY_F11; case 0x6F: return CUI_KEY_F12;
    default: break;
    }
    NSString *s = e.charactersIgnoringModifiers;
    if (s.length == 0) return CUI_KEY_NONE;
    unichar c = [s.lowercaseString characterAtIndex:0];
    if (c >= 'a' && c <= 'z') return (cui_key)(CUI_KEY_A + (c - 'a'));
    if (c >= '0' && c <= '9') return (cui_key)(CUI_KEY_0 + (c - '0'));
    switch (c) {
    case '-': return CUI_KEY_MINUS;
    case '=': return CUI_KEY_EQUAL;
    case ',': return CUI_KEY_COMMA;
    case '.': return CUI_KEY_PERIOD;
    case '/': return CUI_KEY_SLASH;
    default: return CUI_KEY_NONE;
    }
}

@interface CUIView : NSView <NSTextInputClient>
@property (nonatomic, assign) cui_app *app;
@property (nonatomic, strong) NSTrackingArea *tracking;
@end

@implementation CUIView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { (void)e; return YES; }
- (BOOL)wantsUpdateLayer { return YES; }

- (void)updateTrackingAreas {
    if (self.tracking) [self removeTrackingArea:self.tracking];
    self.tracking = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                 options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                                                         NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect | NSTrackingCursorUpdate
                                                   owner:self userInfo:nil];
    [self addTrackingArea:self.tracking];
    [super updateTrackingAreas];
}

- (void)mouseAt:(NSEvent *)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    cui_input_mouse_move(self.app->ui, (float)p.x, (float)p.y);
}
- (void)mouseMoved:(NSEvent *)e { [self mouseAt:e]; }

/* files dragged from Finder */
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)info {
    NSPoint p = [self convertPoint:[info draggingLocation] fromView:nil];
    cui_input_file_hover(self.app->ui, true, (float)p.x, (float)p.y);
    return NSDragOperationCopy;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info { return [self draggingUpdated:info]; }
- (void)draggingExited:(id<NSDraggingInfo>)info { (void)info; cui_input_file_hover(self.app->ui, false, 0, 0); }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
    NSPoint p = [self convertPoint:[info draggingLocation] fromView:nil];
    NSArray<NSURL *> *urls = [[info draggingPasteboard] readObjectsForClasses:@[ [NSURL class] ]
                                                                      options:@{ NSPasteboardURLReadingFileURLsOnlyKey : @YES }];
    const char *paths[64];
    int n = 0;
    for (NSURL *u in urls) {
        if (n >= 64) break;
        if (u.fileSystemRepresentation) paths[n++] = u.fileSystemRepresentation;
    }
    cui_input_file_drop(self.app->ui, paths, n, (float)p.x, (float)p.y);
    return n > 0;
}
- (void)mouseDragged:(NSEvent *)e { [self mouseAt:e]; }
- (void)rightMouseDragged:(NSEvent *)e { [self mouseAt:e]; }
- (void)otherMouseDragged:(NSEvent *)e { [self mouseAt:e]; }
- (void)mouseEntered:(NSEvent *)e { [self mouseAt:e]; }
- (void)mouseExited:(NSEvent *)e { (void)e; if (!cui_mouse_down(self.app->ui, CUI_MOUSE_LEFT)) cui_input_mouse_leave(self.app->ui); }
- (void)mouseDown:(NSEvent *)e { [self mouseAt:e]; cui_input_mouse_button(self.app->ui, CUI_MOUSE_LEFT, true); }
- (void)mouseUp:(NSEvent *)e { [self mouseAt:e]; cui_input_mouse_button(self.app->ui, CUI_MOUSE_LEFT, false); }
- (void)rightMouseDown:(NSEvent *)e { [self mouseAt:e]; cui_input_mouse_button(self.app->ui, CUI_MOUSE_RIGHT, true); }
- (void)rightMouseUp:(NSEvent *)e { [self mouseAt:e]; cui_input_mouse_button(self.app->ui, CUI_MOUSE_RIGHT, false); }
- (void)otherMouseDown:(NSEvent *)e { [self mouseAt:e]; cui_input_mouse_button(self.app->ui, CUI_MOUSE_MIDDLE, true); }
- (void)otherMouseUp:(NSEvent *)e { [self mouseAt:e]; cui_input_mouse_button(self.app->ui, CUI_MOUSE_MIDDLE, false); }

- (void)scrollWheel:(NSEvent *)e {
    float dx = (float)e.scrollingDeltaX, dy = (float)e.scrollingDeltaY;
    if (e.hasPreciseScrollingDeltas) { dx /= 44.0f; dy /= 44.0f; }
    cui_input_scroll(self.app->ui, dx, dy);
}

- (void)cursorUpdate:(NSEvent *)e { (void)e; }

- (void)keyDown:(NSEvent *)e {
    cui_key k = cui__mac_key(e);
    int mods = cui__mac_mods(e.modifierFlags);
    if (k) cui_input_key(self.app->ui, k, true, mods);
    if (!(mods & (CUI_MOD_SUPER | CUI_MOD_CTRL))) [self interpretKeyEvents:@[ e ]];
}
- (void)keyUp:(NSEvent *)e {
    cui_key k = cui__mac_key(e);
    if (k) cui_input_key(self.app->ui, k, false, cui__mac_mods(e.modifierFlags));
}
- (void)flagsChanged:(NSEvent *)e {
    /* modifiers alone: forward state with no key */
    cui_input_key(self.app->ui, CUI_KEY_NONE, false, cui__mac_mods(e.modifierFlags));
}
- (BOOL)performKeyEquivalent:(NSEvent *)e {
    /* let Cmd shortcuts reach the UI (copy, paste, select all...) except the app menu ones */
    if ((e.modifierFlags & NSEventModifierFlagCommand) && e.type == NSEventTypeKeyDown) {
        NSString *c = e.charactersIgnoringModifiers.lowercaseString;
        if ([c isEqualToString:@"q"] || [c isEqualToString:@"w"] || [c isEqualToString:@"m"] || [c isEqualToString:@"h"]) return [super performKeyEquivalent:e];
        [self keyDown:e];
        return YES;
    }
    return [super performKeyEquivalent:e];
}

/* NSTextInputClient */
- (void)insertText:(id)string replacementRange:(NSRange)r {
    (void)r;
    NSString *s = [string isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString *)string string] : (NSString *)string;
    const char *utf8 = s.UTF8String;
    if (utf8) cui_input_text(self.app->ui, utf8);
}
- (void)doCommandBySelector:(SEL)sel { (void)sel; }
- (void)setMarkedText:(id)s selectedRange:(NSRange)a replacementRange:(NSRange)b { (void)s; (void)a; (void)b; }
- (void)unmarkText {}
- (NSRange)selectedRange { return NSMakeRange(NSNotFound, 0); }
- (NSRange)markedRange { return NSMakeRange(NSNotFound, 0); }
- (BOOL)hasMarkedText { return NO; }
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)r actualRange:(NSRangePointer)a { (void)r; (void)a; return nil; }
- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText { return @[]; }
- (NSRect)firstRectForCharacterRange:(NSRange)r actualRange:(NSRangePointer)a {
    (void)r; (void)a;
    NSRect local = NSMakeRect(0, 0, 1, 1);
    return [self.window convertRectToScreen:[self convertRect:local toView:nil]];
}
- (NSUInteger)characterIndexForPoint:(NSPoint)p { (void)p; return NSNotFound; }
@end

@implementation CUIPlat
- (BOOL)windowShouldClose:(NSWindow *)w { (void)w; self.app->running = false; return NO; }
- (void)windowDidResize:(NSNotification *)n {
    (void)n;
    if (self.gl) [self.gl update];
    cui_request_redraw(self.app->ui);
    if (self.window.inLiveResize) cui__app_live_frame(self.app);
}
- (void)windowDidChangeBackingProperties:(NSNotification *)n { (void)n; if (self.gl) [self.gl update]; cui_request_redraw(self.app->ui); }
- (void)windowDidBecomeKey:(NSNotification *)n { (void)n; cui_input_focus(self.app->ui, true); }
- (void)windowDidResignKey:(NSNotification *)n { (void)n; cui_input_focus(self.app->ui, false); }
@end

static CUIPlat *cui__p(cui_app *app) { return (__bridge CUIPlat *)app->plat; }

static void cui__mac_set_cursor(void *user, cui_cursor c) {
    cui_app *app = user;
    CUIPlat *p = cui__p(app);
    if (p.cursor == c) return;
    p.cursor = c;
    NSCursor *cur = [NSCursor arrowCursor];
    switch (c) {
    case CUI_CURSOR_HAND: cur = [NSCursor pointingHandCursor]; break;
    case CUI_CURSOR_TEXT: cur = [NSCursor IBeamCursor]; break;
    case CUI_CURSOR_RESIZE_EW: cur = [NSCursor resizeLeftRightCursor]; break;
    case CUI_CURSOR_RESIZE_NS: cur = [NSCursor resizeUpDownCursor]; break;
    case CUI_CURSOR_MOVE: cur = [NSCursor openHandCursor]; break;
    case CUI_CURSOR_NOT_ALLOWED: cur = [NSCursor operationNotAllowedCursor]; break;
    default: break;
    }
    [cur set];
}

static void cui__mac_set_clip(void *user, const char *utf8) {
    (void)user;
    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:[NSString stringWithUTF8String:utf8] forType:NSPasteboardTypeString];
}

static const char *cui__mac_get_clip(void *user) {
    cui_app *app = user;
    NSString *s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (!s) return NULL;
    cui__app_set_clipboard_cache(app, s.UTF8String);
    return app->clipboard;
}

static void cui__mac_menu(void) {
    NSMenu *bar = [[NSMenu alloc] init];
    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    [bar addItem:appItem];
    NSMenu *appMenu = [[NSMenu alloc] init];
    NSString *name = [[NSProcessInfo processInfo] processName];
    [appMenu addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    NSMenuItem *winItem = [[NSMenuItem alloc] init];
    [bar addItem:winItem];
    NSMenu *winMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [winMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [winMenu addItemWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];
    [winItem setSubmenu:winMenu];
    [NSApp setMainMenu:bar];
    [NSApp setWindowsMenu:winMenu];
}

bool cui__plat_init(cui_app *app) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        if (![NSApp mainMenu]) cui__mac_menu();
        [NSApp finishLaunching];
        CUIPlat *p = [[CUIPlat alloc] init];
        p.app = app;
        p.cursor = CUI_CURSOR_ARROW;
        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
        if (!app->desc.fixed_size) style |= NSWindowStyleMaskResizable;
        NSRect frame = NSMakeRect(0, 0, app->desc.width, app->desc.height);
        NSWindow *w = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
        w.releasedWhenClosed = NO;
        w.title = [NSString stringWithUTF8String:app->title];
        w.contentMinSize = NSMakeSize(app->desc.min_width, app->desc.min_height);
        w.acceptsMouseMovedEvents = YES;
        w.delegate = p;
        cui_color bg = app->ui->theme.colors[CUI_TOKEN_BACKGROUND];
        w.backgroundColor = [NSColor colorWithSRGBRed:bg.r / 255.0 green:bg.g / 255.0 blue:bg.b / 255.0 alpha:1];
        if (@available(macOS 10.14, *)) {
            w.appearance = [NSAppearance appearanceNamed:app->desc.theme == CUI_THEME_DARK ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
        }
        [w center];
        CUIView *v = [[CUIView alloc] initWithFrame:frame];
        v.app = app;
        v.wantsBestResolutionOpenGLSurface = YES;
        w.contentView = v;
        [v registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
        [w makeFirstResponder:v];
        p.window = w;
        p.view = v;
        app->plat = (__bridge_retained void *)p;
        cui_platform_iface pi;
        memset(&pi, 0, sizeof pi);
        pi.user = app;
        pi.set_clipboard = cui__mac_set_clip;
        pi.get_clipboard = cui__mac_get_clip;
        pi.set_cursor = cui__mac_set_cursor;
        cui_set_platform(app->ui, &pi);
    }
    return true;
}

void cui__plat_show(cui_app *app) {
    CUIPlat *p = cui__p(app);
    [p.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

void cui__plat_shutdown(cui_app *app) {
    if (!app->plat) return;
    CUIPlat *p = (__bridge_transfer CUIPlat *)app->plat;
    app->plat = NULL;
    if (p.gl) { [NSOpenGLContext clearCurrentContext]; p.gl = nil; }
    p.window.delegate = nil;
    [p.window close];
    p.window = nil;
    p.view = nil;
}

bool cui__plat_gl_create(cui_app *app, bool vsync) {
    CUIPlat *p = cui__p(app);
    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
        NSOpenGLPFAColorSize, 24, NSOpenGLPFAAlphaSize, 8,
        NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, 0
    };
    NSOpenGLPixelFormat *pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    if (!pf) {
        NSOpenGLPixelFormatAttribute soft[] = { NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core, NSOpenGLPFADoubleBuffer, 0 };
        pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:soft];
    }
    if (!pf) return false;
    NSOpenGLContext *ctx = [[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
    if (!ctx) return false;
    GLint interval = vsync ? 1 : 0;
    [ctx setValues:&interval forParameter:NSOpenGLContextParameterSwapInterval];
    [ctx setView:p.view];
    [ctx makeCurrentContext];
    p.gl = ctx;
    return true;
}

void *cui__plat_gl_proc(const char *name) {
    if (!cui__mac_libgl) cui__mac_libgl = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
    return cui__mac_libgl ? dlsym(cui__mac_libgl, name) : NULL;
}

void cui__plat_gl_swap(cui_app *app) {
    CUIPlat *p = cui__p(app);
    [p.gl flushBuffer];
}

void cui__plat_present(cui_app *app, const void *bgra, int w, int h, int stride) {
    CUIPlat *p = cui__p(app);
    @autoreleasepool {
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef ctx = CGBitmapContextCreate((void *)bgra, (size_t)w, (size_t)h, 8, (size_t)stride, cs,
                                                 kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGImageRef img = ctx ? CGBitmapContextCreateImage(ctx) : NULL;
        if (!p.view.wantsLayer) p.view.wantsLayer = YES;
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        p.view.layer.contents = (__bridge id)img;
        p.view.layer.contentsScale = p.window.backingScaleFactor;
        [CATransaction commit];
        if (img) CGImageRelease(img);
        if (ctx) CGContextRelease(ctx);
        CGColorSpaceRelease(cs);
    }
}

void cui__plat_metrics(cui_app *app, float *w, float *h, float *scale, int *fb_w, int *fb_h) {
    CUIPlat *p = cui__p(app);
    NSRect b = p.view.bounds;
    float s = (float)p.window.backingScaleFactor;
    const char *env = getenv("CUI_SCALE");
    if (env) { float e = (float)atof(env); if (e > 0.25f && e < 8) s = e; }
    *w = (float)b.size.width;
    *h = (float)b.size.height;
    *scale = s;
    *fb_w = (int)(b.size.width * s + 0.5);
    *fb_h = (int)(b.size.height * s + 0.5);
}

void cui__plat_set_title(cui_app *app, const char *title) {
    cui__p(app).window.title = [NSString stringWithUTF8String:title];
}

void cui__plat_set_size(cui_app *app, int w, int h) {
    [cui__p(app).window setContentSize:NSMakeSize(w, h)];
}

void *cui__plat_native(cui_app *app) { return (__bridge void *)cui__p(app).window; }

void cui__plat_wait(cui_app *app, double timeout) {
    @autoreleasepool {
        NSDate *until = timeout < 0 ? [NSDate distantFuture] : [NSDate dateWithTimeIntervalSinceNow:timeout];
        NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until inMode:NSDefaultRunLoopMode dequeue:YES];
        while (e) {
            [NSApp sendEvent:e];
            e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
        }
        [NSApp updateWindows];
        if (app->desc.follow_system_theme) {
            int dark = cui__plat_system_dark();
            if (dark >= 0) cui_set_theme(app->ui, dark ? CUI_THEME_DARK : CUI_THEME_LIGHT);
        }
    }
}

#pragma clang diagnostic pop

bool cui__plat_present_part(cui_app *app, const void *bgra, int w, int h, int stride, int x, int y, int pw, int ph) {
    (void)app; (void)bgra; (void)w; (void)h; (void)stride; (void)x; (void)y; (void)pw; (void)ph;
    return false;   /* the whole buffer is shown each time */
}

void cui__plat_gl_release(cui_app *app) { (void)app; }   /* not used: these keep OpenGL */

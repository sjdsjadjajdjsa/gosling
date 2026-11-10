#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <ImageIO/ImageIO.h>
#import <QuartzCore/QuartzCore.h>
#include <os/lock.h>
#include <stdlib.h>
#include "native.h"

static const unsigned char inter[] = {
#embed "../fonts/Inter-Medium.ttf"
};

enum { C_RECT, C_CIRCLE, C_RING, C_TRI, C_TEXT, C_CLIP, C_UNCLIP, C_IMAGE };

typedef struct {
    int kind;
    uint32_t color;
    float a[7];
    uint32_t text, len;
    void *image;
} cmd;

typedef struct { cmd *items; size_t len, cap; char *text; size_t tlen, tcap; } drawing;

static drawing now, shown;
static void **doomed;
static size_t ndoomed, doomed_cap;
static os_unfair_lock doomed_lock = OS_UNFAIR_LOCK_INIT;
static NSWindow *window;
static NSView *view;
static bool closed, quit;
static int width, height;
static float mx, my, wheel_acc, wheel;
static bool down, clicked, unclicked, pressed, released;
static int pending_keys[32], npending, keys[32], nkeys;
static int pending_chars[64], npchars, chars[64], nchars, char_at;
static char dropped_path[1024];
static int cursor = -1;
static CGFontRef inter_font;
static struct { float size; CTFontRef font; } fonts[12];
static int nfonts;

static CTFontRef font_at(float size) {
    for (int i = 0; i < nfonts; i++)
        if (fonts[i].size == size) return fonts[i].font;
    if (!inter_font) {
        CGDataProviderRef data = CGDataProviderCreateWithData(NULL, inter, sizeof inter, NULL);
        inter_font = CGFontCreateWithDataProvider(data);
        CGDataProviderRelease(data);
    }
    CTFontRef f = CTFontCreateWithGraphicsFont(inter_font, size, NULL, NULL);
    if (nfonts < 12) {
        fonts[nfonts].size = size;
        fonts[nfonts++].font = f;
    }
    return f;
}

static CTLineRef make_line(const char *s, size_t n, float size, uint32_t color, float max_w) {
    CFStringRef str = CFStringCreateWithBytes(NULL, (const UInt8 *)s, (CFIndex)n, kCFStringEncodingUTF8, false);
    if (!str) str = CFSTR("");
    CGFloat rgba[4] = { (color >> 24) / 255.0, ((color >> 16) & 255) / 255.0, ((color >> 8) & 255) / 255.0, (color & 255) / 255.0 };
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGColorRef c = CGColorCreate(space, rgba);
    CGColorSpaceRelease(space);
    const void *keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    const void *values[] = { font_at(size), c };
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef text = CFAttributedStringCreate(NULL, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(text);
    if (max_w > 0 && CTLineGetTypographicBounds(line, NULL, NULL, NULL) > max_w) {
        CFAttributedStringRef dots = CFAttributedStringCreate(NULL, CFSTR("\u2026"), attrs);
        CTLineRef token = CTLineCreateWithAttributedString(dots);
        CTLineRef cut = CTLineCreateTruncatedLine(line, max_w, kCTLineTruncationEnd, token);
        if (cut) {
            CFRelease(line);
            line = cut;
        }
        CFRelease(token);
        CFRelease(dots);
    }
    CFRelease(text);
    CFRelease(attrs);
    CGColorRelease(c);
    CFRelease(str);
    return line;
}

static cmd *push(int kind, uint32_t color) {
    if (now.len == now.cap) {
        size_t cap = now.cap ? now.cap * 2 : 256;
        cmd *grown = realloc(now.items, cap * sizeof *grown);
        if (!grown) return NULL;
        now.items = grown;
        now.cap = cap;
    }
    cmd *c = &now.items[now.len++];
    memset(c, 0, sizeof *c);
    c->kind = kind;
    c->color = color;
    return c;
}

static uint32_t push_text(const char *s, size_t n) {
    if (now.tlen + n > now.tcap) {
        size_t cap = now.tcap ? now.tcap : 4096;
        while (cap < now.tlen + n) cap *= 2;
        char *grown = realloc(now.text, cap);
        if (!grown) return 0;
        now.text = grown;
        now.tcap = cap;
    }
    memcpy(now.text + now.tlen, s, n);
    uint32_t at = (uint32_t)now.tlen;
    now.tlen += n;
    return at;
}

static void fill(CGContextRef g, uint32_t c) {
    CGContextSetRGBFillColor(g, (c >> 24) / 255.0, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0);
}

static void replay(CGContextRef g) {
    for (size_t i = 0; i < shown.len; i++) {
        cmd *c = &shown.items[i];
        float *a = c->a;
        switch (c->kind) {
        case C_RECT: {
            fill(g, c->color);
            CGRect r = CGRectMake(a[0], a[1], a[2], a[3]);
            float m = fminf(a[2], a[3]) / 2, rad = fminf(a[4], m);
            if (rad <= 0) CGContextFillRect(g, r);
            else {
                CGPathRef p = CGPathCreateWithRoundedRect(r, rad, rad, NULL);
                CGContextAddPath(g, p);
                CGContextFillPath(g);
                CGPathRelease(p);
            }
            break;
        }
        case C_CIRCLE:
            fill(g, c->color);
            CGContextFillEllipseInRect(g, CGRectMake(a[0] - a[2], a[1] - a[2], a[2] * 2, a[2] * 2));
            break;
        case C_RING: {
            fill(g, c->color);
            float s = a[4] * (float)M_PI / 180, e = a[5] * (float)M_PI / 180;
            CGContextAddArc(g, a[0], a[1], a[3], s, e, 0);
            CGContextAddArc(g, a[0], a[1], a[2], e, s, 1);
            CGContextClosePath(g);
            CGContextFillPath(g);
            break;
        }
        case C_TRI:
            fill(g, c->color);
            CGContextMoveToPoint(g, a[0], a[1]);
            CGContextAddLineToPoint(g, a[2], a[3]);
            CGContextAddLineToPoint(g, a[4], a[5]);
            CGContextClosePath(g);
            CGContextFillPath(g);
            break;
        case C_TEXT: {
            CTLineRef line = make_line(shown.text + c->text, c->len, a[2], c->color, a[3]);
            CGContextSetTextMatrix(g, CGAffineTransformMakeScale(1, -1));
            CGContextSetTextPosition(g, a[0], a[1] + CTFontGetAscent(font_at(a[2])));
            CTLineDraw(line, g);
            CFRelease(line);
            break;
        }
        case C_CLIP:
            CGContextSaveGState(g);
            CGContextClipToRect(g, CGRectMake(a[0], a[1], a[2], a[3]));
            break;
        case C_UNCLIP:
            CGContextRestoreGState(g);
            break;
        case C_IMAGE: {
            CGContextSaveGState(g);
            CGRect r = CGRectMake(a[0], a[1], a[2], a[2]);
            CGPathRef p = CGPathCreateWithRoundedRect(r, a[3], a[3], NULL);
            CGContextAddPath(g, p);
            CGContextClip(g);
            CGPathRelease(p);
            CGContextTranslateCTM(g, a[0], a[1] + a[2]);
            CGContextScaleCTM(g, 1, -1);
            CGContextSetInterpolationQuality(g, kCGInterpolationHigh);
            CGContextDrawImage(g, CGRectMake(0, 0, a[2], a[2]), (CGImageRef)c->image);
            CGContextRestoreGState(g);
            break;
        }
        }
    }
}

static int map_key(unsigned short code) {
    switch (code) {
    case 36: case 76: return 257;
    case 53: return 256;
    case 51: return 259;
    case 49: return 32;
    case 123: return 263;
    case 124: return 262;
    case 125: return 264;
    case 126: return 265;
    case 9: return 86;
    default: return 0;
    }
}

@interface GView : NSView <NSDraggingDestination>
- (void)applyCursor;
@end

@implementation GView
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { (void)e; return YES; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    replay(NSGraphicsContext.currentContext.CGContext);
}
- (void)track:(NSEvent *)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    mx = (float)p.x;
    my = (float)p.y;
}
- (void)mouseMoved:(NSEvent *)e { [self track:e]; [self applyCursor]; }
- (void)cursorUpdate:(NSEvent *)e { (void)e; [self applyCursor]; }
- (void)applyCursor {
    if (cursor == 2) [NSCursor.IBeamCursor set];
    else if (cursor == 4) [NSCursor.pointingHandCursor set];
    else [NSCursor.arrowCursor set];
}
- (void)mouseDragged:(NSEvent *)e { [self track:e]; }
- (void)mouseDown:(NSEvent *)e { [self track:e]; down = clicked = true; }
- (void)mouseUp:(NSEvent *)e { [self track:e]; down = false; unclicked = true; }
- (void)scrollWheel:(NSEvent *)e { wheel_acc += (float)(e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / 56.0 : e.scrollingDeltaY); }
- (void)keyDown:(NSEvent *)e {
    int k = map_key(e.keyCode);
    if (k && npending < 32) pending_keys[npending++] = k;
    if (e.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) return;
    NSString *s = e.characters;
    for (NSUInteger i = 0; i < s.length && npchars < 64; i++) {
        unichar u = [s characterAtIndex:i];
        if (u < 32 || u == 127 || (u >= 0xF700 && u <= 0xF8FF)) continue;
        int cp = u;
        if (CFStringIsSurrogateHighCharacter(u) && i + 1 < s.length) cp = (int)CFStringGetLongCharacterForSurrogatePair(u, [s characterAtIndex:++i]);
        pending_chars[npchars++] = cp;
    }
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info { (void)info; return NSDragOperationCopy; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
    NSArray<NSURL *> *urls = [info.draggingPasteboard readObjectsForClasses:@[ NSURL.class ] options:@{ NSPasteboardURLReadingFileURLsOnlyKey : @YES }];
    if (!urls.count) return NO;
    snprintf(dropped_path, sizeof dropped_path, "%s", urls[0].fileSystemRepresentation);
    return YES;
}
@end

@interface GDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation GDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app { (void)app; quit = true; return NSTerminateCancel; }
- (void)windowWillClose:(NSNotification *)n { (void)n; closed = true; }
- (void)windowDidResize:(NSNotification *)n { (void)n; ui_wake(); }
- (void)windowDidChangeOcclusionState:(NSNotification *)n { (void)n; ui_wake(); }
- (void)windowDidDeminiaturize:(NSNotification *)n { (void)n; ui_wake(); }
@end

static GDelegate *delegate;

void ui_open(gs_str title, int32_t w, int32_t h, int32_t min_w, int32_t min_h) {
    char t[256];
    [NSApplication sharedApplication];
    NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
    delegate = [GDelegate new];
    NSApp.delegate = delegate;
    NSMenu *bar = [NSMenu new], *menu = [NSMenu new];
    NSMenuItem *item = [NSMenuItem new];
    [bar addItem:item];
    [menu addItemWithTitle:@"Quit Gosling" action:@selector(terminate:) keyEquivalent:@"q"];
    item.submenu = menu;
    NSApp.mainMenu = bar;
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, w, h)
                                         styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    window.title = [NSString stringWithUTF8String:gs_cstr(title, t, sizeof t)];
    window.contentMinSize = NSMakeSize(min_w, min_h);
    window.releasedWhenClosed = NO;
    window.acceptsMouseMovedEvents = YES;
    window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    window.delegate = delegate;
    window.backgroundColor = [NSColor colorWithSRGBRed:0x12 / 255.0 green:0x12 / 255.0 blue:0x14 / 255.0 alpha:1];
    view = [[GView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
    [view registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    window.contentView = view;
    [window makeFirstResponder:view];
    [window center];
    [NSApp finishLaunching];
    const char *minimize = getenv("GOSLING_MINIMIZE");
    if (minimize && *minimize) [window miniaturize:nil];
    else [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    width = w;
    height = h;
}

bool ui_running(void) { return !closed && !quit; }

bool ui_visible(void) { return !window.miniaturized && window.visible && (window.occlusionState & NSWindowOcclusionStateVisible); }

static void dispatch(NSEvent *e) {
    if (e.type != NSEventTypeApplicationDefined) [NSApp sendEvent:e];
}

void ui_wait(double timeout) {
    @autoreleasepool {
        NSDate *until = timeout < 0 ? NSDate.distantFuture : [NSDate dateWithTimeIntervalSinceNow:timeout];
        NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until inMode:NSDefaultRunLoopMode dequeue:YES];
        if (e) dispatch(e);
        while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:NSDate.distantPast inMode:NSDefaultRunLoopMode dequeue:YES])) dispatch(e);
    }
}

void ui_wake(void) {
    @autoreleasepool {
        NSEvent *e = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0];
        [NSApp postEvent:e atStart:NO];
    }
}

double ui_time(void) { return CACurrentMediaTime(); }

void ui_input(void) {
    @autoreleasepool {
        ui_wait(0);
    }
    pressed = clicked;
    released = unclicked;
    clicked = unclicked = false;
    wheel = wheel_acc;
    wheel_acc = 0;
    memcpy(keys, pending_keys, sizeof keys);
    nkeys = npending;
    npending = 0;
    memcpy(chars, pending_chars, sizeof chars);
    nchars = npchars;
    npchars = 0;
    char_at = 0;
    NSSize s = view.bounds.size;
    width = (int)s.width;
    height = (int)s.height;
}

void ui_frame(uint32_t bg) {
    now.len = 0;
    now.tlen = 0;
    ui_rect(0, 0, (float)width, (float)height, 0, bg);
}

void ui_present(void) {
    bool same = now.len == shown.len && now.tlen == shown.tlen &&
                !memcmp(now.items, shown.items, now.len * sizeof *now.items) && !memcmp(now.text, shown.text, now.tlen);
    drawing t = shown;
    shown = now;
    now = t;
    if (!same) {
        @autoreleasepool {
            view.needsDisplay = YES;
            [view displayIfNeeded];
        }
    }
    os_unfair_lock_lock(&doomed_lock);
    for (size_t i = 0; i < ndoomed; i++) CGImageRelease((CGImageRef)doomed[i]);
    ndoomed = 0;
    os_unfair_lock_unlock(&doomed_lock);
}

int32_t ui_width(void) { return width; }
int32_t ui_height(void) { return height; }

void ui_rect(float x, float y, float w, float h, float r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    cmd *c = push(C_RECT, color);
    if (c) memcpy(c->a, (float[]){ x, y, w, h, r }, 5 * sizeof(float));
}

void ui_ring(float x, float y, float inner, float outer, float start, float end, uint32_t color) {
    cmd *c = push(C_RING, color);
    if (c) memcpy(c->a, (float[]){ x, y, inner, outer, start, end }, 6 * sizeof(float));
}

void ui_circle(float x, float y, float r, uint32_t color) {
    cmd *c = push(C_CIRCLE, color);
    if (c) memcpy(c->a, (float[]){ x, y, r }, 3 * sizeof(float));
}

void ui_triangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color) {
    cmd *c = push(C_TRI, color);
    if (c) memcpy(c->a, (float[]){ x1, y1, x2, y2, x3, y3 }, 6 * sizeof(float));
}

static void text(gs_str s, float x, float y, float size, uint32_t color, float max_w) {
    cmd *c = push(C_TEXT, color);
    if (!c) return;
    memcpy(c->a, (float[]){ x, y, size, max_w }, 4 * sizeof(float));
    c->text = push_text((const char *)s.data, (size_t)s.len);
    c->len = (uint32_t)s.len;
}

void ui_text(gs_str s, float x, float y, float size, uint32_t color) { text(s, x, y, size, color, 0); }
void ui_text_fit(gs_str s, float x, float y, float size, uint32_t color, float max_w) { text(s, x, y, size, color, max_w > 1 ? max_w : 1); }

float ui_text_width(gs_str s, float size) {
    CTLineRef line = make_line((const char *)s.data, (size_t)s.len, size, 0xffffffff, 0);
    float w = (float)CTLineGetTypographicBounds(line, NULL, NULL, NULL);
    CFRelease(line);
    return w;
}

void ui_clip(float x, float y, float w, float h) {
    cmd *c = push(C_CLIP, 0);
    if (c) memcpy(c->a, (float[]){ x, y, w, h }, 4 * sizeof(float));
}

void ui_unclip(void) { push(C_UNCLIP, 0); }

void *ui_image_decode(const unsigned char *data, size_t len, int px) {
    CFDataRef d = CFDataCreate(NULL, data, (CFIndex)len);
    CGImageSourceRef src = CGImageSourceCreateWithData(d, NULL);
    CFRelease(d);
    if (!src) return NULL;
    NSDictionary *opts = @{
        (id)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
        (id)kCGImageSourceThumbnailMaxPixelSize : @(px * 16 / 9),
        (id)kCGImageSourceCreateThumbnailWithTransform : @YES,
        (id)kCGImageSourceShouldCacheImmediately : @YES,
    };
    CGImageRef img = CGImageSourceCreateThumbnailAtIndex(src, 0, (__bridge CFDictionaryRef)opts);
    CFRelease(src);
    if (!img) return NULL;
    size_t w = CGImageGetWidth(img), h = CGImageGetHeight(img), side = w < h ? w : h;
    CGImageRef square = CGImageCreateWithImageInRect(img, CGRectMake((w - side) / 2.0, (h - side) / 2.0, side, side));
    CGImageRelease(img);
    if (!square) return NULL;
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL, (size_t)px, (size_t)px, 8, 0, space, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    if (!ctx) {
        CGImageRelease(square);
        return NULL;
    }
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextDrawImage(ctx, CGRectMake(0, 0, px, px), square);
    CGImageRelease(square);
    CGImageRef out = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    return (void *)out;
}

void ui_image_draw(void *image, float x, float y, float size, float radius) {
    cmd *c = push(C_IMAGE, 0);
    if (!c) return;
    memcpy(c->a, (float[]){ x, y, size, radius }, 4 * sizeof(float));
    c->image = image;
}

void ui_image_free(void *image) {
    if (!image) return;
    os_unfair_lock_lock(&doomed_lock);
    if (ndoomed == doomed_cap) {
        size_t cap = doomed_cap ? doomed_cap * 2 : 16;
        void **grown = realloc(doomed, cap * sizeof *grown);
        if (grown) {
            doomed = grown;
            doomed_cap = cap;
        }
    }
    if (ndoomed < doomed_cap) doomed[ndoomed++] = image;
    os_unfair_lock_unlock(&doomed_lock);
}

float ui_mouse_x(void) { return mx; }
float ui_mouse_y(void) { return my; }
bool ui_mouse_pressed(void) { return pressed; }
bool ui_mouse_down(void) { return down; }
bool ui_mouse_released(void) { return released; }
float ui_wheel(void) { return wheel; }

int32_t ui_char(void) { return char_at < nchars ? chars[char_at++] : 0; }

bool ui_key(int32_t key) {
    for (int i = 0; i < nkeys; i++)
        if (keys[i] == key) return true;
    return false;
}

bool ui_command(void) { return (NSEvent.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0; }

int32_t ui_paste(char out[1024]) {
    @autoreleasepool {
        NSString *s = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
        return gs_copy(out, 1024, s ? s.UTF8String : "");
    }
}

int32_t ui_dropped(char out[1024]) {
    if (!dropped_path[0]) return 0;
    int32_t n = gs_copy(out, 1024, dropped_path);
    dropped_path[0] = 0;
    return n;
}

void ui_cursor(int32_t kind) {
    if (kind == cursor) return;
    cursor = kind;
    [(GView *)view applyCursor];
}

void ui_close(void) {
    for (int i = 0; i < nfonts; i++) CFRelease(fonts[i].font);
    nfonts = 0;
    [window close];
}

#include "gui.h"

#include "agent.h"
#include "iofmt.h"
#include "screen.h"
#include "wpd.h"

#include <ncurses.h>
#include <string.h>

#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#import <IOKit/IOKitLib.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

static App *Gapp;
static Doc *Gdoc;
static View *Gview;
static Screen Gscr;

@interface WPDocView : NSView <NSTextInputClient>
@property (nonatomic) BOOL blinkOn;
@property (nonatomic, copy) NSString *markedText;
@property (nonatomic) NSRange markedSel;
@property (nonatomic) size_t clickAnchor;
@property (nonatomic) BOOL didDrag;
@end

@interface FKeyStripView : NSView
@property (nonatomic, strong) NSButton *ctrlBtn;
@property (nonatomic, strong) NSButton *shiftBtn;
@property (nonatomic, strong) NSMutableArray<NSButton *> *fbtns;
- (void)reloadTitles;
@end

@interface NSTouchBar (WPSystemModal)
+ (void)presentSystemModalFunctionBar:(NSTouchBar *)touchBar systemTrayItemIdentifier:(NSString *)identifier;
+ (void)dismissSystemModalFunctionBar:(NSTouchBar *)touchBar;
+ (void)presentSystemModalTouchBar:(NSTouchBar *)touchBar systemTrayItemIdentifier:(NSString *)identifier;
+ (void)dismissSystemModalTouchBar:(NSTouchBar *)touchBar;
@end

@interface WPDocument : NSDocument
@end

@interface WPApp : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTouchBarDelegate>
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) NSWindow *settings;
@property (nonatomic, strong) NSPopUpButton *schemePop;
@property (nonatomic, strong) NSPopUpButton *formatPop;
@property (nonatomic, strong) NSPopUpButton *wrapPop;
@property (nonatomic, strong) NSButton *forceFkeysCheck;
@property (nonatomic, strong) NSButton *spellCheck;
@property (nonatomic, strong) NSPopUpButton *spellLangPop;
@property (nonatomic, strong) NSTextField *schemeNameField;
@property (nonatomic, strong) WPDocView *docView;
@property (nonatomic, strong) FKeyStripView *strip;
@property (nonatomic, strong) NSMutableArray<NSButton *> *tbFbtns;
@property (nonatomic, strong) NSButton *tbCtrl;
@property (nonatomic, strong) NSButton *tbShift;
@property (nonatomic, strong) NSTitlebarAccessoryViewController *stripAcc;
@property (nonatomic, strong) NSTouchBar *fnBar;
@property (nonatomic, strong) NSTimer *blink;
@property (nonatomic, strong) NSTimer *agent;
- (NSTouchBar *)makeTouchBar API_AVAILABLE(macos(10.12.2));
- (void)showKeyboardStrip;
- (void)copyDoc:(id)sender;
- (void)cutDoc:(id)sender;
- (void)pasteDoc:(id)sender;
- (void)selectAllDoc:(id)sender;
- (void)syncFkeyLabels;
@end

static WPApp *Gui;
static int g_fkey_mod;
static void gui_refresh(void);

static int gui_fkey_mapped(int n)
{
    if (n < 1 || n > 12) {
        return 0;
    }
    if (g_fkey_mod == FKEY_MOD_CTRL || g_fkey_mod == FKEY_MOD_SHIFT) {
        if (n == 8) {
            return WP_KEY_FONT;
        }
        if (n == 2) {
            return KEY_F(14);
        }
        if (g_fkey_mod == FKEY_MOD_CTRL && n == 6) {
            return WP_KEY_ITALIC;
        }
    }
    return KEY_F(n);
}

static void gui_set_fkey_mod(int mod)
{
    if (g_fkey_mod == mod) {
        g_fkey_mod = FKEY_MOD_NONE;
    } else {
        g_fkey_mod = mod;
    }
    [Gui syncFkeyLabels];
    if (Gview) {
        if (g_fkey_mod == FKEY_MOD_CTRL) {
            view_message(Gview, "Ctrl  F8 Font  F6 Italc  F2 Search back");
        } else if (g_fkey_mod == FKEY_MOD_SHIFT) {
            view_message(Gview, "Shift  F8 Font  F2 Search back");
        } else {
            view_message(Gview, "F-keys");
        }
        gui_refresh();
    }
}

enum {
    WPColorBg = 0,
    WPColorText,
    WPColorBold,
    WPColorStatusBg,
    WPColorStatusFg,
    WPColorCursor,
    WPColorCount
};

static NSString *const kWPColorKey[WPColorCount] = {
    @"wp.color.bg",
    @"wp.color.text",
    @"wp.color.bold",
    @"wp.color.statusBg",
    @"wp.color.statusFg",
    @"wp.color.cursor"
};

static const CGFloat kWPColorDefault[WPColorCount][3] = {
    {0.00, 0.00, 0.67},
    {1.00, 1.00, 0.55},
    {1.00, 1.00, 1.00},
    {0.40, 0.90, 0.95},
    {0.00, 0.00, 0.00},
    {1.00, 1.00, 0.55}
};

static const char *kWPColorLabel[WPColorCount] = {
    "Screen",
    "Text",
    "Bold",
    "Status bar",
    "Status text",
    "Cursor"
};

static NSColor *wp_rgb(CGFloat r, CGFloat g, CGFloat b)
{
    return [NSColor colorWithCalibratedRed:r green:g blue:b alpha:1];
}

static NSColor *wp_color(int slot)
{
    if (slot < 0 || slot >= WPColorCount) {
        return wp_rgb(0, 0, 0);
    }
    NSArray *a = [[NSUserDefaults standardUserDefaults] arrayForKey:kWPColorKey[slot]];
    if ([a isKindOfClass:[NSArray class]] && a.count >= 3) {
        return wp_rgb([a[0] doubleValue], [a[1] doubleValue], [a[2] doubleValue]);
    }
    return wp_rgb(kWPColorDefault[slot][0], kWPColorDefault[slot][1], kWPColorDefault[slot][2]);
}

static void wp_set_color(int slot, NSColor *color)
{
    if (slot < 0 || slot >= WPColorCount || !color) {
        return;
    }
    NSColor *rgb = [color colorUsingColorSpace:NSColorSpace.genericRGBColorSpace];
    if (!rgb) {
        rgb = color;
    }
    NSArray *a = @[
        @(rgb.redComponent),
        @(rgb.greenComponent),
        @(rgb.blueComponent)
    ];
    [[NSUserDefaults standardUserDefaults] setObject:a forKey:kWPColorKey[slot]];
    [Gui.docView setNeedsDisplay:YES];
}

static void wp_reset_colors(void)
{
    int i;
    for (i = 0; i < WPColorCount; i++) {
        [[NSUserDefaults standardUserDefaults] removeObjectForKey:kWPColorKey[i]];
    }
    [Gui.docView setNeedsDisplay:YES];
}

static NSString *const kWPSchemesKey = @"wp.color.schemes";
static NSString *const kWPSchemeNameKey = @"wp.color.schemeName";
static NSString *const kWPBuiltinScheme = @"WP 5.1";
static NSString *const kWPNightScheme = @"Night";

/* Saved from the Night well set: near-black screen, green text, red bold. */
static const CGFloat kWPColorNight[WPColorCount][3] = {
    {0.000616, 0.003743, 0.025121},
    {0.00, 1.00, 0.00},
    {1.00, 0.121968, 0.00},
    {0.40, 0.90, 0.95},
    {0.00, 0.00, 0.00},
    {0.976229, 0.987033, 0.998637}
};
static NSString *const kWPSaveFormatKey = @"wp.save.format";
static NSString *const kWPWrapModeKey = @"wp.wrap.mode";
static NSString *const kWPForceFkeysKey = @"wp.fkey.forceScreen";
static NSString *const kWPSpellOnKey = @"wp.spell.on";
static NSString *const kWPSpellLangKey = @"wp.spell.lang";

static const struct {
    const char *title;
    const char *code;
} kWPSpellLangs[] = {
    { "English", "en" },
    { "Mandarin Chinese", "zh_CN" },
    { "Hindi", "hi" },
    { "Spanish", "es" },
    { "French", "fr" },
    { "Arabic", "ar" },
    { "Bengali", "bn" },
    { "Portuguese", "pt" },
    { "Russian", "ru" },
    { "Urdu", "ur" }
};

static int GspellTag;
static size_t GspellGuessLo;
static size_t GspellGuessHi;

static BOOL wp_force_fkeys(void)
{
    return [[NSUserDefaults standardUserDefaults] boolForKey:kWPForceFkeysKey];
}

static int wp_fmt_from_name(NSString *s)
{
    if ([s isEqualToString:@"md"]) {
        return IO_FMT_MD;
    }
    if ([s isEqualToString:@"pdf"]) {
        return IO_FMT_PDF;
    }
    if ([s isEqualToString:@"wpd"]) {
        return IO_FMT_WPD;
    }
    return IO_FMT_DOCX;
}

static NSString *wp_fmt_name(int fmt)
{
    switch (fmt) {
    case IO_FMT_MD:
        return @"md";
    case IO_FMT_PDF:
        return @"pdf";
    case IO_FMT_WPD:
        return @"wpd";
    default:
        return @"docx";
    }
}

static void wp_apply_io_prefs(void)
{
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    io_set_default_format(wp_fmt_from_name([ud stringForKey:kWPSaveFormatKey]));
    io_set_wrap_mode([[ud stringForKey:kWPWrapModeKey] isEqualToString:@"column"]
                         ? IO_WRAP_STRICT80
                         : IO_WRAP_WORD);
    if (Gview) {
        view_apply_prefs(Gview);
    }
}

static void wp_register_color_defaults(void)
{
    NSMutableDictionary *d = [NSMutableDictionary dictionary];
    int i;
    for (i = 0; i < WPColorCount; i++) {
        d[kWPColorKey[i]] = @[
            @(kWPColorDefault[i][0]),
            @(kWPColorDefault[i][1]),
            @(kWPColorDefault[i][2])
        ];
    }
    d[kWPSchemeNameKey] = kWPBuiltinScheme;
    d[kWPSaveFormatKey] = @"docx";
    d[kWPWrapModeKey] = @"word";
    d[kWPForceFkeysKey] = @NO;
    d[kWPSpellOnKey] = @NO;
    d[kWPSpellLangKey] = @"en";
    [[NSUserDefaults standardUserDefaults] registerDefaults:d];
}

static BOOL wp_spell_on(void)
{
    return [[NSUserDefaults standardUserDefaults] boolForKey:kWPSpellOnKey];
}

static NSString *wp_spell_lang_code(void)
{
    NSString *s = [[NSUserDefaults standardUserDefaults] stringForKey:kWPSpellLangKey];
    return s.length ? s : @"en";
}

static NSString *wp_spell_lang_resolved(void)
{
    NSString *want = wp_spell_lang_code();
    NSArray *avail = [NSSpellChecker sharedSpellChecker].availableLanguages;
    NSString *a;
    for (a in avail) {
        if ([a isEqualToString:want] || [a hasPrefix:[want stringByAppendingString:@"_"]] ||
            [a hasPrefix:[want stringByAppendingString:@"-"]]) {
            return a;
        }
    }
    for (a in avail) {
        if ([a.lowercaseString hasPrefix:want.lowercaseString]) {
            return a;
        }
    }
    return want;
}

static NSArray *wp_rgb_triple(NSColor *color)
{
    NSColor *rgb = [color colorUsingColorSpace:NSColorSpace.genericRGBColorSpace];
    if (!rgb) {
        rgb = color;
    }
    return @[
        @(rgb.redComponent),
        @(rgb.greenComponent),
        @(rgb.blueComponent)
    ];
}

static NSArray *wp_current_palette(void)
{
    NSMutableArray *pal = [NSMutableArray arrayWithCapacity:WPColorCount];
    int i;
    for (i = 0; i < WPColorCount; i++) {
        [pal addObject:wp_rgb_triple(wp_color(i))];
    }
    return pal;
}

static void wp_apply_palette(NSArray *pal)
{
    int i;
    if (![pal isKindOfClass:[NSArray class]] || pal.count < WPColorCount) {
        return;
    }
    for (i = 0; i < WPColorCount; i++) {
        NSArray *rgb = pal[i];
        if (![rgb isKindOfClass:[NSArray class]] || rgb.count < 3) {
            continue;
        }
        wp_set_color(i, wp_rgb([rgb[0] doubleValue], [rgb[1] doubleValue], [rgb[2] doubleValue]));
    }
}

static NSDictionary *wp_schemes(void)
{
    NSDictionary *d = [[NSUserDefaults standardUserDefaults] dictionaryForKey:kWPSchemesKey];
    return [d isKindOfClass:[NSDictionary class]] ? d : @{};
}

static BOOL wp_is_builtin_scheme(NSString *name)
{
    return [name isEqualToString:kWPBuiltinScheme] || [name isEqualToString:kWPNightScheme];
}

static NSArray *wp_builtin_palette(NSString *name)
{
    const CGFloat (*src)[3] = NULL;
    NSMutableArray *pal;
    int i;
    if (!name.length || [name isEqualToString:kWPBuiltinScheme]) {
        src = kWPColorDefault;
    } else if ([name isEqualToString:kWPNightScheme]) {
        src = kWPColorNight;
    } else {
        return nil;
    }
    pal = [NSMutableArray arrayWithCapacity:WPColorCount];
    for (i = 0; i < WPColorCount; i++) {
        [pal addObject:@[ @(src[i][0]), @(src[i][1]), @(src[i][2]) ]];
    }
    return pal;
}

static NSString *wp_scheme_name(void)
{
    NSString *n = [[NSUserDefaults standardUserDefaults] stringForKey:kWPSchemeNameKey];
    return n.length ? n : kWPBuiltinScheme;
}

static NSArray *wp_scheme_titles(void)
{
    NSMutableArray *names = [NSMutableArray arrayWithObjects:kWPBuiltinScheme, kWPNightScheme, nil];
    NSArray *user = [[wp_schemes() allKeys] sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
    for (NSString *n in user) {
        if (!wp_is_builtin_scheme(n)) {
            [names addObject:n];
        }
    }
    return names;
}

static void wp_load_scheme(NSString *name)
{
    NSArray *pal;
    if (!name.length) {
        name = kWPBuiltinScheme;
    }
    pal = wp_schemes()[name];
    if (![pal isKindOfClass:[NSArray class]]) {
        pal = wp_builtin_palette(name);
    }
    if (![pal isKindOfClass:[NSArray class]]) {
        return;
    }
    if ([name isEqualToString:kWPBuiltinScheme] && !wp_schemes()[name]) {
        wp_reset_colors();
    } else {
        wp_apply_palette(pal);
    }
    [[NSUserDefaults standardUserDefaults] setObject:name forKey:kWPSchemeNameKey];
}

static NSString *wp_save_scheme(NSString *name)
{
    name = [name stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (!name.length) {
        return nil;
    }
    if ([name isEqualToString:kWPBuiltinScheme]) {
        return nil;
    }
    NSMutableDictionary *d = [wp_schemes() mutableCopy];
    d[name] = wp_current_palette();
    [[NSUserDefaults standardUserDefaults] setObject:d forKey:kWPSchemesKey];
    [[NSUserDefaults standardUserDefaults] setObject:name forKey:kWPSchemeNameKey];
    return name;
}

static void wp_delete_scheme(NSString *name)
{
    if (!name.length || wp_is_builtin_scheme(name)) {
        return;
    }
    NSMutableDictionary *d = [wp_schemes() mutableCopy];
    [d removeObjectForKey:name];
    [[NSUserDefaults standardUserDefaults] setObject:d forKey:kWPSchemesKey];
    if ([wp_scheme_name() isEqualToString:name]) {
        wp_load_scheme(kWPBuiltinScheme);
    }
}

static BOOL wp_iokit_named(const char *name)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceNameMatching(name));
    if (!svc) {
        return NO;
    }
    IOObjectRelease(svc);
    return YES;
}

static BOOL wp_iokit_class(const char *name)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching(name));
    if (!svc) {
        return NO;
    }
    IOObjectRelease(svc);
    return YES;
}

static BOOL wp_touchbar_server_running(void)
{
    NSTask *t = [[NSTask alloc] init];
    t.launchPath = @"/usr/bin/pgrep";
    t.arguments = @[ @"-x", @"TouchBarServer" ];
    t.standardOutput = [NSPipe pipe];
    t.standardError = [NSPipe pipe];
    @try {
        [t launch];
        [t waitUntilExit];
    } @catch (NSException *e) {
        (void)e;
        return NO;
    }
    return t.terminationStatus == 0;
}

static BOOL wp_has_keyboard_touch_strip(void)
{
    if (@available(macOS 10.12.2, *)) {
        if (wp_iokit_named("backlight-dfr") ||
            wp_iokit_named("dispdfr") ||
            wp_iokit_named("dfr-drv") ||
            wp_iokit_named("lcddfr") ||
            wp_iokit_class("AppleDFRInterface") ||
            wp_iokit_class("AppleDFRBrightness") ||
            wp_touchbar_server_running()) {
            return YES;
        }
    }
    return NO;
}

static void gui_spell_recheck(void)
{
    NSMutableString *text;
    NSMutableArray *map;
    NSSpellChecker *chk;
    NSString *lang;
    NSInteger start;
    size_t i;
    Gview->spell_n = 0;
    if (!wp_spell_on() || !Gdoc) {
        return;
    }
    if (GspellTag == 0) {
        GspellTag = [NSSpellChecker uniqueSpellDocumentTag];
    }
    text = [NSMutableString string];
    map = [NSMutableArray array];
    for (i = 0; i < Gdoc->len; ) {
        size_t n = doc_unit_len(Gdoc, i);
        uint8_t b;
        if (!n) {
            break;
        }
        b = Gdoc->data[i];
        if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
            [text appendString:@"\n"];
            [map addObject:@(i)];
        } else if (doc_is_visible(Gdoc, i)) {
            size_t wn = 0;
            char *frag = doc_range_utf8(Gdoc, i, i + n, &wn);
            if (frag && wn) {
                NSString *piece = [[NSString alloc] initWithBytes:frag length:wn encoding:NSUTF8StringEncoding];
                NSUInteger k;
                for (k = 0; k < piece.length; k++) {
                    [text appendString:[piece substringWithRange:NSMakeRange(k, 1)]];
                    [map addObject:@(i)];
                }
            }
            free(frag);
        }
        i += n;
    }
    chk = [NSSpellChecker sharedSpellChecker];
    lang = wp_spell_lang_resolved();
    [chk setLanguage:lang];
    start = 0;
    while (start < (NSInteger)text.length && Gview->spell_n < 400) {
        NSRange r = [chk checkSpellingOfString:text
                                    startingAt:start
                                      language:lang
                                          wrap:NO
                        inSpellDocumentWithTag:GspellTag
                                     wordCount:NULL];
        if (r.location == NSNotFound || r.length == 0) {
            break;
        }
        if (r.location < map.count) {
            NSUInteger last = r.location + r.length - 1;
            size_t lo = [map[r.location] unsignedLongValue];
            size_t hi_unit;
            if (last >= map.count) {
                last = map.count - 1;
            }
            hi_unit = [map[last] unsignedLongValue];
            Gview->spell_lo[Gview->spell_n] = lo;
            Gview->spell_hi[Gview->spell_n] = hi_unit + doc_unit_len(Gdoc, hi_unit);
            Gview->spell_n++;
        }
        start = (NSInteger)(r.location + r.length);
    }
}

static void gui_refresh(void)
{
    if (!Gapp->running) {
        [NSApp terminate:nil];
        return;
    }
    view_apply_prefs(Gview);
    view_relayout(Gview, Gdoc);
    gui_spell_recheck();
    view_ensure_cursor_visible(Gview);
    view_status(Gview, Gdoc);
    app_prepare_draw(Gapp, Gview);
    if (screen_resize(&Gscr, WP51_ROWS, WP51_COLS) != 0) {
        return;
    }
    app_render(Gapp, Gview, Gdoc, &Gscr);
    [Gui.docView setNeedsDisplay:YES];
    [Gui.docView displayIfNeeded];
    [Gui.strip setNeedsDisplay:YES];
    if (Gdoc->path[0]) {
        Gui.window.title = [NSString stringWithUTF8String:Gdoc->path];
    } else {
        Gui.window.title = @"WordPerfect 5.1";
    }
    if (Gdoc->dirty) {
        Gui.window.documentEdited = YES;
    } else {
        Gui.window.documentEdited = NO;
    }
}

static void gui_send(int key)
{
    app_handle_key(Gapp, Gdoc, Gview, key);
    gui_refresh();
}

static void gui_clip_copy(void)
{
    size_t lo, hi, n = 0;
    char *utf;
    if (!doc_sel_active(Gdoc)) {
        return;
    }
    doc_sel_bounds(Gdoc, &lo, &hi);
    utf = doc_range_utf8(Gdoc, lo, hi, &n);
    if (utf) {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        [pb setString:[[NSString alloc] initWithBytes:utf length:n encoding:NSUTF8StringEncoding]
              forType:NSPasteboardTypeString];
        free(utf);
    }
}

static void gui_clip_cut(void)
{
    if (!doc_sel_active(Gdoc) || Gapp->mode != MODE_EDIT) {
        return;
    }
    gui_clip_copy();
    doc_sel_delete(Gdoc);
    gui_refresh();
}

static void gui_clip_paste(void)
{
    NSString *s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (!s.length) {
        return;
    }
    app_insert_text(Gapp, Gdoc, Gview, s.UTF8String);
    gui_refresh();
}

static void gui_clip_select_all(void)
{
    if (Gapp->mode != MODE_EDIT) {
        return;
    }
    doc_sel_all(Gdoc);
    gui_refresh();
}

static int map_event(NSEvent *event)
{
    NSString *chars = event.charactersIgnoringModifiers;
    if (chars.length == 0) {
        return 0;
    }
    unichar c = [chars characterAtIndex:0];
    NSUInteger mods = event.modifierFlags;
    int shifted = (mods & NSEventModifierFlagShift) != 0;
    int ctrl = (mods & NSEventModifierFlagControl) != 0;
    int cmd = (mods & NSEventModifierFlagCommand) != 0;

    /* macOS owns Ctrl-F8 (status menus). Font is Cmd-T, Shift-F8, Option-F8. */
    if (c == NSF8FunctionKey && (ctrl || shifted || (mods & NSEventModifierFlagOption))) {
        return WP_KEY_FONT;
    }
    if (ctrl && shifted && !cmd) {
        if (c == '1' || c == NSF1FunctionKey) {
            return WP_KEY_FINE;
        }
        if (c == '2' || c == NSF2FunctionKey) {
            return WP_KEY_SMALL;
        }
        if (c == '3' || c == NSF3FunctionKey) {
            return WP_KEY_LARGE;
        }
        if (c == '4' || c == NSF4FunctionKey) {
            return WP_KEY_VRY;
        }
        if (c == '5' || c == NSF5FunctionKey) {
            return WP_KEY_EXT;
        }
        if (c == '0' || c == NSF6FunctionKey) {
            return WP_KEY_SIZE_NORMAL;
        }
        unichar lc = (c >= 'A' && c <= 'Z') ? (unichar)(c + 32) : c;
        if (lc == 'n') {
            return WP_KEY_NORMAL;
        }
        if (lc == 'j') {
            return WP_KEY_JUST_FULL;
        }
        if (lc == 'l') {
            return WP_KEY_JUST;
        }
    }

    if (cmd && !ctrl) {
        unichar lc = (c >= 'A' && c <= 'Z') ? (unichar)(c + 32) : c;
        if (lc == 'b') {
            return KEY_F(6);
        }
        if (lc == 'u') {
            return KEY_F(8);
        }
        if (lc == 'e') {
            return KEY_F(12);
        }
        if (lc == 'i') {
            return WP_KEY_ITALIC;
        }
        if (lc == 't') {
            return WP_KEY_FONT;
        }
    }

    if (c == NSF1FunctionKey) {
        return KEY_F(1);
    }
    if (c == NSF2FunctionKey) {
        return shifted ? KEY_F(14) : KEY_F(2);
    }
    if (c == NSF3FunctionKey) {
        return KEY_F(3);
    }
    if (c == NSF4FunctionKey) {
        return KEY_F(4);
    }
    if (c == NSF5FunctionKey) {
        return KEY_F(5);
    }
    if (c == NSF6FunctionKey) {
        return KEY_F(6);
    }
    if (c == NSF7FunctionKey) {
        return KEY_F(7);
    }
    if (c == NSF8FunctionKey) {
        return KEY_F(8);
    }
    if (c == NSF9FunctionKey) {
        return KEY_F(9);
    }
    if (c == NSF10FunctionKey) {
        return KEY_F(10);
    }
    if (c == NSF11FunctionKey) {
        return KEY_F(11);
    }
    if (c == NSF12FunctionKey) {
        return KEY_F(12);
    }
    if (c == NSLeftArrowFunctionKey) {
        return KEY_LEFT;
    }
    if (c == NSRightArrowFunctionKey) {
        return KEY_RIGHT;
    }
    if (c == NSUpArrowFunctionKey) {
        return KEY_UP;
    }
    if (c == NSDownArrowFunctionKey) {
        return KEY_DOWN;
    }
    if (c == NSHomeFunctionKey) {
        return KEY_HOME;
    }
    if (c == NSEndFunctionKey) {
        return KEY_END;
    }
    if (c == NSPageUpFunctionKey) {
        return KEY_PPAGE;
    }
    if (c == NSPageDownFunctionKey) {
        return KEY_NPAGE;
    }
    if (c == NSDeleteFunctionKey) {
        return KEY_DC;
    }
    if (c == 3) {
        return 3;
    }
    if (c == 27) {
        return KEY_F(1);
    }
    if (c == 127 || c == 8) {
        return KEY_BACKSPACE;
    }
    if (c == '\r' || c == '\n') {
        return '\n';
    }
    if (c == '\t') {
        return '\t';
    }
    if (mods & NSEventModifierFlagCommand) {
        return 0;
    }
    NSString *typed = event.characters;
    if (typed.length == 0) {
        return 0;
    }
    unichar t = [typed characterAtIndex:0];
    if (t >= 32 && t < 127) {
        return (int)t;
    }
    return 0;
}

static NSString *wp_suggested_save_name(void)
{
    NSString *name = Gdoc->path[0]
        ? [[NSString stringWithUTF8String:Gdoc->path] lastPathComponent]
        : @"document";
    NSArray *exts = @[@"docx", @"md", @"markdown", @"pdf", @"wpd", @"wps", @"wp", @"wkb"];
    NSString *e;
    for (e in exts) {
        NSString *suf = [@"." stringByAppendingString:e];
        if ([name.lowercaseString hasSuffix:suf]) {
            name = [name substringToIndex:name.length - suf.length];
            break;
        }
    }
    return [name stringByAppendingPathExtension:[NSString stringWithUTF8String:io_format_ext(io_default_format())]];
}

static NSArray<UTType *> *wp_doc_types(void)
{
    NSMutableArray *a = [NSMutableArray array];
    int pref = io_default_format();
    NSArray *exts = @[
        [NSString stringWithUTF8String:io_format_ext(pref)],
        @"docx", @"md", @"pdf", @"wpd", @"wps", @"wp", @"wkb"
    ];
    NSString *e;
    NSMutableSet *seen = [NSMutableSet set];
    for (e in exts) {
        UTType *t;
        if ([seen containsObject:e]) {
            continue;
        }
        [seen addObject:e];
        t = [UTType typeWithFilenameExtension:e];
        if (t) {
            [a addObject:t];
        }
    }
    if (!a.count) {
        [a addObject:UTTypeData];
    }
    return a;
}

static int gui_save_pdf_print(const char *path);

static int gui_open_path(const char *path)
{
    if (!path || !path[0] || !Gdoc) {
        return -1;
    }
    if (io_load(Gdoc, path) != 0) {
        if (Gview) {
            view_message(Gview, "ERROR: cannot open document");
            gui_refresh();
        }
        return -1;
    }
    if (Gview) {
        view_message(Gview, "Retrieved %s", path);
        gui_refresh();
    }
    return 0;
}

static void wp_claim_document_types(void)
{
    NSURL *appURL = [[NSBundle mainBundle] bundleURL];
    if (![appURL.pathExtension.lowercaseString isEqualToString:@"app"]) {
        return;
    }
    if (@available(macOS 12.0, *)) {
        NSArray<NSString *> *exts = @[ @"wpd" ];
        NSString *ext;
        for (ext in exts) {
            UTType *t = [UTType typeWithFilenameExtension:ext];
            if (!t) {
                continue;
            }
            [NSWorkspace.sharedWorkspace setDefaultApplicationAtURL:appURL
                                                  toOpenContentType:t
                                                  completionHandler:^(NSError *err) {
                                                      (void)err;
                                                  }];
        }
        UTType *own = [UTType typeWithIdentifier:@"uk.wp51.document"];
        if (own) {
            [NSWorkspace.sharedWorkspace setDefaultApplicationAtURL:appURL
                                                  toOpenContentType:own
                                                  completionHandler:^(NSError *err) {
                                                      (void)err;
                                                  }];
        }
    }
}

@implementation WPDocument

- (void)makeWindowControllers
{
}

+ (BOOL)autosavesInPlace
{
    return NO;
}

- (BOOL)readFromURL:(NSURL *)url ofType:(NSString *)typeName error:(NSError **)outError
{
    (void)typeName;
    if (url.isFileURL && gui_open_path(url.fileSystemRepresentation) == 0) {
        return YES;
    }
    if (outError) {
        *outError = [NSError errorWithDomain:NSCocoaErrorDomain
                                        code:NSFileReadCorruptFileError
                                    userInfo:@{ NSLocalizedDescriptionKey: @"Cannot open WordPerfect document." }];
    }
    return NO;
}

- (BOOL)readFromData:(NSData *)data ofType:(NSString *)typeName error:(NSError **)outError
{
    (void)typeName;
    if (!Gdoc || !data.length || wpd_decode(Gdoc, data.bytes, data.length) != 0) {
        if (outError) {
            *outError = [NSError errorWithDomain:NSCocoaErrorDomain
                                            code:NSFileReadCorruptFileError
                                        userInfo:@{ NSLocalizedDescriptionKey: @"Cannot open WordPerfect document." }];
        }
        return NO;
    }
    Gdoc->path[0] = 0;
    Gdoc->dirty = 0;
    if (Gview) {
        view_message(Gview, "Retrieved document");
        gui_refresh();
    }
    return YES;
}

@end

static void gui_open(void)
{
    NSOpenPanel *p = [NSOpenPanel openPanel];
    p.canChooseFiles = YES;
    p.canChooseDirectories = NO;
    p.allowsMultipleSelection = NO;
    p.allowedContentTypes = wp_doc_types();
    if ([p runModal] != NSModalResponseOK) {
        return;
    }
    gui_open_path(p.URL.path.fileSystemRepresentation);
}

static void gui_save_to(const char *path)
{
    char use[1200];
    char real[1200];
    int fmt = io_format_from_path(path);
    real[0] = '\0';
    if (fmt < 0) {
        fmt = io_default_format();
    }
    if (io_path_for_format(path, fmt, use, sizeof(use)) != 0) {
        view_message(Gview, "ERROR: cannot save %s", path);
        return;
    }
    if (fmt == IO_FMT_PDF && gui_save_pdf_print(use) == 0) {
        snprintf(Gdoc->path, sizeof(Gdoc->path), "%s", use);
        Gdoc->dirty = 0;
        view_message(Gview, "Document saved: %s", use);
        return;
    }
    if (io_save(Gdoc, use, real, sizeof(real)) != 0) {
        view_message(Gview, "ERROR: cannot save %s", use);
        return;
    }
    snprintf(Gdoc->path, sizeof(Gdoc->path), "%s", real[0] ? real : use);
    Gdoc->dirty = 0;
    view_message(Gview, "Document saved: %s", Gdoc->path);
}

static void gui_save_as(void)
{
    NSSavePanel *p = [NSSavePanel savePanel];
    p.allowedContentTypes = wp_doc_types();
    p.allowsOtherFileTypes = YES;
    p.nameFieldStringValue = wp_suggested_save_name();
    if ([p runModal] != NSModalResponseOK) {
        return;
    }
    gui_save_to(p.URL.path.fileSystemRepresentation);
    gui_refresh();
}

static void gui_save(void)
{
    char use[1200];
    if (!Gdoc->path[0]) {
        gui_save_as();
        return;
    }
    if (io_path_for_format(Gdoc->path, io_default_format(), use, sizeof(use)) != 0) {
        view_message(Gview, "ERROR: cannot save");
        return;
    }
    gui_save_to(use);
    gui_refresh();
}

@implementation FKeyStripView

- (NSButton *)makeModButton:(NSString *)title tag:(NSInteger)tag
{
    NSButton *b = [NSButton buttonWithTitle:title target:self action:@selector(hitMod:)];
    b.tag = tag;
    b.buttonType = NSButtonTypePushOnPushOff;
    b.bezelStyle = NSBezelStyleFlexiblePush;
    b.font = [NSFont boldSystemFontOfSize:10];
    b.focusRingType = NSFocusRingTypeNone;
    return b;
}

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (!self) {
        return nil;
    }
    int i;
    self.fbtns = [NSMutableArray array];
    self.ctrlBtn = [self makeModButton:@"Ctrl" tag:100];
    self.shiftBtn = [self makeModButton:@"Shift" tag:101];
    [self addSubview:self.ctrlBtn];
    [self addSubview:self.shiftBtn];
    for (i = 0; i < 12; i++) {
        NSButton *b = [NSButton buttonWithTitle:@"" target:self action:@selector(hit:)];
        b.tag = i + 1;
        b.bezelStyle = NSBezelStyleFlexiblePush;
        b.font = [NSFont systemFontOfSize:10];
        b.focusRingType = NSFocusRingTypeNone;
        [self addSubview:b];
        [self.fbtns addObject:b];
    }
    [self reloadTitles];
    return self;
}

- (void)reloadTitles
{
    int i;
    self.ctrlBtn.state = (g_fkey_mod == FKEY_MOD_CTRL) ? NSControlStateValueOn : NSControlStateValueOff;
    self.shiftBtn.state = (g_fkey_mod == FKEY_MOD_SHIFT) ? NSControlStateValueOn : NSControlStateValueOff;
    for (i = 0; i < 12; i++) {
        self.fbtns[i].title = [NSString stringWithFormat:@"%s\nF%d", view_fkey_name(i, g_fkey_mod), i + 1];
    }
}

- (NSSize)intrinsicContentSize
{
    return NSMakeSize(NSViewNoIntrinsicMetric, 30);
}

- (void)layout
{
    [super layout];
    NSUInteger n = 14;
    CGFloat w = self.bounds.size.width / n;
    NSUInteger i;
    self.ctrlBtn.frame = NSMakeRect(0, 0, floor(w), self.bounds.size.height);
    self.shiftBtn.frame = NSMakeRect(floor(w), 0, floor(w), self.bounds.size.height);
    for (i = 0; i < 12; i++) {
        self.fbtns[i].frame = NSMakeRect(floor((i + 2) * w), 0, floor(w), self.bounds.size.height);
    }
}

- (void)hitMod:(NSButton *)btn
{
    gui_set_fkey_mod(btn.tag == 100 ? FKEY_MOD_CTRL : FKEY_MOD_SHIFT);
}

- (void)hit:(NSButton *)btn
{
    int key = gui_fkey_mapped((int)btn.tag);
    if (key) {
        gui_send(key);
    }
}

@end

@implementation WPDocView

- (BOOL)acceptsFirstResponder
{
    return YES;
}

typedef struct {
    CGFloat ox;
    CGFloat cw;
    CGFloat ch;
} GuiCrt;

/* 80x25 at 4:3. Use the full window height; leftover width is split left/right. */
static GuiCrt gui_crt_layout(NSRect bounds, int cols, int rows)
{
    GuiCrt m = {0, 6, 10};
    CGFloat ch, cw, text_w, text_h, max_w;
    if (cols < 1) {
        cols = WP51_COLS;
    }
    if (rows < 1) {
        rows = WP51_ROWS;
    }
    ch = floor(bounds.size.height / rows);
    if (ch < 10) {
        ch = 10;
    }
    text_h = ch * (CGFloat)rows;
    text_w = floor(text_h * 4.0 / 3.0);
    cw = floor(text_w / cols);
    if (cw < 6) {
        cw = 6;
    }
    text_w = cw * (CGFloat)cols;
    max_w = bounds.size.width;
    if (text_w > max_w) {
        cw = floor(max_w / cols);
        if (cw < 6) {
            cw = 6;
        }
        text_w = cw * (CGFloat)cols;
    }
    m.cw = cw;
    m.ch = ch;
    m.ox = floor((max_w - text_w) / 2.0);
    if (m.ox < 0) {
        m.ox = 0;
    }
    return m;
}

static size_t gui_hit_pos(NSView *view, NSEvent *event)
{
    NSRect bounds = view.bounds;
    NSPoint pt = [view convertPoint:event.locationInWindow fromView:nil];
    int cols = Gscr.cols > 0 ? Gscr.cols : WP51_COLS;
    int rows = Gscr.rows > 0 ? Gscr.rows : WP51_ROWS;
    GuiCrt crt = gui_crt_layout(bounds, cols, rows);
    CGFloat cw = crt.cw;
    CGFloat ch = crt.ch;
    int col, row, line;
    int text_rows = 0;
    int reveal_y = 0;
    int reveal_rows = 0;
    col = (int)floor((pt.x - crt.ox) / cw);
    row = (int)((bounds.size.height - pt.y) / ch);
    if (col < 0) {
        col = 0;
    }
    if (row < 0) {
        row = 0;
    }
    view_relayout(Gview, Gdoc);
    view_split(Gview, rows, &text_rows, &reveal_y, &reveal_rows);
    if (Gview->reveal && reveal_rows > 0 && row >= reveal_y && row < reveal_y + reveal_rows) {
        return view_pos_at_reveal(Gview, Gdoc, cols, row - reveal_y, col);
    }
    if (row >= text_rows) {
        row = text_rows - 1;
    }
    line = Gview->top_line + row;
    return view_pos_at(Gview, Gdoc, line, col);
}

- (void)mouseDown:(NSEvent *)event
{
    size_t pos = gui_hit_pos(self, event);
    self.clickAnchor = pos;
    self.didDrag = NO;
    if (event.modifierFlags & NSEventModifierFlagShift) {
        if (Gdoc->mark == SIZE_MAX) {
            Gdoc->mark = Gdoc->cursor;
        }
    } else {
        Gdoc->mark = SIZE_MAX;
    }
    Gdoc->cursor = pos;
    [self.window makeFirstResponder:self];
    gui_refresh();
}

- (void)mouseDragged:(NSEvent *)event
{
    size_t pos = gui_hit_pos(self, event);
    self.didDrag = YES;
    if (Gdoc->mark == SIZE_MAX) {
        Gdoc->mark = self.clickAnchor;
    }
    Gdoc->cursor = pos;
    gui_refresh();
}

- (void)mouseUp:(NSEvent *)event
{
    size_t pos = gui_hit_pos(self, event);
    int k;
    if (self.didDrag || !wp_spell_on() || Gapp->mode != MODE_EDIT) {
        return;
    }
    for (k = 0; k < Gview->spell_n; k++) {
        if (pos >= Gview->spell_lo[k] && pos < Gview->spell_hi[k]) {
            [self showSpellMenuAt:[self convertPoint:event.locationInWindow fromView:nil]
                              lo:Gview->spell_lo[k]
                              hi:Gview->spell_hi[k]];
            return;
        }
    }
    (void)event;
}

- (void)showSpellMenuAt:(NSPoint)winPt lo:(size_t)lo hi:(size_t)hi
{
    size_t n = 0;
    char *utf = doc_range_utf8(Gdoc, lo, hi, &n);
    NSString *word = utf ? [[NSString alloc] initWithBytes:utf length:n encoding:NSUTF8StringEncoding] : nil;
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Spelling"];
    NSArray *guesses;
    NSSpellChecker *chk = [NSSpellChecker sharedSpellChecker];
    NSString *lang = wp_spell_lang_resolved();
    free(utf);
    if (!word.length) {
        return;
    }
    GspellGuessLo = lo;
    GspellGuessHi = hi;
    guesses = [chk guessesForWordRange:NSMakeRange(0, word.length)
                              inString:word
                              language:lang
                inSpellDocumentWithTag:GspellTag];
    if (guesses.count == 0) {
        NSMenuItem *none = [menu addItemWithTitle:@"No suggestions" action:nil keyEquivalent:@""];
        none.enabled = NO;
    } else {
        for (NSString *g in guesses) {
            NSMenuItem *it = [menu addItemWithTitle:g action:@selector(applySpellGuess:) keyEquivalent:@""];
            it.target = self;
            it.representedObject = g;
        }
    }
    [menu addItem:[NSMenuItem separatorItem]];
    {
        NSMenuItem *ign = [menu addItemWithTitle:@"Ignore" action:@selector(ignoreSpellWord:) keyEquivalent:@""];
        ign.target = self;
        ign.representedObject = word;
    }
    [menu popUpMenuPositioningItem:nil atLocation:winPt inView:self];
}

- (void)applySpellGuess:(NSMenuItem *)item
{
    NSString *g = item.representedObject;
    if (!g.length || Gapp->mode != MODE_EDIT) {
        return;
    }
    Gdoc->mark = GspellGuessLo;
    Gdoc->cursor = GspellGuessHi;
    doc_insert_utf8(Gdoc, g.UTF8String);
    gui_refresh();
}

- (void)ignoreSpellWord:(NSMenuItem *)item
{
    NSString *w = item.representedObject;
    if (w.length) {
        [[NSSpellChecker sharedSpellChecker] ignoreWord:w inSpellDocumentWithTag:GspellTag];
    }
    gui_refresh();
}

- (void)copy:(id)sender { (void)sender; gui_clip_copy(); }
- (void)cut:(id)sender { (void)sender; gui_clip_cut(); }
- (void)paste:(id)sender { (void)sender; gui_clip_paste(); }
- (void)selectAll:(id)sender { (void)sender; gui_clip_select_all(); }
- (void)save:(id)sender { (void)sender; gui_save(); gui_refresh(); }
- (void)saveDocument:(id)sender { [self save:sender]; }

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    SEL a = item.action;
    if (a == @selector(cut:) || a == @selector(copy:)) {
        return doc_sel_active(Gdoc);
    }
    if (a == @selector(paste:)) {
        return [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString].length > 0;
    }
    return YES;
}

- (BOOL)performKeyEquivalent:(NSEvent *)event
{
    NSUInteger mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    NSString *c;
    if (!(mods & NSEventModifierFlagCommand) ||
        (mods & (NSEventModifierFlagOption | NSEventModifierFlagControl))) {
        return [super performKeyEquivalent:event];
    }
    c = event.charactersIgnoringModifiers.lowercaseString;
    if ([c isEqualToString:@"s"]) {
        if (mods & NSEventModifierFlagShift) {
            gui_save_as();
        } else {
            gui_save();
            gui_refresh();
        }
        return YES;
    }
    if ([c isEqualToString:@"x"]) {
        gui_clip_cut();
        return YES;
    }
    if ([c isEqualToString:@"b"]) {
        gui_send(KEY_F(6));
        return YES;
    }
    if ([c isEqualToString:@"u"]) {
        gui_send(KEY_F(8));
        return YES;
    }
    if ([c isEqualToString:@"e"]) {
        gui_send(KEY_F(12));
        return YES;
    }
    if ([c isEqualToString:@"i"]) {
        gui_send(WP_KEY_ITALIC);
        return YES;
    }
    if ([c isEqualToString:@"t"]) {
        gui_send(WP_KEY_FONT);
        return YES;
    }
    return [super performKeyEquivalent:event];
}

- (void)keyDown:(NSEvent *)event
{
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        if ([self performKeyEquivalent:event]) {
            return;
        }
        [super keyDown:event];
        return;
    }
    if (Gapp->mode == MODE_EDIT) {
        NSString *chars = event.charactersIgnoringModifiers;
        unichar c = chars.length ? [chars characterAtIndex:0] : 0;
        int extend = (event.modifierFlags & NSEventModifierFlagShift) != 0;
        if (c == NSLeftArrowFunctionKey) {
            doc_nav_left(Gdoc, extend, Gview->reveal);
            gui_refresh();
            return;
        }
        if (c == NSRightArrowFunctionKey) {
            doc_nav_right(Gdoc, extend, Gview->reveal);
            gui_refresh();
            return;
        }
        if (c == NSUpArrowFunctionKey) {
            view_nav_vert(Gview, Gdoc, -1, extend);
            gui_refresh();
            return;
        }
        if (c == NSDownArrowFunctionKey) {
            view_nav_vert(Gview, Gdoc, 1, extend);
            gui_refresh();
            return;
        }
        if (c == NSHomeFunctionKey) {
            doc_nav_home(Gdoc, extend);
            gui_refresh();
            return;
        }
        if (c == NSEndFunctionKey) {
            doc_nav_end(Gdoc, extend);
            gui_refresh();
            return;
        }
    }
    if (Gapp->mode == MODE_CONFIRM || Gapp->mode == MODE_LIST || Gapp->mode == MODE_FONT ||
        Gapp->mode == MODE_JUST) {
        int key = map_event(event);
        if (key) {
            gui_send(key);
        }
        return;
    }
    if ([self hasMarkedText]) {
        [self interpretKeyEvents:@[event]];
        return;
    }
    int key = map_event(event);
    if (key && (key < 32 || key >= 127)) {
        gui_send(key);
        return;
    }
    [self interpretKeyEvents:@[event]];
}

- (void)doCommandBySelector:(SEL)sel
{
    if (sel == @selector(insertNewline:)) {
        gui_send('\n');
        return;
    }
    if (sel == @selector(insertTab:)) {
        gui_send('\t');
        return;
    }
    if (sel == @selector(deleteBackward:)) {
        gui_send(KEY_BACKSPACE);
        return;
    }
    if (sel == @selector(deleteForward:)) {
        gui_send(KEY_DC);
        return;
    }
    if (sel == @selector(cancelOperation:)) {
        gui_send(KEY_F(1));
        return;
    }
}

- (void)insertText:(id)string
{
    [self insertText:string replacementRange:NSMakeRange(NSNotFound, 0)];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    NSString *s = [string isKindOfClass:[NSAttributedString class]]
        ? [(NSAttributedString *)string string]
        : string;
    (void)replacementRange;
    self.markedText = @"";
    self.markedSel = NSMakeRange(0, 0);
    if (Gapp->mode == MODE_CONFIRM && s.length == 1) {
        unichar c = [s characterAtIndex:0];
        if (c == 'y' || c == 'Y' || c == 'n' || c == 'N') {
            gui_send((int)c);
            return;
        }
    }
    if ((Gapp->mode == MODE_FONT || Gapp->mode == MODE_JUST) && s.length == 1) {
        unichar c = [s characterAtIndex:0];
        if (c >= '0' && c <= '9') {
            gui_send((int)c);
            return;
        }
    }
    if (s.length) {
        app_insert_text(Gapp, Gdoc, Gview, s.UTF8String);
        gui_refresh();
    }
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
    NSString *s = [string isKindOfClass:[NSAttributedString class]]
        ? [(NSAttributedString *)string string]
        : string;
    (void)replacementRange;
    self.markedText = s ?: @"";
    self.markedSel = selectedRange;
    [self setNeedsDisplay:YES];
}

- (void)unmarkText
{
    self.markedText = @"";
    self.markedSel = NSMakeRange(0, 0);
}

- (NSRange)selectedRange
{
    return NSMakeRange(NSNotFound, 0);
}

- (NSRange)markedRange
{
    return self.markedText.length ? NSMakeRange(0, self.markedText.length) : NSMakeRange(NSNotFound, 0);
}

- (BOOL)hasMarkedText
{
    return self.markedText.length > 0;
}

- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText
{
    return @[NSUnderlineStyleAttributeName];
}

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
    (void)range;
    if (actualRange) {
        *actualRange = NSMakeRange(NSNotFound, 0);
    }
    return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    (void)point;
    return (NSUInteger)NSNotFound;
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
    NSRect bounds = self.bounds;
    CGFloat cw, ch, x, y;
    NSRect r;
    (void)range;
    if (actualRange) {
        *actualRange = range;
    }
    if (Gscr.cols <= 0 || Gscr.rows <= 0) {
        return [self.window convertRectToScreen:[self convertRect:self.bounds toView:nil]];
    }
    {
        GuiCrt crt = gui_crt_layout(bounds, Gscr.cols, Gscr.rows);
        cw = crt.cw;
        ch = crt.ch;
        x = crt.ox + Gscr.cx * cw;
    }
    y = bounds.size.height - (Gscr.cy + 1) * ch;
    r = NSMakeRect(x, y, cw * 2, ch);
    return [self.window convertRectToScreen:[self convertRect:r toView:nil]];
}

- (NSTouchBar *)makeTouchBar
{
    if (@available(macOS 10.12.2, *)) {
        return [Gui makeTouchBar];
    }
    return nil;
}

static BOOL wp_cp_is_cjk(uint32_t cp)
{
    return (cp >= 0x2E80 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE10 && cp <= 0xFE6F) ||
           (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0x20000 && cp <= 0x3FFFD);
}

static NSFont *wp_cjk_font(CGFloat size, BOOL bold)
{
    NSArray *names = bold
        ? @[ @"PingFangSC-Semibold", @"HiraginoSansGB-W6", @"STHeiti", @"Menlo-Bold" ]
        : @[ @"PingFangSC-Regular", @"HiraginoSansGB-W3", @"STHeiti", @"Menlo" ];
    for (NSString *n in names) {
        NSFont *f = [NSFont fontWithName:n size:size];
        if (f) {
            return f;
        }
    }
    return bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];
}

static void gui_paint_screen(NSRect bounds, const Screen *scr, BOOL blinkOn)
{
    NSColor *blue = wp_color(WPColorBg);
    [blue setFill];
    NSRectFill(bounds);

    if (!scr || !scr->cells || scr->cols <= 0 || scr->rows <= 0) {
        return;
    }
    GuiCrt crt = gui_crt_layout(bounds, scr->cols, scr->rows);
    CGFloat cw = crt.cw;
    CGFloat ch = crt.ch;
    NSFont *probe = [NSFont fontWithName:@"Menlo" size:12] ?: [NSFont userFixedPitchFontOfSize:12];
    NSSize em = [@"M" sizeWithAttributes:@{NSFontAttributeName: probe}];
    /* Fill the cell: Latin was sized to 92% of width and left-aligned, so it looked sparse. */
    CGFloat fs = 12.0 * MIN(cw / MAX(em.width, 1), (ch * 0.88) / MAX(em.height, 1));
    if (fs < 8) {
        fs = 8;
    }
    NSFont *font = [NSFont fontWithName:@"Menlo" size:fs] ?: [NSFont userFixedPitchFontOfSize:fs];
    NSFont *fontBold = [NSFont fontWithName:@"Menlo-Bold" size:fs] ?: [NSFont boldSystemFontOfSize:fs];
    NSFontManager *fm = [NSFontManager sharedFontManager];
    NSFont *fontIt = [fm convertFont:font toHaveTrait:NSFontItalicTrait] ?: font;
    NSFont *fontBI = [fm convertFont:fontBold toHaveTrait:NSFontItalicTrait] ?: fontBold;
    NSFont *cjkProbe = wp_cjk_font(fs, NO);
    NSSize cjkEm = [@"中" sizeWithAttributes:@{NSFontAttributeName: cjkProbe}];
    CGFloat cjkFs = fs * MIN((cw * 0.90) / MAX(cjkEm.width, 1), (ch * 0.86) / MAX(cjkEm.height, 1));
    if (cjkFs < 8) {
        cjkFs = 8;
    }
    NSFont *fontCjk = wp_cjk_font(cjkFs, NO);
    NSFont *fontCjkBold = wp_cjk_font(cjkFs, YES);
    NSColor *yellow = wp_color(WPColorText);
    NSColor *white = wp_color(WPColorBold);
    NSColor *cyan = wp_color(WPColorStatusBg);
    NSColor *black = wp_color(WPColorStatusFg);
    NSColor *cursorFill = wp_color(WPColorCursor);
    NSMutableParagraphStyle *para = [[NSMutableParagraphStyle alloc] init];
    para.alignment = NSTextAlignmentLeft;
    para.lineBreakMode = NSLineBreakByClipping;

    int y, x;
    CGFloat top = bounds.size.height;
    for (y = 0; y < scr->rows; y++) {
        CGFloat ypt = top - (y + 1) * ch;
        for (x = 0; x < scr->cols; x++) {
            Cell cell = scr->cells[y * scr->cols + x];
            unsigned a = cell.attr;
            int wide = cell.cp ? wp_cp_width(cell.cp) : 0;
            int cursor = scr->show_cursor && scr->cy == y && scr->cx == x && blinkOn;
            NSRect r = NSMakeRect(crt.ox + x * cw, ypt, (wide > 1 ? 2 : 1) * cw, ch);
            if (cell.cp == 0) {
                continue;
            }
            if (a & SCR_REVERSE) {
                [cyan setFill];
                NSRectFill(r);
            } else if (cursor) {
                [cursorFill setFill];
                NSRectFill(r);
            } else {
                [blue setFill];
                NSRectFill(r);
            }
            if (cell.cp == ' ') {
                continue;
            }
            NSColor *fg = (a & SCR_REVERSE) || cursor ? black : ((a & SCR_BOLD) ? white : yellow);
            if ((a & SCR_DIM) && !((a & SCR_REVERSE) || cursor)) {
                fg = [fg blendedColorWithFraction:0.4 ofColor:blue];
            }
            char u8[8];
            size_t n = screen_cell_utf8(&cell, u8, sizeof(u8));
            NSString *str = n
                ? [[NSString alloc] initWithBytes:u8 length:n encoding:NSUTF8StringEncoding]
                : nil;
            if (!str.length) {
                continue;
            }
            BOOL cjk = wp_cp_is_cjk(cell.cp);
            NSFont *use;
            if (cjk) {
                use = (a & SCR_BOLD) ? fontCjkBold : fontCjk;
            } else if ((a & SCR_BOLD) && (a & SCR_ITALIC)) {
                use = fontBI;
            } else if (a & SCR_ITALIC) {
                use = fontIt;
            } else if (a & SCR_BOLD) {
                use = fontBold;
            } else {
                use = font;
            }
            NSDictionary *attrs = @{
                NSFontAttributeName: use,
                NSForegroundColorAttributeName: fg,
                NSParagraphStyleAttributeName: para,
                NSUnderlineStyleAttributeName: ((a & SCR_UNDERLINE) ? @(NSUnderlineStyleSingle) : @(NSUnderlineStyleNone))
            };
            NSSize sz = [str sizeWithAttributes:attrs];
            NSPoint pt = NSMakePoint(r.origin.x + (r.size.width - sz.width) * 0.5,
                                     r.origin.y + (r.size.height - sz.height) * 0.5);
            [[NSGraphicsContext currentContext] saveGraphicsState];
            [[NSBezierPath bezierPathWithRect:r] addClip];
            [str drawAtPoint:pt withAttributes:attrs];
            if (a & SCR_SPELL) {
                NSBezierPath *wave = [NSBezierPath bezierPath];
                CGFloat wx, wy = r.origin.y + 1.5;
                int up = 0;
                wave.lineWidth = 1.1;
                [wave moveToPoint:NSMakePoint(r.origin.x, wy)];
                for (wx = r.origin.x + 2; wx <= NSMaxX(r); wx += 2) {
                    up = !up;
                    [wave lineToPoint:NSMakePoint(wx, wy + (up ? 1.6 : 0))];
                }
                [[NSColor colorWithCalibratedRed:1 green:0.15 blue:0.15 alpha:1] setStroke];
                [wave stroke];
            }
            [[NSGraphicsContext currentContext] restoreGraphicsState];
        }
    }
}

static BOOL wp_cp_is_cjk(uint32_t cp);
static NSFont *wp_cjk_font(CGFloat size, BOOL bold);

static int doc_has_font_code(const Doc *d)
{
    size_t i = 0;
    while (i < d->len) {
        if (d->data[i] == WP_FONT) {
            return 1;
        }
        i += doc_unit_len(d, i);
    }
    return 0;
}

static NSFont *wp_print_font(const DocFont *f, int default_times, NSString *sample)
{
    int pt = wp_effective_pt(f);
    int bold = (f->attrs & ATTR_BIT_BOLD) != 0;
    int italic = (f->attrs & ATTR_BIT_ITALIC) != 0;
    uint8_t fam = f->family;
    NSString *name;
    NSFont *font;
    NSUInteger k;
    if (pt < 8) {
        pt = 8;
    }
    if (default_times && fam == WP_FONT_COURIER) {
        fam = WP_FONT_TIMES;
    }
    if (sample.length) {
        for (k = 0; k < sample.length; k++) {
            unichar ch = [sample characterAtIndex:k];
            if (wp_cp_is_cjk(ch)) {
                return wp_cjk_font((CGFloat)pt, bold);
            }
        }
    }
    if (fam == WP_FONT_TIMES) {
        name = (bold && italic) ? @"Times-BoldItalic" : bold ? @"Times-Bold" : italic ? @"Times-Italic" : @"Times-Roman";
    } else if (fam == WP_FONT_HELVETICA) {
        name = (bold && italic) ? @"Helvetica-BoldOblique" : bold ? @"Helvetica-Bold" : italic ? @"Helvetica-Oblique" : @"Helvetica";
    } else {
        name = (bold && italic) ? @"Courier-BoldOblique" : bold ? @"Courier-Bold" : italic ? @"Courier-Oblique" : @"Courier";
    }
    font = [NSFont fontWithName:name size:(CGFloat)pt];
    return font ?: ([NSFont fontWithName:@"Times-Roman" size:(CGFloat)pt] ?: [NSFont userFontOfSize:(CGFloat)pt]);
}

static NSAttributedString *wp_doc_print_string(const Doc *d)
{
    NSMutableAttributedString *out = [[NSMutableAttributedString alloc] init];
    int default_times = !doc_has_font_code(d);
    size_t i = 0;
    while (i < d->len) {
        size_t pe = i;
        int center = 0;
        NSUInteger para0;
        NSMutableParagraphStyle *ps;
        while (pe < d->len) {
            uint8_t b = d->data[pe];
            if (b == WP_CENTER) {
                center = 1;
            }
            if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
                break;
            }
            pe += doc_unit_len(d, pe);
        }
        ps = [[NSMutableParagraphStyle alloc] init];
        {
            uint8_t just = doc_just_at(d, i);
            if (center || just == WP_JUST_CENTER) {
                ps.alignment = NSTextAlignmentCenter;
            } else if (just == WP_JUST_FULL) {
                ps.alignment = NSTextAlignmentJustified;
            } else if (just == WP_JUST_RIGHT) {
                ps.alignment = NSTextAlignmentRight;
            } else {
                ps.alignment = NSTextAlignmentLeft;
            }
        }
        ps.lineBreakMode = NSLineBreakByWordWrapping;
        ps.hyphenationFactor = 0.0;
        ps.lineSpacing = 0.0;
        ps.paragraphSpacing = 0.0;
        ps.paragraphSpacingBefore = 0.0;
        ps.lineHeightMultiple = 1.0;
        para0 = out.length;
        {
            size_t j = i;
            while (j < pe) {
                size_t n = doc_unit_len(d, j);
                uint8_t b = d->data[j];
                if (n && doc_is_visible(d, j) && b != WP_HARD_EOL && b != WP_SOFT_EOL &&
                    b != WP_HARD_PAGE) {
                    size_t wn = 0;
                    char *frag = doc_range_utf8(d, j, j + n, &wn);
                    if (frag && wn) {
                        NSString *s = [[NSString alloc] initWithBytes:frag
                                                              length:wn
                                                            encoding:NSUTF8StringEncoding];
                        if (s.length) {
                            DocFont f;
                            doc_font_at(d, j, &f);
                            NSDictionary *attrs = @{
                                NSFontAttributeName: wp_print_font(&f, default_times, s),
                                NSForegroundColorAttributeName: [NSColor blackColor],
                                NSParagraphStyleAttributeName: ps,
                                NSUnderlineStyleAttributeName:
                                    ((f.attrs & ATTR_BIT_UNDERLINE) ? @(NSUnderlineStyleSingle)
                                                                    : @(NSUnderlineStyleNone))
                            };
                            [out appendAttributedString:[[NSAttributedString alloc] initWithString:s
                                                                                        attributes:attrs]];
                        }
                    }
                    free(frag);
                }
                j += n ? n : 1;
            }
        }
        if (out.length > para0) {
            [out addAttribute:NSParagraphStyleAttributeName
                        value:ps
                        range:NSMakeRange(para0, out.length - para0)];
        }
        if (pe < d->len && d->data[pe] == WP_HARD_PAGE) {
            [out appendAttributedString:[[NSAttributedString alloc] initWithString:@"\f"]];
            i = pe + doc_unit_len(d, pe);
        } else if (pe < d->len && d->data[pe] == WP_SOFT_EOL) {
            /* Wrap-only [SRt]: let Core Text reflow. Keep a space if the next glyph needs one. */
            i = pe + doc_unit_len(d, pe);
        } else if (pe < d->len) {
            NSDictionary *nl = @{
                NSFontAttributeName: [NSFont fontWithName:@"Times-Roman" size:12] ?: [NSFont userFontOfSize:12],
                NSParagraphStyleAttributeName: ps
            };
            [out appendAttributedString:[[NSAttributedString alloc] initWithString:@"\n" attributes:nl]];
            i = pe + doc_unit_len(d, pe);
        } else {
            i = d->len;
        }
    }
    if (out.length == 0) {
        NSMutableParagraphStyle *empty = [[NSMutableParagraphStyle alloc] init];
        empty.lineBreakMode = NSLineBreakByWordWrapping;
        [out appendAttributedString:[[NSAttributedString alloc] initWithString:@" "
                                                                    attributes:@{
                                                                        NSFontAttributeName: [NSFont fontWithName:@"Times-Roman" size:12]
                                                                            ?: [NSFont userFontOfSize:12],
                                                                        NSParagraphStyleAttributeName: empty
                                                                    }]];
    }
    return out;
}

/* Letter page, 1" unprintable margins — Core Text places each glyph. */
static const CGFloat kWPPrintPageW = 612.0;
static const CGFloat kWPPrintPageH = 792.0;
static const CGFloat kWPPrintMargin = 72.0;
static const CGFloat kWPPrintTextW = 468.0;
static const CGFloat kWPPrintTextH = 648.0;

static void wp_print_begin_page(CGContextRef ctx, int *open)
{
    CGRect media = CGRectMake(0, 0, kWPPrintPageW, kWPPrintPageH);
    CGPDFContextBeginPage(ctx, NULL);
    CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
    CGContextFillRect(ctx, media);
    CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
    CGContextSaveGState(ctx);
    CGContextClipToRect(ctx, CGRectMake(kWPPrintMargin, kWPPrintMargin, kWPPrintTextW, kWPPrintTextH));
    *open = 1;
}

static void wp_print_end_page(CGContextRef ctx, int *open)
{
    if (*open) {
        CGContextRestoreGState(ctx);
        CGPDFContextEndPage(ctx);
        *open = 0;
    }
}

static int gui_draw_print_chunk(CGContextRef ctx, NSAttributedString *chunk)
{
    CTFramesetterRef fs;
    CFIndex loc = 0;
    int pages = 0;
    CGRect textRect = CGRectMake(kWPPrintMargin, kWPPrintMargin, kWPPrintTextW, kWPPrintTextH);

    if (!chunk) {
        return -1;
    }
    fs = CTFramesetterCreateWithAttributedString((__bridge CFAttributedStringRef)chunk);
    if (!fs) {
        return -1;
    }
    if (chunk.length == 0) {
        int open = 0;
        wp_print_begin_page(ctx, &open);
        wp_print_end_page(ctx, &open);
        CFRelease(fs);
        return 0;
    }
    do {
        int open = 0;
        CGMutablePathRef path;
        CTFrameRef frame;
        CFRange vis;
        wp_print_begin_page(ctx, &open);
        path = CGPathCreateMutable();
        CGPathAddRect(path, NULL, textRect);
        frame = CTFramesetterCreateFrame(fs, CFRangeMake(loc, 0), path, NULL);
        CTFrameDraw(frame, ctx);
        vis = CTFrameGetVisibleStringRange(frame);
        CFRelease(frame);
        CGPathRelease(path);
        wp_print_end_page(ctx, &open);
        pages++;
        if (vis.length <= 0) {
            break;
        }
        loc += vis.length;
    } while (loc < (CFIndex)chunk.length && pages < 200);
    CFRelease(fs);
    return 0;
}

static int gui_save_pdf_print(const char *path)
{
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    CGRect media = CGRectMake(0, 0, 612, 792);
    CGContextRef ctx = CGPDFContextCreateWithURL((__bridge CFURLRef)url, &media, NULL);
    NSAttributedString *full;
    NSString *plain;
    NSUInteger start = 0;
    if (!ctx) {
        return -1;
    }
    full = wp_doc_print_string(Gdoc);
    plain = full.string;
    while (start <= plain.length) {
        NSRange rest = NSMakeRange(start, plain.length - start);
        NSRange ff = rest.length ? [plain rangeOfString:@"\f" options:0 range:rest]
                                 : NSMakeRange(NSNotFound, 0);
        NSRange section = (ff.location == NSNotFound) ? rest
                                                      : NSMakeRange(start, ff.location - start);
        NSAttributedString *chunk = section.length
            ? [full attributedSubstringFromRange:section]
            : [[NSAttributedString alloc] initWithString:@" "];
        if (gui_draw_print_chunk(ctx, chunk) != 0) {
            CGPDFContextClose(ctx);
            CGContextRelease(ctx);
            return -1;
        }
        if (ff.location == NSNotFound) {
            break;
        }
        start = ff.location + 1;
        if (start >= plain.length) {
            break;
        }
    }
    CGPDFContextClose(ctx);
    CGContextRelease(ctx);
    return io_pdf_append_wpd(path, Gdoc);
}

int gui_write_print_pdf(Doc *d, const char *path)
{
    Doc *prev = Gdoc;
    int rc;
    if (!d || !path) {
        return -1;
    }
    Gdoc = d;
    rc = gui_save_pdf_print(path);
    Gdoc = prev;
    return rc;
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    gui_paint_screen(self.bounds, &Gscr, self.blinkOn);
    if (!self.markedText.length || Gscr.cols <= 0 || Gscr.rows <= 0) {
        return;
    }
    {
        NSRect bounds = self.bounds;
        GuiCrt crt = gui_crt_layout(bounds, Gscr.cols, Gscr.rows);
        CGFloat cw = crt.cw;
        CGFloat ch = crt.ch;
        CGFloat top = bounds.size.height;
        const char *utf = self.markedText.UTF8String;
        const uint8_t *p = (const uint8_t *)utf;
        size_t left = utf ? strlen(utf) : 0;
        int mx = Gscr.cx;
        int my = Gscr.cy;
        NSColor *blue = wp_color(WPColorBg);
        NSColor *markFg = wp_color(WPColorBold);
        NSFont *font = [NSFont fontWithName:@"Menlo" size:12] ?: [NSFont userFixedPitchFontOfSize:12];
        NSMutableParagraphStyle *para = [[NSMutableParagraphStyle alloc] init];
        para.lineBreakMode = NSLineBreakByClipping;
        while (left && mx < Gscr.cols) {
            uint32_t cp;
            size_t u = wp_utf8_decode(p, left, &cp);
            int w = wp_cp_width(cp);
            uint8_t enc[8];
            size_t en;
            NSRect r;
            NSString *str;
            if (!u) {
                break;
            }
            if (w < 1) {
                w = 1;
            }
            en = wp_utf8_encode(cp, enc);
            str = [[NSString alloc] initWithBytes:enc length:en encoding:NSUTF8StringEncoding];
            r = NSMakeRect(crt.ox + mx * cw, top - (my + 1) * ch, w * cw, ch);
            [blue setFill];
            NSRectFill(r);
            [str drawWithRect:r
                      options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingTruncatesLastVisibleLine
                   attributes:@{
                       NSFontAttributeName: font,
                       NSForegroundColorAttributeName: markFg,
                       NSParagraphStyleAttributeName: para,
                       NSUnderlineStyleAttributeName: @(NSUnderlineStyleSingle)
                   }
                      context:nil];
            mx += w;
            p += u;
            left -= u;
        }
    }
}

@end

@implementation WPApp

- (NSMenu *)buildMenu
{
    NSMenu *menubar = [[NSMenu alloc] initWithTitle:@"MainMenu"];

    NSMenuItem *appItem = [[NSMenuItem alloc] initWithTitle:@"WordPerfect" action:nil keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"WordPerfect"];
    [appMenu addItemWithTitle:@"About WordPerfect 5.1" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [[appMenu addItemWithTitle:@"Settings…" action:@selector(openSettings:) keyEquivalent:@","] setTarget:self];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit WordPerfect" action:@selector(quitWP:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    [menubar addItem:appItem];

    NSMenuItem *fileItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    [[fileMenu addItemWithTitle:@"Open…" action:@selector(openDoc:) keyEquivalent:@"o"] setTarget:self];
    [[fileMenu addItemWithTitle:@"List Files" action:@selector(listFiles:) keyEquivalent:@""] setTarget:self];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [[fileMenu addItemWithTitle:@"Save" action:@selector(save:) keyEquivalent:@"s"] setTarget:nil];
    [[fileMenu addItemWithTitle:@"Save As…" action:@selector(saveAs:) keyEquivalent:@"S"] setTarget:self];
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [[fileMenu addItemWithTitle:@"Exit" action:@selector(exitWP:) keyEquivalent:@""] setTarget:self];
    fileItem.submenu = fileMenu;
    [menubar addItem:fileItem];

    NSMenuItem *editItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
    NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [[editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"] setTarget:nil];
    [[editMenu addItemWithTitle:@"Copy" action:@selector(copyDoc:) keyEquivalent:@"c"] setTarget:self];
    [[editMenu addItemWithTitle:@"Paste" action:@selector(pasteDoc:) keyEquivalent:@"v"] setTarget:self];
    [[editMenu addItemWithTitle:@"Select All" action:@selector(selectAllDoc:) keyEquivalent:@"a"] setTarget:self];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [[editMenu addItemWithTitle:@"Cancel" action:@selector(cancel:) keyEquivalent:@"\033"] setTarget:self];
    [[editMenu addItemWithTitle:@"Reveal Codes" action:@selector(reveal:) keyEquivalent:@"r"] setTarget:self];
    editItem.submenu = editMenu;
    [menubar addItem:editItem];

    NSMenuItem *srchItem = [[NSMenuItem alloc] initWithTitle:@"Search" action:nil keyEquivalent:@""];
    NSMenu *srchMenu = [[NSMenu alloc] initWithTitle:@"Search"];
    [[srchMenu addItemWithTitle:@"Forward" action:@selector(searchFwd:) keyEquivalent:@"f"] setTarget:self];
    [[srchMenu addItemWithTitle:@"Backward" action:@selector(searchBack:) keyEquivalent:@"F"] setTarget:self];
    srchItem.submenu = srchMenu;
    [menubar addItem:srchItem];

    NSMenuItem *fontItem = [[NSMenuItem alloc] initWithTitle:@"Font" action:nil keyEquivalent:@""];
    NSMenu *fontMenu = [[NSMenu alloc] initWithTitle:@"Font"];
    {
        NSMenuItem *it;
        it = [fontMenu addItemWithTitle:@"Font…" action:@selector(fontDlg:) keyEquivalent:@"t"];
        it.target = self;
        [fontMenu addItem:[NSMenuItem separatorItem]];
        [[fontMenu addItemWithTitle:@"Bold (F6)" action:@selector(bold:) keyEquivalent:@"b"] setTarget:self];
        [[fontMenu addItemWithTitle:@"Italic" action:@selector(italic:) keyEquivalent:@"i"] setTarget:self];
        [[fontMenu addItemWithTitle:@"Underline (F8)" action:@selector(undln:) keyEquivalent:@"u"] setTarget:self];
        [fontMenu addItem:[NSMenuItem separatorItem]];
        it = [fontMenu addItemWithTitle:@"Fine" action:@selector(sizeFine:) keyEquivalent:@"1"];
        it.target = self;
        it.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagShift;
        it = [fontMenu addItemWithTitle:@"Small" action:@selector(sizeSmall:) keyEquivalent:@"2"];
        it.target = self;
        it.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagShift;
        it = [fontMenu addItemWithTitle:@"Large" action:@selector(sizeLarge:) keyEquivalent:@"3"];
        it.target = self;
        it.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagShift;
        it = [fontMenu addItemWithTitle:@"Very Large" action:@selector(sizeVry:) keyEquivalent:@"4"];
        it.target = self;
        it.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagShift;
        it = [fontMenu addItemWithTitle:@"Extra Large" action:@selector(sizeExt:) keyEquivalent:@"5"];
        it.target = self;
        it.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagShift;
        it = [fontMenu addItemWithTitle:@"Normal Size" action:@selector(sizeNormal:) keyEquivalent:@"0"];
        it.target = self;
        it.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagShift;
        it = [fontMenu addItemWithTitle:@"Normal" action:@selector(fontNormal:) keyEquivalent:@"n"];
        it.target = self;
        it.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagShift;
        [fontMenu addItem:[NSMenuItem separatorItem]];
        [[fontMenu addItemWithTitle:@"Courier 10pt" action:@selector(baseCour10:) keyEquivalent:@""] setTarget:self];
        [[fontMenu addItemWithTitle:@"Courier 12pt" action:@selector(baseCour12:) keyEquivalent:@""] setTarget:self];
        [[fontMenu addItemWithTitle:@"Times 12pt" action:@selector(baseTimes12:) keyEquivalent:@""] setTarget:self];
        [[fontMenu addItemWithTitle:@"Times 14pt" action:@selector(baseTimes14:) keyEquivalent:@""] setTarget:self];
        [[fontMenu addItemWithTitle:@"Helvetica 12pt" action:@selector(baseHelv12:) keyEquivalent:@""] setTarget:self];
        [[fontMenu addItemWithTitle:@"Helvetica 14pt" action:@selector(baseHelv14:) keyEquivalent:@""] setTarget:self];
    }
    fontItem.submenu = fontMenu;
    [menubar addItem:fontItem];

    NSMenuItem *layItem = [[NSMenuItem alloc] initWithTitle:@"Layout" action:nil keyEquivalent:@""];
    NSMenu *layMenu = [[NSMenu alloc] initWithTitle:@"Layout"];
    [[layMenu addItemWithTitle:@"Indent" action:@selector(indent:) keyEquivalent:@""] setTarget:self];
    [[layMenu addItemWithTitle:@"Center (F12)" action:@selector(center:) keyEquivalent:@"e"] setTarget:self];
    {
        NSMenuItem *it;
        it = [layMenu addItemWithTitle:@"Justification…" action:@selector(justDlg:) keyEquivalent:@"l"];
        it.target = self;
        it.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagShift;
        it = [layMenu addItemWithTitle:@"Full Justify" action:@selector(justFull:) keyEquivalent:@"j"];
        it.target = self;
        it.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagShift;
        [[layMenu addItemWithTitle:@"Left Justify" action:@selector(justLeft:) keyEquivalent:@""] setTarget:self];
        [[layMenu addItemWithTitle:@"Right Justify" action:@selector(justRight:) keyEquivalent:@""] setTarget:self];
        [[layMenu addItemWithTitle:@"Center Justify" action:@selector(justCenter:) keyEquivalent:@""] setTarget:self];
    }
    [[layMenu addItemWithTitle:@"End Field" action:@selector(endField:) keyEquivalent:@""] setTarget:self];
    layItem.submenu = layMenu;
    [menubar addItem:layItem];

    NSMenuItem *helpItem = [[NSMenuItem alloc] initWithTitle:@"Help" action:nil keyEquivalent:@""];
    NSMenu *helpMenu = [[NSMenu alloc] initWithTitle:@"Help"];
    [[helpMenu addItemWithTitle:@"WordPerfect Help" action:@selector(help:) keyEquivalent:@"?"] setTarget:self];
    helpItem.submenu = helpMenu;
    [menubar addItem:helpItem];

    return menubar;
}

- (void)openDoc:(id)sender { (void)sender; gui_open(); }
- (void)listFiles:(id)sender { (void)sender; gui_send(KEY_F(5)); }
- (void)saveDoc:(id)sender { (void)sender; gui_save(); }
- (void)save:(id)sender { [self saveDoc:sender]; }
- (void)saveAs:(id)sender { (void)sender; gui_save_as(); }
- (void)exitWP:(id)sender { (void)sender; gui_send(KEY_F(7)); }
- (void)quitWP:(id)sender { (void)sender; gui_send(KEY_F(7)); }
- (void)cancel:(id)sender { (void)sender; gui_send(KEY_F(1)); }
- (void)reveal:(id)sender { (void)sender; gui_send(KEY_F(11)); }
- (void)searchFwd:(id)sender { (void)sender; gui_send(KEY_F(2)); }
- (void)searchBack:(id)sender { (void)sender; gui_send(KEY_F(14)); }
- (void)bold:(id)sender { (void)sender; gui_send(KEY_F(6)); }
- (void)italic:(id)sender { (void)sender; gui_send(WP_KEY_ITALIC); }
- (void)undln:(id)sender { (void)sender; gui_send(KEY_F(8)); }
- (void)fontDlg:(id)sender { (void)sender; gui_send(WP_KEY_FONT); }
- (void)sizeFine:(id)sender { (void)sender; gui_send(WP_KEY_FINE); }
- (void)sizeSmall:(id)sender { (void)sender; gui_send(WP_KEY_SMALL); }
- (void)sizeLarge:(id)sender { (void)sender; gui_send(WP_KEY_LARGE); }
- (void)sizeVry:(id)sender { (void)sender; gui_send(WP_KEY_VRY); }
- (void)sizeExt:(id)sender { (void)sender; gui_send(WP_KEY_EXT); }
- (void)sizeNormal:(id)sender { (void)sender; gui_send(WP_KEY_SIZE_NORMAL); }
- (void)fontNormal:(id)sender { (void)sender; gui_send(WP_KEY_NORMAL); }
- (void)baseCour10:(id)sender { (void)sender; doc_insert_font(Gdoc, WP_FONT_COURIER, 10); gui_refresh(); }
- (void)baseCour12:(id)sender { (void)sender; doc_insert_font(Gdoc, WP_FONT_COURIER, 12); gui_refresh(); }
- (void)baseTimes12:(id)sender { (void)sender; doc_insert_font(Gdoc, WP_FONT_TIMES, 12); gui_refresh(); }
- (void)baseTimes14:(id)sender { (void)sender; doc_insert_font(Gdoc, WP_FONT_TIMES, 14); gui_refresh(); }
- (void)baseHelv12:(id)sender { (void)sender; doc_insert_font(Gdoc, WP_FONT_HELVETICA, 12); gui_refresh(); }
- (void)baseHelv14:(id)sender { (void)sender; doc_insert_font(Gdoc, WP_FONT_HELVETICA, 14); gui_refresh(); }
- (void)indent:(id)sender { (void)sender; gui_send(KEY_F(4)); }
- (void)center:(id)sender { (void)sender; gui_send(KEY_F(12)); }
- (void)justDlg:(id)sender { (void)sender; gui_send(WP_KEY_JUST); }
- (void)justFull:(id)sender { (void)sender; gui_send(WP_KEY_JUST_FULL); }
- (void)justLeft:(id)sender { (void)sender; gui_send(WP_KEY_JUST_LEFT); }
- (void)justRight:(id)sender { (void)sender; gui_send(WP_KEY_JUST_RIGHT); }
- (void)justCenter:(id)sender { (void)sender; gui_send(WP_KEY_JUST_CENTER); }
- (void)endField:(id)sender { (void)sender; gui_send(KEY_F(9)); }
- (void)help:(id)sender { (void)sender; gui_send(KEY_F(3)); }

- (void)copyDoc:(id)sender { (void)sender; gui_clip_copy(); }
- (void)cutDoc:(id)sender { (void)sender; gui_clip_cut(); }
- (void)pasteDoc:(id)sender { (void)sender; gui_clip_paste(); }
- (void)selectAllDoc:(id)sender { (void)sender; gui_clip_select_all(); }

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    SEL a = item.action;
    if (a == @selector(copyDoc:) || a == @selector(cutDoc:) ||
        a == @selector(copy:) || a == @selector(cut:)) {
        return doc_sel_active(Gdoc);
    }
    if (a == @selector(pasteDoc:) || a == @selector(paste:)) {
        return [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString].length > 0;
    }
    return YES;
}

- (void)syncSettingsWells
{
    if (!self.settings) {
        return;
    }
    for (NSView *v in self.settings.contentView.subviews) {
        if (![v isKindOfClass:[NSColorWell class]]) {
            continue;
        }
        NSColorWell *well = (NSColorWell *)v;
        well.color = wp_color((int)well.tag);
    }
    [self refreshSchemePop];
    [self syncFormatWrapPops];
}

- (void)refreshSchemePop
{
    if (!self.schemePop) {
        return;
    }
    NSString *cur = wp_scheme_name();
    [self.schemePop removeAllItems];
    [self.schemePop addItemsWithTitles:wp_scheme_titles()];
    if ([self.schemePop itemWithTitle:cur]) {
        [self.schemePop selectItemWithTitle:cur];
    }
    if (self.schemeNameField && ![self.schemeNameField currentEditor]) {
        self.schemeNameField.stringValue = [cur isEqualToString:kWPBuiltinScheme] ? @"" : cur;
    }
}

- (void)addSection:(NSString *)title atY:(CGFloat)y inView:(NSView *)box
{
    NSTextField *lab = [NSTextField labelWithString:title];
    lab.font = [NSFont boldSystemFontOfSize:13];
    lab.frame = NSMakeRect(20, y, 400, 20);
    [box addSubview:lab];
    NSBox *rule = [[NSBox alloc] initWithFrame:NSMakeRect(20, y - 4, 420, 1)];
    rule.boxType = NSBoxSeparator;
    [box addSubview:rule];
}

- (NSWindow *)makeSettingsWindow
{
    const CGFloat width = 460;
    const CGFloat height = 656;
    const CGFloat rowH = 30;
    NSRect frame = NSMakeRect(0, 0, width, height);
    NSWindow *w = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    w.title = @"Settings";
    w.releasedWhenClosed = NO;
    NSView *box = w.contentView;
    CGFloat y;
    int i;

    y = height - 40;
    [self addSection:@"Files" atY:y inView:box];
    y -= 32;
    {
        NSTextField *lab = [NSTextField labelWithString:@"Default save format"];
        lab.frame = NSMakeRect(20, y, 160, 24);
        [box addSubview:lab];
        self.formatPop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(190, y, 250, 25) pullsDown:NO];
        [self.formatPop addItemWithTitle:@"Word (.docx)"];
        [self.formatPop addItemWithTitle:@"Markdown (.md)"];
        [self.formatPop addItemWithTitle:@"PDF (.pdf)"];
        [self.formatPop addItemWithTitle:@"WordPerfect 5.1 (.wpd)"];
        self.formatPop.target = self;
        self.formatPop.action = @selector(formatChosen:);
        [box addSubview:self.formatPop];
    }

    y -= 44;
    [self addSection:@"Display" atY:y inView:box];
    y -= 32;
    {
        NSTextField *lab = [NSTextField labelWithString:@"Line wrap"];
        lab.frame = NSMakeRect(20, y, 160, 24);
        [box addSubview:lab];
        self.wrapPop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(190, y, 250, 25) pullsDown:NO];
        [self.wrapPop addItemWithTitle:@"On whitespace"];
        [self.wrapPop addItemWithTitle:@"Strict 80 columns"];
        self.wrapPop.target = self;
        self.wrapPop.action = @selector(wrapChosen:);
        [box addSubview:self.wrapPop];
    }
    y -= 32;
    {
        self.forceFkeysCheck = [NSButton checkboxWithTitle:@"Force show F-key bindings on screen"
                                                    target:self
                                                    action:@selector(forceFkeysChanged:)];
        self.forceFkeysCheck.frame = NSMakeRect(20, y, 420, 24);
        [box addSubview:self.forceFkeysCheck];
    }
    y -= 32;
    {
        self.spellCheck = [NSButton checkboxWithTitle:@"Modern spell checking"
                                               target:self
                                               action:@selector(spellOnChanged:)];
        self.spellCheck.frame = NSMakeRect(20, y, 420, 24);
        [box addSubview:self.spellCheck];
    }
    y -= 32;
    {
        NSTextField *lab = [NSTextField labelWithString:@"Spell language"];
        int li;
        lab.frame = NSMakeRect(20, y, 160, 24);
        [box addSubview:lab];
        self.spellLangPop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(190, y, 250, 25) pullsDown:NO];
        for (li = 0; li < 10; li++) {
            [self.spellLangPop addItemWithTitle:[NSString stringWithUTF8String:kWPSpellLangs[li].title]];
        }
        self.spellLangPop.target = self;
        self.spellLangPop.action = @selector(spellLangChosen:);
        [box addSubview:self.spellLangPop];
    }

    y -= 44;
    [self addSection:@"Colors" atY:y inView:box];
    y -= 32;
    {
        NSTextField *schLab = [NSTextField labelWithString:@"Scheme"];
        schLab.frame = NSMakeRect(20, y, 50, 24);
        [box addSubview:schLab];
        self.schemePop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(74, y, 366, 25) pullsDown:NO];
        self.schemePop.target = self;
        self.schemePop.action = @selector(schemeChosen:);
        [box addSubview:self.schemePop];
    }
    y -= 32;
    {
        NSTextField *nameLab = [NSTextField labelWithString:@"Name"];
        nameLab.frame = NSMakeRect(20, y, 50, 24);
        [box addSubview:nameLab];
        self.schemeNameField = [[NSTextField alloc] initWithFrame:NSMakeRect(74, y, 250, 24)];
        self.schemeNameField.placeholderString = @"Night, Amber, …";
        [box addSubview:self.schemeNameField];
        NSButton *save = [NSButton buttonWithTitle:@"Save"
                                           target:self
                                           action:@selector(saveScheme:)];
        save.bezelStyle = NSBezelStyleRounded;
        save.frame = NSMakeRect(334, y - 2, 96, 28);
        [box addSubview:save];
    }
    y -= 8;
    for (i = 0; i < WPColorCount; i++) {
        y -= rowH;
        NSTextField *lab = [NSTextField labelWithString:[NSString stringWithUTF8String:kWPColorLabel[i]]];
        lab.frame = NSMakeRect(20, y, 140, 24);
        lab.font = [NSFont systemFontOfSize:13];
        [box addSubview:lab];
        NSColorWell *well = [[NSColorWell alloc] initWithFrame:NSMakeRect(200, y, 48, 24)];
        well.tag = i;
        well.color = wp_color(i);
        well.continuous = YES;
        well.target = self;
        well.action = @selector(colorChanged:);
        [box addSubview:well];
    }

    NSButton *del = [NSButton buttonWithTitle:@"Delete"
                                      target:self
                                      action:@selector(deleteScheme:)];
    del.bezelStyle = NSBezelStyleRounded;
    del.frame = NSMakeRect(20, 14, 90, 28);
    [box addSubview:del];
    NSButton *reset = [NSButton buttonWithTitle:@"Restore WP 5.1"
                                        target:self
                                        action:@selector(resetColors:)];
    reset.bezelStyle = NSBezelStyleRounded;
    reset.frame = NSMakeRect(118, 14, 160, 28);
    [box addSubview:reset];
    [self refreshSchemePop];
    [self syncFormatWrapPops];
    return w;
}

- (void)syncFormatWrapPops
{
    if (self.formatPop) {
        [self.formatPop selectItemAtIndex:io_default_format()];
    }
    if (self.wrapPop) {
        [self.wrapPop selectItemAtIndex:io_wrap_mode() == IO_WRAP_STRICT80 ? 1 : 0];
    }
    if (self.forceFkeysCheck) {
        self.forceFkeysCheck.state = wp_force_fkeys() ? NSControlStateValueOn : NSControlStateValueOff;
    }
    if (self.spellCheck) {
        self.spellCheck.state = wp_spell_on() ? NSControlStateValueOn : NSControlStateValueOff;
    }
    if (self.spellLangPop) {
        NSString *code = wp_spell_lang_code();
        int li, sel = 0;
        for (li = 0; li < 10; li++) {
            if ([code isEqualToString:[NSString stringWithUTF8String:kWPSpellLangs[li].code]]) {
                sel = li;
                break;
            }
        }
        [self.spellLangPop selectItemAtIndex:sel];
    }
}

- (void)formatChosen:(NSPopUpButton *)pop
{
    int fmt = (int)pop.indexOfSelectedItem;
    io_set_default_format(fmt);
    [[NSUserDefaults standardUserDefaults] setObject:wp_fmt_name(fmt) forKey:kWPSaveFormatKey];
}

- (void)wrapChosen:(NSPopUpButton *)pop
{
    int mode = pop.indexOfSelectedItem == 1 ? IO_WRAP_STRICT80 : IO_WRAP_WORD;
    io_set_wrap_mode(mode);
    [[NSUserDefaults standardUserDefaults] setObject:(mode == IO_WRAP_STRICT80 ? @"column" : @"word")
                                              forKey:kWPWrapModeKey];
    view_apply_prefs(Gview);
    gui_refresh();
}

- (void)forceFkeysChanged:(NSButton *)btn
{
    BOOL on = btn.state == NSControlStateValueOn;
    [[NSUserDefaults standardUserDefaults] setBool:on forKey:kWPForceFkeysKey];
    [self applyFKeyPlacement];
    gui_refresh();
}

- (void)spellOnChanged:(NSButton *)btn
{
    [[NSUserDefaults standardUserDefaults] setBool:(btn.state == NSControlStateValueOn) forKey:kWPSpellOnKey];
    gui_refresh();
}

- (void)spellLangChosen:(NSPopUpButton *)pop
{
    NSInteger i = pop.indexOfSelectedItem;
    if (i >= 0 && i < 10) {
        [[NSUserDefaults standardUserDefaults] setObject:[NSString stringWithUTF8String:kWPSpellLangs[i].code]
                                                  forKey:kWPSpellLangKey];
    }
    gui_refresh();
}

- (void)openSettings:(id)sender
{
    (void)sender;
    if (!self.settings || !self.spellCheck) {
        self.settings = [self makeSettingsWindow];
    }
    [self syncSettingsWells];
    [self.settings center];
    [self.settings makeKeyAndOrderFront:nil];
}

- (void)colorChanged:(NSColorWell *)well
{
    wp_set_color((int)well.tag, well.color);
}

- (void)schemeChosen:(NSPopUpButton *)pop
{
    wp_load_scheme(pop.titleOfSelectedItem);
    [self syncSettingsWells];
}

- (void)saveScheme:(id)sender
{
    (void)sender;
    NSString *name = [self.schemeNameField.stringValue
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (!name.length) {
        NSAlert *a = [[NSAlert alloc] init];
        a.messageText = @"Name this color scheme";
        a.informativeText = @"Type a name, then click Save.";
        [a beginSheetModalForWindow:self.settings completionHandler:nil];
        [self.settings makeFirstResponder:self.schemeNameField];
        return;
    }
    if ([name isEqualToString:kWPBuiltinScheme]) {
        NSAlert *a = [[NSAlert alloc] init];
        a.messageText = @"WP 5.1 is built in";
        a.informativeText = @"Choose another name for your scheme.";
        [a beginSheetModalForWindow:self.settings completionHandler:nil];
        return;
    }
    wp_save_scheme(name);
    [self refreshSchemePop];
}

- (void)deleteScheme:(id)sender
{
    (void)sender;
    NSString *name = self.schemePop.titleOfSelectedItem;
    if (!name.length || wp_is_builtin_scheme(name)) {
        return;
    }
    wp_delete_scheme(name);
    [self syncSettingsWells];
}

- (void)resetColors:(id)sender
{
    (void)sender;
    wp_load_scheme(kWPBuiltinScheme);
    [self syncSettingsWells];
}

- (void)applicationDidFinishLaunching:(NSNotification *)n
{
    (void)n;
    NSMenu *menu = [self buildMenu];
    [NSApp setMainMenu:menu];

    NSRect vis = [NSScreen mainScreen].visibleFrame;
    NSRect frame = NSIsEmptyRect(vis) ? NSMakeRect(80, 80, 80 * 10 + 16, 25 * 18 + 8) : vis;
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                       NSWindowStyleMaskUnifiedTitleAndToolbar;
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 800, 600)
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"WordPerfect 5.1";
    self.window.delegate = self;
    self.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    self.window.minSize = NSMakeSize(640, 400);
    [self.window setFrame:frame display:NO];
    {
        NSRect content = [self.window contentRectForFrameRect:self.window.frame];
        content.origin = NSZeroPoint;
        frame = content;
    }

    self.strip = [[FKeyStripView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, 44)];
    self.strip.translatesAutoresizingMaskIntoConstraints = YES;
    self.strip.autoresizingMask = NSViewWidthSizable;
    self.stripAcc = [[NSTitlebarAccessoryViewController alloc] init];
    self.stripAcc.layoutAttribute = NSLayoutAttributeBottom;
    self.stripAcc.view = self.strip;
    if (@available(macOS 10.12, *)) {
        self.stripAcc.fullScreenMinHeight = 44;
    }

    NSView *root = [[NSView alloc] initWithFrame:frame];
    root.wantsLayer = YES;
    self.docView = [[WPDocView alloc] initWithFrame:root.bounds];
    self.docView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.docView.blinkOn = YES;
    [root addSubview:self.docView];
    self.window.contentView = root;

    if (@available(macOS 10.12.2, *)) {
        NSApp.automaticCustomizeTouchBarMenuItemEnabled = YES;
        NSTouchBar *bar = [self makeTouchBar];
        self.window.touchBar = bar;
        self.docView.touchBar = bar;
    }
    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:self.docView];
    [NSApp activateIgnoringOtherApps:YES];
    [self applyFKeyPlacement];
    [self showKeyboardStrip];
    wp_claim_document_types();
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(400 * NSEC_PER_MSEC)),
                   dispatch_get_main_queue(), ^{
                       [self showKeyboardStrip];
                   });

    self.blink = [NSTimer scheduledTimerWithTimeInterval:0.53
                                                  target:self
                                                selector:@selector(blink:)
                                                userInfo:nil
                                                 repeats:YES];
    if (agent_active()) {
        self.agent = [NSTimer scheduledTimerWithTimeInterval:0.05
                                                      target:self
                                                    selector:@selector(pollAgent:)
                                                    userInfo:nil
                                                     repeats:YES];
    }
    gui_refresh();
}

- (void)pollAgent:(NSTimer *)t
{
    (void)t;
    if (!agent_active() || !Gapp) {
        return;
    }
    agent_dispatch(Gapp, Gdoc, Gview);
    if (!Gapp->running) {
        [NSApp terminate:nil];
        return;
    }
    gui_refresh();
}

static BOOL g_tb_modal;

- (void)applyFKeyPlacement
{
    BOOL hideOnWindow = g_tb_modal && !wp_force_fkeys();
    NSArray *accs = self.window.titlebarAccessoryViewControllers;
    NSUInteger i;
    BOOL attached = NO;
    for (i = 0; i < accs.count; i++) {
        if (accs[i] == self.stripAcc) {
            attached = YES;
            if (hideOnWindow) {
                [self.window removeTitlebarAccessoryViewControllerAtIndex:i];
            }
            break;
        }
    }
    if (!hideOnWindow && !attached && self.stripAcc) {
        [self.window addTitlebarAccessoryViewController:self.stripAcc];
    }
    /* Green F-key template is TUI-only. GUI uses the gray titlebar strip. */
    Gview->hide_strip = 1;
}

- (void)windowDidBecomeKey:(NSNotification *)n
{
    (void)n;
    [self applyFKeyPlacement];
    [self showKeyboardStrip];
}

- (void)windowDidResignKey:(NSNotification *)n
{
    (void)n;
}

- (void)blink:(NSTimer *)t
{
    (void)t;
    self.docView.blinkOn = !self.docView.blinkOn;
    [self.docView setNeedsDisplay:YES];
}

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    if (sender != self.window) {
        return YES;
    }
    if (Gdoc->dirty && Gapp->running) {
        gui_send(KEY_F(7));
        return NO;
    }
    Gapp->running = 0;
    return YES;
}

- (void)windowWillClose:(NSNotification *)n
{
    if (n.object != self.window) {
        return;
    }
    Gapp->running = 0;
    [NSApp stop:nil];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void)sender;
    if (Gdoc->dirty && Gapp->running) {
        gui_send(KEY_F(7));
        return NSTerminateCancel;
    }
    Gapp->running = 0;
    return NSTerminateNow;
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app
{
    (void)app;
    return YES;
}

- (BOOL)application:(NSApplication *)sender openFile:(NSString *)filename
{
    (void)sender;
    return gui_open_path(filename.fileSystemRepresentation) == 0;
}

- (void)application:(NSApplication *)application openURLs:(NSArray<NSURL *> *)urls
{
    (void)application;
    NSURL *u;
    for (u in urls) {
        if (u.isFileURL) {
            gui_open_path(u.path.fileSystemRepresentation);
            break;
        }
    }
}

- (BOOL)applicationShouldOpenUntitledFile:(NSApplication *)sender
{
    (void)sender;
    return NO;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)flag
{
    (void)sender;
    if (!flag) {
        [self.window makeKeyAndOrderFront:nil];
    }
    return YES;
}

- (NSTouchBar *)makeTouchBar API_AVAILABLE(macos(10.12.2))
{
    if (self.fnBar) {
        return self.fnBar;
    }
    NSTouchBar *bar = [[NSTouchBar alloc] init];
    bar.delegate = self;
    bar.customizationIdentifier = @"uk.wp51.touchbar";
    bar.defaultItemIdentifiers = @[ @"uk.wp51.fstrip", @"uk.wp51.tray" ];
    self.fnBar = bar;
    return bar;
}

- (NSTouchBarItem *)touchBar:(NSTouchBar *)touchBar
       makeItemForIdentifier:(NSTouchBarItemIdentifier)identifier
    API_AVAILABLE(macos(10.12.2))
{
    (void)touchBar;
    if ([identifier isEqualToString:@"uk.wp51.tray"]) {
        NSCustomTouchBarItem *tray = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
        NSButton *wp = [NSButton buttonWithTitle:@"WP" target:self action:@selector(showKeyboardStrip)];
        tray.view = wp;
        return tray;
    }
    if (![identifier isEqualToString:@"uk.wp51.fstrip"]) {
        return nil;
    }
    {
        const CGFloat bw = 72;
        const CGFloat bh = 30;
        NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:identifier];
        NSScrollView *sv = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 680, bh)];
        NSView *row = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 14 * bw, bh)];
        int i;
        sv.hasHorizontalScroller = YES;
        sv.drawsBackground = NO;
        sv.borderType = NSNoBorder;
        sv.horizontalScrollElasticity = NSScrollElasticityAllowed;
        self.tbFbtns = [NSMutableArray array];
        self.tbCtrl = [NSButton buttonWithTitle:@"Ctrl" target:self action:@selector(touchMod:)];
        self.tbCtrl.tag = 100;
        self.tbCtrl.buttonType = NSButtonTypePushOnPushOff;
        self.tbCtrl.font = [NSFont boldSystemFontOfSize:10];
        self.tbCtrl.bezelStyle = NSBezelStyleRounded;
        self.tbCtrl.frame = NSMakeRect(0, 0, bw - 2, bh);
        [row addSubview:self.tbCtrl];
        self.tbShift = [NSButton buttonWithTitle:@"Shift" target:self action:@selector(touchMod:)];
        self.tbShift.tag = 101;
        self.tbShift.buttonType = NSButtonTypePushOnPushOff;
        self.tbShift.font = [NSFont boldSystemFontOfSize:10];
        self.tbShift.bezelStyle = NSBezelStyleRounded;
        self.tbShift.frame = NSMakeRect(bw, 0, bw - 2, bh);
        [row addSubview:self.tbShift];
        for (i = 0; i < 12; i++) {
            NSButton *b = [NSButton buttonWithTitle:@"" target:self action:@selector(touchF:)];
            b.tag = i + 1;
            b.font = [NSFont systemFontOfSize:10];
            b.bezelStyle = NSBezelStyleRounded;
            b.frame = NSMakeRect((i + 2) * bw, 0, bw - 2, bh);
            [row addSubview:b];
            [self.tbFbtns addObject:b];
        }
        [self syncFkeyLabels];
        sv.documentView = row;
        item.view = sv;
        item.visibilityPriority = NSTouchBarItemPriorityHigh;
        return item;
    }
}

- (void)showKeyboardStrip
{
    NSTouchBar *bar;
    if (!wp_has_keyboard_touch_strip()) {
        g_tb_modal = NO;
        [self applyFKeyPlacement];
        return;
    }
    bar = [self makeTouchBar];
    self.window.touchBar = bar;
    self.docView.touchBar = bar;
    g_tb_modal = NO;
    if ([NSTouchBar respondsToSelector:@selector(presentSystemModalTouchBar:systemTrayItemIdentifier:)]) {
        [NSTouchBar presentSystemModalTouchBar:bar systemTrayItemIdentifier:@"uk.wp51.tray"];
        g_tb_modal = YES;
    } else if ([NSTouchBar respondsToSelector:@selector(presentSystemModalFunctionBar:systemTrayItemIdentifier:)]) {
        [NSTouchBar presentSystemModalFunctionBar:bar systemTrayItemIdentifier:@"uk.wp51.tray"];
        g_tb_modal = YES;
    }
    [self applyFKeyPlacement];
}

- (void)hideKeyboardStrip
{
    if (self.fnBar &&
        [NSTouchBar respondsToSelector:@selector(dismissSystemModalTouchBar:)]) {
        [NSTouchBar dismissSystemModalTouchBar:self.fnBar];
    }
    if (self.fnBar &&
        [NSTouchBar respondsToSelector:@selector(dismissSystemModalFunctionBar:)]) {
        [NSTouchBar dismissSystemModalFunctionBar:self.fnBar];
    }
    g_tb_modal = NO;
}

- (void)syncFkeyLabels
{
    int i;
    [self.strip reloadTitles];
    if (self.tbCtrl) {
        self.tbCtrl.state = (g_fkey_mod == FKEY_MOD_CTRL) ? NSControlStateValueOn : NSControlStateValueOff;
    }
    if (self.tbShift) {
        self.tbShift.state = (g_fkey_mod == FKEY_MOD_SHIFT) ? NSControlStateValueOn : NSControlStateValueOff;
    }
    for (i = 0; i < (int)self.tbFbtns.count; i++) {
        self.tbFbtns[i].title = [NSString stringWithFormat:@"F%d %s", i + 1, view_fkey_name(i, g_fkey_mod)];
    }
}

- (void)touchMod:(id)sender
{
    NSInteger tag = 0;
    if ([sender isKindOfClass:[NSButton class]]) {
        tag = [(NSButton *)sender tag];
    }
    gui_set_fkey_mod(tag == 100 ? FKEY_MOD_CTRL : FKEY_MOD_SHIFT);
}

- (void)touchF:(id)sender
{
    int n = 0;
    int key;
    if ([sender isKindOfClass:[NSButton class]]) {
        n = (int)[(NSButton *)sender tag];
    } else if ([sender isKindOfClass:[NSTouchBarItem class]]) {
        sscanf([(NSTouchBarItem *)sender identifier].UTF8String, "uk.wp51.f%d", &n);
    }
    key = gui_fkey_mapped(n);
    if (key) {
        gui_send(key);
    }
}

@end

int gui_run(App *a, Doc *d, View *v)
{
    Gapp = a;
    Gdoc = d;
    Gview = v;
    v->hide_strip = 1;
    screen_init(&Gscr);

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [[NSProcessInfo processInfo] setProcessName:@"WordPerfect"];
    wp_register_color_defaults();
    wp_load_scheme(wp_scheme_name());
    wp_apply_io_prefs();
    {
        NSString *icns = [[NSBundle mainBundle] pathForResource:@"AppIcon" ofType:@"icns"];
        if (!icns) {
            NSString *exe = [[NSProcessInfo processInfo].arguments firstObject];
            NSString *dir = [exe stringByDeletingLastPathComponent];
            NSArray *cands = @[
                [dir stringByAppendingPathComponent:@"macos/AppIcon.icns"],
                [[dir stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"macos/AppIcon.icns"],
                [dir stringByAppendingPathComponent:@"AppIcon.icns"]
            ];
            for (NSString *p in cands) {
                if ([[NSFileManager defaultManager] fileExistsAtPath:p]) {
                    icns = p;
                    break;
                }
            }
        }
        if (icns) {
            NSImage *img = [[NSImage alloc] initWithContentsOfFile:icns];
            if (img) {
                [NSApp setApplicationIconImage:img];
            }
        }
    }

    Gui = [[WPApp alloc] init];
    NSApp.delegate = Gui;
    [NSApp setMainMenu:[Gui buildMenu]];
    [NSApp run];

    [Gui.blink invalidate];
    [Gui.agent invalidate];
    screen_free(&Gscr);
    return a->running ? 0 : 0;
}

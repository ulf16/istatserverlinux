#import <Cocoa/Cocoa.h>
#import "ISCReport.h"

@interface ISCFlippedView : NSView
@end
@implementation ISCFlippedView
- (BOOL)isFlipped { return YES; }
@end

@interface ISCApp : NSObject <NSApplicationDelegate, NSMenuItemValidation>
@property NSWindow *window;
@property NSStackView *rows;
@property NSTextField *heading;
@property NSTextField *summary;
@property NSTextField *timestamp;
@property NSButton *refreshButton;
@property NSButton *exportButton;
@property NSDictionary *report;
@property NSString *prefix;
@property BOOL busy;
@property BOOL snapshot;
@end

@implementation ISCApp

- (NSTextField *)text:(NSString *)text size:(CGFloat)size secondary:(BOOL)secondary {
    NSTextField *field = [NSTextField wrappingLabelWithString:text ?: @""];
    field.font = [NSFont systemFontOfSize:size];
    field.textColor = secondary ? NSColor.secondaryLabelColor : NSColor.labelColor;
    field.selectable = YES;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    [field setContentCompressionResistancePriority:250 forOrientation:NSLayoutConstraintOrientationHorizontal];
    return field;
}

- (NSMenu *)menu:(NSString *)title root:(NSMenu *)root {
    NSMenuItem *item = [root addItemWithTitle:title action:nil keyEquivalent:@""];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:title];
    item.submenu = menu;
    return menu;
}

- (void)item:(NSString *)title action:(SEL)action key:(NSString *)key menu:(NSMenu *)menu {
    NSMenuItem *item = [menu addItemWithTitle:title action:action keyEquivalent:key];
    if ([self respondsToSelector:action]) item.target = self;
}

- (void)menus {
    NSMenu *root = [NSMenu new];
    NSMenu *app = [self menu:@"iStat Server Control" root:root];
    [self item:@"About iStat Server Control" action:@selector(orderFrontStandardAboutPanel:) key:@"" menu:app];
    [app addItem:NSMenuItem.separatorItem];
    NSMenu *services = [self menu:@"Services" root:app];
    NSApp.servicesMenu = services;
    [self item:@"Hide iStat Server Control" action:@selector(hide:) key:@"h" menu:app];
    [self item:@"Hide Others" action:@selector(hideOtherApplications:) key:@"h" menu:app];
    app.itemArray.lastObject.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [self item:@"Show All" action:@selector(unhideAllApplications:) key:@"" menu:app];
    [app addItem:NSMenuItem.separatorItem];
    [self item:@"Quit iStat Server Control" action:@selector(terminate:) key:@"q" menu:app];
    NSMenu *file = [self menu:@"File" root:root];
    [self item:@"Inspect This Mac" action:@selector(inspectLocal:) key:@"l" menu:file];
    [self item:@"Choose Installation..." action:@selector(choosePrefix:) key:@"" menu:file];
    [file addItem:NSMenuItem.separatorItem];
    [self item:@"Open Status Report..." action:@selector(openReport:) key:@"o" menu:file];
    [self item:@"Export Status Report..." action:@selector(exportReport:) key:@"s" menu:file];
    [file addItem:NSMenuItem.separatorItem];
    [self item:@"Close Window" action:@selector(performClose:) key:@"w" menu:file];
    NSMenu *edit = [self menu:@"Edit" root:root];
    [self item:@"Copy" action:@selector(copy:) key:@"c" menu:edit];
    [self item:@"Select All" action:@selector(selectAll:) key:@"a" menu:edit];
    NSMenu *view = [self menu:@"View" root:root];
    [self item:@"Refresh" action:@selector(refresh:) key:@"r" menu:view];
    NSMenu *window = [self menu:@"Window" root:root];
    [self item:@"Minimize" action:@selector(performMiniaturize:) key:@"m" menu:window];
    [self item:@"Zoom" action:@selector(performZoom:) key:@"" menu:window];
    NSApp.windowsMenu = window;
    NSMenu *help = [self menu:@"Help" root:root];
    [self item:@"Server Control Notes" action:@selector(showHelp:) key:@"?" menu:help];
    NSApp.mainMenu = root;
}

- (NSButton *)button:(NSString *)symbol label:(NSString *)label action:(SEL)action {
    NSButton *button = [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:symbol accessibilityDescription:label]
                                        target:self action:action];
    button.bezelStyle = NSBezelStyleTexturedRounded;
    button.toolTip = label;
    [button setAccessibilityLabel:label];
    [button.widthAnchor constraintEqualToConstant:36].active = YES;
    [button.heightAnchor constraintEqualToConstant:28].active = YES;
    return button;
}

- (void)applicationDidFinishLaunching:(NSNotification *)note {
    self.prefix = @"/opt/istatserverlinux";
    [self menus];
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 780, 670)
                                             styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                               backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"iStat Server Control";
    self.window.contentMinSize = NSMakeSize(600, 480);
    self.window.releasedWhenClosed = NO;
    [self.window setFrameAutosaveName:@"ServerControlStatus"];
    NSView *content = self.window.contentView;
    NSStackView *outer = [NSStackView new];
    outer.orientation = NSUserInterfaceLayoutOrientationVertical;
    outer.alignment = NSLayoutAttributeLeading;
    outer.spacing = 16;
    outer.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:outer];
    [NSLayoutConstraint activateConstraints:@[
        [outer.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:24],
        [outer.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-24],
        [outer.topAnchor constraintEqualToAnchor:content.topAnchor constant:20],
        [outer.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-16]]];
    NSImageView *icon = [NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:@"server.rack" accessibilityDescription:@"Server"]];
    icon.image = [icon.image imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:30 weight:NSFontWeightRegular]];
    icon.contentTintColor = NSColor.controlAccentColor;
    [icon.widthAnchor constraintEqualToConstant:40].active = YES;
    [icon.heightAnchor constraintEqualToConstant:40].active = YES;
    self.heading = [self text:@"Inspecting this Mac" size:20 secondary:NO];
    self.heading.font = [NSFont systemFontOfSize:20 weight:NSFontWeightSemibold];
    self.summary = [self text:@"" size:13 secondary:YES];
    NSStackView *titles = [NSStackView stackViewWithViews:@[self.heading, self.summary]];
    titles.orientation = NSUserInterfaceLayoutOrientationVertical;
    titles.alignment = NSLayoutAttributeLeading;
    titles.spacing = 4;
    NSStackView *header = [NSStackView stackViewWithViews:@[icon, titles]];
    header.spacing = 16;
    [outer addArrangedSubview:header];
    [header.widthAnchor constraintEqualToAnchor:outer.widthAnchor].active = YES;
    NSScrollView *scroll = [NSScrollView new];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    scroll.borderType = NSNoBorder;
    [outer addArrangedSubview:scroll];
    [scroll.widthAnchor constraintEqualToAnchor:outer.widthAnchor].active = YES;
    self.rows = [NSStackView new];
    self.rows.orientation = NSUserInterfaceLayoutOrientationVertical;
    self.rows.alignment = NSLayoutAttributeLeading;
    self.rows.spacing = 9;
    self.rows.translatesAutoresizingMaskIntoConstraints = NO;
    NSView *document = [ISCFlippedView new];
    document.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.documentView = document;
    [document addSubview:self.rows];
    [NSLayoutConstraint activateConstraints:@[
        [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor],
        [self.rows.topAnchor constraintEqualToAnchor:document.topAnchor],
        [self.rows.leadingAnchor constraintEqualToAnchor:document.leadingAnchor],
        [self.rows.trailingAnchor constraintEqualToAnchor:document.trailingAnchor constant:-12],
        [self.rows.bottomAnchor constraintEqualToAnchor:document.bottomAnchor]]];
    self.timestamp = [self text:@"" size:11 secondary:YES];
    self.refreshButton = [self button:@"arrow.clockwise" label:@"Refresh (Command-R)" action:@selector(refresh:)];
    self.exportButton = [self button:@"square.and.arrow.up" label:@"Export status report" action:@selector(exportReport:)];
    NSButton *open = [self button:@"doc" label:@"Open status report" action:@selector(openReport:)];
    NSStackView *footer = [NSStackView stackViewWithViews:@[self.timestamp, open, self.exportButton, self.refreshButton]];
    footer.spacing = 8;
    [outer addArrangedSubview:footer];
    [footer.widthAnchor constraintEqualToAnchor:outer.widthAnchor].active = YES;
    [self.timestamp setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self refresh:nil];
}

- (void)section:(NSString *)title {
    if (self.rows.arrangedSubviews.count) {
        NSBox *line = [NSBox new]; line.boxType = NSBoxSeparator;
        [self.rows addArrangedSubview:line];
        [line.widthAnchor constraintEqualToAnchor:self.rows.widthAnchor].active = YES;
    }
    NSTextField *heading = [self text:title size:13 secondary:NO];
    heading.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
    [self.rows addArrangedSubview:heading];
}

- (void)row:(NSString *)label value:(NSString *)value {
    NSTextField *name = [self text:label size:13 secondary:YES];
    [name.widthAnchor constraintEqualToConstant:146].active = YES;
    NSTextField *field = [self text:value.length ? value : @"Not observed" size:13 secondary:NO];
    field.toolTip = field.stringValue;
    NSStackView *row = [NSStackView stackViewWithViews:@[name, field]];
    row.alignment = NSLayoutAttributeFirstBaseline;
    row.spacing = 16;
    [self.rows addArrangedSubview:row];
    [row.widthAnchor constraintEqualToAnchor:self.rows.widthAnchor].active = YES;
}

- (NSString *)state:(id)value {
    return [value isKindOfClass:NSString.class] ? [[value stringByReplacingOccurrencesOfString:@"-" withString:@" "] capitalizedString] : @"Not observed";
}

- (NSString *)date:(id)value {
    if (![value isKindOfClass:NSNumber.class] || [value doubleValue] <= 0) return @"Not observed";
    return [NSDateFormatter localizedStringFromDate:[NSDate dateWithTimeIntervalSince1970:[value doubleValue]]
                                          dateStyle:NSDateFormatterMediumStyle timeStyle:NSDateFormatterMediumStyle];
}

- (void)render {
    for (NSView *view in self.rows.arrangedSubviews.copy) { [self.rows removeArrangedSubview:view]; [view removeFromSuperview]; }
    NSDictionary *r = self.report, *i = r[@"installation"], *s = r[@"service"], *c = r[@"configuration"], *h = r[@"health"];
    self.heading.stringValue = self.snapshot ? @"Saved status report" : @"Local server";
    NSString *status = [i[@"installed"] boolValue] ? [self state:s[@"state"]] : @"Maintained server not installed";
    self.summary.stringValue = [NSString stringWithFormat:@"%@ · %@", r[@"host"], status];
    [self section:@"Installation"];
    [self row:@"Server" value:@"istatserverlinux"];
    [self row:@"Location" value:i[@"prefix"]];
    [self row:@"Service" value:[NSString stringWithFormat:@"%@ (%@)", s[@"label"], [self state:s[@"identity"]]]];
    [self row:@"Process" value:[s[@"pid"] isKindOfClass:NSNumber.class] ? [NSString stringWithFormat:@"PID %@", s[@"pid"]] : @"Not observed"];
    if ([i[@"classic_present"] boolValue]) [self row:@"Classic server" value:@"Detected separately; not managed"];
    [self section:@"Configuration"];
    [self row:@"File" value:i[@"configuration"]];
    [self row:@"Access" value:[self state:c[@"state"]]];
    [self row:@"Port in file" value:[c[@"port"] isKindOfClass:NSNumber.class] ? [c[@"port"] stringValue] : @"Not available"];
    [self row:@"Address in file" value:[c[@"address"] isKindOfClass:NSString.class] ? c[@"address"] : @"Not available"];
    NSArray *ports = r[@"listeners"][@"ports"];
    [self row:@"Listening ports" value:ports.count ? [ports componentsJoinedByString:@", "] : @"Not observed"];
    [self row:@"Pairing" value:[self state:c[@"pairing"]]];
    [self row:@"Bonjour" value:@"Not observed"];
    [self section:@"Collection helpers"];
    for (NSDictionary *helper in r[@"helpers"]) {
        NSString *label = helper[@"label"];
        NSString *name = [label containsString:@"powermetrics"] ? @"Power / frequency" : ([label hasSuffix:@"timer"] ? @"Health schedule" : @"Disk health");
        NSString *exit = [helper[@"last_exit"] isKindOfClass:NSNumber.class] ? [NSString stringWithFormat:@"; last exit %@", helper[@"last_exit"]] : @"";
        [self row:name value:[[self state:helper[@"state"]] stringByAppendingString:exit]];
    }
    [self row:self.snapshot ? @"Cache at capture" : @"Health cache" value:[self state:h[@"state"]]];
    [self row:@"Devices / skipped" value:[NSString stringWithFormat:@"%@ / %@", h[@"devices"], h[@"skipped"]]];
    [self row:@"Last observation" value:[self date:h[@"checked"]]];
    self.timestamp.stringValue = [NSString stringWithFormat:@"%@ · %@", self.snapshot ? @"Report captured" : @"Checked", [self date:r[@"checked"]]];
    self.refreshButton.enabled = !self.snapshot && !self.busy;
    self.exportButton.enabled = YES;
}

- (BOOL)acceptData:(NSData *)data {
    NSDictionary *r = ISCReadReport(data);
    if (!r) return NO;
    self.report = r;
    return YES;
}

- (void)failure:(NSString *)message {
    NSAlert *alert = [NSAlert new];
    alert.messageText = @"Status unavailable";
    alert.informativeText = message;
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

- (void)refresh:(id)sender {
    if (self.busy || self.snapshot) return;
    self.busy = YES; self.refreshButton.enabled = NO; self.exportButton.enabled = NO;
    self.timestamp.stringValue = @"Checking this Mac...";
    NSString *prefix = self.prefix;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        NSString *python = nil;
        for (NSString *path in @[@"/opt/homebrew/bin/python3", @"/usr/local/bin/python3", @"/usr/bin/python3"])
            if ([NSFileManager.defaultManager isExecutableFileAtPath:path]) { python = path; break; }
        NSData *data = nil;
        if (python) {
            NSTask *task = [NSTask new];
            task.executableURL = [NSURL fileURLWithPath:python];
            task.arguments = @[[NSBundle.mainBundle pathForResource:@"istat-server-status" ofType:@"py"], @"--prefix", prefix];
            NSPipe *pipe = [NSPipe pipe]; task.standardOutput = pipe;
            task.standardError = [NSFileHandle fileHandleWithNullDevice];
            if ([task launchAndReturnError:nil]) {
                data = [pipe.fileHandleForReading readDataToEndOfFile];
                [task waitUntilExit];
                if (task.terminationStatus != 0) data = nil;
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            self.busy = NO; self.refreshButton.enabled = YES;
            if ([self acceptData:data]) [self render];
            else {
                self.report = nil;
                for (NSView *view in self.rows.arrangedSubviews.copy) { [self.rows removeArrangedSubview:view]; [view removeFromSuperview]; }
                self.heading.stringValue = @"Status unavailable";
                self.summary.stringValue = @"Local inspection failed";
                self.timestamp.stringValue = @"Inspection failed";
                [self failure:python ? @"The local status collector could not complete. No server settings were changed." : @"Python 3 is required. Install Python 3, then refresh."];
            }
        });
    });
}

- (void)inspectLocal:(id)sender { if (!self.busy) { self.snapshot = NO; [self refresh:nil]; } }
- (void)choosePrefix:(id)sender {
    if (self.busy) return;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES; panel.canChooseFiles = NO;
    panel.prompt = @"Inspect"; panel.message = @"Choose the maintained server's installation folder.";
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK) { self.prefix = panel.URL.path; self.snapshot = NO; [self refresh:nil]; }
    }];
}

- (void)openReport:(id)sender {
    if (self.busy) return;
    NSOpenPanel *panel = [NSOpenPanel openPanel]; panel.allowedFileTypes = @[@"json"];
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result != NSModalResponseOK) return;
        NSNumber *size = nil; [panel.URL getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
        NSData *data = size.unsignedLongLongValue <= 65536 ? [NSData dataWithContentsOfURL:panel.URL] : nil;
        if ([self acceptData:data]) {
            self.snapshot = YES; [self render];
            [self.rows.enclosingScrollView.documentView scrollPoint:NSZeroPoint];
        }
        else [self failure:@"This is not a supported server status report."];
    }];
}

- (void)exportReport:(id)sender {
    if (!self.report || self.busy) return;
    NSSavePanel *panel = [NSSavePanel savePanel]; panel.allowedFileTypes = @[@"json"];
    panel.nameFieldStringValue = @"istat-server-status.json";
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK) {
            NSData *data = [NSJSONSerialization dataWithJSONObject:self.report options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:nil];
            if (![data writeToURL:panel.URL options:NSDataWritingAtomic error:nil]) [self failure:@"The status report could not be saved."];
        }
    }];
}

- (void)showHelp:(id)sender {
    [NSWorkspace.sharedWorkspace openURL:[NSBundle.mainBundle URLForResource:@"README" withExtension:@"md"]];
}
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action == @selector(refresh:)) return !self.busy && !self.snapshot;
    if (item.action == @selector(exportReport:)) return !self.busy && self.report != nil;
    if (item.action == @selector(inspectLocal:) || item.action == @selector(choosePrefix:) || item.action == @selector(openReport:)) return !self.busy;
    return YES;
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)app hasVisibleWindows:(BOOL)visible {
    [self.window makeKeyAndOrderFront:nil]; return YES;
}
@end

int main(int argc, const char **argv) {
    @autoreleasepool {
        NSApplication *app = NSApplication.sharedApplication;
        ISCApp *delegate = [ISCApp new];
        app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}

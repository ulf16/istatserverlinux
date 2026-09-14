#import <Cocoa/Cocoa.h>
#import "ISCReport.h"
#import "ISCSettingsClient.h"
#import "ISCSettingsProtocol.h"
#import "ISCHelperRegistration.h"
#import "ISCProtectedSettingsState.h"
#import <ServiceManagement/ServiceManagement.h>

@interface ISCFlippedView : NSView
@end
@implementation ISCFlippedView
- (BOOL)isFlipped { return YES; }
@end

@interface ISCStatusBand : NSView
@end
@implementation ISCStatusBand
- (void)drawRect:(NSRect)dirtyRect {
    NSGradient *finish = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithWhite:0.17 alpha:1]
                                                      endingColor:[NSColor colorWithWhite:0.095 alpha:1]];
    [finish drawInRect:self.bounds angle:270];
    [[NSColor colorWithWhite:1 alpha:0.08] setFill];
    NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 1));
}
@end

@interface ISCApp : NSObject <NSApplicationDelegate, NSMenuItemValidation, NSWindowDelegate>
@property NSWindow *window;
@property NSStackView *rows;
@property NSTextField *heading;
@property NSTextField *summary;
@property NSTextField *timestamp;
@property NSButton *refreshButton;
@property NSButton *exportButton;
@property NSButton *openButton;
@property NSButton *primaryButton;
@property NSSegmentedControl *edition;
@property NSSegmentedControl *tabs;
@property NSTextField *editionTitle;
@property NSImageView *serverImage;
@property BOOL choseEdition;
@property NSDictionary *report;
@property NSString *prefix;
@property BOOL busy;
@property BOOL snapshot;
@property ISCProtectedSettingsState *protectedState;
@property NSTimer *credentialDeliveryTimer;
@property ISCSettingsClient *settingsClient;
@property NSAlert *credentialSheet;
@property NSTextField *credentialField;
@property NSInteger credentialClipboardChange;
@property ISCHelperRegistrationState helperRegistration;
@property NSTimer *approvalTimer;
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
    [self item:@"Open Classic Server" action:@selector(openClassic:) key:@"" menu:file];
    [self item:@"Disable Protected Access..." action:@selector(disableProtected:) key:@"" menu:file];
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
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 740, 550)
                                             styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                               backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"iStat Server Control";
    self.window.delegate = self;
    self.window.contentMinSize = NSMakeSize(600, 470);
    self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    self.window.releasedWhenClosed = NO;
    [self.window setFrameAutosaveName:@"ServerControlDashboard"];
    NSView *content = self.window.contentView;
    NSStackView *outer = [NSStackView new];
    outer.orientation = NSUserInterfaceLayoutOrientationVertical;
    outer.alignment = NSLayoutAttributeLeading;
    outer.spacing = 0;
    outer.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:outer];
    [NSLayoutConstraint activateConstraints:@[
        [outer.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [outer.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [outer.topAnchor constraintEqualToAnchor:content.topAnchor],
        [outer.bottomAnchor constraintEqualToAnchor:content.bottomAnchor]]];
    ISCStatusBand *band = [ISCStatusBand new];
    [outer addArrangedSubview:band];
    [band.widthAnchor constraintEqualToAnchor:outer.widthAnchor].active = YES;
    [band.heightAnchor constraintEqualToConstant:196].active = YES;
    self.edition = [NSSegmentedControl segmentedControlWithLabels:@[@"Modern Server", @"Classic Server"]
                                                   trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(changeEdition:)];
    self.edition.segmentStyle = NSSegmentStyleRounded;
    self.edition.selectedSegment = 0;
    self.edition.translatesAutoresizingMaskIntoConstraints = NO;
    [self.edition setAccessibilityLabel:@"Server installation"];
    self.refreshButton = [self button:@"arrow.clockwise" label:@"Refresh (Command-R)" action:@selector(refresh:)];
    self.refreshButton.translatesAutoresizingMaskIntoConstraints = NO;
    [band addSubview:self.edition]; [band addSubview:self.refreshButton];
    self.editionTitle = [self text:@"ISTAT SERVER" size:12 secondary:YES];
    self.editionTitle.font = [NSFont systemFontOfSize:12 weight:NSFontWeightSemibold];
    self.heading = [self text:@"Checking..." size:34 secondary:NO];
    self.heading.font = [NSFont systemFontOfSize:34 weight:NSFontWeightLight];
    self.heading.maximumNumberOfLines = 1;
    self.heading.lineBreakMode = NSLineBreakByTruncatingTail;
    self.summary = [self text:@"" size:13 secondary:YES];
    self.summary.maximumNumberOfLines = 1;
    self.summary.lineBreakMode = NSLineBreakByTruncatingMiddle;
    NSStackView *titles = [NSStackView stackViewWithViews:@[self.editionTitle, self.heading, self.summary]];
    titles.orientation = NSUserInterfaceLayoutOrientationVertical;
    titles.alignment = NSLayoutAttributeLeading;
    titles.spacing = 5;
    titles.translatesAutoresizingMaskIntoConstraints = NO;
    self.serverImage = [NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:@"server.rack" accessibilityDescription:@"Server"]];
    self.serverImage.image = [self.serverImage.image imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:52 weight:NSFontWeightUltraLight]];
    self.serverImage.contentTintColor = [NSColor colorWithCalibratedRed:0.10 green:0.74 blue:0.94 alpha:1];
    self.serverImage.translatesAutoresizingMaskIntoConstraints = NO;
    [band addSubview:titles]; [band addSubview:self.serverImage];
    [NSLayoutConstraint activateConstraints:@[
        [self.edition.leadingAnchor constraintEqualToAnchor:band.leadingAnchor constant:24],
        [self.edition.topAnchor constraintEqualToAnchor:band.topAnchor constant:16],
        [self.edition.widthAnchor constraintEqualToConstant:272],
        [self.refreshButton.trailingAnchor constraintEqualToAnchor:band.trailingAnchor constant:-24],
        [self.refreshButton.centerYAnchor constraintEqualToAnchor:self.edition.centerYAnchor],
        [titles.leadingAnchor constraintEqualToAnchor:band.leadingAnchor constant:28],
        [titles.topAnchor constraintEqualToAnchor:self.edition.bottomAnchor constant:22],
        [titles.trailingAnchor constraintEqualToAnchor:self.serverImage.leadingAnchor constant:-20],
        [self.serverImage.trailingAnchor constraintEqualToAnchor:band.trailingAnchor constant:-32],
        [self.serverImage.centerYAnchor constraintEqualToAnchor:titles.centerYAnchor],
        [self.serverImage.widthAnchor constraintEqualToConstant:68],
        [self.serverImage.heightAnchor constraintEqualToConstant:68]]];
    NSView *navigation = [NSView new];
    [outer addArrangedSubview:navigation];
    [navigation.widthAnchor constraintEqualToAnchor:outer.widthAnchor].active = YES;
    [navigation.heightAnchor constraintEqualToConstant:52].active = YES;
    self.tabs = [NSSegmentedControl segmentedControlWithLabels:@[@"Overview", @"Connection", @"Diagnostics"]
                                                trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(changeTab:)];
    self.tabs.segmentStyle = NSSegmentStyleSeparated;
    self.tabs.selectedSegment = 0;
    self.tabs.translatesAutoresizingMaskIntoConstraints = NO;
    [self.tabs setAccessibilityLabel:@"Server detail view"];
    [navigation addSubview:self.tabs];
    [NSLayoutConstraint activateConstraints:@[
        [self.tabs.leadingAnchor constraintEqualToAnchor:navigation.leadingAnchor constant:24],
        [self.tabs.trailingAnchor constraintEqualToAnchor:navigation.trailingAnchor constant:-24],
        [self.tabs.centerYAnchor constraintEqualToAnchor:navigation.centerYAnchor]]];
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
    self.rows.spacing = 12;
    self.rows.translatesAutoresizingMaskIntoConstraints = NO;
    NSView *document = [ISCFlippedView new];
    document.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.documentView = document;
    [document addSubview:self.rows];
    [NSLayoutConstraint activateConstraints:@[
        [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor],
        [self.rows.topAnchor constraintEqualToAnchor:document.topAnchor constant:12],
        [self.rows.leadingAnchor constraintEqualToAnchor:document.leadingAnchor constant:28],
        [self.rows.trailingAnchor constraintEqualToAnchor:document.trailingAnchor constant:-28],
        [self.rows.bottomAnchor constraintEqualToAnchor:document.bottomAnchor constant:-16]]];
    self.timestamp = [self text:@"" size:11 secondary:YES];
    self.exportButton = [self button:@"square.and.arrow.up" label:@"Export status report" action:@selector(exportReport:)];
    self.openButton = [self button:@"doc" label:@"Open status report" action:@selector(openReport:)];
    self.primaryButton = [NSButton buttonWithTitle:@"Locate Server..." target:self action:@selector(primaryAction:)];
    self.primaryButton.bezelStyle = NSBezelStyleRounded;
    self.primaryButton.imagePosition = NSImageLeft;
    NSStackView *footer = [NSStackView stackViewWithViews:@[self.timestamp, self.openButton, self.exportButton, self.primaryButton]];
    footer.spacing = 8;
    footer.translatesAutoresizingMaskIntoConstraints = NO;
    NSView *bottom = [NSView new];
    [outer addArrangedSubview:bottom];
    [bottom.widthAnchor constraintEqualToAnchor:outer.widthAnchor].active = YES;
    [bottom.heightAnchor constraintEqualToConstant:56].active = YES;
    [bottom addSubview:footer];
    [NSLayoutConstraint activateConstraints:@[
        [footer.leadingAnchor constraintEqualToAnchor:bottom.leadingAnchor constant:24],
        [footer.trailingAnchor constraintEqualToAnchor:bottom.trailingAnchor constant:-24],
        [footer.centerYAnchor constraintEqualToAnchor:bottom.centerYAnchor]]];
    [self.timestamp setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(lockProtected:) name:NSApplicationDidResignActiveNotification object:nil];
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
    field.alignment = NSTextAlignmentRight;
    if ([value isEqualToString:@"Running"] || [value isEqualToString:@"Matched executable"])
        field.textColor = NSColor.systemGreenColor;
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
    if (!self.report) return;
    for (NSView *view in self.rows.arrangedSubviews.copy) { [self.rows removeArrangedSubview:view]; [view removeFromSuperview]; }
    NSDictionary *r = self.report, *i = r[@"installation"], *s = r[@"service"], *c = r[@"configuration"], *h = r[@"health"];
    BOOL classic = self.edition.selectedSegment == 1;
    NSDictionary *selected = classic ? r[@"legacy"] : s;
    NSString *status = [selected[@"state"] isEqual:@"idle"] ? @"Stopped" : [self state:selected[@"state"]];
    BOOL running = [selected[@"state"] isEqual:@"running"];
    self.heading.stringValue = status;
    self.heading.textColor = running ? [NSColor colorWithCalibratedRed:0.12 green:0.77 blue:0.98 alpha:1] : NSColor.secondaryLabelColor;
    self.editionTitle.stringValue = classic ? @"ISTAT SERVER CLASSIC" : @"ISTAT SERVER";
    self.summary.stringValue = [NSString stringWithFormat:@"%@%@", self.snapshot ? @"Saved report · " : @"", r[@"host"]];
    self.summary.toolTip = self.summary.stringValue;
    NSString *symbol = classic ? @"desktopcomputer" : @"server.rack";
    self.serverImage.image = [[NSImage imageWithSystemSymbolName:symbol accessibilityDescription:classic ? @"Classic server" : @"Modern server"]
                             imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:52 weight:NSFontWeightUltraLight]];
    NSArray *ports = classic ? selected[@"listeners"][@"ports"] : r[@"listeners"][@"ports"];
    NSString *portText = ports.count ? [ports componentsJoinedByString:@", "] : @"Not observed";
    if (self.tabs.selectedSegment == 0) {
        [self section:@"Service"];
        [self row:@"Status" value:status];
        [self row:@"Daemon process" value:[selected[@"pid"] isKindOfClass:NSNumber.class] ? [NSString stringWithFormat:@"PID %@", selected[@"pid"]] : @"Not observed"];
        [self row:@"Listening ports" value:portText];
        if (classic) {
            [self row:@"Settings app version" value:[selected[@"app_version"] isKindOfClass:NSString.class] ? selected[@"app_version"] : @"Not observed"];
        } else {
            [self row:@"Installation" value:i[@"prefix"]];
        }
        [self section:@"Other installation"];
        [self row:classic ? @"Modern server" : @"Classic server" value:[self state:classic ? s[@"state"] : r[@"legacy"][@"state"]]];
    } else if (self.tabs.selectedSegment == 1) {
        [self section:@"Network"];
        [self row:@"Listening ports" value:portText];
        if (classic) {
            [self row:@"Authentication" value:@"Managed by classic server"];
            [self row:@"Settings application" value:[selected[@"app_present"] boolValue] ? @"Installed" : @"Not observed"];
            [self row:@"Configuration access" value:@"Classic settings app"];
        } else {
            [self row:@"Port in file" value:[c[@"port"] isKindOfClass:NSNumber.class] ? [c[@"port"] stringValue] : @"Not available"];
            [self row:@"Address in file" value:[c[@"address"] isKindOfClass:NSString.class] ? c[@"address"] : @"Not available"];
            [self row:@"Bonjour" value:@"Not observed"];
            [self section:@"Pairing & configuration"];
            [self row:@"Pairing credential" value:[self state:c[@"pairing"]]];
            [self row:@"Direct file access" value:[c[@"state"] isEqual:@"permission-denied"] ? @"Protected by macOS" : [self state:c[@"state"]]];
            [self row:@"File" value:i[@"configuration"]];
            [self section:@"Protected settings"];
            [self row:@"Read-only helper" value:[self protectedAccessStatus]];
            if (self.protectedState.metadata && !self.snapshot) {
                [self row:@"Configured port" value:[self.protectedState.metadata[@"port"] stringValue]];
                [self row:@"Configured address" value:self.protectedState.metadata[@"address"]];
                [self row:@"Authentication mode" value:[self state:self.protectedState.metadata[@"auth_mode"]]];
                [self row:@"Credential source" value:[self state:self.protectedState.metadata[@"credential_source"]]];
                [self row:@"Pairing credential" value:@"Hidden"];
            }
            NSButton *enable = [NSButton buttonWithTitle:@"Enable Protected Access..." target:self action:@selector(enableProtected:)];
            if (self.helperRegistration == ISCHelperRegistrationWaiting)
                enable.title = @"Open Approval Settings...";
            else if (self.helperRegistration == ISCHelperRegistrationReady)
                enable.title = @"Protected Access Enabled";
            enable.bezelStyle = NSBezelStyleRounded;
            enable.enabled = !self.snapshot && !self.settingsClient && !self.busy && [self canRegisterHelper] &&
                self.helperRegistration != ISCHelperRegistrationReady;
            NSButton *read = [NSButton buttonWithTitle:@"Read Settings..." target:self action:@selector(readProtected:)];
            read.bezelStyle = NSBezelStyleRounded;
            read.enabled = [self canReadProtected];
            NSButton *reveal = [self button:@"eye" label:@"Reveal pairing credential" action:@selector(revealProtected:)];
            NSButton *copy = [self button:@"doc.on.doc" label:@"Copy pairing credential" action:@selector(copyProtected:)];
            reveal.enabled = copy.enabled = read.enabled;
            NSStackView *actions = [NSStackView stackViewWithViews:@[enable, read, reveal, copy]];
            actions.spacing = 8;
            [self.rows addArrangedSubview:actions];
            [self row:@"Settings scope" value:@"Configuration file; running options may differ"];
        }
    } else {
        [self section:@"Daemon identity"];
        [self row:@"Service identifier" value:selected[@"label"]];
        [self row:@"Executable" value:classic ? selected[@"binary"] : i[@"binary"]];
        [self row:@"Verification" value:[selected[@"identity"] isEqual:@"matched"] ? @"Matched executable" : [self state:selected[@"identity"]]];
        if (classic) {
            [self row:@"Last exit" value:[selected[@"last_exit"] isKindOfClass:NSNumber.class] ? [selected[@"last_exit"] stringValue] : @"Not recorded"];
            [self row:@"Administration" value:@"Original settings app only"];
        } else {
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
        }
    }
    self.timestamp.stringValue = [NSString stringWithFormat:@"%@ · %@", self.snapshot ? @"Report captured" : @"Checked", [self date:r[@"checked"]]];
    self.refreshButton.enabled = !self.snapshot && !self.busy;
    self.exportButton.enabled = !self.busy;
    self.primaryButton.title = classic ? @"Open Classic Server" : @"Locate Server...";
    self.primaryButton.image = [NSImage imageWithSystemSymbolName:classic ? @"arrow.up.forward.app" : @"folder" accessibilityDescription:nil];
    self.primaryButton.enabled = !self.busy && !self.snapshot && (!classic || [self canOpenClassic]);
}

- (BOOL)acceptData:(NSData *)data {
    NSDictionary *r = ISCReadReport(data);
    if (!r) return NO;
    self.report = r;
    if (!self.choseEdition) {
        self.edition.selectedSegment = ![r[@"installation"][@"installed"] boolValue] &&
                                      [r[@"legacy"][@"state"] isEqual:@"running"] ? 1 : 0;
    }
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
    [self clearProtected];
    self.busy = YES; self.refreshButton.enabled = NO; self.exportButton.enabled = NO;
    self.primaryButton.enabled = NO; self.openButton.enabled = NO;
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
            self.busy = NO; self.refreshButton.enabled = YES; self.openButton.enabled = YES;
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
- (void)changeEdition:(id)sender {
    [self clearProtected];
    self.choseEdition = YES;
    [self render];
    [self.rows.enclosingScrollView.documentView scrollPoint:NSZeroPoint];
}
- (void)changeTab:(id)sender {
    [self clearProtected];
    [self render];
    [self.rows.enclosingScrollView.documentView scrollPoint:NSZeroPoint];
}
- (BOOL)canOpenClassic {
    return !self.snapshot && [[[NSBundle bundleWithPath:@"/Applications/iStat Server.app"] bundleIdentifier] isEqual:@"com.bjango.iStatServer"];
}
- (void)openClassic:(id)sender {
    if (self.busy || ![self canOpenClassic]) return;
    NSURL *url = [NSURL fileURLWithPath:@"/Applications/iStat Server.app"];
    if (![NSWorkspace.sharedWorkspace openURL:url]) [self failure:@"The original iStat Server settings app could not be opened."];
}
- (void)primaryAction:(id)sender {
    if (self.edition.selectedSegment == 1) [self openClassic:sender];
    else [self choosePrefix:sender];
}
- (void)choosePrefix:(id)sender {
    if (self.busy) return;
    [self clearProtected];
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES; panel.canChooseFiles = NO;
    panel.prompt = @"Inspect"; panel.message = @"Choose the maintained server's installation folder.";
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK) { self.prefix = panel.URL.path; self.snapshot = NO; [self refresh:nil]; }
    }];
}

- (void)openReport:(id)sender {
    if (self.busy) return;
    [self clearProtected];
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

- (NSString *)settingsInstallation {
    if ([self.prefix isEqual:@"/opt/istatserverlinux"]) return @"opt";
    if ([self.prefix isEqual:@"/usr/local"]) return @"local";
    return nil;
}
- (BOOL)canRegisterHelper {
    if (@available(macOS 13.0, *)) {
        return ISCSettingsPeerRequirement(ISCSettingsService) != nil &&
            [[NSBundle.mainBundle.bundlePath stringByDeletingLastPathComponent] isEqual:@"/Applications"];
    }
    return NO;
}
- (NSString *)protectedAccessStatus {
    if (self.snapshot) return @"Unavailable for saved reports";
    if (@available(macOS 13.0, *)) {
        if (!ISCSettingsPeerRequirement(ISCSettingsService)) return @"Signed build required";
        if (![self canRegisterHelper]) return @"Install signed app in /Applications";
        if (self.helperRegistration == ISCHelperRegistrationWaiting) return @"Waiting for approval in System Settings";
        switch ([SMAppService daemonServiceWithPlistName:ISCSettingsPlist].status) {
            case SMAppServiceStatusEnabled:
                if (self.settingsClient) return @"Awaiting authorization / reading...";
                if ([self.protectedState hasPendingCredentialAtTime:NSProcessInfo.processInfo.systemUptime]) return @"Authorized; waiting for this window";
                return self.protectedState.metadata ? @"Settings read successfully" : @"Ready";
            case SMAppServiceStatusRequiresApproval: return @"Approval needed in System Settings";
            default: return @"Not enabled";
        }
    }
    return @"Requires macOS 13 or later";
}
- (BOOL)canReadProtected {
    if (self.snapshot || self.busy || self.settingsClient || self.edition.selectedSegment != 0 ||
        ![self settingsInstallation] || ![self.report[@"installation"][@"installed"] boolValue] || ![self canRegisterHelper]) return NO;
    if (@available(macOS 13.0, *)) return [SMAppService daemonServiceWithPlistName:ISCSettingsPlist].status == SMAppServiceStatusEnabled;
    return NO;
}
- (void)enableProtected:(id)sender {
    if (self.snapshot || self.busy || self.settingsClient || ![self canRegisterHelper]) return;
    if (@available(macOS 13.0, *)) {
        if (self.helperRegistration == ISCHelperRegistrationWaiting ||
            [SMAppService daemonServiceWithPlistName:ISCSettingsPlist].status == SMAppServiceStatusRequiresApproval) {
            self.helperRegistration = ISCHelperRegistrationWaiting;
            [self observeHelperApproval];
            [SMAppService openSystemSettingsLoginItems];
            [self render];
            return;
        }
    }
    NSAlert *alert = [NSAlert new];
    alert.messageText = @"Enable protected settings access?";
    alert.informativeText = @"This registers a root helper that can read only the modern server's known configuration files. Each read requires macOS authorization. It cannot change settings, control services or access databases. macOS may require approval in Login Items & Extensions.";
    [alert addButtonWithTitle:@"Enable"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result != NSAlertFirstButtonReturn) return;
        if (@available(macOS 13.0, *)) {
            SMAppService *service = [SMAppService daemonServiceWithPlistName:ISCSettingsPlist];
            NSError *error = nil;
            BOOL registered = YES;
            if (service.status != SMAppServiceStatusEnabled && service.status != SMAppServiceStatusRequiresApproval)
                registered = [service registerAndReturnError:&error];
            self.helperRegistration = ISCRegistrationResult(service.status, registered, error);
            if (self.helperRegistration == ISCHelperRegistrationFailed) {
                [self failure:@"macOS did not register the signed helper. Distribution builds require Developer ID signing and notarization. No server settings were changed."];
            } else if (self.helperRegistration == ISCHelperRegistrationWaiting) {
                [self observeHelperApproval];
                [SMAppService openSystemSettingsLoginItems];
            }
            [self render];
        }
    }];
}
- (void)observeHelperApproval {
    if (self.approvalTimer) return;
    __weak ISCApp *weak = self;
    self.approvalTimer = [NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer) {
        [weak refreshHelperApproval];
    }];
}
- (void)refreshHelperApproval {
    if (@available(macOS 13.0, *)) {
        if (![self canRegisterHelper]) return;
        ISCHelperRegistrationState state = ISCRegistrationObserved(
            [SMAppService daemonServiceWithPlistName:ISCSettingsPlist].status, self.helperRegistration);
        BOOL changed = state != self.helperRegistration;
        self.helperRegistration = state;
        if (state == ISCHelperRegistrationWaiting) [self observeHelperApproval];
        else { [self.approvalTimer invalidate]; self.approvalTimer = nil; }
        // Observe only: approval must never initiate a credential read or retry.
        if (changed) [self render];
    }
}
- (BOOL)canDisableHelper {
    if (self.snapshot || self.busy || self.settingsClient || ![self canRegisterHelper]) return NO;
    if (@available(macOS 13.0, *)) {
        SMAppServiceStatus status = [SMAppService daemonServiceWithPlistName:ISCSettingsPlist].status;
        return status == SMAppServiceStatusEnabled || status == SMAppServiceStatusRequiresApproval ||
            self.helperRegistration == ISCHelperRegistrationWaiting;
    }
    return NO;
}
- (void)disableProtected:(id)sender {
    if (![self canDisableHelper]) return;
    NSAlert *alert = [NSAlert new];
    alert.messageText = @"Disable protected settings access?";
    alert.informativeText = @"This unregisters only the settings reader. The iStat server and its collection helpers keep running; configuration and history are unchanged.";
    [alert addButtonWithTitle:@"Disable"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        if (result != NSAlertFirstButtonReturn) return;
        [self clearProtected];
        if (@available(macOS 13.0, *)) {
            if (![[SMAppService daemonServiceWithPlistName:ISCSettingsPlist] unregisterAndReturnError:nil])
                [self failure:@"macOS could not unregister the settings reader. Its access can also be disabled in Login Items & Extensions."];
            else {
                self.helperRegistration = ISCHelperRegistrationIdle;
                [self.approvalTimer invalidate]; self.approvalTimer = nil;
            }
        }
        [self render];
    }];
}
- (ISCSettingsClient *)makeSettingsClient { return [ISCSettingsClient new]; }
- (BOOL)protectedWindowIsForeground {
    return NSApp.active && self.window.visible && self.window.keyWindow && !self.window.attachedSheet;
}
- (void)requestProtected:(NSInteger)action {
    if (![self canReadProtected]) return;
    if (!self.protectedState) self.protectedState = [ISCProtectedSettingsState new];
    [self.protectedState beginRequest];
    [self.credentialDeliveryTimer invalidate]; self.credentialDeliveryTimer = nil;
    [self hideCredential];
    ISCSettingsClient *client = [self makeSettingsClient];
    self.settingsClient = client;
    [self render];
    __weak ISCApp *weak = self;
    [client readInstallation:[self settingsInstallation] reveal:action != 0 completion:^(NSDictionary *result, NSString *error) {
        ISCApp *app = weak;
        if (app.settingsClient != client) return;
        app.settingsClient = nil;
        if (error) { [app render]; [app failure:error]; return; }
        if (!result || app.snapshot || !app.window.visible) { [app render]; return; }
        // Authorization can finish before AppKit restores focus. Retain only a
        // short-lived, one-shot delivery; never repeat the authorized read.
        [app.protectedState receiveResult:result action:action atTime:NSProcessInfo.processInfo.systemUptime];
        [app render];
        [app deliverProtectedCredential];
        if ([app.protectedState hasPendingCredentialAtTime:NSProcessInfo.processInfo.systemUptime]) {
            app.credentialDeliveryTimer = [NSTimer timerWithTimeInterval:0.25 repeats:YES block:^(NSTimer *timer) {
                [weak deliverProtectedCredential];
            }];
            [NSRunLoop.mainRunLoop addTimer:app.credentialDeliveryTimer forMode:NSRunLoopCommonModes];
        }
    }];
}
- (void)deliverProtectedCredential {
    BOOL pending = [self.protectedState hasPendingCredentialAtTime:NSProcessInfo.processInfo.systemUptime];
    if (!pending) {
        BOOL expired = self.credentialDeliveryTimer != nil;
        [self.credentialDeliveryTimer invalidate]; self.credentialDeliveryTimer = nil;
        if (expired) [self render];
        return;
    }
    if (![self protectedWindowIsForeground]) return;
    if (self.snapshot || self.edition.selectedSegment != 0 || self.tabs.selectedSegment != 1) {
        [self clearProtected]; return;
    }
    NSDictionary *result = [self.protectedState takePendingCredentialAtTime:NSProcessInfo.processInfo.systemUptime];
    [self.credentialDeliveryTimer invalidate]; self.credentialDeliveryTimer = nil;
    [self render];
    NSInteger action = [result[@"action"] integerValue];
    if (action == 1) [self showCredential:result[@"credential"]];
    if (action == 2) {
        NSPasteboard *pasteboard = NSPasteboard.generalPasteboard;
        [pasteboard clearContents];
        [pasteboard setString:result[@"credential"] forType:NSPasteboardTypeString];
        // Mark as transient/concealed for clipboard managers which honor it.
        [pasteboard setData:[NSData data] forType:@"org.nspasteboard.ConcealedType"];
        [pasteboard setData:[NSData data] forType:@"org.nspasteboard.TransientType"];
        NSInteger count = pasteboard.changeCount;
        self.credentialClipboardChange = count;
        self.timestamp.stringValue = @"Credential copied; clipboard clears in 30 seconds";
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            if (pasteboard.changeCount == count) [pasteboard clearContents];
        });
    }
}
- (void)readProtected:(id)sender { [self requestProtected:0]; }
- (void)revealProtected:(id)sender { [self requestProtected:1]; }
- (void)copyProtected:(id)sender { [self requestProtected:2]; }
- (void)showCredential:(NSString *)credential {
    NSAlert *sheet = [NSAlert new];
    sheet.messageText = @"Pairing credential";
    sheet.informativeText = @"From the modern server configuration. Hidden when this window loses focus or after 30 seconds.";
    NSTextField *field = [NSTextField wrappingLabelWithString:credential];
    field.font = [NSFont monospacedSystemFontOfSize:22 weight:NSFontWeightMedium];
    field.selectable = NO;
    field.frame = NSMakeRect(0, 0, 360, 80);
    field.maximumNumberOfLines = 3;
    field.lineBreakMode = NSLineBreakByTruncatingTail;
    sheet.accessoryView = field;
    [sheet addButtonWithTitle:@"Hide"];
    self.credentialSheet = sheet; self.credentialField = field;
    [sheet beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result) {
        field.stringValue = @"";
        if (self.credentialSheet == sheet) { self.credentialSheet = nil; self.credentialField = nil; }
    }];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        if (self.credentialSheet == sheet) [self hideCredential];
    });
}
- (void)hideCredential {
    self.credentialField.stringValue = @"";
    if (self.credentialSheet) [self.window endSheet:self.credentialSheet.window];
    self.credentialSheet = nil; self.credentialField = nil;
}
- (void)clearProtected {
    ISCSettingsClient *client = self.settingsClient;
    self.settingsClient = nil;
    [client cancel];
    [self.protectedState clear];
    [self.credentialDeliveryTimer invalidate]; self.credentialDeliveryTimer = nil;
    [self hideCredential];
}
- (void)lockProtected:(NSNotification *)note {
    [self hideCredential];
    // Focus loss hides the displayed secret, not previously read non-secret
    // metadata. The OS authorization dialog itself can cause this notification.
    if (!self.settingsClient) [self render];
}
- (void)windowWillClose:(NSNotification *)note { [self clearProtected]; }
- (void)applicationDidBecomeActive:(NSNotification *)note {
    // Approval can change while System Settings is foreground. Refresh only
    // controls; never start another authorization request automatically.
    [self refreshHelperApproval];
    [self render];
    [self deliverProtectedCredential];
}
- (void)applicationWillTerminate:(NSNotification *)note {
    [self.approvalTimer invalidate];
    [self clearProtected];
    if (self.credentialClipboardChange && NSPasteboard.generalPasteboard.changeCount == self.credentialClipboardChange)
        [NSPasteboard.generalPasteboard clearContents];
}
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action == @selector(refresh:)) return !self.busy && !self.snapshot;
    if (item.action == @selector(exportReport:)) return !self.busy && self.report != nil;
    if (item.action == @selector(openClassic:)) return !self.busy && [self canOpenClassic];
    if (item.action == @selector(disableProtected:)) return [self canDisableHelper];
    if (item.action == @selector(inspectLocal:) || item.action == @selector(choosePrefix:) || item.action == @selector(openReport:)) return !self.busy;
    return YES;
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)app hasVisibleWindows:(BOOL)visible {
    [self.window makeKeyAndOrderFront:nil]; return YES;
}
@end

#ifndef ISC_APP_TESTING
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
#endif

#import <AppKit/AppKit.h>

#include "NativeMac.hpp"

namespace blubox::platform {
namespace {

NSString* ToNSString(std::string_view value) {
    return [[[NSString alloc] initWithBytes:value.data()
                                     length:value.size()
                                   encoding:NSUTF8StringEncoding] autorelease];
}

} // namespace

void ShowNativeErrorDialog(std::string_view title, std::string_view message) {
    @autoreleasepool {
        NSAlert* alert = [[[NSAlert alloc] init] autorelease];
        alert.messageText = ToNSString(title) ?: @"Blubox";
        alert.informativeText = ToNSString(message) ?: @"An unexpected error occurred.";
        alert.alertStyle = NSAlertStyleCritical;
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
}

std::optional<std::filesystem::path> ShowNativeFileDialog(
    bool save, std::string_view title, const std::vector<std::string_view>& extensions) {
    @autoreleasepool {
        NSSavePanel* panel = save ? [NSSavePanel savePanel] : [NSOpenPanel openPanel];
        panel.title = ToNSString(title);
        NSMutableArray<NSString*>* allowedTypes = [NSMutableArray arrayWithCapacity:extensions.size()];
        for (const auto extension : extensions) {
            NSString* type = ToNSString(extension);
            if (type != nil) [allowedTypes addObject:type];
        }
        if (allowedTypes.count != 0) panel.allowedFileTypes = allowedTypes;
        if ([panel runModal] != NSModalResponseOK || panel.URL == nil) return std::nullopt;
        const char* path = panel.URL.fileSystemRepresentation;
        if (path == nullptr) return std::nullopt;
        return std::filesystem::path(path);
    }
}

} // namespace blubox::platform

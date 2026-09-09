// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Documents.h"
#include <UIKit/UIKit.h>
#include <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <atomic>
#include <array>

using namespace GAGCore::ApplicationHost;
namespace {
std::atomic<bool> reserved{false};
UIViewController* presenter() {
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (scene.activationState != UISceneActivationStateForegroundActive || ![scene isKindOfClass:UIWindowScene.class]) continue;
        for (UIWindow* window in ((UIWindowScene*)scene).windows) {
            if (!window.isKeyWindow) continue;
            UIViewController* controller = window.rootViewController;
            while (controller.presentedViewController) controller = controller.presentedViewController;
            return controller;
        }
    }
    return nil;
}
void exportError(NSString* message) {
    UIViewController* controller = presenter();
    if (!controller) return;
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:nil
        message:message
        preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [controller presentViewController:alert animated:YES completion:nil];
}
}
@interface Glob2DocumentPicker : NSObject <UIDocumentPickerDelegate, UIAdaptivePresentationControllerDelegate>
@property(nonatomic) MobileDocuments::Request request;
@property(nonatomic) BOOL reading;
@property(nonatomic) BOOL cancelled;
@property(nonatomic, strong) UIDocumentPickerViewController* picker;
@property(nonatomic, strong) NSURL* temporaryDirectory;
- (void)finish;
- (void)cancel;
- (BOOL)present;
@end
static Glob2DocumentPicker* active;
@implementation Glob2DocumentPicker
- (BOOL)present {
    UIViewController* controller = presenter();
    if (!controller || controller.isBeingDismissed || controller.isBeingPresented) return NO;
    self.picker.delegate = self;
    [controller presentViewController:self.picker animated:YES completion:nil];
    self.picker.presentationController.delegate = self;
    return YES;
}
- (void)finish {
    if (self.temporaryDirectory) [[NSFileManager defaultManager] removeItemAtURL:self.temporaryDirectory error:nil];
    self.picker.delegate = nil;
    self.picker = nil;
    if (active == self) { active = nil; reserved.store(false); }
}
- (void)cancel {
    self.cancelled = YES;
    [self.picker dismissViewControllerAnimated:YES completion:nil];
    if (!self.reading) [self finish];
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    if (self.request) MobileDocuments::complete(self.request, FileSelectionState::Cancelled);
    [self cancel];
}
- (void)presentationControllerDidDismiss:(UIPresentationController*)controller {
    [self documentPickerWasCancelled:self.picker];
}
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    if (!self.request) { [self finish]; return; } // Export is performed by the system picker.
    if (urls.count != 1) { MobileDocuments::complete(self.request, FileSelectionState::Failed); [self finish]; return; }
    self.reading = YES;
    NSURL* url = urls.firstObject;
    const auto request = self.request;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            SelectedFile selected;
            bool succeeded = false;
            const BOOL access = [url startAccessingSecurityScopedResource];
            NSInputStream* input = nil;
            try {
            @try {
                NSData* name = [url.lastPathComponent dataUsingEncoding:NSUTF8StringEncoding];
                NSNumber* size = nil;
                [url getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
                if (name.length && name.length <= 1024 && (!size || size.unsignedLongLongValue <= MobileDocuments::maximumBytes)) {
                    selected.name.assign(static_cast<const char*>(name.bytes), name.length);
                    input = [NSInputStream inputStreamWithURL:url];
                    [input open];
                    std::array<unsigned char, 65536> buffer;
                    for (;;) {
                        NSInteger count = [input read:buffer.data() maxLength:buffer.size()];
                        if (count == 0) { succeeded = input != nil; break; }
                        if (count < 0 || static_cast<std::size_t>(count) > MobileDocuments::maximumBytes - selected.bytes.size()) break;
                        selected.bytes.insert(selected.bytes.end(), buffer.begin(), buffer.begin() + count);
                    }
                }
            } @catch (NSException*) { succeeded = false; }
            } catch (...) { succeeded = false; selected = {}; }
            [input close];
            if (access) [url stopAccessingSecurityScopedResource];
            MobileDocuments::complete(request, succeeded ? FileSelectionState::Selected : FileSelectionState::Failed, std::move(selected));
            dispatch_async(dispatch_get_main_queue(), ^{ self.reading = NO; [self finish]; });
        }
    });
}
@end

namespace MobileDocuments {
bool platformOpen(Request request, const std::string&) {
    if (reserved.exchange(true)) return false;
    dispatch_async(dispatch_get_main_queue(), ^{
        active = [Glob2DocumentPicker new];
        active.request = request;
        active.picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeData] asCopy:YES];
        active.picker.allowsMultipleSelection = NO;
        if (![active present]) { complete(request, FileSelectionState::Failed); [active finish]; }
    });
    return true;
}
void platformCancel(Request request) {
    dispatch_async(dispatch_get_main_queue(), ^{ if (active.request == request) [active cancel]; });
}
bool platformExport(const std::string& name, const std::vector<unsigned char>& bytes, const std::string& error) {
    NSString* message = [[NSString alloc] initWithBytes:error.data() length:error.size() encoding:NSUTF8StringEncoding];
    NSData* contents = [NSData dataWithBytes:bytes.data() length:bytes.size()];
    NSString* filename = [[NSString alloc] initWithBytes:name.data() length:name.size() encoding:NSUTF8StringEncoding];
    if (!contents || !filename || reserved.exchange(true)) return false;
    dispatch_async(dispatch_get_main_queue(), ^{
        Glob2DocumentPicker* operation = [Glob2DocumentPicker new];
        active = operation;
        operation.temporaryDirectory = [NSURL fileURLWithPath:[NSTemporaryDirectory()
            stringByAppendingPathComponent:[@"Glob2-export-" stringByAppendingString:NSUUID.UUID.UUIDString]] isDirectory:YES];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            NSURL* file = [operation.temporaryDirectory URLByAppendingPathComponent:filename];
            BOOL written = [[NSFileManager defaultManager] createDirectoryAtURL:operation.temporaryDirectory
                withIntermediateDirectories:YES attributes:nil error:nil] &&
                [contents writeToURL:file options:NSDataWritingAtomic error:nil];
            dispatch_async(dispatch_get_main_queue(), ^{
                if (written) {
                    operation.picker = [[UIDocumentPickerViewController alloc] initForExportingURLs:@[file] asCopy:YES];
                    if ([operation present]) return;
                }
                [operation finish];
                exportError(message);
            });
        });
    });
    return true;
}
}

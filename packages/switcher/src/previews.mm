#include "previews.h"
#import <CoreImage/CoreImage.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <algorithm>
#include <atomic>
#include <utility>

using namespace spacewing;
using namespace spacewing::switcher;

API_AVAILABLE(macos(12.3))
@interface SWPreviewStream : NSObject <SCStreamOutput, SCStreamDelegate> {
    SCStream *stream_;
    CIContext *context_;
    std::atomic_bool stopped_;
    std::atomic_bool pendingFrame_;
    BOOL starting_;
    BOOL receivedFrame_;
    void (^frame_)(CFRef<CGImageRef>, PreviewStatus);
}
- (instancetype)initWithWindow:(SCWindow *)window
                      interval:(CMTime)interval
                         queue:(dispatch_queue_t)queue
                       context:(CIContext *)context
                         frame:(void (^)(CFRef<CGImageRef>, PreviewStatus))frame;
- (void)stop;
@end

@implementation SWPreviewStream
- (instancetype)initWithWindow:(SCWindow *)window
                      interval:(CMTime)interval
                         queue:(dispatch_queue_t)queue
                       context:(CIContext *)context
                         frame:(void (^)(CFRef<CGImageRef>, PreviewStatus))frame {
    self = [super init];
    if (!self)
        return nil;
    stopped_ = false;
    pendingFrame_ = false;
    context_ = context;
    frame_ = [frame copy];
    auto configuration = [SCStreamConfiguration new];
    configuration.width = 400;
    configuration.height = 250;
    configuration.minimumFrameInterval = interval;
    configuration.queueDepth = 3;
    configuration.pixelFormat = kCVPixelFormatType_32BGRA;
    configuration.showsCursor = NO;
    configuration.scalesToFit = YES;
    // No audio output is registered. Audio capture is off by default on 12.3 too.
    if (@available(macOS 13.0, *))
        configuration.capturesAudio = NO;
    if (@available(macOS 14.0, *)) {
        configuration.preservesAspectRatio = YES;
        configuration.ignoreShadowsSingleWindow = YES;
    }
    auto filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
    stream_ = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:self];
    NSError *error = nil;
    if (![stream_ addStreamOutput:self
                             type:SCStreamOutputTypeScreen
               sampleHandlerQueue:queue
                            error:&error]) {
        frame_({}, PreviewStatus::Unavailable);
        [self stop];
        return self;
    }
    starting_ = YES;
    [stream_ startCaptureWithCompletionHandler:^(NSError *error) {
      dispatch_async(dispatch_get_main_queue(), ^{
        starting_ = NO;
        // Closing or changing the filter can race a pending start. Stop again after it completes.
        if (stopped_) {
            [self stop];
        } else if (error) {
            frame_({}, PreviewStatus::Unavailable);
            [self stop];
        }
      });
    }];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
      if (!stopped_ && !receivedFrame_)
          frame_({}, PreviewStatus::Unavailable);
    });
    return self;
}
- (void)stop {
    stopped_ = true;
    frame_ = nil;
    if (!stream_ || starting_)
        return;
    SCStream *stream = stream_;
    stream_ = nil;
    [stream removeStreamOutput:self type:SCStreamOutputTypeScreen error:nil];
    [stream stopCaptureWithCompletionHandler:^(NSError *) {
      // Retain the stream until asynchronous teardown completes.
      (void)stream;
    }];
}
- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    dispatch_async(dispatch_get_main_queue(), ^{
      if (stopped_)
          return;
      frame_({}, PreviewStatus::Unavailable);
      [self stop];
    });
}
- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sample
                   ofType:(SCStreamOutputType)type {
    @autoreleasepool {
        if (stopped_ || type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sample))
            return;
        NSArray *attachments =
            (__bridge NSArray *)CMSampleBufferGetSampleAttachmentsArray(sample, NO);
        NSNumber *status = [attachments.firstObject objectForKey:SCStreamFrameInfoStatus];
        if (!status || status.integerValue == SCFrameStatusIdle ||
            status.integerValue == SCFrameStatusStarted)
            return;
        // Bound delivery to one pending image per tile if AppKit is busy.
        if (pendingFrame_.exchange(true))
            return;
        CFRef<CGImageRef> image;
        if (status.integerValue == SCFrameStatusComplete) {
            if (auto buffer = CMSampleBufferGetImageBuffer(sample)) {
                auto input = [CIImage imageWithCVPixelBuffer:buffer];
                image = CFRef<CGImageRef>([context_ createCGImage:input fromRect:input.extent]);
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
          pendingFrame_ = false;
          if (!stopped_) {
              receivedFrame_ = image.get() != nullptr;
              frame_(image, image ? PreviewStatus::Live : PreviewStatus::Unavailable);
          }
        });
    }
}
@end

namespace spacewing::switcher {
struct Previews::Impl : std::enable_shared_from_this<Previews::Impl> {
    uint64_t generation = 0;
    Handler handler;
    NSMutableArray *streams = [NSMutableArray new];
    dispatch_queue_t queue =
        dispatch_queue_create("dev.spacewing.switcher.previews", DISPATCH_QUEUE_SERIAL);
    CIContext *context = nil;

    void stop() {
        ++generation;
        handler = {};
        if (@available(macOS 12.3, *))
            for (SWPreviewStream *stream in streams)
                [stream stop];
        [streams removeAllObjects];
    }
    void show(std::vector<WindowID> windows, Handler callback) {
        stop();
        handler = std::move(callback);
        const auto state = Previews::availability();
        for (auto id : windows)
            handler(id, {}, state);
        if (state != PreviewStatus::Loading || windows.empty())
            return;
        if (@available(macOS 12.3, *)) {
            if (!context)
                context = [CIContext contextWithOptions:@{kCIContextCacheIntermediates : @NO}];
            // Share a 60-frame/second budget across the grid, at most 12 fps per window.
            const auto interval = CMTimeMake(std::max<size_t>(5, windows.size()), 60);
            const auto token = generation;
            auto weak = weak_from_this();
            // Fast taps and quick filter changes should not spin up capture streams.
            dispatch_after(
                dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                  auto owner = weak.lock();
                  if (!owner || owner->generation != token)
                      return;
                  [SCShareableContent
                      getShareableContentExcludingDesktopWindows:YES
                                             onScreenWindowsOnly:NO
                                               completionHandler:^(SCShareableContent *content,
                                                                   NSError *error) {
                                                 dispatch_async(dispatch_get_main_queue(), ^{
                                                   auto self = weak.lock();
                                                   if (!self || self->generation != token)
                                                       return;
                                                   for (auto id : windows) {
                                                       SCWindow *match = nil;
                                                       for (SCWindow *window in content.windows)
                                                           if (window.windowID == id.number &&
                                                               window.owningApplication.processID ==
                                                                   id.pid) {
                                                               match = window;
                                                               break;
                                                           }
                                                       if (error || !match) {
                                                           self->handler(
                                                               id, {}, PreviewStatus::Unavailable);
                                                           continue;
                                                       }
                                                       auto stream = [[SWPreviewStream alloc]
                                                           initWithWindow:match
                                                                 interval:interval
                                                                    queue:self->queue
                                                                  context:self->context
                                                                    frame:^(CFRef<CGImageRef> image,
                                                                            PreviewStatus status) {
                                                                      auto current = weak.lock();
                                                                      if (current &&
                                                                          current->generation ==
                                                                              token)
                                                                          current->handler(
                                                                              id, std::move(image),
                                                                              status);
                                                                    }];
                                                       [self->streams addObject:stream];
                                                   }
                                                 });
                                               }];
                });
        }
    }
};

Previews::Previews() : impl_(std::make_shared<Impl>()) {}
Previews::~Previews() { stop(); }
void Previews::show(std::vector<WindowID> windows, Handler handler) {
    impl_->show(std::move(windows), std::move(handler));
}
void Previews::stop() { impl_->stop(); }
PreviewStatus Previews::availability() {
    if (@available(macOS 12.3, *))
        return CGPreflightScreenCaptureAccess() ? PreviewStatus::Loading
                                                : PreviewStatus::PermissionRequired;
    return PreviewStatus::Unsupported;
}
} // namespace spacewing::switcher

#include "macdrop.hpp"

#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>

namespace {

std::function<void(double, double, std::string)> g_on_drop;

NSDragOperation dragging_entered(id, SEL, id<NSDraggingInfo>)
{
  return NSDragOperationCopy;
}

NSDragOperation dragging_updated(id, SEL, id<NSDraggingInfo>)
{
  return NSDragOperationCopy;
}

BOOL prepare_for_drag(id, SEL, id<NSDraggingInfo>)
{
  return YES;
}

BOOL perform_drag(id self, SEL, id<NSDraggingInfo> sender)
{
  auto* view = static_cast<NSView*>(self);
  NSArray<NSURL*>* urls = [[sender draggingPasteboard]
      readObjectsForClasses:@[ [NSURL class] ]
                    options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
  if (urls.count == 0 || !g_on_drop) {
    return NO;
  }
  NSPoint p = [view convertPoint:[sender draggingLocation] fromView:nil];
  double x = p.x;
  double y = view.isFlipped ? p.y : view.bounds.size.height - p.y;
  g_on_drop(x, y, std::string(urls.firstObject.path.UTF8String));
  return YES;
}

}  // namespace

namespace spdsx {

void install_file_drop_handler(
    std::function<void(double x, double y, std::string path)> on_drop)
{
  g_on_drop = std::move(on_drop);

  NSView* view = [NSApp windows].firstObject.contentView;
  if (!view) {
    return;
  }
  // class_replaceMethod both overrides winit's implementations (it handles
  // file drops itself, but Slint discards the resulting events) and adds
  // any that are missing.
  Class cls = object_getClass(view);
  class_replaceMethod(cls, @selector(draggingEntered:),
      reinterpret_cast<IMP>(dragging_entered), "Q@:@");
  class_replaceMethod(cls, @selector(draggingUpdated:),
      reinterpret_cast<IMP>(dragging_updated), "Q@:@");
  class_replaceMethod(cls, @selector(prepareForDragOperation:),
      reinterpret_cast<IMP>(prepare_for_drag), "B@:@");
  class_replaceMethod(cls, @selector(performDragOperation:),
      reinterpret_cast<IMP>(perform_drag), "B@:@");
  [view registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
}

}  // namespace spdsx

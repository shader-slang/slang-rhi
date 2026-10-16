#pragma once

namespace rhi {

// Utility functions for Cocoa
struct CocoaUtil
{
    static void* createMetalLayer(void* nswindow);
    static void destroyMetalLayer(void* metalLayer);
    static void setDisplaySyncEnabled(void* metalLayer, bool enabled);
    static void* nextDrawable(void* metalLayer);
};

} // namespace rhi

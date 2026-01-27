#pragma once

#include <slang-rhi.h>
#include <atomic>

namespace rhi {
class RHI : public IRHI
{
private:
    DebugLayerOptions m_debugLayerOptions = {};
    std::atomic<uint32_t> m_liveDeviceCount = 0;

public:
    void incrementLiveDeviceCount();
    void decrementLiveDeviceCount();

    virtual Result setDebugLayerOptions(DebugLayerOptions options) override;
    virtual DebugLayerOptions getDebugLayerOptions() override;

    virtual const FormatInfo& getFormatInfo(Format format) override;
    virtual const char* getDeviceTypeName(DeviceType type) override;
    virtual bool isDeviceTypeSupported(DeviceType type) override;
    virtual const char* getFeatureName(Feature feature) override;
    virtual const char* getCapabilityName(Capability capability) override;

    virtual IAdapter* getAdapter(DeviceType type, uint32_t index) override;
    virtual Result getAdapters(DeviceType type, ISlangBlob** outAdaptersBlob) override;
    virtual Result createDevice(const DeviceDesc& desc, IDevice** outDevice) override;

    virtual Result createBlob(const void* data, size_t size, ISlangBlob** outBlob) override;

    virtual Result reportLiveObjects() override;
    virtual Result setTaskPool(ITaskPool* scheduler) override;

    static RHI* getInstance()
    {
        static RHI instance;
        return &instance;
    }
};
} // namespace rhi

#pragma once

#include "SinglePortModule.h"
#include "modules/XiaoLd2410Parser.h"

struct XiaoLd2410PresenceStatus {
    XiaoLd2410Sample radar;
    bool digitalPresence = false;
    bool uartLive = false;
    uint32_t updatedAtMs = 0;
};

class XiaoLd2410PresenceModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    XiaoLd2410PresenceModule();

    const XiaoLd2410PresenceStatus &getStatus() const { return current; }

  protected:
    int32_t runOnce() override;

  private:
    static constexpr uint32_t PollIntervalMs = 20;
    static constexpr uint32_t UartStaleMs = 1500;
    static constexpr uint32_t KeepaliveMs = 5000;
    static constexpr uint16_t DistanceChangeCm = 25;
    static constexpr uint8_t DebounceSamples = 3;

    bool initialized = false;
    bool rawDigitalPresence = false;
    bool debouncedDigitalPresence = false;
    uint8_t digitalStableSamples = 0;
    uint32_t lastUartFrameMs = 0;
    uint32_t lastPublishedMs = 0;
    XiaoLd2410Parser parser;
    XiaoLd2410Sample lastUartSample;
    XiaoLd2410PresenceStatus current;
    XiaoLd2410PresenceStatus lastPublished;
    bool hasPublished = false;

    void initialize();
    void readDigitalPresence();
    void readUart();
    void refreshStatus(uint32_t now);
    bool shouldPublish(uint32_t now) const;
    void publishToConnectedClient(uint32_t now);
};

extern XiaoLd2410PresenceModule *xiaoLd2410PresenceModule;

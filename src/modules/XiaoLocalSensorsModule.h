#pragma once

#include "SinglePortModule.h"

class XiaoLocalSensorsModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    XiaoLocalSensorsModule();

  protected:
    int32_t runOnce() override;

  private:
    static constexpr uint32_t ActivePollMs = 20;
    static constexpr uint32_t IdlePollMs = 250;
    static constexpr uint32_t ImuPublishMs = 40;
    static constexpr uint32_t AudioPublishMs = 100;
    static constexpr uint32_t GpsKeepaliveMs = 1000;

    bool clientActive = false;
    bool imuPowered = false;
    bool imuReady = false;
    bool microphoneReady = false;
    uint8_t imuAddress = 0;
    uint32_t lastImuPublishMs = 0;
    uint32_t lastAudioPublishMs = 0;
    uint32_t lastGpsPublishMs = 0;
    uint32_t lastGpsSequence = 0;
    uint32_t lastGpsTimestamp = 0;

    void setClientActive(bool active);
    bool initializeImu();
    void stopImu();
    bool readImu(float &ax, float &ay, float &az, float &gx, float &gy, float &gz, float &tempC);
    bool initializeMicrophone();
    void stopMicrophone();
    void publishImu(uint32_t now);
    void publishAudio(uint32_t now);
    void publishGps(uint32_t now);
    void sendLocalPayload(const char *payload, size_t length);
};

extern XiaoLocalSensorsModule *xiaoLocalSensorsModule;

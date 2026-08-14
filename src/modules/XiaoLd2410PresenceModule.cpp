#include "modules/XiaoLd2410PresenceModule.h"

#include "MeshService.h"
#include "NodeDB.h"
#include "configuration.h"
#include "main.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

XiaoLd2410PresenceModule *xiaoLd2410PresenceModule;

namespace {
constexpr uint32_t Ld2410Baud = 256000;
constexpr uint32_t NrfUarteClockHz = 16000000;

constexpr uint32_t calculateNrfUarteBaudRegister(uint32_t baud)
{
    return static_cast<uint32_t>(((((static_cast<uint64_t>(baud) << 32U) + (NrfUarteClockHz / 2U)) / NrfUarteClockHz) +
                                  0x800ULL) &
                                 0xFFFFF000ULL);
}

constexpr uint32_t Ld2410BaudRegister = calculateNrfUarteBaudRegister(Ld2410Baud);
static_assert(Ld2410BaudRegister == 0x04189000UL, "Unexpected LD2410C UARTE baud register");

uint16_t distanceDelta(uint16_t first, uint16_t second)
{
    return first > second ? first - second : second - first;
}
} // namespace

XiaoLd2410PresenceModule::XiaoLd2410PresenceModule()
    : SinglePortModule("xiao-presence", meshtastic_PortNum_DETECTION_SENSOR_APP), OSThread("XiaoLD2410")
{
}

void XiaoLd2410PresenceModule::initialize()
{
    pinMode(XIAO_LD2410C_OUT_PIN, INPUT);
    rawDigitalPresence = digitalRead(XIAO_LD2410C_OUT_PIN) == HIGH;
    debouncedDigitalPresence = rawDigitalPresence;
    digitalStableSamples = DebounceSamples;

    // The Adafruit nRF52 core otherwise rounds 256000 up to 460800. Serial2 is
    // UARTE1 on this target, so start the peripheral at its nearest supported
    // rate and then apply Nordic's documented custom-baud register formula.
    Serial2.begin(250000);
    NRF_UARTE1->BAUDRATE = Ld2410BaudRegister;
    while (Serial2.available() > 0) {
        Serial2.read();
    }

    initialized = true;
    LOG_INFO("XIAO LD2410C: local presence input ready (OUT=D0, UART RX=NFC1, TX=NFC2, 256000 baud)");
}

void XiaoLd2410PresenceModule::readDigitalPresence()
{
    const bool sample = digitalRead(XIAO_LD2410C_OUT_PIN) == HIGH;
    if (sample != rawDigitalPresence) {
        rawDigitalPresence = sample;
        digitalStableSamples = 1;
        return;
    }

    if (digitalStableSamples < DebounceSamples) {
        digitalStableSamples++;
        if (digitalStableSamples == DebounceSamples) {
            debouncedDigitalPresence = rawDigitalPresence;
        }
    }
}

void XiaoLd2410PresenceModule::readUart()
{
    while (Serial2.available() > 0) {
        XiaoLd2410Sample sample;
        if (parser.consume(static_cast<uint8_t>(Serial2.read()), sample)) {
            lastUartSample = sample;
            lastUartFrameMs = millis();
        }
    }
}

void XiaoLd2410PresenceModule::refreshStatus(uint32_t now)
{
    const bool uartLive = lastUartFrameMs != 0 && static_cast<uint32_t>(now - lastUartFrameMs) <= UartStaleMs;
    current.digitalPresence = debouncedDigitalPresence;
    current.uartLive = uartLive;
    current.radar = uartLive ? lastUartSample : XiaoLd2410Sample{};
    current.radar.valid = uartLive;
    current.radar.presence = current.radar.presence || debouncedDigitalPresence;
    current.updatedAtMs = now;
}

bool XiaoLd2410PresenceModule::shouldPublish(uint32_t now) const
{
    if (!hasPublished) {
        return true;
    }

    if (current.radar.presence != lastPublished.radar.presence || current.radar.movingTarget != lastPublished.radar.movingTarget ||
        current.radar.stationaryTarget != lastPublished.radar.stationaryTarget ||
        current.digitalPresence != lastPublished.digitalPresence || current.uartLive != lastPublished.uartLive) {
        return true;
    }

    if (current.uartLive &&
        (distanceDelta(current.radar.movingDistanceCm, lastPublished.radar.movingDistanceCm) >= DistanceChangeCm ||
         distanceDelta(current.radar.stationaryDistanceCm, lastPublished.radar.stationaryDistanceCm) >= DistanceChangeCm ||
         distanceDelta(current.radar.detectionDistanceCm, lastPublished.radar.detectionDistanceCm) >= DistanceChangeCm)) {
        return true;
    }

    return static_cast<uint32_t>(now - lastPublishedMs) >= KeepaliveMs;
}

void XiaoLd2410PresenceModule::publishToConnectedClient(uint32_t now)
{
    if (service == nullptr || service->api_state == MeshService::STATE_DISCONNECTED ||
        !service->isToPhoneQueueEmpty()) {
        return;
    }

    char payload[192];
    const int length = snprintf(payload, sizeof(payload),
                                "TSV_RADAR_V1,id=ld2410c,t=%lu,p=%u,m=%u,s=%u,md=%u,me=%u,sd=%u,se=%u,dd=%u,out=%u,uart=%u",
                                static_cast<unsigned long>(current.updatedAtMs), current.radar.presence ? 1U : 0U,
                                current.radar.movingTarget ? 1U : 0U,
                                current.radar.stationaryTarget ? 1U : 0U, current.radar.movingDistanceCm,
                                current.radar.movingEnergy, current.radar.stationaryDistanceCm,
                                current.radar.stationaryEnergy, current.radar.detectionDistanceCm,
                                current.digitalPresence ? 1U : 0U, current.uartLive ? 1U : 0U);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(payload)) {
        LOG_ERROR("XIAO LD2410C: presence payload overflow");
        return;
    }

    meshtastic_MeshPacket *packet = allocDataPacket();
    packet->from = nodeDB->getNodeNum();
    packet->to = nodeDB->getNodeNum();
    packet->want_ack = false;
    packet->decoded.payload.size = static_cast<pb_size_t>(length);
    memcpy(packet->decoded.payload.bytes, payload, packet->decoded.payload.size);

    // Local PhoneAPI delivery only. Presence data must never be emitted over
    // LoRa by this module. Only mark the report delivered after reserving an
    // empty PhoneAPI queue; Meshtastic drops non-text packets when that queue
    // is full.
    service->sendToPhone(packet);
    lastPublished = current;
    lastPublishedMs = now;
    hasPublished = true;
}

int32_t XiaoLd2410PresenceModule::runOnce()
{
    if (!initialized) {
        initialize();
    }

    readDigitalPresence();
    readUart();
    const uint32_t now = millis();
    refreshStatus(now);
    if (shouldPublish(now)) {
        publishToConnectedClient(now);
    }
    return PollIntervalMs;
}

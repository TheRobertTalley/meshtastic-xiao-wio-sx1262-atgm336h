#include "modules/XiaoLocalSensorsModule.h"

#include "MeshService.h"
#include "NodeDB.h"
#include "main.h"

#include <Arduino.h>
#include <PDM.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

XiaoLocalSensorsModule *xiaoLocalSensorsModule;

namespace {
constexpr uint8_t ImuPowerPin = 15;
constexpr uint8_t ImuClockPin = 16;
constexpr uint8_t ImuDataPin = 17;
constexpr uint8_t MicrophonePowerPin = 19;
constexpr uint8_t MicrophoneClockPin = 20;
constexpr uint8_t MicrophoneDataPin = 21;
constexpr uint8_t LsmWhoAmI = 0x0f;
constexpr uint8_t LsmWhoAmIValue = 0x6a;
constexpr uint8_t LsmCtrl1Xl = 0x10;
constexpr uint8_t LsmCtrl2G = 0x11;
constexpr uint8_t LsmCtrl3C = 0x12;
constexpr uint8_t LsmOutTempL = 0x20;
constexpr uint32_t I2cHalfPeriodUs = 4;
constexpr size_t PdmReadSamples = 256;

volatile uint64_t audioSquareSum = 0;
volatile uint32_t audioSampleCount = 0;
volatile uint16_t audioPeak = 0;
int16_t pdmSamples[PdmReadSamples];

void driveLow(uint8_t pin)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}

void releaseLine(uint8_t pin)
{
    pinMode(pin, INPUT_PULLUP);
}

void i2cDelay()
{
    delayMicroseconds(I2cHalfPeriodUs);
}

void i2cStart()
{
    releaseLine(ImuDataPin);
    releaseLine(ImuClockPin);
    i2cDelay();
    driveLow(ImuDataPin);
    i2cDelay();
    driveLow(ImuClockPin);
}

void i2cStop()
{
    driveLow(ImuDataPin);
    i2cDelay();
    releaseLine(ImuClockPin);
    i2cDelay();
    releaseLine(ImuDataPin);
    i2cDelay();
}

bool i2cWriteByte(uint8_t value)
{
    for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
        if ((value & mask) != 0) {
            releaseLine(ImuDataPin);
        } else {
            driveLow(ImuDataPin);
        }
        i2cDelay();
        releaseLine(ImuClockPin);
        i2cDelay();
        driveLow(ImuClockPin);
    }
    releaseLine(ImuDataPin);
    i2cDelay();
    releaseLine(ImuClockPin);
    i2cDelay();
    const bool acknowledged = digitalRead(ImuDataPin) == LOW;
    driveLow(ImuClockPin);
    return acknowledged;
}

uint8_t i2cReadByte(bool acknowledge)
{
    uint8_t value = 0;
    releaseLine(ImuDataPin);
    for (uint8_t bit = 0; bit < 8; bit++) {
        value <<= 1;
        releaseLine(ImuClockPin);
        i2cDelay();
        if (digitalRead(ImuDataPin) == HIGH) {
            value |= 1;
        }
        driveLow(ImuClockPin);
        i2cDelay();
    }
    if (acknowledge) {
        driveLow(ImuDataPin);
    } else {
        releaseLine(ImuDataPin);
    }
    releaseLine(ImuClockPin);
    i2cDelay();
    driveLow(ImuClockPin);
    releaseLine(ImuDataPin);
    return value;
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value)
{
    i2cStart();
    const bool ok = i2cWriteByte(static_cast<uint8_t>(address << 1)) && i2cWriteByte(reg) && i2cWriteByte(value);
    i2cStop();
    return ok;
}

bool readRegisters(uint8_t address, uint8_t reg, uint8_t *destination, size_t length)
{
    i2cStart();
    if (!i2cWriteByte(static_cast<uint8_t>(address << 1)) || !i2cWriteByte(reg)) {
        i2cStop();
        return false;
    }
    i2cStart();
    if (!i2cWriteByte(static_cast<uint8_t>((address << 1) | 1))) {
        i2cStop();
        return false;
    }
    for (size_t index = 0; index < length; index++) {
        destination[index] = i2cReadByte(index + 1 < length);
    }
    i2cStop();
    return true;
}

int16_t signed16(const uint8_t *bytes)
{
    return static_cast<int16_t>(static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8));
}

void onPdmReceive()
{
    int available = PDM.available();
    while (available > 0) {
        const size_t bytesRequested = min(static_cast<size_t>(available), sizeof(pdmSamples));
        const int bytesRead = PDM.read(pdmSamples, bytesRequested);
        if (bytesRead <= 0) {
            return;
        }
        const size_t samplesRead = static_cast<size_t>(bytesRead) / sizeof(int16_t);
        for (size_t index = 0; index < samplesRead; index++) {
            const int32_t sample = pdmSamples[index];
            const uint16_t magnitude = static_cast<uint16_t>(sample < 0 ? -sample : sample);
            audioSquareSum += static_cast<uint64_t>(sample * sample);
            audioSampleCount++;
            if (magnitude > audioPeak) {
                audioPeak = magnitude;
            }
        }
        available = PDM.available();
    }
}
} // namespace

XiaoLocalSensorsModule::XiaoLocalSensorsModule()
    : SinglePortModule("xiao-local-sensors", meshtastic_PortNum_DETECTION_SENSOR_APP), OSThread("XiaoSensors")
{
}

void XiaoLocalSensorsModule::setClientActive(bool active)
{
    if (clientActive == active) {
        return;
    }
    clientActive = active;
    if (active) {
        initializeImu();
        initializeMicrophone();
        return;
    }
    stopMicrophone();
    stopImu();
}

bool XiaoLocalSensorsModule::initializeImu()
{
    if (imuReady) {
        return true;
    }
    pinMode(ImuPowerPin, OUTPUT);
#if defined(NRF52840_XXAA)
    // Seeed's reference driver requires high-drive mode on P1.08 for the
    // onboard IMU power switch. Standard GPIO drive leaves some boards with
    // an unpowered LSM6DS3TR-C even though the pin reads logically high.
    NRF_P1->PIN_CNF[8] =
        (NRF_GPIO_PIN_DIR_OUTPUT << GPIO_PIN_CNF_DIR_Pos) |
        (NRF_GPIO_PIN_INPUT_DISCONNECT << GPIO_PIN_CNF_INPUT_Pos) |
        (NRF_GPIO_PIN_NOPULL << GPIO_PIN_CNF_PULL_Pos) |
        (NRF_GPIO_PIN_H0H1 << GPIO_PIN_CNF_DRIVE_Pos) |
        (NRF_GPIO_PIN_NOSENSE << GPIO_PIN_CNF_SENSE_Pos);
#endif
    digitalWrite(ImuPowerPin, HIGH);
    imuPowered = true;
    releaseLine(ImuClockPin);
    releaseLine(ImuDataPin);
    delay(12);

    uint8_t identity = 0;
    for (const uint8_t address : {static_cast<uint8_t>(0x6a), static_cast<uint8_t>(0x6b)}) {
        if (readRegisters(address, LsmWhoAmI, &identity, 1) && identity == LsmWhoAmIValue) {
            imuAddress = address;
            break;
        }
    }
    if (imuAddress == 0 || !writeRegister(imuAddress, LsmCtrl3C, 0x44) || !writeRegister(imuAddress, LsmCtrl1Xl, 0x40) ||
        !writeRegister(imuAddress, LsmCtrl2G, 0x40)) {
        LOG_WARN("XIAO sensors: LSM6DS3TR-C unavailable on software I2C");
        stopImu();
        return false;
    }
    imuReady = true;
    LOG_INFO("XIAO sensors: LSM6DS3TR-C ready at 0x%02x (software I2C)", imuAddress);
    return true;
}

void XiaoLocalSensorsModule::stopImu()
{
    imuReady = false;
    imuAddress = 0;
    releaseLine(ImuClockPin);
    releaseLine(ImuDataPin);
    if (imuPowered) {
        digitalWrite(ImuPowerPin, LOW);
        pinMode(ImuPowerPin, INPUT);
        imuPowered = false;
    }
}

bool XiaoLocalSensorsModule::readImu(float &ax, float &ay, float &az, float &gx, float &gy, float &gz, float &tempC)
{
    uint8_t raw[14];
    if (!imuReady || !readRegisters(imuAddress, LsmOutTempL, raw, sizeof(raw))) {
        return false;
    }
    tempC = 25.0f + static_cast<float>(signed16(raw)) / 256.0f;
    gx = static_cast<float>(signed16(raw + 2)) * 0.00875f;
    gy = static_cast<float>(signed16(raw + 4)) * 0.00875f;
    gz = static_cast<float>(signed16(raw + 6)) * 0.00875f;
    ax = static_cast<float>(signed16(raw + 8)) * 0.000061f;
    ay = static_cast<float>(signed16(raw + 10)) * 0.000061f;
    az = static_cast<float>(signed16(raw + 12)) * 0.000061f;
    return true;
}

bool XiaoLocalSensorsModule::initializeMicrophone()
{
    if (microphoneReady) {
        return true;
    }
    audioSquareSum = 0;
    audioSampleCount = 0;
    audioPeak = 0;
    PDM.setPins(MicrophoneDataPin, MicrophoneClockPin, MicrophonePowerPin);
    PDM.setBufferSize(sizeof(pdmSamples));
    PDM.onReceive(onPdmReceive);
    microphoneReady = PDM.begin(1, 16000) == 1;
    if (microphoneReady) {
        LOG_INFO("XIAO sensors: PDM microphone ready at 16 kHz");
    } else {
        LOG_WARN("XIAO sensors: PDM microphone failed to start");
    }
    return microphoneReady;
}

void XiaoLocalSensorsModule::stopMicrophone()
{
    if (!microphoneReady) {
        return;
    }
    PDM.end();
    microphoneReady = false;
    audioSquareSum = 0;
    audioSampleCount = 0;
    audioPeak = 0;
}

bool XiaoLocalSensorsModule::sendLocalPayload(const char *payload, size_t length)
{
    if (service == nullptr || service->api_state == MeshService::STATE_DISCONNECTED || payload == nullptr || length == 0 ||
        length > meshtastic_Constants_DATA_PAYLOAD_LEN || !service->isToPhoneQueueEmpty()) {
        return false;
    }
    meshtastic_MeshPacket *packet = allocDataPacket();
    packet->from = nodeDB->getNodeNum();
    packet->to = nodeDB->getNodeNum();
    packet->want_ack = false;
    packet->decoded.payload.size = static_cast<pb_size_t>(length);
    memcpy(packet->decoded.payload.bytes, payload, length);
    service->sendToPhone(packet);
    return true;
}

void XiaoLocalSensorsModule::publishImu(uint32_t now)
{
    if (static_cast<uint32_t>(now - lastImuPublishMs) < ImuPublishMs) {
        return;
    }
    float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0, tempC = 0;
    if (!readImu(ax, ay, az, gx, gy, gz, tempC)) {
        imuReady = false;
        initializeImu();
        return;
    }
    char payload[192];
    const int length = snprintf(payload, sizeof(payload),
                                "TSV_IMU_V1,t=%lu,ax=%.5f,ay=%.5f,az=%.5f,gx=%.3f,gy=%.3f,gz=%.3f,tc=%.2f,hz=25",
                                static_cast<unsigned long>(now), ax, ay, az, gx, gy, gz, tempC);
    if (length > 0 && static_cast<size_t>(length) < sizeof(payload) &&
        sendLocalPayload(payload, static_cast<size_t>(length))) {
        lastImuPublishMs = now;
    }
}

void XiaoLocalSensorsModule::publishAudio(uint32_t now)
{
    if (!microphoneReady || static_cast<uint32_t>(now - lastAudioPublishMs) < AudioPublishMs) {
        return;
    }
    noInterrupts();
    const uint64_t squareSum = audioSquareSum;
    const uint32_t sampleCount = audioSampleCount;
    const uint16_t peak = audioPeak;
    audioSquareSum = 0;
    audioSampleCount = 0;
    audioPeak = 0;
    interrupts();
    if (sampleCount == 0) {
        return;
    }
    const float rms = sqrtf(static_cast<float>(squareSum) / static_cast<float>(sampleCount)) / 32768.0f;
    const float normalizedPeak = static_cast<float>(peak) / 32768.0f;
    const bool active = rms >= 0.02f || normalizedPeak >= 0.08f;
    char payload[128];
    const int length = snprintf(payload, sizeof(payload), "TSV_AUDIO_V1,t=%lu,rms=%.5f,peak=%.5f,active=%u,hz=10",
                                static_cast<unsigned long>(now), rms, normalizedPeak, active ? 1U : 0U);
    if (length > 0 && static_cast<size_t>(length) < sizeof(payload) &&
        sendLocalPayload(payload, static_cast<size_t>(length))) {
        lastAudioPublishMs = now;
    }
}

void XiaoLocalSensorsModule::publishGps(uint32_t now)
{
    if (!nodeDB->hasLocalPositionSinceBoot() || !localPosition.has_latitude_i || !localPosition.has_longitude_i ||
        (localPosition.latitude_i == 0 && localPosition.longitude_i == 0)) {
        return;
    }
    const bool changed = localPosition.seq_number != lastGpsSequence || localPosition.timestamp != lastGpsTimestamp;
    if (!changed && static_cast<uint32_t>(now - lastGpsPublishMs) < GpsKeepaliveMs) {
        return;
    }
    float accuracyMeters = 0.0f;
    if (localPosition.HDOP > 0) {
        const float baseAccuracyMeters = localPosition.gps_accuracy > 0 ? localPosition.gps_accuracy / 1000.0f : 3.0f;
        accuracyMeters = baseAccuracyMeters * (localPosition.HDOP / 100.0f);
    }
    char payload[208];
    const int length = snprintf(
        payload, sizeof(payload),
        "TSV_GPS_V1,t=%lu,lat=%.7f,lon=%.7f,alt=%ld,ha=%u,spd=%.2f,hs=%u,trk=%.2f,ht=%u,acc=%.2f,hacc=%u,sat=%lu,fix=%lu",
        static_cast<unsigned long>(localPosition.timestamp), localPosition.latitude_i * 1e-7, localPosition.longitude_i * 1e-7,
        static_cast<long>(localPosition.altitude), localPosition.has_altitude ? 1U : 0U,
        static_cast<double>(localPosition.ground_speed), localPosition.has_ground_speed ? 1U : 0U,
        localPosition.ground_track / 100.0, localPosition.has_ground_track ? 1U : 0U, accuracyMeters,
        accuracyMeters > 0.0f ? 1U : 0U, static_cast<unsigned long>(localPosition.sats_in_view),
        static_cast<unsigned long>(localPosition.fix_quality));
    if (length > 0 && static_cast<size_t>(length) < sizeof(payload) &&
        sendLocalPayload(payload, static_cast<size_t>(length))) {
        lastGpsPublishMs = now;
        lastGpsSequence = localPosition.seq_number;
        lastGpsTimestamp = localPosition.timestamp;
    }
}

int32_t XiaoLocalSensorsModule::runOnce()
{
    const bool connected = service != nullptr && service->api_state != MeshService::STATE_DISCONNECTED;
    setClientActive(connected);
    if (!connected) {
        return IdlePollMs;
    }
    const uint32_t now = millis();
    // Preserve the low-rate navigation and alert channels first. The queue
    // gate above lets IMU/audio use all remaining PhoneAPI bandwidth without
    // building a stale sample backlog.
    publishGps(now);
    publishAudio(now);
    publishImu(now);
    return ActivePollMs;
}

#include "modules/XiaoLd2410Parser.h"

namespace {
constexpr uint8_t ReportHeader[] = {0xF4, 0xF3, 0xF2, 0xF1};
constexpr uint8_t ReportFooter[] = {0xF8, 0xF7, 0xF6, 0xF5};

uint16_t readLittleEndian16(const uint8_t *bytes)
{
    return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8U);
}
} // namespace

void XiaoLd2410Parser::reset()
{
    frameSize = 0;
    expectedFrameSize = 0;
    headerMatched = 0;
}

void XiaoLd2410Parser::consumeHeader(uint8_t byte)
{
    if (byte == ReportHeader[headerMatched]) {
        headerMatched++;
        if (headerMatched == HeaderSize) {
            for (size_t index = 0; index < HeaderSize; index++) {
                frame[index] = ReportHeader[index];
            }
            frameSize = HeaderSize;
            headerMatched = 0;
        }
        return;
    }

    headerMatched = byte == ReportHeader[0] ? 1U : 0U;
}

bool XiaoLd2410Parser::consume(uint8_t byte, XiaoLd2410Sample &sample)
{
    if (frameSize == 0) {
        consumeHeader(byte);
        return false;
    }

    if (frameSize >= MaximumFrameSize) {
        reset();
        consumeHeader(byte);
        return false;
    }

    frame[frameSize++] = byte;
    if (frameSize == HeaderSize + LengthSize) {
        const size_t payloadSize = readLittleEndian16(frame + HeaderSize);
        if (payloadSize < MinimumPayloadSize || payloadSize > MaximumPayloadSize) {
            reset();
            consumeHeader(byte);
            return false;
        }
        expectedFrameSize = HeaderSize + LengthSize + payloadSize + FooterSize;
    }

    if (expectedFrameSize == 0 || frameSize < expectedFrameSize) {
        return false;
    }

    const bool parsed = parseFrame(sample);
    reset();
    return parsed;
}

bool XiaoLd2410Parser::parseFrame(XiaoLd2410Sample &sample) const
{
    if (expectedFrameSize != frameSize) {
        return false;
    }

    const size_t footerOffset = frameSize - FooterSize;
    for (size_t index = 0; index < FooterSize; index++) {
        if (frame[footerOffset + index] != ReportFooter[index]) {
            return false;
        }
    }

    const size_t payloadSize = readLittleEndian16(frame + HeaderSize);
    const uint8_t *payload = frame + HeaderSize + LengthSize;
    if ((payload[0] != 0x01 && payload[0] != 0x02) || payload[1] != 0xAA || payload[payloadSize - 2] != 0x55 ||
        payload[payloadSize - 1] != 0x00) {
        return false;
    }

    XiaoLd2410Sample parsed;
    parsed.valid = true;
    parsed.targetState = payload[2] & 0x03U;
    parsed.presence = parsed.targetState != 0;
    parsed.movingTarget = (parsed.targetState & 0x01U) != 0;
    parsed.stationaryTarget = (parsed.targetState & 0x02U) != 0;
    parsed.movingDistanceCm = readLittleEndian16(payload + 3);
    parsed.movingEnergy = payload[5];
    parsed.stationaryDistanceCm = readLittleEndian16(payload + 6);
    parsed.stationaryEnergy = payload[8];
    parsed.detectionDistanceCm = readLittleEndian16(payload + 9);
    sample = parsed;
    return true;
}

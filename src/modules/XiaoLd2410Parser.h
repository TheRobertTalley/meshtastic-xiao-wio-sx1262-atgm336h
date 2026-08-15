#pragma once

#include <stddef.h>
#include <stdint.h>

struct XiaoLd2410Sample {
    bool valid = false;
    bool presence = false;
    bool movingTarget = false;
    bool stationaryTarget = false;
    uint8_t targetState = 0;
    uint16_t movingDistanceCm = 0;
    uint8_t movingEnergy = 0;
    uint16_t stationaryDistanceCm = 0;
    uint8_t stationaryEnergy = 0;
    uint16_t detectionDistanceCm = 0;
};

// Streaming parser for LD2410/LD2410C normal and engineering report frames.
// It deliberately ignores command/ack frames and never allocates memory.
class XiaoLd2410Parser
{
  public:
    bool consume(uint8_t byte, XiaoLd2410Sample &sample);
    void reset();

  private:
    static constexpr size_t HeaderSize = 4;
    static constexpr size_t LengthSize = 2;
    static constexpr size_t FooterSize = 4;
    static constexpr size_t MinimumPayloadSize = 13;
    static constexpr size_t MaximumPayloadSize = 96;
    static constexpr size_t MaximumFrameSize = HeaderSize + LengthSize + MaximumPayloadSize + FooterSize;

    uint8_t frame[MaximumFrameSize] = {};
    size_t frameSize = 0;
    size_t expectedFrameSize = 0;
    size_t headerMatched = 0;

    bool parseFrame(XiaoLd2410Sample &sample) const;
    void consumeHeader(uint8_t byte);
};

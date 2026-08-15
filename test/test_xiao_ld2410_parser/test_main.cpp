#include <unity.h>

#include <string.h>

#include "modules/XiaoLd2410Parser.h"

namespace {
const uint8_t ValidFrame[] = {0xF4, 0xF3, 0xF2, 0xF1, 0x0D, 0x00, 0x02, 0xAA, 0x03, 0x51, 0x00, 0x64,
                              0x2C, 0x01, 0x50, 0x3B, 0x00, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};

bool consumeFrame(XiaoLd2410Parser &parser, const uint8_t *frame, size_t size, XiaoLd2410Sample &sample)
{
    bool parsed = false;
    for (size_t index = 0; index < size; index++) {
        parsed = parser.consume(frame[index], sample) || parsed;
    }
    return parsed;
}
} // namespace

void test_decodes_normal_target_frame()
{
    XiaoLd2410Parser parser;
    XiaoLd2410Sample sample;
    TEST_ASSERT_TRUE(consumeFrame(parser, ValidFrame, sizeof(ValidFrame), sample));
    TEST_ASSERT_TRUE(sample.valid);
    TEST_ASSERT_TRUE(sample.presence);
    TEST_ASSERT_TRUE(sample.movingTarget);
    TEST_ASSERT_TRUE(sample.stationaryTarget);
    TEST_ASSERT_EQUAL_UINT8(3, sample.targetState);
    TEST_ASSERT_EQUAL_UINT16(81, sample.movingDistanceCm);
    TEST_ASSERT_EQUAL_UINT8(100, sample.movingEnergy);
    TEST_ASSERT_EQUAL_UINT16(300, sample.stationaryDistanceCm);
    TEST_ASSERT_EQUAL_UINT8(80, sample.stationaryEnergy);
    TEST_ASSERT_EQUAL_UINT16(59, sample.detectionDistanceCm);
}

void test_rejects_bad_tail_and_resynchronizes()
{
    XiaoLd2410Parser parser;
    XiaoLd2410Sample sample;
    uint8_t badFrame[sizeof(ValidFrame)];
    memcpy(badFrame, ValidFrame, sizeof(ValidFrame));
    badFrame[sizeof(badFrame) - 1] = 0;
    TEST_ASSERT_FALSE(consumeFrame(parser, badFrame, sizeof(badFrame), sample));

    const uint8_t noise[] = {0x00, 0xF4, 0x00, 0xF4, 0xF3, 0xF2, 0xF1};
    for (uint8_t byte : noise) {
        parser.consume(byte, sample);
    }
    for (size_t index = 4; index < sizeof(ValidFrame); index++) {
        parser.consume(ValidFrame[index], sample);
    }
    TEST_ASSERT_TRUE(sample.valid);
    TEST_ASSERT_EQUAL_UINT16(59, sample.detectionDistanceCm);
}

extern "C" void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_decodes_normal_target_frame);
    RUN_TEST(test_rejects_bad_tail_and_resynchronizes);
    UNITY_END();
}

extern "C" void loop() {}

#if defined(TSV_PARSER_ONLY_NATIVE)
int main()
{
    setup();
    return Unity.TestFailures == 0 ? 0 : 1;
}
#endif

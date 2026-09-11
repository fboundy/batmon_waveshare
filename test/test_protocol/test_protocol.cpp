// Host unit tests for the BatMon protocol codec.  Run with: pio test -e native
#include <unity.h>
#include <string.h>

#include "batmon/batmon_protocol.h"

using namespace batmon;

void setUp() {}
void tearDown() {}

// Request bytes must match what the HA integration writes:
//   pushI08(sensor_type); pushI08(mode); pushI08(0)
static void test_encode_sensor_request() {
    uint8_t b[SENSOR_REQ_LEN];
    encodeSensorRequest(SensorType::BatCurrent, Mode::Value, b);
    TEST_ASSERT_EQUAL_UINT8(4, b[0]);
    TEST_ASSERT_EQUAL_UINT8(0, b[1]);
    TEST_ASSERT_EQUAL_UINT8(0, b[2]);

    encodeSensorRequest(SensorType::BatAmpHours, Mode::Max, b);
    TEST_ASSERT_EQUAL_UINT8(5, b[0]);
    TEST_ASSERT_EQUAL_UINT8(2, b[1]);
}

// 12.8 V as little-endian IEEE-754: 0x414CCCCD -> CD CC 4C 41
static void test_decode_value_reply() {
    const uint8_t rx[] = {0x00, 0x00, 0x04, 0xCD, 0xCC, 0x4C, 0x41};
    SensorReply r;
    TEST_ASSERT_TRUE(decodeSensorReply(rx, sizeof rx, r));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)r.type);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)r.mode);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.8f, r.value);
}

// Negative current (discharging): -5.25 A = 0xC0A80000 -> 00 00 A8 C0
static void test_decode_negative_current() {
    const uint8_t rx[] = {0x04, 0x00, 0x04, 0x00, 0x00, 0xA8, 0xC0};
    SensorReply r;
    TEST_ASSERT_TRUE(decodeSensorReply(rx, sizeof rx, r));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -5.25f, r.value);
}

// MAX reply carries the value and a u32 epoch.
static void test_decode_max_reply_with_epoch() {
    // 100.0f = 0x42C80000 -> 00 00 C8 42 ; epoch 1700000000 = 0x6553F100 -> 00 F1 53 65
    const uint8_t rx[] = {0x05, 0x02, 0x08, 0x00, 0x00, 0xC8, 0x42, 0x00, 0xF1, 0x53, 0x65};
    SensorReply r;
    TEST_ASSERT_TRUE(decodeSensorReply(rx, sizeof rx, r));
    TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)r.mode);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, r.value);
    TEST_ASSERT_EQUAL_UINT32(1700000000u, r.epoch);
}

static void test_decode_short_reply_rejected() {
    const uint8_t rx[] = {0x00, 0x00, 0x04, 0xCD, 0xCC};
    SensorReply r;
    TEST_ASSERT_FALSE(decodeSensorReply(rx, sizeof rx, r));
    TEST_ASSERT_FALSE(r.valid);
    TEST_ASSERT_FALSE(decodeSensorReply(nullptr, 0, r));
}

// Must match HA _set_batmon_switch():
//   pushI16(606); pushI08(1); pushI32(io_type); pushI08(1); pushI32(value); pushI08(0)
static void test_encode_set_io_relay_on() {
    uint8_t b[API_SET_IO_LEN];
    encodeSetIo(IoType::Relay, true, b);
    const uint8_t expect[] = {0x5E, 0x02, 0x01, 0x02, 0x00, 0x00, 0x00,
                              0x01, 0x01, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_MEMORY(expect, b, sizeof expect);
}

static void test_encode_set_io_switch_off() {
    uint8_t b[API_SET_IO_LEN];
    encodeSetIo(IoType::Switch, false, b);
    const uint8_t expect[] = {0x5E, 0x02, 0x01, 0x03, 0x00, 0x00, 0x00,
                              0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_MEMORY(expect, b, sizeof expect);
}

static void test_state_of_charge() {
    // Full: amp_hours == max
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, stateOfCharge(50.0f, 50.0f, 100.0f));
    // 30 Ah below the full reference in a 100 Ah bank -> 70 %
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 70.0f, stateOfCharge(20.0f, 50.0f, 100.0f));
    // Clamped
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, stateOfCharge(-200.0f, 50.0f, 100.0f));
    // No max reference yet (max <= 0): HA uses 0 as the reference
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, stateOfCharge(-10.0f, 0.0f, 100.0f));
    // Unconfigured capacity
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.0f, stateOfCharge(0.0f, 0.0f, 0.0f));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_encode_sensor_request);
    RUN_TEST(test_decode_value_reply);
    RUN_TEST(test_decode_negative_current);
    RUN_TEST(test_decode_max_reply_with_epoch);
    RUN_TEST(test_decode_short_reply_rejected);
    RUN_TEST(test_encode_set_io_relay_on);
    RUN_TEST(test_encode_set_io_switch_off);
    RUN_TEST(test_state_of_charge);
    return UNITY_END();
}

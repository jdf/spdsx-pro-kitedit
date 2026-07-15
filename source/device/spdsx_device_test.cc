#include "device/spdsx_device.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fake_port_backend.h"
#include "fake_serial_port.h"

namespace spdsx::device {
namespace {

using spdsx_testing::FakePortBackend;
using spdsx_testing::FakeSerialPort;

// What the device sends back to a ping.
Bytes PingReply() {
  return {0xF0, 0x41, 0x6A, 0x02, 0x16, 0x01, 0xF7};
}

// A commit-poll reply: f0 41 6a 02 .. 22 40 00 00 00 04 <status LE32> f7.
// Byte 8 is the sub-command echoed back, byte 14 the status word's low byte.
Bytes CommitPoll(uint8_t status) {
  Bytes r(19, 0x00);
  r[0] = 0xF0;
  r[1] = 0x41;
  r[2] = 0x6A;
  r[3] = 0x02;
  r[8] = 0x22;
  r[14] = status;
  r[18] = 0xF7;
  return r;
}

class SpdsxDeviceTest : public ::testing::Test {
protected:
  FakeSerialPort port;
  SpdsxDevice dev {&port};
};

// ---- The transport frame ----

TEST(Wrap, PutsThePayloadInTheTransportFrame) {
  const Bytes payload = {0xF0, 0x41, 0x10, 0x12};
  const Bytes frame = Wrap(payload);

  ASSERT_EQ(frame.size(), 20u + payload.size());
  EXPECT_EQ(Bytes(frame.begin(), frame.begin() + 3), Bytes({0x0d, 0x60, 0xe0}));
  EXPECT_EQ(frame[3], 0x07);  // channel, from the message family
  EXPECT_EQ(Bytes(frame.begin() + 12, frame.begin() + 16),
            Bytes({0x01, 0x00, 0x00, 0x00}));
  // The length is little-endian, unlike the 7-bit addresses in the payload.
  EXPECT_EQ(Bytes(frame.begin() + 16, frame.begin() + 20),
            Bytes({0x04, 0x00, 0x00, 0x00}));
  EXPECT_EQ(Bytes(frame.begin() + 20, frame.end()), payload);
}

// The channel is not a choice the caller makes: it follows from what is
// being sent.
TEST(Wrap, PicksTheChannelFromTheMessageFamily) {
  const auto channel = [](Bytes family) {
    family.resize(4, 0x00);
    return Wrap(family)[3];
  };
  EXPECT_EQ(channel({0xF0, 0x41, 0x10}), 0x07);  // DT1 parameter writes
  EXPECT_EQ(channel({0xF0, 0x41, 0x6A}), 0x09);  // control / status
  EXPECT_EQ(channel({0xF0, 0x41, 0x6C}), 0x08);  // bulk block transfer
  EXPECT_EQ(channel({0xF0, 0x41, 0x7A}), 0x06);  // remote file transfer

  // Anything unrecognised falls back to the parameter channel.
  EXPECT_EQ(channel({0xF0, 0x41, 0x99}), 0x07);
  EXPECT_EQ(Wrap({}).at(3), 0x07);
  EXPECT_EQ(Wrap({0xF0, 0x99, 0x10}).at(3), 0x07);
}

TEST(Unwrap, RoundTripsWhatWrapBuilt) {
  const Bytes payloads[] = {{0xF0, 0x41, 0x10, 0x12, 0xF7},
                            {0xF0, 0x41, 0x6C, 0x02},
                            Bytes(1000, 0xab)};
  for (const Bytes& payload : payloads) {
    EXPECT_EQ(Unwrap(Wrap(payload)), payload);
  }
}

TEST(Unwrap, RejectsAFrameTooShortToHoldAHeader) {
  EXPECT_TRUE(Unwrap({}).empty());
  EXPECT_TRUE(Unwrap(Bytes(19, 0x00)).empty());
}

// A frame claiming more than it carries yields what is actually there,
// rather than reading past the end of it.
TEST(Unwrap, TakesOnlyWhatTheFrameActuallyCarries) {
  Bytes frame = Wrap({0x01, 0x02, 0x03});
  frame[16] = 0xff;  // the length now claims 255 bytes

  EXPECT_EQ(Unwrap(frame), Bytes({0x01, 0x02, 0x03}));
}

TEST(Unwrap, ReadsAnEmptyPayloadAsEmpty) {
  EXPECT_TRUE(Unwrap(Wrap({})).empty());
}

// ---- Command / Send / ReadFrame ----

TEST_F(SpdsxDeviceTest, CommandFramesThePayloadAndReturnsTheReply) {
  const Bytes reply = {0xF0, 0x41, 0x6A, 0x02, 0xF7};
  port.QueueReply(reply);

  EXPECT_EQ(dev.Command({0xF0, 0x41, 0x10, 0x12}), reply);

  ASSERT_EQ(port.writes().size(), 1u);
  EXPECT_EQ(port.writes()[0], Wrap({0xF0, 0x41, 0x10, 0x12}));
  EXPECT_EQ(port.unread(), 0u);  // the whole reply was consumed
}

// A silent device is a timeout, not a hang or a throw.
TEST_F(SpdsxDeviceTest, CommandIsEmptyWhenNothingAnswers) {
  EXPECT_TRUE(dev.Command({0xF0, 0x41, 0x10, 0x12}).empty());
  EXPECT_EQ(port.writes().size(), 1u);  // it still went out
}

// Parameter writes are not acked, so nothing is waited for.
TEST_F(SpdsxDeviceTest, SendWritesAndDoesNotRead) {
  port.QueueReply({0x01});
  dev.Send({0xF0, 0x41, 0x10, 0x12});

  EXPECT_EQ(port.writes().size(), 1u);
  EXPECT_GT(port.unread(), 0u);  // the queued reply was left alone
}

TEST_F(SpdsxDeviceTest, ReadFrameNeedsAWholeHeader) {
  port.QueueRaw(Bytes(10, 0x00));
  EXPECT_TRUE(dev.ReadFrame().empty());
}

// A length of zero or an implausibly large one is a malformed frame, not a
// body to go read.
TEST_F(SpdsxDeviceTest, ReadFrameRejectsAnImpossibleLength) {
  port.QueueRaw(Wrap({}));  // length 0
  EXPECT_TRUE(dev.ReadFrame().empty());

  Bytes huge = Wrap({0x01});
  huge[16] = 0x01;
  huge[17] = 0x00;
  huge[18] = 0x01;  // 0x10001 = 65537, past the 4096 cap
  port.QueueRaw(huge);
  EXPECT_TRUE(dev.ReadFrame().empty());
}

// ---- Ping ----

TEST_F(SpdsxDeviceTest, PingSendsTheCapturedRequestAndReturnsTheReply) {
  const Bytes reply = {0xF0, 0x41, 0x6A, 0x02, 0x16, 0x01, 0xF7};
  port.QueueReply(reply);

  EXPECT_EQ(dev.Ping(), reply);

  Bytes expected = {0xF0, 0x41, 0x6A, 0x03, 0x16};
  expected.insert(expected.end(), 11, 0x00);
  expected.push_back(0xF7);
  ASSERT_EQ(port.payloads().size(), 1u);
  EXPECT_EQ(port.payloads()[0], expected);
  // The control family goes on channel 0x09.
  EXPECT_EQ(port.writes()[0][3], 0x09);
}

TEST_F(SpdsxDeviceTest, PingIsEmptyWhenTheDeviceDoesNotAnswer) {
  EXPECT_TRUE(dev.Ping().empty());
}

// ---- FirmwareField ----

TEST_F(SpdsxDeviceTest, FirmwareFieldReadsTheAsciiOutOfTheReply) {
  Bytes reply(14, 0x00);
  reply[0] = 0xF0;
  reply[1] = 0x41;
  reply[2] = 0x6A;
  reply[3] = 0x02;
  reply[8] = 0x17;
  reply[13] = 4;  // length
  for (const char c : std::string("2.00")) {
    reply.push_back(static_cast<uint8_t>(c));
  }
  reply.push_back(0xF7);
  port.QueueReply(reply);

  EXPECT_EQ(dev.FirmwareField(0), "2.00");
}

TEST_F(SpdsxDeviceTest, FirmwareFieldIsEmptyWithoutAnAnswerItUnderstands) {
  EXPECT_EQ(dev.FirmwareField(0), "");

  port.QueueReply(Bytes(20, 0x00));  // right size, wrong category byte
  EXPECT_EQ(dev.FirmwareField(0), "");
}

// ---- Select ----

TEST_F(SpdsxDeviceTest, SelectKitWritesTheKitSelectParameter) {
  port.QueueReply({0x01});
  dev.SelectKit(129);

  ASSERT_EQ(port.payloads().size(), 1u);
  EXPECT_EQ(port.payloads()[0], Dt1(kKitSelectAddr, EncodeKit(129)));
}

TEST_F(SpdsxDeviceTest, SelectObjectFocusesTheObject) {
  port.QueueReply({0x01});
  dev.SelectObject(ObjectKind::kPad, 7);

  ASSERT_EQ(port.payloads().size(), 1u);
  EXPECT_EQ(port.payloads()[0],
            Dt1(kObjectSelectAddr, {SelectValue(ObjectKind::kPad, 7)}));
}

// ---- The writes that target a kit ----

// The kit lives in the write address, so no kit-select is sent -- and the
// name has to land on the kit asked for. A prefix that ignored the argument
// once sent every write to kit 129 while looking correct.
TEST_F(SpdsxDeviceTest, SetKitNameWritesSixteenCharsToThatKitsAddresses) {
  dev.SetKitName(200, "ZZZ", 0.0);

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 16u);
  for (int i = 0; i < kKitNameLength; ++i) {
    const uint8_t ch = i < 3 ? 'Z' : 0x20;  // space-padded
    EXPECT_EQ(sent[static_cast<size_t>(i)], Dt1(KitNameAddr(200, i), {ch}))
        << "char " << i;
  }
}

TEST_F(SpdsxDeviceTest, SetKitNameAlwaysWritesTheWholeField) {
  dev.SetKitName(1, std::string(40, 'X'), 0.0);
  EXPECT_EQ(port.payloads().size(), 16u);  // truncated, not overrun
}

TEST_F(SpdsxDeviceTest, SetPadWaveWritesTheWaveThenTheEnableFlag) {
  dev.SetPadWave(129, 7, PadSlot::kTop, 127, 0.0);

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 2u);
  EXPECT_EQ(sent[0],
            Dt1(PadWaveAddr(129, 7, PadSlot::kTop), NibbleEncode(127)));
  // Without the companion flag the slot stays unused.
  EXPECT_EQ(sent[1], Dt1(PadWaveEnableAddr(129, 7, PadSlot::kTop), {0x01}));
}

TEST_F(SpdsxDeviceTest, SetPadLinkFocusesTheObjectThenWritesTheGroup) {
  port.QueueReply({0x01});  // the focus reply, which is drained
  dev.SetPadLink(200, ObjectKind::kPad, 7, 11, 0.0);

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 2u);
  EXPECT_EQ(sent[0],
            Dt1(kObjectSelectAddr, {SelectValue(ObjectKind::kPad, 7)}));
  EXPECT_EQ(sent[1], Dt1(PadLinkAddr(ObjectKind::kPad, 7, 200), {0x0b}));
}

// One DT1 per field, at the param indices the kit record stores them at --
// including the hi-hat trio at 0x07/08/09.
TEST_F(SpdsxDeviceTest, SetPadLayerParamsWritesEveryFieldToItsOwnParam) {
  port.QueueReply({0x01});  // the pad focus
  PadDeviceParams params;
  params.layer_mode = 3;
  params.fade_point = 100;
  params.fade_end = 120;
  params.dynamics = 1;
  params.dynamics_curve = 2;
  params.fixed_velocity = 50;
  params.hi_hat_volume = 80;
  params.hi_hat_fade_in = 5;
  params.hi_hat_decay = 25;
  params.trigger_reserve = 1;

  dev.SetPadLayerParams(200, 9, params, 0.0);

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 11u);  // the focus, then ten fields
  EXPECT_EQ(sent[0],
            Dt1(kObjectSelectAddr, {SelectValue(ObjectKind::kPad, 9)}));

  const struct {
    int param;
    uint8_t value;
  } fields[] = {{0x00, 3},
                {0x01, 100},
                {0x02, 120},
                {0x03, 1},
                {0x04, 2},
                {0x05, 50},
                {0x07, 80},
                {0x08, 5},
                {0x09, 25},
                {0x13, 1}};

  for (size_t i = 0; i < std::size(fields); ++i) {
    EXPECT_EQ(sent[i + 1],
              Dt1(PadParamAddr(200, 9, fields[i].param), {fields[i].value}))
        << "param " << fields[i].param;
  }
}

// ---- Commit ----

TEST_F(SpdsxDeviceTest, CommitBeginsThenPollsUntilTheFlashWriteIsDone) {
  port.QueueReply({0x7a});  // the begin ack
  port.QueueReply(CommitPoll(0x01));  // done on the first poll

  EXPECT_TRUE(dev.Commit(5.0));

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 2u);
  EXPECT_EQ(sent[0][4], 0x21);  // begin
  EXPECT_EQ(sent[1][4], 0x22);  // poll
}

TEST_F(SpdsxDeviceTest, CommitKeepsPollingWhileTheFlashWriteIsBusy) {
  port.QueueReply({0x7a});
  port.QueueReply(CommitPoll(0x00));  // busy
  port.QueueReply(CommitPoll(0x01));  // then done

  EXPECT_TRUE(dev.Commit(5.0));
  EXPECT_EQ(port.payloads().size(), 3u);  // begin + two polls
}

// The flash write is what makes an edit survive a power cycle, so a commit
// that never reports done has to say so rather than claim success.
TEST_F(SpdsxDeviceTest, CommitGivesUpWhenDoneNeverComes) {
  EXPECT_FALSE(dev.Commit(0.0));
}

// ---- DeleteWave ----

TEST_F(SpdsxDeviceTest, DeleteWaveBracketsTheDeleteInASessionAndCommits) {
  for (int i = 0; i < 4; ++i) {
    port.QueueReply({0x7a});
  }
  port.QueueReply(CommitPoll(0x01));

  dev.DeleteWave(1590);

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 5u);
  EXPECT_EQ(sent[0][4], 0x09);  // session begin
  EXPECT_EQ(sent[0][15], 0x01);
  EXPECT_EQ(sent[1][4], 0x1D);  // the delete itself
  EXPECT_EQ(sent[2][4], 0x09);  // session end
  EXPECT_EQ(sent[2][15], 0x00);
  EXPECT_EQ(sent[3][4], 0x21);  // commit begin
  EXPECT_EQ(sent[4][4], 0x22);  // commit poll

  // The sample index rides in the argument at byte 15, little-endian.
  EXPECT_EQ(sent[1][15], 1590 & 0xFF);
  EXPECT_EQ(sent[1][16], (1590 >> 8) & 0xFF);
}

// ---- FindDevicePort ----
//
// The node number changes on every replug, so finding the device means
// trying each candidate. None of that is platform-specific — the backend is
// — which is why this can be driven without a device attached.

TEST(FindDevicePort, ReturnsTheNodeThatAnswers) {
  FakePortBackend ports;
  ports.AddNode("/dev/cu.usbmodem001");  // silent: something else entirely
  ports.AddNode("/dev/cu.usbmodem002").QueueReply(PingReply());

  EXPECT_EQ(FindDevicePort(ports), "/dev/cu.usbmodem002");
  // It stopped at the one that answered.
  EXPECT_EQ(
      ports.opened(),
      std::vector<std::string>({"/dev/cu.usbmodem001", "/dev/cu.usbmodem002"}));
}

TEST(FindDevicePort, TakesTheFirstAnswerAndStopsLooking) {
  FakePortBackend ports;
  ports.AddNode("/dev/cu.usbmodem001").QueueReply(PingReply());
  ports.AddNode("/dev/cu.usbmodem002").QueueReply(PingReply());

  EXPECT_EQ(FindDevicePort(ports), "/dev/cu.usbmodem001");
  EXPECT_EQ(ports.opened().size(), 1u);  // the second was never touched
}

// Another program holding a node (the official app is the usual one) must
// not stop the search: that node is skipped, not fatal.
TEST(FindDevicePort, SkipsANodeItCannotOpen) {
  FakePortBackend ports;
  ports.AddUnopenableNode("/dev/cu.usbmodem001");
  ports.AddNode("/dev/cu.usbmodem002").QueueReply(PingReply());

  EXPECT_EQ(FindDevicePort(ports), "/dev/cu.usbmodem002");
}

TEST(FindDevicePort, SaysSoWhenThereIsNothingToTry) {
  FakePortBackend ports;
  EXPECT_THROW((void)FindDevicePort(ports), std::runtime_error);

  try {
    (void)FindDevicePort(ports);
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("plugged in"), std::string::npos)
        << e.what();
  }
}

// Nodes exist but none is the device — the official app is probably still
// holding it, which is what the message has to suggest.
TEST(FindDevicePort, SaysSoWhenNothingAnswers) {
  FakePortBackend ports;
  ports.AddNode("/dev/cu.usbmodem001");
  ports.AddUnopenableNode("/dev/cu.usbmodem002");

  try {
    (void)FindDevicePort(ports);
    FAIL() << "expected it to throw";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("answered"), std::string::npos)
        << e.what();
  }
  EXPECT_EQ(ports.opened().size(), 2u);  // it tried them all first
}

TEST(FindDevicePort, PingsEachCandidateItOpens) {
  FakePortBackend ports;
  FakeSerialPort& node = ports.AddNode("/dev/cu.usbmodem001");
  node.QueueReply(PingReply());

  EXPECT_EQ(FindDevicePort(ports), "/dev/cu.usbmodem001");

  ASSERT_EQ(node.payloads().size(), 1u);
  EXPECT_EQ(node.payloads()[0][4], 0x16);  // the ping's sub-command
}

}  // namespace
}  // namespace spdsx::device

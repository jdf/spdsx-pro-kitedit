#include "device/spdsx_device.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "device/sample_image.h"  // PcmToRfwv, kRfwvHeaderSize
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

// The selects are DT1 writes, and DT1s are never acked: waiting for a
// reply here stalled every caller for the full read timeout.
TEST_F(SpdsxDeviceTest, SelectKitWritesTheKitSelectParameterWithoutWaiting) {
  port.QueueReply({0x01});  // anything queued must be left alone
  dev.SelectKit(129);

  ASSERT_EQ(port.payloads().size(), 1u);
  EXPECT_EQ(port.payloads()[0], Dt1(kKitSelectAddr, EncodeKit(129)));
  EXPECT_GT(port.unread(), 0u);  // fire-and-forget: nothing was read
}

TEST_F(SpdsxDeviceTest, SelectObjectFocusesTheObjectWithoutWaiting) {
  port.QueueReply({0x01});
  dev.SelectObject(ObjectKind::kPad, 7);

  ASSERT_EQ(port.payloads().size(), 1u);
  EXPECT_EQ(port.payloads()[0],
            Dt1(kObjectSelectAddr, {SelectValue(ObjectKind::kPad, 7)}));
  EXPECT_GT(port.unread(), 0u);
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

  EXPECT_TRUE(dev.Commit());

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 2u);
  EXPECT_EQ(sent[0][4], 0x21);  // begin
  EXPECT_EQ(sent[1][4], 0x22);  // poll
}

TEST_F(SpdsxDeviceTest, CommitKeepsPollingWhileTheFlashWriteIsBusy) {
  port.QueueReply({0x7a});
  port.QueueReply(CommitPoll(0x00));  // busy
  port.QueueReply(CommitPoll(0x01));  // then done

  EXPECT_TRUE(dev.Commit());
  EXPECT_EQ(port.payloads().size(), 3u);  // begin + two polls
}

// There is no timeout: the poll runs until the device reports done or the
// caller's should_abort asks it to stop. An immediate abort returns false
// without claiming the commit finished.
TEST_F(SpdsxDeviceTest, CommitStopsWhenAskedToAbort) {
  EXPECT_FALSE(dev.Commit([] { return true; }));
}

// The commit that ends an import batch adds the 6a 0c arg-1 and 6a 02
// messages between begin and the poll — official app, every import capture.
TEST_F(SpdsxDeviceTest, CommitUploadBatchAddsTheImportEpilogue) {
  port.QueueReply({0x7a});  // begin ack
  port.QueueReply({0x7a});  // 0c reply
  port.QueueReply({0x7a});  // 02 reply
  port.QueueReply(CommitPoll(0x01));

  EXPECT_TRUE(dev.CommitUploadBatch());

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 4u);
  EXPECT_EQ(sent[0][4], 0x21);  // begin
  EXPECT_EQ(sent[1][4], 0x0C);
  EXPECT_EQ(sent[1][15], 0x01);  // arg 1
  EXPECT_EQ(sent[2][4], 0x02);
  EXPECT_EQ(sent[3][4], 0x22);  // poll
}

TEST_F(SpdsxDeviceTest, CommitUploadBatchStopsWhenAskedToAbort) {
  EXPECT_FALSE(dev.CommitUploadBatch([] { return true; }));
}

// ---- DeleteWave ----

// The official app's delete (fileops-1.log): slot + last-index queries,
// the delete in a session bracket with the 18/23 status queries after it,
// then a plain commit.
TEST_F(SpdsxDeviceTest, DeleteWaveFollowsTheOfficialSequenceAndCommits) {
  for (int i = 0; i < 4; ++i) {
    port.QueueReply({0x7a});  // 0c, 0d, session begin, delete
  }
  port.QueueIdle();  // nothing unsolicited follows this delete
  for (int i = 0; i < 4; ++i) {
    port.QueueReply({0x7a});  // 18, 23, session end, commit begin
  }
  port.QueueReply(CommitPoll(0x01));

  dev.DeleteWave(1590);

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 9u);
  EXPECT_EQ(sent[0][4], 0x0C);  // slot query, carrying the index
  EXPECT_EQ(sent[0][15], 1590 & 0xFF);
  EXPECT_EQ(sent[0][16], (1590 >> 8) & 0xFF);
  EXPECT_EQ(sent[1][4], 0x0D);  // last-index query
  EXPECT_EQ(sent[2][4], 0x09);  // session begin
  EXPECT_EQ(sent[2][15], 0x01);
  EXPECT_EQ(sent[3][4], 0x1D);  // the delete itself
  EXPECT_EQ(sent[3][15], 1590 & 0xFF);
  EXPECT_EQ(sent[3][16], (1590 >> 8) & 0xFF);
  EXPECT_EQ(sent[4][4], 0x18);  // post-delete status queries
  EXPECT_EQ(sent[5][4], 0x23);
  EXPECT_EQ(sent[6][4], 0x09);  // session end
  EXPECT_EQ(sent[6][15], 0x00);
  EXPECT_EQ(sent[7][4], 0x21);  // commit begin
  EXPECT_EQ(sent[8][4], 0x22);  // commit poll
}

// Deleting an assigned wave makes the device push unsolicited DT1
// notifications (a kit-select echo, the assignments clearing) right after
// the delete's ack. They must be drained — read one as a later command's
// ack and the whole stream shifts by a frame.
TEST_F(SpdsxDeviceTest, DeleteWaveDrainsTheUnsolicitedNotifications) {
  for (int i = 0; i < 4; ++i) {
    port.QueueReply({0x7a});
  }
  port.QueueReply(Dt1(kKitSelectAddr, EncodeKit(129)));
  port.QueueReply(Dt1(PadWaveEnableAddr(129, 1, PadSlot::kTop), {0x00}));
  port.QueueIdle();  // then the device goes quiet
  for (int i = 0; i < 4; ++i) {
    port.QueueReply({0x7a});
  }
  port.QueueReply(CommitPoll(0x01));

  dev.DeleteWave(1590);

  // Same nine commands — the notifications didn't shift anything — and
  // every queued reply was consumed by the command it belonged to.
  EXPECT_EQ(port.payloads().size(), 9u);
  EXPECT_EQ(port.unread(), 0u);
}

// ---- UploadWave ----
//
// The upload replays a sequence decoded from synthupload-1.log and verified
// against the hardware. The sequence IS the contract: it writes flash, and
// the device has no opinion about a step arriving out of order until it
// desyncs. Nothing below is reachable without a port to watch.

// A directory listing reply carrying a recognisable handle at bytes 4..8 —
// what an existing directory answers.
Bytes DirReply() {
  return {0xF0, 0x41, 0x7A, 0x7A, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF7};
}

// A listing of a directory that does not exist: the handle bytes all zero.
Bytes MissingDirReply() {
  return {0xF0, 0x41, 0x7A, 0x7A, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7};
}

// Replies for the commands one UploadWave sends (no flash commit — the
// batch commits once, in the caller): the per-file session open, the file
// write, then the register. When the D0NN directory exists its listing
// answers with a handle and the upload is 19 commands; a missing one takes
// the mkdir walk (five more). The free-space query answers with 16 raw
// off-format bytes AND a normal ack frame, both of which must come off.
void QueueUploadReplies(FakeSerialPort& port, bool dir_exists) {
  port.QueueReply({0x7a});  // session open 09
  port.QueueReply({0x7a});  // session open 0a
  port.QueueReply({0x7a});  // stat tmp
  port.QueueReply({0x7a});  // create
  if (dir_exists) {
    port.QueueReply(DirReply());  // dir D0NN -> handle
  } else {
    port.QueueReply(MissingDirReply());  // dir D0NN -> no handle
    port.QueueReply(DirReply());  // dir DATA -> the parent's handle
    port.QueueReply({0x7a});  // dir 0d
    port.QueueReply(MissingDirReply());  // dir D0NN, still missing
    port.QueueReply({0x7a});  // mkdir D0NN
    port.QueueReply(DirReply());  // dir D0NN -> now a handle
  }
  port.QueueReply({0x7a});  // dir 0d
  port.QueueReply({0x7a});  // open write
  port.QueueReply({0x7a});  // seek
  port.QueueRaw(Bytes(16, 0x00));  // free-space: the off-format fragment...
  port.QueueReply({0x7a});  // ...then its normal ack frame
  port.QueueReply({0x7a});  // write pcm ack
  port.QueueReply({0x7a});  // seek 0
  port.QueueReply({0x7a});  // write hdr ack
  port.QueueReply({0x7a});  // close
  port.QueueReply({0x7a});  // finalize tmp (stat)
  port.QueueReply({0x7a});  // register 0b
  port.QueueReply({0x7a});  // register 0c
  // (base record: a DT1, never acked)
  port.QueueReply({0x7a});  // session close 09/0
  // (name record: a DT1, never acked)
}

// The wave from the capture: 8192 bytes of PCM is 4096 mono 16-bit frames.
Bytes CapturedSmp() {
  return PcmToRfwv(Bytes(8192, 0), 48000, 1, 16);
}

// A file with a header and nothing after it is not a wave to upload.
TEST_F(SpdsxDeviceTest, UploadWaveRefusesAnSmpWithNoPcm) {
  EXPECT_THROW(dev.UploadWave(1587, Bytes(kRfwvHeaderSize, 0), "n", "n.wav"),
               std::runtime_error);
  EXPECT_THROW(dev.UploadWave(1587, Bytes(10, 0), "n", "n.wav"),
               std::runtime_error);
  EXPECT_TRUE(port.writes().empty());  // nothing went out
}

// The faithful sequence for a fresh directory (import-multi-1.log, first
// file into D016): session open, stat, create, the walk that mkdir's the
// D0NN folder, open, write, close, then register — and crucially NO
// per-file flash commit (that wedged the device; the batch commits once in
// the caller).
TEST_F(SpdsxDeviceTest,
       UploadWaveWritesTheFileThenRegistersItWithoutCommitting) {
  QueueUploadReplies(port, /*dir_exists=*/false);

  dev.UploadWave(1587, CapturedSmp(), "B_noise_4096", "B_noise_4096.wav");

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 24u);

  // The per-file session open on the control family.
  EXPECT_EQ(sent[0][4], 0x09);
  EXPECT_EQ(sent[0][15], 0x01);
  EXPECT_EQ(sent[1][4], 0x0A);

  // The file write, in the capture's order — including the mkdir walk.
  EXPECT_EQ(sent[2][4], 0x0A);  // stat tmp
  EXPECT_EQ(sent[3][4], 0x18);  // create (two paths)
  EXPECT_EQ(sent[4][4], 0x0C);  // dir D0NN (missing)
  EXPECT_EQ(sent[5][4], 0x0C);  // dir DATA (parent)
  EXPECT_EQ(sent[6][4], 0x0D);  // dir handle (the parent's)
  EXPECT_EQ(sent[7][4], 0x0C);  // dir D0NN
  EXPECT_EQ(sent[8][4], 0x09);  // mkdir D0NN
  EXPECT_EQ(sent[9][4], 0x0C);  // dir D0NN
  EXPECT_EQ(sent[10][4], 0x0D);  // dir handle
  EXPECT_EQ(sent[11][4], 0x00);  // open for write
  EXPECT_EQ(sent[12][4], 0x07);  // seek
  EXPECT_EQ(sent[13][4], 0x19);  // free-space query
  EXPECT_EQ(sent[14][4], 0x06);  // write pcm
  EXPECT_EQ(sent[15][4], 0x07);  // seek back
  EXPECT_EQ(sent[16][4], 0x06);  // write header
  EXPECT_EQ(sent[17][4], 0x03);  // close

  // Then the registration: the base record between the register queries and
  // the session close, the name record after — and never a 21/22.
  EXPECT_EQ(sent[18][4], 0x0A);  // finalize tmp
  EXPECT_EQ(sent[19][4], 0x0B);  // register
  EXPECT_EQ(sent[20][4], 0x0C);
  EXPECT_EQ(sent[21][2], 0x10);  // the base record, a DT1
  EXPECT_EQ(sent[22][2], 0x6A);  // session close 09 arg 0
  EXPECT_EQ(sent[22][4], 0x09);
  EXPECT_EQ(sent[22][15], 0x00);
  EXPECT_EQ(sent[23][2], 0x10);  // the name record, a DT1
  for (const Bytes& p : sent) {
    if (p[2] == 0x6A) {
      EXPECT_NE(p[4], 0x21) << "UploadWave must not flash-commit per file";
    }
  }

  // The file ops go on the remote-file channel, control on 0x09.
  EXPECT_EQ(port.writes()[14][3], 0x06);  // write pcm
  EXPECT_EQ(port.writes()[19][3], 0x09);  // register (control family)

  // Every queued reply — including the free-space ack frame — was consumed
  // by the command it belonged to: the ack stream stayed aligned.
  EXPECT_EQ(port.unread(), 0u);
}

// An existing directory answers the listing with a handle, and the official
// app then skips the whole walk: no parent listing, no mkdir.
TEST_F(SpdsxDeviceTest, UploadWaveSkipsTheMkdirWalkWhenTheDirectoryExists) {
  QueueUploadReplies(port, /*dir_exists=*/true);

  dev.UploadWave(1587, CapturedSmp(), "n", "n.wav");

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 19u);
  EXPECT_EQ(sent[4][4], 0x0C);  // the one dir listing...
  EXPECT_EQ(sent[5][4], 0x0D);  // ...echoes its handle...
  EXPECT_EQ(sent[6][4], 0x00);  // ...and goes straight to open-for-write
  for (const Bytes& p : sent) {
    if (p[2] == 0x7A) {
      EXPECT_NE(p[4], 0x09) << "no mkdir for a directory that exists";
    }
  }
  EXPECT_EQ(port.unread(), 0u);
}

// The handle is the device's, not ours: it comes back in the dir listing and
// has to go straight into the next request.
TEST_F(SpdsxDeviceTest, UploadWaveCarriesTheDirHandleIntoTheNextRequest) {
  QueueUploadReplies(port, /*dir_exists=*/true);

  dev.UploadWave(1587, CapturedSmp(), "n", "n.wav");

  const Bytes handle_req = port.payloads()[5];
  ASSERT_GE(handle_req.size(), 10u);
  EXPECT_EQ(handle_req[4], 0x0D);
  EXPECT_EQ(Bytes(handle_req.begin() + 5, handle_req.begin() + 10),
            Bytes({0xAA, 0xBB, 0xCC, 0xDD, 0xEE}));
}

// In the mkdir walk the missing directory has no handle to echo: the first
// 0d carries the PARENT's (the official app's exact bytes; echoing the
// missing directory's all-zero reply was the old shortcut).
TEST_F(SpdsxDeviceTest, UploadWaveEchoesTheParentHandleWhenMkdirIsNeeded) {
  QueueUploadReplies(port, /*dir_exists=*/false);

  dev.UploadWave(1587, CapturedSmp(), "n", "n.wav");

  const Bytes handle_req = port.payloads()[6];
  ASSERT_GE(handle_req.size(), 10u);
  EXPECT_EQ(handle_req[4], 0x0D);
  EXPECT_EQ(Bytes(handle_req.begin() + 5, handle_req.begin() + 10),
            Bytes({0xAA, 0xBB, 0xCC, 0xDD, 0xEE}));
}

// The PCM goes down first and the header last, seeking back for it.
TEST_F(SpdsxDeviceTest, UploadWaveWritesThePcmThenSeeksBackForTheHeader) {
  const Bytes smp = CapturedSmp();
  QueueUploadReplies(port, /*dir_exists=*/true);

  dev.UploadWave(1587, smp, "n", "n.wav");

  const std::vector<Bytes> sent = port.payloads();
  // A write body is 5 zeros, the 40 00 marker, three length septets, data.
  const Bytes pcm_written(sent[9].begin() + 15, sent[9].end() - 1);
  EXPECT_EQ(pcm_written, Bytes(smp.begin() + kRfwvHeaderSize, smp.end()));

  const Bytes header_written(sent[11].begin() + 15, sent[11].end() - 1);
  EXPECT_EQ(header_written, Bytes(smp.begin(), smp.begin() + kRfwvHeaderSize));
  // The seek before the header (10) returns to the start of the file; the
  // one before the PCM (7) positions past it. They are not the same seek.
  EXPECT_EQ(Bytes(sent[10].begin() + 5, sent[10].end() - 1), Bytes(11, 0x00));
  EXPECT_NE(sent[7], sent[10]);
}

// Three big-endian base-128 septets, cracked with controlled uploads: 8192
// is 00 40 00. Get it wrong and the device reads the wrong number of bytes
// and hangs, so this is worth holding exactly.
TEST_F(SpdsxDeviceTest, UploadWaveEncodesTheWriteLengthAsSeptets) {
  QueueUploadReplies(port, /*dir_exists=*/true);
  dev.UploadWave(1587, CapturedSmp(), "n", "n.wav");

  const Bytes pcm_write = port.payloads()[9];
  EXPECT_EQ(Bytes(pcm_write.begin() + 12, pcm_write.begin() + 15),
            Bytes({0x00, 0x40, 0x00}));  // 8192

  const Bytes hdr_write = port.payloads()[11];
  EXPECT_EQ(Bytes(hdr_write.begin() + 12, hdr_write.begin() + 15),
            Bytes({0x00, 0x04, 0x00}));  // 512
}

// A real sample is hundreds of KB, and the device caps a write-data frame
// at 64 KiB: a larger PCM must go as a back-to-back burst of 64 KiB frames
// with ONE ack (import-large-1.log). Sending it as a single oversized frame
// wedges the unit — this is the bug the first live upload hit.
TEST_F(SpdsxDeviceTest, UploadWaveSplitsALargePcmIntoSixtyFourKChunks) {
  // 150000 bytes of PCM -> two full 64 KiB frames + an 18928-byte remainder.
  Bytes pcm(150000);
  for (size_t i = 0; i < pcm.size(); ++i) {
    pcm[i] = static_cast<uint8_t>(i * 7 + 1);
  }
  Bytes smp = PcmToRfwv(pcm, 48000, 1, 16);
  QueueUploadReplies(port, /*dir_exists=*/true);

  dev.UploadWave(1601, smp, "big", "big.wav");

  // The PCM went out as one Write() (one burst); locate it: the only frame
  // long enough to exceed a single 64 KiB payload.
  const std::vector<Bytes>& writes = port.writes();
  const Bytes* burst = nullptr;
  for (const Bytes& w : writes) {
    if (w.size() > 70000) {
      burst = &w;
      break;
    }
  }
  ASSERT_NE(burst, nullptr) << "no multi-frame burst was written";

  // Walk the transport frames packed into that one write and reassemble
  // their data, checking each carries at most 64 KiB.
  Bytes reassembled;
  size_t frames = 0;
  size_t i = 0;
  while (i + 20 <= burst->size()) {
    ASSERT_EQ((*burst)[i], 0x0d);
    ASSERT_EQ((*burst)[i + 3], 0x06);  // remote-file channel
    const size_t len = (*burst)[i + 16] | (*burst)[i + 17] << 8
        | (*burst)[i + 18] << 16 | (*burst)[i + 19] << 24;
    const Bytes payload(burst->begin() + static_cast<long>(i + 20),
                        burst->begin() + static_cast<long>(i + 20 + len));
    // payload: f0 41 7a 03 06 | 5 zeros 40 00 <3 septets> <data> | f7
    ASSERT_EQ(payload[4], 0x06);
    const Bytes data(payload.begin() + 15, payload.end() - 1);
    EXPECT_LE(data.size(), 65536u) << "frame " << frames << " over 64 KiB";
    reassembled.insert(reassembled.end(), data.begin(), data.end());
    ++frames;
    i += 20 + len;
  }
  EXPECT_EQ(frames, 3u);  // 64K + 64K + remainder
  EXPECT_EQ(reassembled, pcm);
}

// The two directory records are what make the wave visible and assignable.
// Byte-exact against the same capture protocol_test pins them from.
TEST_F(SpdsxDeviceTest, UploadWaveRegistersTheDirectoryRecordsForThatIndex) {
  QueueUploadReplies(port, /*dir_exists=*/true);

  dev.UploadWave(1587, CapturedSmp(), "B_noise_4096", "B_noise_4096.wav");

  const std::vector<Bytes> sent = port.payloads();
  // frames = the PCM's 8192 bytes as 16-bit mono, which the size field wants.
  EXPECT_EQ(sent[16],
            Dt1(SampleRecordAddr(1587, 0x00), SampleBaseRecord(4096)));
  // The hash field goes out as 0: the device stores it verbatim and ignores
  // it (see SAMPLE-RECORD-HASH.md).
  EXPECT_EQ(sent[18],
            Dt1(SampleRecordAddr(1587, 0x1B),
                SampleNameRecord("B_noise_4096", "B_noise_4096.wav", 0)));
}

TEST_F(SpdsxDeviceTest, UploadWaveSizesTheRecordFromThePcmLength) {
  QueueUploadReplies(port, /*dir_exists=*/true);
  // 4096 PCM bytes is 2048 frames, half the captured wave.
  dev.UploadWave(1587, PcmToRfwv(Bytes(4096, 0), 48000, 1, 16), "n", "n.wav");

  EXPECT_EQ(port.payloads()[16],
            Dt1(SampleRecordAddr(1587, 0x00), SampleBaseRecord(2048)));
}

// ---- DumpBank ----
//
// The streaming reads pull until the device runs dry, so what a fake has to
// get right is how a batch ends: an idle read, or a frame that is not data.

// One bank-0x20-family data block, as the device streams them.
Bytes BulkBlockFrame(uint8_t bank, const Bytes& data) {
  Bytes p = {0xF0, 0x41, 0x6C, 0x02, 0x00, 0x00, 0x00, 0x00, bank};
  p.insert(p.end(), data.begin(), data.end());
  p.push_back(0xF7);
  return p;
}

// The device's generic ack — not data, so it ends a batch.
Bytes BulkAck() {
  return {0xF0, 0x41, 0x6C, 0x7A, 0x00, 0x00, 0x00, 0x00, 0x10, 0xF7};
}

TEST_F(SpdsxDeviceTest, DumpBankFollowsThePrepareBeginReadEndHandshake) {
  for (int i = 0; i < 5; ++i) {
    port.QueueReply(BulkAck());  // four prepares, then begin
  }
  port.QueueReply(BulkBlockFrame(0x10, {0xaa}));

  dev.DumpBank(kBankKits, {}, 0.01, 0.01);

  const std::vector<Bytes> sent = port.payloads();
  // Every bank is prepared, whichever one is being read.
  ASSERT_GE(sent.size(), 8u);
  EXPECT_EQ(sent[0], BulkRequest(kBulkPrepare, kBankKits, 0));
  EXPECT_EQ(sent[1], BulkRequest(kBulkPrepare, kBankSamples, 0));
  EXPECT_EQ(sent[2], BulkRequest(kBulkPrepare, kBankMeta, 0));
  EXPECT_EQ(sent[3], BulkRequest(kBulkPrepare, kBankConfig, 0));
  EXPECT_EQ(sent[4], BulkRequest(kBulkBegin, kBankKits, 0));
  EXPECT_EQ(sent[5], BulkRequest(kBulkRead, kBankKits, kBulkNextChunk));
  // A read that yields nothing ends it, then END.
  EXPECT_EQ(sent[6], BulkRequest(kBulkRead, kBankKits, kBulkNextChunk));
  EXPECT_EQ(sent.back(), BulkRequest(kBulkEnd, kBankKits, 0));
}

// The image is the block frames concatenated with their headers on — the
// layout SplitBulkImage parses and the RE image cache stores. Which means
// the two have to agree, so check them against each other.
TEST_F(SpdsxDeviceTest, DumpBankReturnsAnImageSplitBulkImageCanParse) {
  for (int i = 0; i < 5; ++i) {
    port.QueueReply(BulkAck());
  }
  port.QueueReply(BulkBlockFrame(0x10, {0xaa, 0xbb}));
  port.QueueReply(BulkBlockFrame(0x20, {0xcc}));

  const Bytes image = dev.DumpBank(kBankKits, {}, 0.01, 0.01);

  const std::vector<BulkBlock> blocks = SplitBulkImage(image);
  ASSERT_EQ(blocks.size(), 2u);
  EXPECT_EQ(blocks[0].bank, 0x10);
  EXPECT_EQ(blocks[1].bank, 0x20);
  EXPECT_EQ(image, [] {
    Bytes want = BulkBlockFrame(0x10, {0xaa, 0xbb});
    const Bytes second = BulkBlockFrame(0x20, {0xcc});
    want.insert(want.end(), second.begin(), second.end());
    return want;
  }());
}

TEST_F(SpdsxDeviceTest, DumpBankReportsEachBlockAsItArrives) {
  for (int i = 0; i < 5; ++i) {
    port.QueueReply(BulkAck());
  }
  port.QueueReply(BulkBlockFrame(0x10, {0xaa}));
  port.QueueReply(BulkBlockFrame(0x10, {0xbb}));

  std::vector<Bytes> seen;
  dev.DumpBank(
      kBankKits,
      [&](const Bytes& block) { seen.push_back(block); },
      0.01,
      0.01);

  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[0], BulkBlockFrame(0x10, {0xaa}));
  EXPECT_EQ(seen[1], BulkBlockFrame(0x10, {0xbb}));
}

// An ack rather than data means this batch is done, not that the bank is.
TEST_F(SpdsxDeviceTest, DumpBankTreatsANonDataFrameAsTheEndOfABatch) {
  for (int i = 0; i < 5; ++i) {
    port.QueueReply(BulkAck());
  }
  port.QueueReply(BulkBlockFrame(0x10, {0xaa}));
  port.QueueReply(BulkAck());  // ends the batch mid-stream
  port.QueueReply(BulkBlockFrame(0x10, {0xbb}));  // the next batch's block

  const Bytes image = dev.DumpBank(kBankKits, {}, 0.01, 0.01);

  EXPECT_EQ(SplitBulkImage(image).size(), 2u);  // both, across two batches
}

TEST_F(SpdsxDeviceTest, DumpBankOfAnEmptyBankIsEmptyAndStillEnds) {
  for (int i = 0; i < 5; ++i) {
    port.QueueReply(BulkAck());
  }

  EXPECT_TRUE(dev.DumpBank(kBankKits, {}, 0.01, 0.01).empty());
  EXPECT_EQ(port.payloads().back(), BulkRequest(kBulkEnd, kBankKits, 0));
}

// ---- ReadRemoteWave ----

// A file data frame: 14 header bytes, the payload, then the SysEx f7.
Bytes FileDataFrame(const Bytes& data) {
  Bytes p = {0xF0, 0x41, 0x7A, 0x02};
  p.resize(14, 0x00);
  p.insert(p.end(), data.begin(), data.end());
  p.push_back(0xF7);
  return p;
}

Bytes FileAck() {
  return {0xF0, 0x41, 0x7A, 0x7A, 0xF7};
}

// STAT reply: the file size is the second u32, at offset 18.
Bytes StatReply(uint32_t size) {
  Bytes r(23, 0x00);
  r[0] = 0xF0;
  r[1] = 0x41;
  r[2] = 0x7A;
  r[3] = 0x02;
  r[8] = 0x08;
  r[18] = static_cast<uint8_t>(size);
  r[19] = static_cast<uint8_t>(size >> 8);
  r[20] = static_cast<uint8_t>(size >> 16);
  r[21] = static_cast<uint8_t>(size >> 24);
  r[22] = 0xF7;
  return r;
}

// The 512-byte RFWV header the export reads before it STATs.
Bytes HeaderData() {
  return Bytes(512, 0xAA);
}

// Queues the replies up to and including the STAT — the shared prefix of a
// successful export: session open (2), OPEN, SEEK 0, the header data, STAT.
void QueueExportPrefix(FakeSerialPort& port, uint32_t stat_size) {
  port.QueueReply({0x7a});  // session open 09
  port.QueueReply({0x7a});  // session open 0a
  port.QueueReply(FileAck());  // OPEN
  port.QueueReply(FileAck());  // SEEK 0
  port.QueueReply(FileDataFrame(HeaderData()));
  port.QueueReply(StatReply(stat_size));
  port.QueueReply(FileAck());  // SEEK past the header
}

// The full official order (waveexport-1/2.log): session open, OPEN, read
// the header, STAT, seek past the header, exact-size reads, CLOSE, session
// close.
TEST_F(SpdsxDeviceTest, ReadRemoteWaveFollowsTheOfficialExportOrder) {
  QueueExportPrefix(port, 516);
  port.QueueReply(FileDataFrame({1, 2, 3, 4}));
  port.QueueReply(FileAck());  // CLOSE
  port.QueueReply({0x7a});  // session close

  Bytes want = HeaderData();
  want.insert(want.end(), {1, 2, 3, 4});
  EXPECT_EQ(dev.ReadRemoteWave(1554, {}, 0.01), want);

  const std::vector<Bytes> sent = port.payloads();
  ASSERT_EQ(sent.size(), 10u);
  EXPECT_EQ(sent[0][2], 0x6A);  // session open on the control family
  EXPECT_EQ(sent[0][4], 0x09);
  EXPECT_EQ(sent[0][15], 0x01);
  EXPECT_EQ(sent[1][4], 0x0A);
  EXPECT_EQ(sent[2][4], 0x00);  // OPEN
  EXPECT_EQ(sent[3][4], 0x07);  // SEEK 0 (all-zero body)
  EXPECT_EQ(Bytes(sent[3].begin() + 5, sent[3].end() - 1), Bytes(11, 0x00));
  EXPECT_EQ(sent[4][4], 0x04);  // READ, asking exactly 512: septets 00 04 00
  EXPECT_EQ(Bytes(sent[4].begin() + 12, sent[4].begin() + 15),
            Bytes({0x00, 0x04, 0x00}));
  EXPECT_EQ(sent[5][4], 0x13);  // STAT comes after the header, not before
  EXPECT_EQ(sent[6][4], 0x07);  // SEEK 512
  EXPECT_EQ(sent[6][13], 0x04);
  EXPECT_EQ(sent[7][4], 0x04);  // READ, asking exactly the 4-byte remainder
  EXPECT_EQ(Bytes(sent[7].begin() + 12, sent[7].begin() + 15),
            Bytes({0x00, 0x00, 0x04}));
  EXPECT_EQ(sent[8][4], 0x03);  // CLOSE
  EXPECT_EQ(sent[9][2], 0x6A);  // session close
  EXPECT_EQ(sent[9][4], 0x09);
  EXPECT_EQ(sent[9][15], 0x00);
}

// Some factory preloads have no exportable file: the OPEN answers but
// nothing answers the header read. That must be a clean throw — a download
// batch skips such a wave and moves on (device 900, live).
TEST_F(SpdsxDeviceTest, ReadRemoteWaveRejectsAWaveWithNoExportableFile) {
  port.QueueReply({0x7a});
  port.QueueReply({0x7a});
  port.QueueReply(FileAck());  // OPEN succeeds
  // The 7a error the device sends instead of data; the SEEK consumes it,
  // and the header read then finds nothing.
  port.QueueReply({0xF0, 0x41, 0x7A, 0x7A, 0x7F, 0x7F, 0x7F, 0x7F, 0xF7});

  EXPECT_THROW(dev.ReadRemoteWave(900, {}, 0.01), std::runtime_error);
}

TEST_F(SpdsxDeviceTest, ReadRemoteWaveOpensTheWavesOwnPath) {
  QueueExportPrefix(port, 512);

  dev.ReadRemoteWave(1554, {}, 0.01);

  const Bytes open_req = port.payloads()[2];
  const std::string path = RemoteWavePath(1554);
  EXPECT_NE(
      std::search(open_req.begin(), open_req.end(), path.begin(), path.end()),
      open_req.end())
      << "the OPEN does not carry " << path;
}

// The device caps each reply, so a chunk arrives across frames. Each
// carries a 14-byte header and a trailing f7, and BOTH ends have to come
// off: an f7 left in shifts every later frame's sample alignment, which is
// audible as alternating audio and noise.
TEST_F(SpdsxDeviceTest, ReadRemoteWaveReassemblesTheFileAcrossFrames) {
  QueueExportPrefix(port, 520);
  port.QueueReply(FileDataFrame({1, 2, 3, 4}));
  port.QueueReply(FileDataFrame({5, 6, 7, 8}));

  Bytes want = HeaderData();
  want.insert(want.end(), {1, 2, 3, 4, 5, 6, 7, 8});
  EXPECT_EQ(dev.ReadRemoteWave(1554, {}, 0.01), want);
}

// A batch may overshoot the size the STAT gave; the file is what STAT said.
TEST_F(SpdsxDeviceTest, ReadRemoteWaveTrimsABatchThatOvershoots) {
  QueueExportPrefix(port, 518);
  port.QueueReply(FileDataFrame({1, 2, 3, 4}));
  port.QueueReply(FileDataFrame({5, 6, 7, 8}));

  Bytes want = HeaderData();
  want.insert(want.end(), {1, 2, 3, 4, 5, 6});
  EXPECT_EQ(dev.ReadRemoteWave(1554, {}, 0.01), want);
}

TEST_F(SpdsxDeviceTest, ReadRemoteWaveReportsProgressAgainstTheStatSize) {
  QueueExportPrefix(port, 520);
  port.QueueReply(FileDataFrame({1, 2, 3, 4}));
  port.QueueReply(FileDataFrame({5, 6, 7, 8}));

  std::vector<std::pair<size_t, size_t>> progress;
  dev.ReadRemoteWave(
      1554,
      [&](size_t done, size_t total) { progress.emplace_back(done, total); },
      0.01);

  // Once the STAT gives a total: the header already in hand, then each
  // data frame as it lands.
  EXPECT_EQ(progress,
            (std::vector<std::pair<size_t, size_t>> {
                {512, 520}, {516, 520}, {520, 520}}));
}

// Preload waves aren't exportable, so the OPEN is where that shows up.
TEST_F(SpdsxDeviceTest, ReadRemoteWaveThrowsWhenTheDeviceWillNotOpenIt) {
  port.QueueReply({0x7a});
  port.QueueReply({0x7a});
  port.QueueReply({0xF0, 0x41, 0x7A, 0x02, 0xF7});  // not an ack

  try {
    (void)dev.ReadRemoteWave(1554, {}, 0.01);
    FAIL() << "expected it to throw";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("preload"), std::string::npos)
        << e.what();
  }
}

TEST_F(SpdsxDeviceTest, ReadRemoteWaveThrowsOnAStatItCannotRead) {
  port.QueueReply({0x7a});
  port.QueueReply({0x7a});
  port.QueueReply(FileAck());  // OPEN
  port.QueueReply(FileAck());  // SEEK 0
  port.QueueReply(FileDataFrame(HeaderData()));
  port.QueueReply({0xF0, 0x41, 0x7A, 0x02, 0xF7});  // too short to hold a size

  EXPECT_THROW((void)dev.ReadRemoteWave(1554, {}, 0.01), std::runtime_error);
}

// The device going quiet part-way has to end the read, not spin on it.
TEST_F(SpdsxDeviceTest, ReadRemoteWaveStopsWhenTheDeviceStopsSending) {
  QueueExportPrefix(port, 2000);  // claims far more than it sends
  port.QueueReply(FileDataFrame({1, 2, 3, 4}));

  Bytes want = HeaderData();
  want.insert(want.end(), {1, 2, 3, 4});
  EXPECT_EQ(dev.ReadRemoteWave(1554, {}, 0.01), want);
  // It still closed the file and the session.
  const std::vector<Bytes> sent = port.payloads();
  EXPECT_EQ(sent[sent.size() - 2][4], 0x03);
  EXPECT_EQ(sent.back()[2], 0x6A);
  EXPECT_EQ(sent.back()[4], 0x09);
  EXPECT_EQ(sent.back()[15], 0x00);
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

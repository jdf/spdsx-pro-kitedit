#ifndef SPDSX_PATCHEDIT_TESTING_FAKE_SERIAL_PORT_H_
#define SPDSX_PATCHEDIT_TESTING_FAKE_SERIAL_PORT_H_

#include <algorithm>
#include <cstddef>
#include <deque>
#include <vector>

#include "device/serial_port.h"
#include "device/spdsx_device.h"  // Wrap / Unwrap

namespace spdsx_testing {

// A SerialPort with no hardware behind it: reads come from what the test
// queued, and writes are recorded for it to inspect. This is what lets the
// device ops be driven without a device.
class FakeSerialPort : public spdsx::device::SerialPort {
public:
  using Bytes = spdsx::device::Bytes;

  // Queues a reply, in the transport frame a real one would arrive in.
  void QueueReply(const Bytes& payload) {
    QueueRaw(spdsx::device::Wrap(payload));
  }

  // Queues bytes exactly as given — for replies no real device would send.
  void QueueRaw(const Bytes& raw) {
    readable_.insert(readable_.end(), raw.begin(), raw.end());
  }

  // Marks the stream idle here: reads stop at this point — a drain-until-
  // quiet loop sees a timeout — until the next Write, which is when a real
  // device would have something new to say.
  void QueueIdle() { idle_marks_.push_back(readable_.size()); }

  void Write(const Bytes& data) override {
    writes_.push_back(data);
    if (!idle_marks_.empty() && idle_marks_.front() <= read_pos_) {
      idle_marks_.pop_front();
    }
  }

  // Hands back what is queued, up to n. Running dry stands in for the read
  // timing out, which is what a silent device looks like.
  Bytes ReadExact(size_t n, double /*timeout_seconds*/) override {
    size_t limit = readable_.size();
    if (!idle_marks_.empty()) {
      limit = std::min(limit, idle_marks_.front());
    }
    const size_t take = std::min(n, limit - read_pos_);
    const auto begin = readable_.begin() + static_cast<long>(read_pos_);
    Bytes out(begin, begin + static_cast<long>(take));
    read_pos_ += take;
    return out;
  }

  // One entry per Write, framed as it went out.
  const std::vector<Bytes>& writes() const { return writes_; }

  // What the device sent, unwrapped back to payloads — usually what a test
  // wants to assert on.
  std::vector<Bytes> payloads() const {
    std::vector<Bytes> out;
    out.reserve(writes_.size());
    for (const Bytes& frame : writes_) {
      out.push_back(spdsx::device::Unwrap(frame));
    }
    return out;
  }

  size_t unread() const { return readable_.size() - read_pos_; }

private:
  std::vector<Bytes> writes_;
  Bytes readable_;
  std::deque<size_t> idle_marks_;
  size_t read_pos_ = 0;
};

}  // namespace spdsx_testing

#endif  // SPDSX_PATCHEDIT_TESTING_FAKE_SERIAL_PORT_H_

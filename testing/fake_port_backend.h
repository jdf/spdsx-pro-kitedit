#ifndef SPDSX_PATCHEDIT_TESTING_FAKE_PORT_BACKEND_H_
#define SPDSX_PATCHEDIT_TESTING_FAKE_PORT_BACKEND_H_

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "device/serial_port.h"
#include "fake_serial_port.h"

namespace spdsx_testing {

// A PortBackend with no machine under it: the test says which nodes exist,
// which of them refuse to open, and what each one answers.
class FakePortBackend : public spdsx::device::PortBackend {
public:
  // A node that opens and answers whatever is queued on its port.
  FakeSerialPort& AddNode(const std::string& path) {
    auto port = std::make_unique<FakeSerialPort>();
    FakeSerialPort& ref = *port;
    order_.push_back(path);
    nodes_[path] = std::move(port);
    return ref;
  }

  // A node that lists but won't open — held by another program, or gone
  // since it was listed.
  void AddUnopenableNode(const std::string& path) {
    order_.push_back(path);
    nodes_[path] = nullptr;
  }

  std::vector<std::string> ListCandidates() override { return order_; }

  std::unique_ptr<spdsx::device::SerialPort> Open(
      const std::string& path) override {
    opened_.push_back(path);
    const auto it = nodes_.find(path);
    if (it == nodes_.end() || it->second == nullptr) {
      throw std::runtime_error("cannot open " + path);
    }
    // The device borrows a port; these outlive it, so hand over a view that
    // does not own the FakeSerialPort the test still holds a reference to.
    return std::make_unique<Borrowed>(*it->second);
  }

  // Which nodes were tried, in order.
  const std::vector<std::string>& opened() const { return opened_; }

private:
  // Lends a FakeSerialPort without giving it away, so a test can queue
  // replies before the call and read writes back after it.
  class Borrowed : public spdsx::device::SerialPort {
  public:
    explicit Borrowed(FakeSerialPort& port)
        : port_(port) {}

    void Write(const spdsx::device::Bytes& data) override { port_.Write(data); }

    spdsx::device::Bytes ReadExact(size_t n, double timeout) override {
      return port_.ReadExact(n, timeout);
    }

  private:
    FakeSerialPort& port_;
  };

  std::vector<std::string> order_;
  std::map<std::string, std::unique_ptr<FakeSerialPort>> nodes_;
  std::vector<std::string> opened_;
};

}  // namespace spdsx_testing

#endif  // SPDSX_PATCHEDIT_TESTING_FAKE_PORT_BACKEND_H_

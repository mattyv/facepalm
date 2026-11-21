#ifndef FACEPALM_SRC_CALIBRATE_CLI_H_
#define FACEPALM_SRC_CALIBRATE_CLI_H_

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace facepalm {

// What RunCalibrateCli needs to talk to the daemon's socket (#36, #35,
// item 12 of #61), replaceable for tests.
class CalibrateClientIo {
 public:
  virtual ~CalibrateClientIo() = default;
  // Connects to the socket at `path`; false when it cannot.
  virtual bool Connect(const std::string& path) = 0;
  virtual void WriteLine(std::string_view line) = 0;
  // The next line the daemon sends, without its newline; nullopt once the
  // connection ends.
  virtual std::optional<std::string> ReadLine() = 0;
};

// A minimal way to drive one calibration request from outside a client
// with the daemon's full protocol (#36, #35, item 12 of #61): useful for
// testing, and as a model for the macOS and Linux menu's eventual Settings
// entry. `arguments` is argv[1:]: the socket path, then "start" (an
// optional screen count may follow), "next" or "cancel". Writes the
// daemon's JSON reply to `out`, or what went wrong to `err`. Returns the
// process's exit code: 0 for a reply with no "error", 2 for a usage
// problem, 1 for anything else. `out` and `err` follow the standard
// streams' own order (as main's argc, argv do argv's own), so a swap is
// unlikely and visible at once in any test.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int RunCalibrateCli(const std::vector<std::string>& arguments,
                    CalibrateClientIo& io, std::ostream& out,
                    std::ostream& err);

}  // namespace facepalm

#endif  // FACEPALM_SRC_CALIBRATE_CLI_H_

#include "src/calibration_store.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "src/calibration.h"

namespace facepalm {
namespace {

constexpr mode_t kPrivateMode = S_IRUSR | S_IWUSR;
constexpr mode_t kOtherPermissions = S_IRWXG | S_IRWXO;
// Comfortably more than the largest profile (kMaxScreens screens) needs.
constexpr std::size_t kMaxProfileBytes = 16UL * 1024;
constexpr int kFormatVersion = 1;
// ReadWholeFile's read buffer.
constexpr std::size_t kReadChunkBytes = 4096;
// WriteField's longest formatted number, well over what a double needs.
constexpr std::size_t kLongestNumberBytes = 32;

class PosixCalibrationFileIo : public CalibrationFileIo {
 public:
  int OpenTemp(const std::string& path) override {
    return open((path + ".tmp").c_str(),
                O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC,
                kPrivateMode);
  }
  ssize_t Write(int fd, const void* data, std::size_t size) override {
    return write(fd, data, size);
  }
  int Sync(int fd) override { return fsync(fd); }
  void Close(int fd) override { close(fd); }
  int Rename(const std::string& path) override {
    return rename((path + ".tmp").c_str(), path.c_str());
  }
  int OpenDirectory(const std::string& path) override {
    return open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  }
  int OpenForRead(const std::string& path) override {
    return open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  }
  int Stat(int fd, struct stat& result) override { return fstat(fd, &result); }
  uid_t User() override { return geteuid(); }
  ssize_t Read(int fd, void* buffer, std::size_t size) override {
    return read(fd, buffer, size);
  }
};

// Fsyncs the directory holding `path`, so a rename into it survives a
// crash. The slash-finding logic is here, not in CalibrationFileIo, so a
// mock can exercise it through `io` alone, as RatingLog's SyncParent is.
bool SyncParent(const std::string& path, CalibrationFileIo& io) {
  const auto slash = path.find_last_of('/');
  std::string parent = ".";
  if (slash != std::string::npos) {
    parent = slash == 0 ? "/" : path.substr(0, slash);
  }
  const int directory = io.OpenDirectory(parent);
  if (directory < 0) {
    return false;
  }
  const bool synced = io.Sync(directory) == 0;
  io.Close(directory);
  return synced;
}

// Reads the whole of an already-open file into `contents`, up to
// kMaxProfileBytes. False on a read error or an oversized file. Built on
// `io.Read` alone, so a mock can exercise its retry and limit, as
// ClientToken's LoadClientToken is.
bool ReadWholeFile(int fd, CalibrationFileIo& io, std::string& contents) {
  std::array<char, kReadChunkBytes> buffer{};
  contents.clear();
  for (;;) {
    const ssize_t n = io.Read(fd, buffer.data(), buffer.size());
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      return false;
    }
    if (n == 0) {
      return true;
    }
    if (contents.size() + static_cast<std::size_t>(n) > kMaxProfileBytes) {
      return false;
    }
    contents.append(buffer.data(), static_cast<std::size_t>(n));
  }
}

// Whether `fd` is a regular file that only its owner can read or write.
bool PrivateFile(int fd, CalibrationFileIo& io) {
  struct stat info{};
  return io.Stat(fd, info) == 0 && S_ISREG(info.st_mode) &&
         info.st_uid == io.User() && (info.st_mode & kOtherPermissions) == 0;
}

// `line` with `key` + " = " trimmed from its front, or nullopt when it is
// not there. Every call site passes both from a single, unambiguous
// expression (NextLine's result, and the field name literal), so there is
// nothing for the two to be swapped between.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::optional<std::string_view> After(std::string_view line,
                                      std::string_view key) {
  const std::string prefix = std::string(key) + " = ";
  if (!line.starts_with(prefix)) {
    return std::nullopt;
  }
  return line.substr(prefix.size());
}

// All of `text` read as a T, or nullopt.
template <typename T>
std::optional<T> Number(std::string_view text) {
  T value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// The next line of `text`, without its newline, advancing `text` past it;
// nullopt once `text` is exhausted.
std::optional<std::string_view> NextLine(std::string_view& text) {
  if (text.empty()) {
    return std::nullopt;
  }
  const std::size_t end = text.find('\n');
  const std::string_view line = text.substr(0, end);
  text =
      end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
  return line;
}

// The value keyed `key` on the next line of `text`, read as a T; nullopt,
// with `error`, when the line is missing or does not match.
template <typename T>
std::optional<T> ReadField(std::string_view& text, std::string_view key,
                           std::string& error) {
  const std::optional<std::string_view> line = NextLine(text);
  const std::optional<std::string_view> value =
      line ? After(*line, key) : std::nullopt;
  const std::optional<T> parsed = value ? Number<T>(*value) : std::nullopt;
  if (!parsed) {
    error = "expected " + std::string(key) + " = a number";
  }
  return parsed;
}

// Appends `key` = `value` as a line of SerializeCalibrationProfile's text.
void WriteField(std::string& text, std::string_view key, double value) {
  std::array<char, kLongestNumberBytes> buffer{};
  const auto [end, error] =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  text += std::string(key) + " = " + std::string(buffer.data(), end) + "\n";
}

void WritePoint(std::string& text, int screen, CalibrationPoint point,
                const HeadRotation& pose) {
  const std::string prefix =
      "screen." + std::to_string(screen) + "." + std::string(Name(point));
  WriteField(text, prefix + ".yaw", pose.yaw_degrees);
  WriteField(text, prefix + ".pitch", pose.pitch_degrees);
  WriteField(text, prefix + ".roll", pose.roll_degrees);
}

std::optional<HeadRotation> ReadPoint(std::string_view& text, int screen,
                                      CalibrationPoint point,
                                      std::string& error) {
  const std::string prefix =
      "screen." + std::to_string(screen) + "." + std::string(Name(point));
  HeadRotation pose;
  const std::optional<double> yaw =
      ReadField<double>(text, prefix + ".yaw", error);
  if (!yaw) {
    return std::nullopt;
  }
  const std::optional<double> pitch =
      ReadField<double>(text, prefix + ".pitch", error);
  if (!pitch) {
    return std::nullopt;
  }
  const std::optional<double> roll =
      ReadField<double>(text, prefix + ".roll", error);
  if (!roll) {
    return std::nullopt;
  }
  pose.yaw_degrees = *yaw;
  pose.pitch_degrees = *pitch;
  pose.roll_degrees = *roll;
  return pose;
}

}  // namespace

CalibrationFileStore::CalibrationFileStore(
    std::string path, std::shared_ptr<CalibrationFileIo> io)
    : path_(std::move(path)),
      io_(io ? std::move(io) : std::make_shared<PosixCalibrationFileIo>()) {}

bool CalibrationFileStore::Save(const CalibrationProfile& profile,
                                std::string& error) {
  const std::string text = SerializeCalibrationProfile(profile);
  const int fd = io_->OpenTemp(path_);
  if (fd < 0) {
    error = "cannot open calibration file";
    return false;
  }
  std::size_t written = 0;
  while (written < text.size()) {
    const ssize_t wrote =
        io_->Write(fd, text.data() + written, text.size() - written);
    if (wrote <= 0) {
      io_->Close(fd);
      error = "cannot write calibration file";
      return false;
    }
    written += static_cast<std::size_t>(wrote);
  }
  if (io_->Sync(fd) != 0) {
    io_->Close(fd);
    error = "cannot save calibration file";
    return false;
  }
  io_->Close(fd);
  if (io_->Rename(path_) != 0 || !SyncParent(path_, *io_)) {
    error = "cannot save calibration file";
    return false;
  }
  return true;
}

std::string SerializeCalibrationProfile(const CalibrationProfile& profile) {
  std::string text = "version = " + std::to_string(kFormatVersion) + "\n";
  WriteField(text, "resting.yaw", profile.resting.yaw_degrees);
  WriteField(text, "resting.pitch", profile.resting.pitch_degrees);
  WriteField(text, "resting.roll", profile.resting.roll_degrees);
  text += "screens = " + std::to_string(profile.screens.size()) + "\n";
  for (std::size_t screen = 0; screen < profile.screens.size(); ++screen) {
    for (const CalibrationPoint point : kCalibrationPoints) {
      WritePoint(
          text, static_cast<int>(screen), point,
          profile.screens[screen].points[static_cast<std::size_t>(point)]);
    }
  }
  return text;
}

std::optional<CalibrationProfile> ParseCalibrationProfile(std::string_view text,
                                                          std::string& error) {
  const std::optional<int> version = ReadField<int>(text, "version", error);
  if (!version) {
    return std::nullopt;
  }
  if (*version != kFormatVersion) {
    error = "unsupported calibration file version";
    return std::nullopt;
  }
  CalibrationProfile profile;
  const std::optional<double> yaw =
      ReadField<double>(text, "resting.yaw", error);
  const std::optional<double> pitch =
      yaw ? ReadField<double>(text, "resting.pitch", error) : std::nullopt;
  const std::optional<double> roll =
      pitch ? ReadField<double>(text, "resting.roll", error) : std::nullopt;
  if (!roll) {
    return std::nullopt;
  }
  profile.resting = {
      .yaw_degrees = *yaw, .pitch_degrees = *pitch, .roll_degrees = *roll};
  const std::optional<int> screens = ReadField<int>(text, "screens", error);
  if (!screens) {
    return std::nullopt;
  }
  if (*screens < 1 || *screens > CalibrationSession::kMaxScreens) {
    error = "calibration file names an impossible number of screens";
    return std::nullopt;
  }
  profile.screens.assign(static_cast<std::size_t>(*screens),
                         ScreenCalibration{});
  for (int screen = 0; screen < *screens; ++screen) {
    for (const CalibrationPoint point : kCalibrationPoints) {
      const std::optional<HeadRotation> pose =
          ReadPoint(text, screen, point, error);
      if (!pose) {
        return std::nullopt;
      }
      profile.screens[static_cast<std::size_t>(screen)]
          .points[static_cast<std::size_t>(point)] = *pose;
    }
  }
  if (!text.empty()) {
    error = "calibration file has trailing data";
    return std::nullopt;
  }
  return profile;
}

std::optional<CalibrationProfile> LoadCalibrationProfile(
    const std::string& path, std::string& error,
    std::shared_ptr<CalibrationFileIo> io) {
  if (!io) {
    io = std::make_shared<PosixCalibrationFileIo>();
  }
  const int fd = io->OpenForRead(path);
  if (fd < 0) {
    return std::nullopt;
  }
  if (!PrivateFile(fd, *io)) {
    io->Close(fd);
    error = "calibration file is unsafe to read";
    return std::nullopt;
  }
  std::string contents;
  const bool read_ok = ReadWholeFile(fd, *io, contents);
  io->Close(fd);
  if (!read_ok) {
    error = "cannot read calibration file";
    return std::nullopt;
  }
  return ParseCalibrationProfile(contents, error);
}

}  // namespace facepalm

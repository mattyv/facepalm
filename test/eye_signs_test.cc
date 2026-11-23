#include "src/eye_signs.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "src/eyes_replay.h"
#include "src/face_model.h"

// EyeSignsOf (#145): blink rate, a long blink and widened eyes, read from
// a window of samples a caller collects elsewhere, as coarse buckets the
// personal read alone may weigh -- never specs/FeelingBeat.tla's own
// Rule. The thresholds below match src/config.h's own defaults
// (kDefaultEyeSignsElevatedBlinksPerMinute and the rest), whose own
// comment carries the measurement each is reasoned from.

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::Optional;

constexpr EyeSignSettings kSettings = {.elevated_blinks_per_minute = 14.0,
                                       .long_blink_ms = 500,
                                       .widened_eyes_threshold = 0.025,
                                       .widened_eyes_hold_ms = 1000,
                                       .widened_eyes_max_side_look = 0.2,
                                       .min_window_ms = 3000};

TEST(BlinkRateNameTest, RoundTrips) {
  for (const BlinkRate rate :
       {BlinkRate::kNormal, BlinkRate::kElevated, BlinkRate::kUnknown}) {
    EXPECT_THAT(BlinkRateNamed(Name(rate)), Optional(Eq(rate)));
  }
  EXPECT_THAT(BlinkRateNamed("sideways"), Eq(std::nullopt));
}

TEST(LongBlinkNameTest, RoundTrips) {
  for (const LongBlink blink :
       {LongBlink::kNo, LongBlink::kYes, LongBlink::kUnknown}) {
    EXPECT_THAT(LongBlinkNamed(Name(blink)), Optional(Eq(blink)));
  }
  EXPECT_THAT(LongBlinkNamed("sideways"), Eq(std::nullopt));
}

TEST(WidenedEyesNameTest, RoundTrips) {
  for (const WidenedEyes widened :
       {WidenedEyes::kNo, WidenedEyes::kYes, WidenedEyes::kUnknown}) {
    EXPECT_THAT(WidenedEyesNamed(Name(widened)), Optional(Eq(widened)));
  }
  EXPECT_THAT(WidenedEyesNamed("sideways"), Eq(std::nullopt));
}

// A face with blendshapes all zero, but for `blink` (eyeBlink, both eyes)
// and `wide` (eyeWide, both eyes). `blink` and `wide` are never swapped in
// practice: every call site passes a literal blink score first, a
// literal eye-widening score second.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Face FaceWith(float blink, float wide) {
  Face face{};
  // src/eye_gaze.h's own kEyeBlinkLeft/Right (9, 10); src/eye_signs.cc's
  // own kEyeWideLeft/Right (21, 22).
  face.blendshapes[9] = blink;
  face.blendshapes[10] = blink;
  face.blendshapes[21] = wide;
  face.blendshapes[22] = wide;
  return face;
}

TEST(SampleFromTest, ReadsBlinkAndEyeWideFromTheFace) {
  const EyeSignSample blinking = SampleFrom(FaceWith(0.6F, 0.02F), 0.5, 1000);
  EXPECT_TRUE(blinking.blink);
  EXPECT_THAT(blinking.eye_wide, testing::DoubleNear(0.02, 1e-6));
  EXPECT_THAT(blinking.timestamp_ms, Eq(1000));

  const EyeSignSample not_blinking =
      SampleFrom(FaceWith(0.3F, 0.2F), 0.5, 2000);
  EXPECT_FALSE(not_blinking.blink);
  EXPECT_THAT(not_blinking.eye_wide, testing::DoubleNear(0.2, 1e-6));
}

// A face with blendshapes all zero but for eyeLookIn and eyeLookOut (both
// eyes, src/eye_signs.cc's own kEyeLookInLeft/Right (13, 14) and
// kEyeLookOutLeft/Right (15, 16)), for SampleFrom's own side_look field
// (#145/#165). `look_in` and `look_out` are never swapped in practice:
// every call site passes a literal eyeLookIn score first, a literal
// eyeLookOut score second.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Face FaceLookingSideways(float look_in, float look_out) {
  Face face{};
  face.blendshapes[13] = look_in;
  face.blendshapes[14] = look_in;
  face.blendshapes[15] = look_out;
  face.blendshapes[16] = look_out;
  return face;
}

TEST(SampleFromTest, ReadsSideLookAsTheLargerOfMeanInAndMeanOut) {
  const EyeSignSample looking_in =
      SampleFrom(FaceLookingSideways(0.4F, 0.1F), 0.5, 1000);
  EXPECT_THAT(looking_in.side_look, testing::DoubleNear(0.4, 1e-6));

  const EyeSignSample looking_out =
      SampleFrom(FaceLookingSideways(0.1F, 0.4F), 0.5, 2000);
  EXPECT_THAT(looking_out.side_look, testing::DoubleNear(0.4, 1e-6));
}

TEST(EyeSignsOfTest, NoSamplesIsUnknownThroughout) {
  const EyeSigns signs = EyeSignsOf({}, kSettings);
  EXPECT_THAT(signs.blink_rate, Eq(BlinkRate::kUnknown));
  EXPECT_THAT(signs.long_blink, Eq(LongBlink::kUnknown));
  EXPECT_THAT(signs.widened_eyes, Eq(WidenedEyes::kUnknown));
}

// A run of `count` samples, 66 ms apart (about 15 fps, #139's own
// recording rate), each `blink`, with `eye_wide` held throughout.
// `start_ms` and `count` are never swapped in practice: every call site
// passes a literal starting timestamp first, a literal sample count
// second.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<EyeSignSample> Samples(std::int64_t start_ms, int count, bool blink,
                                   double eye_wide) {
  std::vector<EyeSignSample> samples;
  samples.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    samples.push_back(
        {.timestamp_ms = start_ms + (static_cast<std::int64_t>(i) * 66),
         .blink = blink,
         .eye_wide = eye_wide});
  }
  return samples;
}

TEST(EyeSignsOfTest, AShortWindowIsUnknownForTheRateAlone) {
  // A window under min_window_ms: BlinkRate cannot be judged, but a long
  // blink or widened eyes inside it still can.
  std::vector<EyeSignSample> samples = Samples(0, 10, false, 0);
  samples[5].blink = true;

  const EyeSigns signs = EyeSignsOf(samples, kSettings);

  EXPECT_THAT(signs.blink_rate, Eq(BlinkRate::kUnknown));
  EXPECT_THAT(signs.long_blink, Eq(LongBlink::kNo));
  EXPECT_THAT(signs.widened_eyes, Eq(WidenedEyes::kNo));
}

// #171: a single sample (or several sharing one timestamp) is a
// zero-length window, which used to divide blinks by zero once
// min_window_ms itself allowed 0 (src/config.cc now floors it at 1, but
// EyeSignSettings is a plain struct this test can still build with 0).
TEST(EyeSignsOfTest, AZeroLengthWindowIsUnknownForTheRateEvenWithNoFloor) {
  EyeSignSettings settings = kSettings;
  settings.min_window_ms = 0;
  const std::vector<EyeSignSample> samples = Samples(0, 1, true, 0);

  EXPECT_THAT(EyeSignsOf(samples, settings).blink_rate,
              Eq(BlinkRate::kUnknown));
}

TEST(EyeSignsOfTest, AFewBlinksOverALongWindowIsNormal) {
  // One blink run over 10 s: 6 a minute, under the elevated threshold.
  std::vector<EyeSignSample> samples = Samples(0, 152, false, 0);
  samples[75].blink = true;

  EXPECT_THAT(EyeSignsOf(samples, kSettings).blink_rate,
              Eq(BlinkRate::kNormal));
}

TEST(EyeSignsOfTest, ManyBlinksOverTheSameWindowIsElevated) {
  // Four separate one-sample blink runs over about 10 s: 24 a minute,
  // past the elevated threshold.
  std::vector<EyeSignSample> samples = Samples(0, 152, false, 0);
  for (const int at : {20, 50, 80, 110}) {
    samples[at].blink = true;
  }

  EXPECT_THAT(EyeSignsOf(samples, kSettings).blink_rate,
              Eq(BlinkRate::kElevated));
}

TEST(EyeSignsOfTest, AnOrdinaryBlinkIsNotALongOne) {
  // One blink run of three samples, about 132 ms, well under
  // long_blink_ms.
  std::vector<EyeSignSample> samples = Samples(0, 50, false, 0);
  samples[10].blink = true;
  samples[11].blink = true;
  samples[12].blink = true;

  EXPECT_THAT(EyeSignsOf(samples, kSettings).long_blink, Eq(LongBlink::kNo));
}

TEST(EyeSignsOfTest, ABlinkHeldPastTheThresholdIsALongOne) {
  // One run from sample 10 to sample 20, 660 ms, past long_blink_ms.
  std::vector<EyeSignSample> samples = Samples(0, 50, false, 0);
  for (int i = 10; i <= 20; ++i) {
    samples[i].blink = true;
  }

  EXPECT_THAT(EyeSignsOf(samples, kSettings).long_blink, Eq(LongBlink::kYes));
}

TEST(EyeSignsOfTest, EyeWideUnderTheThresholdIsNotWidened) {
  const std::vector<EyeSignSample> samples = Samples(0, 50, false, 0.02);

  EXPECT_THAT(EyeSignsOf(samples, kSettings).widened_eyes,
              Eq(WidenedEyes::kNo));
}

TEST(EyeSignsOfTest, EyeWideOverTheThresholdButNotHeldIsNotWidened) {
  std::vector<EyeSignSample> samples = Samples(0, 50, false, 0);
  samples[10].eye_wide = 0.2;  // One sample only: 0 ms held.

  EXPECT_THAT(EyeSignsOf(samples, kSettings).widened_eyes,
              Eq(WidenedEyes::kNo));
}

TEST(EyeSignsOfTest, EyeWideHeldPastTheThresholdIsWidened) {
  // Samples 10 to 27, 1,122 ms, past widened_eyes_hold_ms (1,000), with
  // side_look left at its own default (0), well under
  // widened_eyes_max_side_look.
  std::vector<EyeSignSample> samples = Samples(0, 50, false, 0);
  for (int i = 10; i <= 27; ++i) {
    samples[i].eye_wide = 0.2;
  }

  EXPECT_THAT(EyeSignsOf(samples, kSettings).widened_eyes,
              Eq(WidenedEyes::kYes));
}

TEST(EyeSignsOfTest, EyeWideHeldWithSideLookWellUnderTheGuardIsStillWidened) {
  // The same held run as above, but with a non-zero side_look that still
  // clears widened_eyes_max_side_look (0.2) comfortably -- the guard
  // should not suppress an ordinary widen just because the eyes are not
  // perfectly still.
  std::vector<EyeSignSample> samples = Samples(0, 50, false, 0);
  for (int i = 10; i <= 27; ++i) {
    samples[i].eye_wide = 0.2;
    samples[i].side_look = 0.05;
  }

  EXPECT_THAT(EyeSignsOf(samples, kSettings).widened_eyes,
              Eq(WidenedEyes::kYes));
}

// #145/#165: eye_wide alone cannot tell a widen from a sideways glance,
// which lifts it too (measured on gaze_other_screen.probe and
// reading_other_screen.probe, src/config.h's own header comment on
// kDefaultEyeSignsWidenedEyesMaxSideLook). A held run that otherwise
// clears widened_eyes_threshold and widened_eyes_hold_ms is not widened
// once side_look is at or past widened_eyes_max_side_look throughout.
TEST(EyeSignsOfTest, EyeWideHeldButWithSideLookAtTheGuardIsNotWidened) {
  std::vector<EyeSignSample> samples = Samples(0, 50, false, 0);
  for (int i = 10; i <= 27; ++i) {
    samples[i].eye_wide = 0.2;
    samples[i].side_look = 0.2;  // At widened_eyes_max_side_look, not under.
  }

  EXPECT_THAT(EyeSignsOf(samples, kSettings).widened_eyes,
              Eq(WidenedEyes::kNo));
}

// Recorded fixtures from #139 (test/fixtures/eyes/README.md), the same
// ones src/eyes_gaze_replay.h's own tests replay.
std::string Fixture(const std::string& name) {
  const std::ifstream file(std::string(FACEPALM_FIXTURES) + "/eyes/" + name);
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

// `name`'s own `eyes` lines, read into samples through SampleFrom, the
// same conversion a live window reader would make.
std::vector<EyeSignSample> FixtureSamples(const std::string& name) {
  std::istringstream input(Fixture(name));
  std::vector<EyesFrame> frames;
  EXPECT_TRUE(ReplayEyes(input, frames));
  std::vector<EyeSignSample> samples;
  samples.reserve(frames.size());
  for (const EyesFrame& frame : frames) {
    Face face{};
    face.blendshapes[9] = static_cast<float>(frame.eye_blink[0]);
    face.blendshapes[10] = static_cast<float>(frame.eye_blink[1]);
    face.blendshapes[13] = static_cast<float>(frame.eye_look_in[0]);
    face.blendshapes[14] = static_cast<float>(frame.eye_look_in[1]);
    face.blendshapes[15] = static_cast<float>(frame.eye_look_out[0]);
    face.blendshapes[16] = static_cast<float>(frame.eye_look_out[1]);
    face.blendshapes[21] = static_cast<float>(frame.eye_wide[0]);
    face.blendshapes[22] = static_cast<float>(frame.eye_wide[1]);
    samples.push_back(SampleFrom(face, 0.5, frame.timestamp_ms));
  }
  return samples;
}

// The `index`th of a fixture's own four six-second cue windows
// (test/fixtures/eyes/README.md): the same per-cue span #145/#165's own
// measurements (src/config.h's header comment on
// kDefaultEyeSignsWidenedEyesThreshold) were taken over. `samples` must be
// oldest first, as FixtureSamples returns them.
std::vector<EyeSignSample> CueWindow(const std::vector<EyeSignSample>& samples,
                                     int index) {
  constexpr std::int64_t kCueMs = 6000;
  if (samples.empty()) {
    return {};
  }
  const std::int64_t start = samples.front().timestamp_ms +
                             (static_cast<std::int64_t>(index) * kCueMs);
  const std::int64_t end = start + kCueMs;
  std::vector<EyeSignSample> window;
  for (const EyeSignSample& sample : samples) {
    if (sample.timestamp_ms >= start && sample.timestamp_ms < end) {
      window.push_back(sample);
    }
  }
  return window;
}

// Cues: look ahead normally; blink a few times, as while reading; rest;
// blink a few more times. Measured through src/eyes_gaze_replay.h's own
// `eye_sign` line (this issue's own extension): eight separate blinks
// over the fixture's 25.257 s span, about 19 a minute, past
// elevated_blinks_per_minute (14); none holds the blink score for more
// than 200 ms, well under long_blink_ms (500).
TEST(EyeSignsOfTest, BlinksFixtureReadsElevatedAndNotLong) {
  const EyeSigns signs = EyeSignsOf(FixtureSamples("blinks.probe"), kSettings);

  EXPECT_THAT(signs.blink_rate, Eq(BlinkRate::kElevated));
  EXPECT_THAT(signs.long_blink, Eq(LongBlink::kNo));
}

// Cues: look ahead normally; close the eyes slowly, hold a couple
// seconds, then open; rest; close again, hold longer, then open.
// Measured the same way: two long blinks, 4,264 ms and 5,264 ms, each
// clearing long_blink_ms by more than 8x.
TEST(EyeSignsOfTest, LongBlinksFixtureReadsALongBlink) {
  const EyeSigns signs =
      EyeSignsOf(FixtureSamples("long_blinks.probe"), kSettings);

  EXPECT_THAT(signs.long_blink, Eq(LongBlink::kYes));
}

// Cues: look straight ahead normally; widen your eyes as if surprised, and
// hold; relax; widen your eyes again, and hold (#145/#165's own tenth
// fixture, test/fixtures/eyes/README.md). Each of the four six-second cue
// windows is read on its own, kDefaultEyeSignsWidenedEyesThreshold and
// kDefaultEyeSignsWidenedEyesHoldMs's own measurements (src/config.h):
// the two widen cues report widened, the ahead and relax cues do not.
TEST(EyeSignsOfTest, WidenedEyesFixtureReadsWidenedInBothWidenWindowsOnly) {
  const std::vector<EyeSignSample> samples =
      FixtureSamples("widened_eyes.probe");
  ASSERT_THAT(samples, ::testing::Not(::testing::IsEmpty()));

  EXPECT_THAT(EyeSignsOf(CueWindow(samples, 0), kSettings).widened_eyes,
              Eq(WidenedEyes::kNo))
      << "ahead";
  EXPECT_THAT(EyeSignsOf(CueWindow(samples, 1), kSettings).widened_eyes,
              Eq(WidenedEyes::kYes))
      << "widen";
  EXPECT_THAT(EyeSignsOf(CueWindow(samples, 2), kSettings).widened_eyes,
              Eq(WidenedEyes::kNo))
      << "relax";
  EXPECT_THAT(EyeSignsOf(CueWindow(samples, 3), kSettings).widened_eyes,
              Eq(WidenedEyes::kYes))
      << "widen2";
}

// Baseline: none of #139's nine fixtures carries a cue that deliberately
// widens the eyes, and none reads as widened -- the ceiling
// widened_eyes_threshold (0.15) is reasoned from (src/eye_signs.h's own
// header comment).
TEST(EyeSignsOfTest, EveryFixtureBaselineIsNotWidened) {
  for (const std::string name :
       {"blinks.probe", "long_blinks.probe", "eyes_to_corners.probe",
        "fully_away.probe", "gaze_other_screen.probe", "glance_keyboard.probe",
        "head_turn_eyes_centered.probe", "reading_other_screen.probe",
        "thinking_aversion.probe"}) {
    EXPECT_THAT(EyeSignsOf(FixtureSamples(name), kSettings).widened_eyes,
                Eq(WidenedEyes::kNo))
        << name;
  }
}

// Baseline: every fixture but blinks.probe and long_blinks.probe reads as
// a normal (not elevated) blink rate -- natural blinking at most, never a
// deliberate run of them.
TEST(EyeSignsOfTest, EveryOtherFixtureBaselineIsNormal) {
  for (const std::string name :
       {"eyes_to_corners.probe", "fully_away.probe", "gaze_other_screen.probe",
        "glance_keyboard.probe", "head_turn_eyes_centered.probe",
        "reading_other_screen.probe", "thinking_aversion.probe"}) {
    EXPECT_THAT(EyeSignsOf(FixtureSamples(name), kSettings).blink_rate,
                Eq(BlinkRate::kNormal))
        << name;
  }
}

}  // namespace
}  // namespace facepalm

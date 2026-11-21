#ifndef FACEPALM_SRC_TUNER_H_
#define FACEPALM_SRC_TUNER_H_

#include "src/feelings.h"

namespace facepalm {

// What the reaction feed asks of the person's own reads (#35): the read for
// a beat's evidence, and the person's corrections. FeelingStore implements
// it, following specs/FeelingFeedback.tla. Thread-safe.
class Tuner {
 public:
  virtual ~Tuner() = default;
  // The read for `evidence`: the person's own where it beats `generic`,
  // the generic rule's read; otherwise `generic`.
  virtual Feeling Read(const Evidence& evidence, Feeling generic) = 0;
  // Counts the person's pick of `feeling` for `evidence`. True only once
  // it is saved where a crash cannot undo it.
  virtual bool Learn(const Evidence& evidence, Feeling feeling) = 0;
  // Forgets every pick. True only once that is saved.
  virtual bool Forget() = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_TUNER_H_

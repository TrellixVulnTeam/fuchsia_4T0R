// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MEDIA_CPP_TIMELINE_RATE_H_
#define LIB_MEDIA_CPP_TIMELINE_RATE_H_

#include <stdint.h>
#include <zircon/assert.h>

#include <limits>

namespace media {

// TODO(dalesat): Consider always allowing inexact results.

// Expresses the relative rate of a timeline as the ratio between two uint32_t
// values subject_delta / reference_delta. "subject" refers to the timeline
// whose rate is being represented, and "reference" refers to the timeline
// relative to which the rate is expressed.
class TimelineRate {
 public:
  // Used to indicate overflow of scaling operations.
  static constexpr int64_t kOverflow = std::numeric_limits<int64_t>::max();

  // Zero as a TimelineRate.
  static const TimelineRate Zero;

  // Nanoseconds (subject) per second (reference) as a TimelineRate.
  static const TimelineRate NsPerSecond;

  // Reduces the ratio of *subject_delta and *reference_delta.
  static void Reduce(uint32_t* subject_delta, uint32_t* reference_delta);

  // Produces the product of the rates. If exact is true, DCHECKs on loss of
  // precision.
  static void Product(uint32_t a_subject_delta, uint32_t a_reference_delta,
                      uint32_t b_subject_delta, uint32_t b_reference_delta,
                      uint32_t* product_subject_delta, uint32_t* product_reference_delta,
                      bool exact = true);

  // Produces the product of the rates and the int64_t as an int64_t. Returns
  // kOverflow on overflow.
  static int64_t Scale(int64_t value, uint32_t subject_delta, uint32_t reference_delta);

  // Returns the product of the rates. If exact is true, DCHECKs on loss of
  // precision.
  static TimelineRate Product(TimelineRate a, TimelineRate b, bool exact = true) {
    uint32_t result_subject_delta;
    uint32_t result_reference_delta;
    Product(a.subject_delta(), a.reference_delta(), b.subject_delta(), b.reference_delta(),
            &result_subject_delta, &result_reference_delta, exact);
    return TimelineRate(result_subject_delta, result_reference_delta);
  }

  TimelineRate() : subject_delta_(0), reference_delta_(1) {}

  explicit TimelineRate(uint32_t subject_delta)
      : subject_delta_(subject_delta), reference_delta_(1) {}

  explicit TimelineRate(float rate_as_float)
      : subject_delta_(rate_as_float > 1.0f ? kFloatFactor
                                            : static_cast<uint32_t>(kFloatFactor * rate_as_float)),
        reference_delta_(rate_as_float > 1.0f ? static_cast<uint32_t>(kFloatFactor / rate_as_float)
                                              : kFloatFactor) {
    // The expressions above are intended to provide good precision for
    // 'reasonable' playback rate values (say in the range 0.0 to 4.0). The
    // expressions always produce a ratio of kFloatFactor and a number smaller
    // than kFloatFactor. kFloatFactor's value was chosen because floats have
    // a 23-bit mantissa, and operations with a larger factor would sacrifice
    // precision.
    ZX_DEBUG_ASSERT(rate_as_float >= 0.0f);
    Reduce(&subject_delta_, &reference_delta_);
  }

  TimelineRate(uint32_t subject_delta, uint32_t reference_delta)
      : subject_delta_(subject_delta), reference_delta_(reference_delta) {
    ZX_DEBUG_ASSERT(reference_delta != 0);
    Reduce(&subject_delta_, &reference_delta_);
  }

  // Determines whether this |TimelineRate| is invertible.
  bool invertible() const { return subject_delta_ != 0; }

  // Returns the inverse of the rate. DCHECKs if the subject_delta of this
  // rate is zero.
  TimelineRate Inverse() const {
    ZX_DEBUG_ASSERT(subject_delta_ != 0);

    // Note: TimelineRates should be always be in their reduced form.  Because
    // of this, we do not want to invoke the subject/reference constructor
    // (which will attempt to reduce the ratio).  Instead, use the default
    // constructor and just swap subject/reference.
    TimelineRate ret;
    ret.subject_delta_ = reference_delta_;
    ret.reference_delta_ = subject_delta_;
    return ret;
  }

  // Scales the value by this rate. Returns kOverflow on overflow.
  int64_t Scale(int64_t value) const { return Scale(value, subject_delta_, reference_delta_); }

  uint32_t subject_delta() const { return subject_delta_; }
  uint32_t reference_delta() const { return reference_delta_; }

 private:
  // A multiplier for float-to-TimelineRate conversions chosen because floats
  // have a 23-bit mantissa.
  static constexpr uint32_t kFloatFactor = 1ul << 23;

  uint32_t subject_delta_;
  uint32_t reference_delta_;
};

// Tests two rates for equality.
inline bool operator==(TimelineRate a, TimelineRate b) {
  return a.subject_delta() == b.subject_delta() && a.reference_delta() == b.reference_delta();
}

// Tests two rates for inequality.
inline bool operator!=(TimelineRate a, TimelineRate b) { return !(a == b); }

// Returns the ratio of the two rates. DCHECKs on loss of precision.
inline TimelineRate operator/(TimelineRate a, TimelineRate b) {
  return TimelineRate::Product(a, b.Inverse());
}

// Returns the product of the two rates. DCHECKs on loss of precision.
inline TimelineRate operator*(TimelineRate a, TimelineRate b) {
  return TimelineRate::Product(a, b);
}

// Returns the product of the rate and the int64_t. Returns kOverflow on
// overflow.
inline int64_t operator*(TimelineRate a, int64_t b) { return a.Scale(b); }

// Returns the product of the rate and the int64_t. Returns kOverflow on
// overflow.
inline int64_t operator*(int64_t a, TimelineRate b) { return b.Scale(a); }

// Returns the the int64_t divided by the rate. Returns kOverflow on
// overflow.
inline int64_t operator/(int64_t a, TimelineRate b) { return b.Inverse().Scale(a); }

}  // namespace media

#endif  // LIB_MEDIA_CPP_TIMELINE_RATE_H_

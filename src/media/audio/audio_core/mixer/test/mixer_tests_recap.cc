// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/media/audio/audio_core/mixer/test/audio_result.h"
#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"

namespace media::audio::test {

//
// These test functions, run after all other details tests have executed, producing a digest of the
// various audio fidelity measurements made.
//
TEST(Recap, FreqResp) {
  printf("\n Frequency Response");
  printf("\n   (in dB, with prior results)");
  const uint32_t num_freqs =
      (FrequencySet::UseFullFrequencySet ? FrequencySet::kNumInBandReferenceFreqs
                                         : FrequencySet::kSummaryIdxs.size());

  printf("\n\n   Point resampler\n       ");

  printf("                No SRC   ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("              191999->48k");
  }
  printf("               96k->48k  ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("              88.2k->48k ");
    printf("              44.1k->48k ");
  }
  printf("               24k->48k  ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("              12001->48k ");
    printf("               Micro-SRC ");
  }

  for (uint32_t idx = 0; idx < num_freqs; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6u Hz", FrequencySet::kRefFreqsTranslated[freq]);
    if (AudioResult::kPrevFreqRespPointUnity[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespPointUnity[freq],
             AudioResult::kPrevFreqRespPointUnity[freq]);
    } else {
      printf("                         ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespPointDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespPointDown0[freq],
               AudioResult::kPrevFreqRespPointDown0[freq]);
      } else {
        printf("                         ");
      }
    }

    if (AudioResult::kPrevFreqRespPointDown1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespPointDown1[freq],
             AudioResult::kPrevFreqRespPointDown1[freq]);
    } else {
      printf("                         ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespPointDown2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespPointDown2[freq],
               AudioResult::kPrevFreqRespPointDown2[freq]);
      } else {
        printf("                         ");
      }

      if (AudioResult::kPrevFreqRespPointUp1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespPointUp1[freq],
               AudioResult::kPrevFreqRespPointUp1[freq]);
      } else {
        printf("                         ");
      }
    }

    if (AudioResult::kPrevFreqRespPointUp2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespPointUp2[freq],
             AudioResult::kPrevFreqRespPointUp2[freq]);
    } else {
      printf("                         ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespPointUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespPointUp3[freq],
               AudioResult::kPrevFreqRespPointUp3[freq]);
      } else {
        printf("                         ");
      }

      if (AudioResult::kPrevFreqRespPointMicro[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespPointMicro[freq],
               AudioResult::kPrevFreqRespPointMicro[freq]);
      }
    }
  }

  printf("\n\n   Linear resampler\n       ");

  if (FrequencySet::UseFullFrequencySet) {
    printf("                No SRC   ");
    printf("              191999->48k");
    printf("               96k->48k  ");
  }
  printf("              88.2k->48k ");
  printf("              44.1k->48k ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("               24k->48k  ");
    printf("              12001->48k ");
  }
  printf("               Micro-SRC ");

  for (uint32_t idx = 0; idx < num_freqs; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %6u Hz", FrequencySet::kRefFreqsTranslated[freq]);

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespLinearUnity[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespLinearUnity[freq],
               AudioResult::kPrevFreqRespLinearUnity[freq]);
      } else {
        printf("                         ");
      }

      if (AudioResult::kPrevFreqRespLinearDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespLinearDown0[freq],
               AudioResult::kPrevFreqRespLinearDown0[freq]);
      } else {
        printf("                         ");
      }

      if (AudioResult::kPrevFreqRespLinearDown1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespLinearDown1[freq],
               AudioResult::kPrevFreqRespLinearDown1[freq]);
      } else {
        printf("                         ");
      }
    }

    if (AudioResult::kPrevFreqRespLinearDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespLinearDown2[freq],
             AudioResult::kPrevFreqRespLinearDown2[freq]);
    } else {
      printf("                         ");
    }

    if (AudioResult::kPrevFreqRespLinearUp1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespLinearUp1[freq],
             AudioResult::kPrevFreqRespLinearUp1[freq]);
    } else {
      printf("                         ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevFreqRespLinearUp2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespLinearUp2[freq],
               AudioResult::kPrevFreqRespLinearUp2[freq]);
      } else {
        printf("                         ");
      }

      if (AudioResult::kPrevFreqRespLinearUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespLinearUp3[freq],
               AudioResult::kPrevFreqRespLinearUp3[freq]);
      } else {
        printf("                         ");
      }
    }

    if (AudioResult::kPrevFreqRespLinearMicro[freq] != -std::numeric_limits<double>::infinity()) {
      printf("   %9.6lf  (%9.6lf)", AudioResult::FreqRespLinearMicro[freq],
             AudioResult::kPrevFreqRespLinearMicro[freq]);
    }
  }

  printf("\n\n");
}

TEST(Recap, SINAD) {
  printf("\n Signal-to-Noise-and-Distortion (SINAD)");
  printf("\n   (in dB, with prior results)");
  uint32_t num_freqs = (FrequencySet::UseFullFrequencySet ? FrequencySet::kNumInBandReferenceFreqs
                                                          : FrequencySet::kSummaryIdxs.size());

  printf("\n\n   Point resampler\n             ");

  printf("           No SRC   ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("         191999->48k");
  }
  printf("          96k->48k  ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("         88.2k->48k ");
    printf("         44.1k->48k ");
  }
  printf("          24k->48k  ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("         12001->48k ");
    printf("          Micro-SRC ");
  }

  for (uint32_t idx = 0; idx < num_freqs; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %8u Hz ", FrequencySet::kRefFreqsTranslated[freq]);
    if (AudioResult::kPrevSinadPointUnity[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadPointUnity[freq],
             AudioResult::kPrevSinadPointUnity[freq]);
    } else {
      printf("                    ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadPointDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("    %6.2lf  (%6.2lf)", AudioResult::SinadPointDown0[freq],
               AudioResult::kPrevSinadPointDown0[freq]);
      } else {
        printf("                    ");
      }
    }

    if (AudioResult::kPrevSinadPointDown1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadPointDown1[freq],
             AudioResult::kPrevSinadPointDown1[freq]);
    } else {
      printf("                    ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadPointDown2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("    %6.2lf  (%6.2lf)", AudioResult::SinadPointDown2[freq],
               AudioResult::kPrevSinadPointDown2[freq]);
      } else {
        printf("                    ");
      }

      if (AudioResult::kPrevSinadPointUp1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("    %6.2lf  (%6.2lf)", AudioResult::SinadPointUp1[freq],
               AudioResult::kPrevSinadPointUp1[freq]);
      } else {
        printf("                    ");
      }
    }

    if (AudioResult::kPrevSinadPointUp2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadPointUp2[freq],
             AudioResult::kPrevSinadPointUp2[freq]);
    } else {
      printf("                    ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadPointUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("    %6.2lf  (%6.2lf)", AudioResult::SinadPointUp3[freq],
               AudioResult::kPrevSinadPointUp3[freq]);
      } else {
        printf("                    ");
      }
      if (AudioResult::kPrevSinadPointMicro[freq] != -std::numeric_limits<double>::infinity()) {
        printf("    %6.2lf  (%6.2lf)", AudioResult::SinadPointMicro[freq],
               AudioResult::kPrevSinadPointMicro[freq]);
      }
    }
  }

  printf("\n\n   Linear resampler\n             ");

  if (FrequencySet::UseFullFrequencySet) {
    printf("           No SRC   ");
    printf("         191999->48k");
    printf("          96k->48k  ");
  }
  printf("         88.2k->48k ");
  printf("         44.1k->48k ");
  if (FrequencySet::UseFullFrequencySet) {
    printf("          24k->48k  ");
    printf("         12001->48k ");
  }
  printf("          Micro-SRC ");

  for (uint32_t idx = 0; idx < num_freqs; ++idx) {
    uint32_t freq = FrequencySet::UseFullFrequencySet ? idx : FrequencySet::kSummaryIdxs[idx];
    printf("\n   %8u Hz ", FrequencySet::kRefFreqsTranslated[freq]);

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadLinearUnity[freq] != -std::numeric_limits<double>::infinity()) {
        printf("    %6.2lf  (%6.2lf)", AudioResult::SinadLinearUnity[freq],
               AudioResult::kPrevSinadLinearUnity[freq]);
      } else {
        printf("                    ");
      }

      if (AudioResult::kPrevSinadLinearDown0[freq] != -std::numeric_limits<double>::infinity()) {
        printf("    %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown0[freq],
               AudioResult::kPrevSinadLinearDown0[freq]);
      } else {
        printf("                    ");
      }

      if (AudioResult::kPrevSinadLinearDown1[freq] != -std::numeric_limits<double>::infinity()) {
        printf("    %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown1[freq],
               AudioResult::kPrevSinadLinearDown1[freq]);
      } else {
        printf("                    ");
      }
    }

    if (AudioResult::kPrevSinadLinearDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown2[freq],
             AudioResult::kPrevSinadLinearDown2[freq]);
    } else {
      printf("                    ");
    }

    if (AudioResult::kPrevSinadLinearUp1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadLinearUp1[freq],
             AudioResult::kPrevSinadLinearUp1[freq]);
    } else {
      printf("                    ");
    }

    if (FrequencySet::UseFullFrequencySet) {
      if (AudioResult::kPrevSinadLinearUp2[freq] != -std::numeric_limits<double>::infinity()) {
        printf("    %6.2lf  (%6.2lf)", AudioResult::SinadLinearUp2[freq],
               AudioResult::kPrevSinadLinearUp2[freq]);
      } else {
        printf("                    ");
      }

      if (AudioResult::kPrevSinadLinearUp3[freq] != -std::numeric_limits<double>::infinity()) {
        printf("    %6.2lf  (%6.2lf)", AudioResult::SinadLinearUp3[freq],
               AudioResult::kPrevSinadLinearUp3[freq]);
      } else {
        printf("                    ");
      }
    }

    if (AudioResult::kPrevSinadLinearMicro[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadLinearMicro[freq],
             AudioResult::kPrevSinadLinearMicro[freq]);
    }
  }

  printf("\n\n");
}

TEST(Recap, Rejection) {
  printf("\n Out-of-band Rejection");
  printf("\n   (in dB, with prior results)");

  if (!FrequencySet::UseFullFrequencySet) {
    printf("\n\n   Results are not applicable, for summary testing\n\n");
    return;
  }

  uint32_t num_freqs = FrequencySet::kReferenceFreqs.size();
  uint32_t start_freq_idx = FrequencySet::kNumInBandReferenceFreqs;

  printf("\n\n   Point resampler\n             ");

  printf("         191999->48k");
  printf("          96k->48k  ");
  printf("         88.2k->48k ");

  for (uint32_t idx = start_freq_idx; idx < num_freqs; ++idx) {
    uint32_t freq = idx;

    printf("\n   %8u Hz ", FrequencySet::kRefFreqsTranslated[freq]);
    if (AudioResult::kPrevSinadPointDown0[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadPointDown0[freq],
             AudioResult::kPrevSinadPointDown0[freq]);
    } else {
      printf("                    ");
    }

    if (AudioResult::kPrevSinadPointDown1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadPointDown1[freq],
             AudioResult::kPrevSinadPointDown1[freq]);
    } else {
      printf("                    ");
    }

    if (AudioResult::kPrevSinadPointDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadPointDown2[freq],
             AudioResult::kPrevSinadPointDown2[freq]);
    } else {
      printf("                    ");
    }
  }

  printf("\n\n   Linear resampler\n             ");

  printf("         191999->48k");
  printf("          96k->48k  ");
  printf("         88.2k->48k ");

  for (uint32_t idx = start_freq_idx; idx < num_freqs; ++idx) {
    uint32_t freq = idx;
    printf("\n   %8u Hz ", FrequencySet::kRefFreqsTranslated[freq]);

    if (AudioResult::kPrevSinadLinearDown0[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown0[freq],
             AudioResult::kPrevSinadLinearDown0[freq]);
    } else {
      printf("                    ");
    }

    if (AudioResult::kPrevSinadLinearDown1[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown1[freq],
             AudioResult::kPrevSinadLinearDown1[freq]);
    } else {
      printf("                    ");
    }

    if (AudioResult::kPrevSinadLinearDown2[freq] != -std::numeric_limits<double>::infinity()) {
      printf("    %6.2lf  (%6.2lf)", AudioResult::SinadLinearDown2[freq],
             AudioResult::kPrevSinadLinearDown2[freq]);
    } else {
      printf("                    ");
    }
  }

  printf("\n\n");
}

//
// Display our baseline noise floor measurements, in decibels below full-scale
//
// 'Source' noise floor is the demonstrated best-case background noise when
// accepting audio (from an AudioRenderer or audio Input device, for example).
// 'Output' noise floor is the demonstrated best-case background noise when
// emitting audio (to an audio Output device or AudioCapturer, for example).
TEST(Recap, NoiseFloor) {
  printf("\n Best-case noise-floor");
  printf("\n   (in dB, with prior results)");

  printf("\n\n   Sources");
  printf("\n\t      8-bit    ");
  printf("         16-bit    ");
  printf("         24-bit    ");
  printf("          Float");
  printf("\n\t %6.2lf  (%6.2lf)   %6.2lf  (%6.2lf)  ", AudioResult::FloorSource8,
         AudioResult::kPrevFloorSource8, AudioResult::FloorSource16,
         AudioResult::kPrevFloorSource16);
  printf(" %6.2lf  (%6.2lf)   %6.2lf  (%6.2lf)", AudioResult::FloorSource24,
         AudioResult::kPrevFloorSource24, AudioResult::FloorSourceFloat,
         AudioResult::kPrevFloorSourceFloat);

  printf("\n\n   Mix Floor");
  printf("\n\t      8-bit    ");
  printf("         16-bit    ");
  printf("         24-bit    ");
  printf("          Float    ");
  printf("      Stereo->Mono");
  printf("\n\t %6.2lf  (%6.2lf)   %6.2lf  (%6.2lf)   %6.2lf  (%6.2lf)  ", AudioResult::FloorMix8,
         AudioResult::kPrevFloorMix8, AudioResult::FloorMix16, AudioResult::kPrevFloorMix16,
         AudioResult::FloorMix24, AudioResult::kPrevFloorMix24);
  printf(" %6.2lf  (%6.2lf)   %6.2lf  (%6.2lf)", AudioResult::FloorMixFloat,
         AudioResult::kPrevFloorMixFloat, AudioResult::FloorStereoMono,
         AudioResult::kPrevFloorStereoMono);

  printf("\n\n   Outputs");
  printf("\n\t      8-bit    ");
  printf("         16-bit    ");
  printf("         24-bit    ");
  printf("          Float");
  printf("\n\t %6.2lf  (%6.2lf)   %6.2lf  (%6.2lf)  ", AudioResult::FloorOutput8,
         AudioResult::kPrevFloorOutput8, AudioResult::FloorOutput16,
         AudioResult::kPrevFloorOutput16);
  printf(" %6.2lf  (%6.2lf)   %6.2lf  (%6.2lf)", AudioResult::FloorOutput24,
         AudioResult::kPrevFloorOutput24, AudioResult::FloorOutputFloat,
         AudioResult::kPrevFloorOutputFloat);

  printf("\n\n");
}

//
// Display our gain sensitivity and dynamic range, in decibels
//
TEST(Recap, DynamicRange) {
  printf("\n Dynamic Range");
  printf("\n   (in dB, with prior results)");

  printf("\n\n      Input Gain       Mixed Result          Usable Range\n");
  printf("\n     %9.6lf  %10.6lf ( > %9.6lf)   %6.2lf (%6.2lf)", AudioResult::kMaxGainDbNonUnity,
         AudioResult::LevelEpsilonDown, AudioResult::kPrevLevelEpsilonDown,
         AudioResult::SinadEpsilonDown, AudioResult::kPrevSinadEpsilonDown);
  printf("\n    -30.0000    %8.4lf   (+/- %6.4lf  )   %6.2lf (%6.2lf)", AudioResult::Level30Down,
         AudioResult::kPrevDynRangeTolerance, AudioResult::Sinad30Down,
         AudioResult::kPrevSinad30Down);
  printf("\n    -60.0000    %8.4lf   (+/- %6.4lf  )   %6.2lf (%6.2lf)", AudioResult::Level60Down,
         AudioResult::kPrevDynRangeTolerance, AudioResult::Sinad60Down,
         AudioResult::kPrevSinad60Down);
  printf("\n    -90.0000    %8.4lf   (+/- %6.4lf  )   %6.2lf (%6.2lf)", AudioResult::Level90Down,
         AudioResult::kPrevDynRangeTolerance, AudioResult::Sinad90Down,
         AudioResult::kPrevSinad90Down);
  printf("\n\n");
}

}  // namespace media::audio::test

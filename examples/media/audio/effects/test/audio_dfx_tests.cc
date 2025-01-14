// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>

#include <cmath>

#include "examples/media/audio/effects/dfx_base.h"
#include "examples/media/audio/effects/dfx_delay.h"
#include "examples/media/audio/effects/dfx_rechannel.h"
#include "examples/media/audio/effects/dfx_swap.h"
#include "gtest/gtest.h"
#include "src/media/audio/lib/effects_loader/effects_loader.h"
#include "src/media/audio/lib/effects_loader/effects_processor.h"

namespace media {

namespace audio {
// We override this method so that we can name our test library differently than
// the hard-coded "audiofx.so" that effects_loader always loads into audio_core.
class TestEffectsLoader : public EffectsLoader {
 public:
  void* OpenLoadableModuleBinary() override {
    return dlopen("audio_dfx.so", RTLD_LAZY | RTLD_GLOBAL);
  }
};
}  // namespace audio

namespace audio_dfx_test {

//
// Tests EffectsLoader, which directly calls the shared library interface.
//
class EffectsLoaderTest : public testing::Test {
 protected:
  audio::TestEffectsLoader effects_loader_;

  void SetUp() override { ASSERT_EQ(effects_loader_.LoadLibrary(), ZX_OK); }
  void TearDown() override { effects_loader_.UnloadLibrary(); }
};

//
// These child classes may not differentiate, but we use different classes for
// Delay/Rechannel/Swap in order to group related test results accordingly.
//
class DelayEffectTest : public EffectsLoaderTest {
 protected:
  void TestDelayBounds(uint32_t frame_rate, uint32_t channels, uint32_t delay_frames);
};
class RechannelEffectTest : public EffectsLoaderTest {};
class SwapEffectTest : public EffectsLoaderTest {};

// When validating controls, we make certain assumptions about the test effects.
static_assert(DfxDelay::kNumControls > 0, "DfxDelay must have controls");
static_assert(DfxRechannel::kNumControls == 0, "DfxRechannel must have no controls");
static_assert(DfxSwap::kNumControls == 0, "DfxSwap must have no controls");

// We test the delay effect with certain control values, making assumptions
// about how those values relate to the allowed range for this DFX.
constexpr float kTestDelay1 = 1.0f;
constexpr float kTestDelay2 = 2.0f;
static_assert(DfxDelay::kMaxDelayFrames >= kTestDelay2, "Test value too high");
static_assert(DfxDelay::kMinDelayFrames <= kTestDelay1, "Test value too low");
static_assert(DfxDelay::kInitialDelayFrames != kTestDelay1,
              "kTestDelay1 must not equal kInitialDelayFrames");
static_assert(DfxDelay::kInitialDelayFrames != kTestDelay2,
              "kTestDelay2 must not equal kInitialDelayFrames");

// For the most part, the below tests use a specific channel_count.
constexpr uint16_t kTestChans = 2;

// When testing or using the delay effect, we make certain channel assumptions.
static_assert(DfxDelay::kNumChannelsIn == kTestChans ||
                  DfxDelay::kNumChannelsIn == FUCHSIA_AUDIO_DFX_CHANNELS_ANY,
              "DfxDelay::kNumChannelsIn must match kTestChans");
static_assert(DfxDelay::kNumChannelsOut == kTestChans ||
                  DfxDelay::kNumChannelsOut == FUCHSIA_AUDIO_DFX_CHANNELS_ANY ||
                  DfxDelay::kNumChannelsOut == FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN,
              "DfxDelay::kNumChannelsOut must match kTestChans");

// When testing or using rechannel effect, we make certain channel assumptions.
static_assert(DfxRechannel::kNumChannelsIn != 2 || DfxRechannel::kNumChannelsOut != 2,
              "DfxRechannel must not be stereo-in/-out");
static_assert(DfxRechannel::kNumChannelsIn != DfxRechannel::kNumChannelsOut &&
                  DfxRechannel::kNumChannelsOut != FUCHSIA_AUDIO_DFX_CHANNELS_ANY &&
                  DfxRechannel::kNumChannelsOut != FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN,
              "DfxRechannel must not be in-place");

// When testing or using the swap effect, we make certain channel assumptions.
static_assert(DfxSwap::kNumChannelsIn == kTestChans ||
                  DfxSwap::kNumChannelsIn == FUCHSIA_AUDIO_DFX_CHANNELS_ANY,
              "DfxSwap::kNumChannelsIn must match kTestChans");
static_assert(DfxSwap::kNumChannelsOut == kTestChans ||
                  DfxSwap::kNumChannelsOut == FUCHSIA_AUDIO_DFX_CHANNELS_ANY ||
                  DfxSwap::kNumChannelsOut == FUCHSIA_AUDIO_DFX_CHANNELS_SAME_AS_IN,
              "DfxSwap::kNumChannelsOut must match kTestChans");

// Tests the get_parameters ABI, and that the test DFX behaves as expected.
TEST_F(DelayEffectTest, GetParameters) {
  fuchsia_audio_dfx_parameters device_fx_params;

  uint32_t frame_rate = 48000;
  fx_token_t dfx_token =
      effects_loader_.CreateFx(Effect::Delay, frame_rate, kTestChans, kTestChans);

  EXPECT_EQ(effects_loader_.FxGetParameters(dfx_token, &device_fx_params), ZX_OK);
  EXPECT_EQ(device_fx_params.frame_rate, frame_rate);
  EXPECT_EQ(device_fx_params.channels_in, kTestChans);
  EXPECT_EQ(device_fx_params.channels_out, kTestChans);
  EXPECT_TRUE(device_fx_params.signal_latency_frames == DfxDelay::kLatencyFrames);
  EXPECT_TRUE(device_fx_params.suggested_frames_per_buffer == DfxDelay::kLatencyFrames);

  // Verify invalid device token
  EXPECT_NE(effects_loader_.FxGetParameters(FUCHSIA_AUDIO_DFX_INVALID_TOKEN, &device_fx_params),
            ZX_OK);

  // Verify null struct*
  EXPECT_NE(effects_loader_.FxGetParameters(dfx_token, nullptr), ZX_OK);

  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests the get_parameters ABI, and that the test DFX behaves as expected.
TEST_F(RechannelEffectTest, GetParameters) {
  fuchsia_audio_dfx_parameters device_fx_params;

  uint32_t frame_rate = 48000;
  fx_token_t dfx_token = effects_loader_.CreateFx(
      Effect::Rechannel, frame_rate, DfxRechannel::kNumChannelsIn, DfxRechannel::kNumChannelsOut);
  device_fx_params.frame_rate = 44100;  // should be overwritten

  EXPECT_EQ(effects_loader_.FxGetParameters(dfx_token, &device_fx_params), ZX_OK);
  EXPECT_EQ(device_fx_params.frame_rate, frame_rate);
  EXPECT_TRUE(device_fx_params.channels_in == DfxRechannel::kNumChannelsIn);
  EXPECT_TRUE(device_fx_params.channels_out == DfxRechannel::kNumChannelsOut);
  EXPECT_TRUE(device_fx_params.signal_latency_frames == DfxRechannel::kLatencyFrames);
  EXPECT_TRUE(device_fx_params.suggested_frames_per_buffer == DfxRechannel::kLatencyFrames);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests the get_parameters ABI, and that the test DFX behaves as expected.
TEST_F(SwapEffectTest, GetParameters) {
  fuchsia_audio_dfx_parameters device_fx_params;

  uint32_t frame_rate = 44100;
  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Swap, frame_rate, kTestChans, kTestChans);
  device_fx_params.frame_rate = 48000;  // should be overwritten

  EXPECT_EQ(effects_loader_.FxGetParameters(dfx_token, &device_fx_params), ZX_OK);
  EXPECT_EQ(device_fx_params.frame_rate, frame_rate);
  EXPECT_EQ(device_fx_params.channels_in, kTestChans);
  EXPECT_EQ(device_fx_params.channels_out, kTestChans);
  EXPECT_TRUE(device_fx_params.signal_latency_frames == DfxSwap::kLatencyFrames);
  EXPECT_TRUE(device_fx_params.suggested_frames_per_buffer == DfxSwap::kLatencyFrames);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests the get_control_value ABI, and that the test DFX behaves as expected.
TEST_F(DelayEffectTest, GetControlValue) {
  uint16_t control_num = 0;
  fuchsia_audio_dfx_description dfx_desc;
  fuchsia_audio_dfx_control_description dfx_control_desc;

  ASSERT_EQ(effects_loader_.GetFxInfo(Effect::Delay, &dfx_desc), ZX_OK);
  ASSERT_GT(dfx_desc.num_controls, control_num);
  ASSERT_EQ(effects_loader_.GetFxControlInfo(Effect::Delay, control_num, &dfx_control_desc), ZX_OK);

  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Delay, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  float val;
  EXPECT_EQ(effects_loader_.FxGetControlValue(dfx_token, control_num, &val), ZX_OK);

  EXPECT_GE(val, dfx_control_desc.min_val);
  EXPECT_LE(val, dfx_control_desc.max_val);
  EXPECT_EQ(val, dfx_control_desc.initial_val);

  // Verify invalid effect token
  EXPECT_NE(effects_loader_.FxGetControlValue(FUCHSIA_AUDIO_DFX_INVALID_TOKEN, control_num, &val),
            ZX_OK);
  // Verify control out of range
  EXPECT_NE(effects_loader_.FxGetControlValue(dfx_token, dfx_desc.num_controls, &val), ZX_OK);
  // Verify null out_param
  EXPECT_NE(effects_loader_.FxGetControlValue(dfx_token, control_num, nullptr), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests cases in which we expect get_control_value to fail.
TEST_F(RechannelEffectTest, GetControlValue) {
  float val;
  fx_token_t dfx_token = effects_loader_.CreateFx(
      Effect::Rechannel, 48000, DfxRechannel::kNumChannelsIn, DfxRechannel::kNumChannelsOut);

  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_NE(effects_loader_.FxGetControlValue(dfx_token, 0, &val), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests cases in which we expect get_control_value to fail.
TEST_F(SwapEffectTest, GetControlValue) {
  float val;
  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Swap, 48000, kTestChans, kTestChans);

  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_NE(effects_loader_.FxGetControlValue(dfx_token, 0, &val), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests the set_control_value ABI, and that the test DFX behaves as expected.
TEST_F(DelayEffectTest, SetControlValue) {
  uint16_t control_num = 0;
  fuchsia_audio_dfx_description dfx_desc;
  fuchsia_audio_dfx_control_description dfx_control_desc;

  ASSERT_EQ(effects_loader_.GetFxInfo(Effect::Delay, &dfx_desc), ZX_OK);
  ASSERT_GT(dfx_desc.num_controls, control_num);
  ASSERT_EQ(effects_loader_.GetFxControlInfo(Effect::Delay, control_num, &dfx_control_desc), ZX_OK);

  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Delay, 48000, kTestChans, kTestChans);

  EXPECT_EQ(effects_loader_.FxSetControlValue(dfx_token, control_num, kTestDelay1), ZX_OK);

  float new_value;
  EXPECT_EQ(effects_loader_.FxGetControlValue(dfx_token, control_num, &new_value), ZX_OK);
  EXPECT_EQ(new_value, kTestDelay1);

  // Verify invalid effect token
  EXPECT_NE(
      effects_loader_.FxSetControlValue(FUCHSIA_AUDIO_DFX_INVALID_TOKEN, control_num, kTestDelay1),
      ZX_OK);
  // Verify control out of range
  EXPECT_NE(effects_loader_.FxSetControlValue(dfx_token, dfx_desc.num_controls, kTestDelay1),
            ZX_OK);
  // Verify value out of range
  EXPECT_NE(effects_loader_.FxSetControlValue(dfx_token, control_num, dfx_control_desc.max_val + 1),
            ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests cases in which we expect set_control_value to fail.
TEST_F(RechannelEffectTest, SetControlValue) {
  fx_token_t dfx_token = effects_loader_.CreateFx(
      Effect::Rechannel, 48000, DfxRechannel::kNumChannelsIn, DfxRechannel::kNumChannelsOut);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_NE(effects_loader_.FxSetControlValue(dfx_token, 0, 0), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests cases in which we expect set_control_value to fail.
TEST_F(SwapEffectTest, SetControlValue) {
  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Swap, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_NE(effects_loader_.FxSetControlValue(dfx_token, 0, 0), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests the reset ABI, and that DFX discards state and control values.
TEST_F(DelayEffectTest, Reset) {
  uint16_t control_num = 0;
  fuchsia_audio_dfx_description dfx_desc;
  fuchsia_audio_dfx_control_description dfx_control_desc;

  ASSERT_EQ(effects_loader_.GetFxInfo(Effect::Delay, &dfx_desc), ZX_OK);
  ASSERT_GT(dfx_desc.num_controls, control_num);
  ASSERT_EQ(effects_loader_.GetFxControlInfo(Effect::Delay, control_num, &dfx_control_desc), ZX_OK);

  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Delay, 48000, kTestChans, kTestChans);

  float new_value;
  ASSERT_EQ(effects_loader_.FxGetControlValue(dfx_token, control_num, &new_value), ZX_OK);
  EXPECT_NE(new_value, kTestDelay1);

  ASSERT_EQ(effects_loader_.FxSetControlValue(dfx_token, control_num, kTestDelay1), ZX_OK);
  ASSERT_EQ(effects_loader_.FxGetControlValue(dfx_token, control_num, &new_value), ZX_OK);
  ASSERT_EQ(new_value, kTestDelay1);

  EXPECT_EQ(effects_loader_.FxReset(dfx_token), ZX_OK);
  EXPECT_EQ(effects_loader_.FxGetControlValue(dfx_token, control_num, &new_value), ZX_OK);
  EXPECT_NE(new_value, kTestDelay1);
  EXPECT_TRUE(new_value == DfxDelay::kInitialDelayFrames);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);

  // Verify invalid effect token
  EXPECT_NE(effects_loader_.FxReset(FUCHSIA_AUDIO_DFX_INVALID_TOKEN), ZX_OK);
}

// Tests the process_inplace ABI, and that the test DFX behaves as expected.
TEST_F(DelayEffectTest, ProcessInPlace) {
  uint32_t num_samples = 12 * kTestChans;
  uint32_t delay_samples = 6 * kTestChans;
  float delay_buff_in_out[num_samples];
  float expect[num_samples];

  for (uint32_t i = 0; i < delay_samples; ++i) {
    delay_buff_in_out[i] = static_cast<float>(i + 1);
    expect[i] = 0.0f;
  }
  for (uint32_t i = delay_samples; i < num_samples; ++i) {
    delay_buff_in_out[i] = static_cast<float>(i + 1);
    expect[i] = delay_buff_in_out[i - delay_samples];
  }

  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Delay, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  uint16_t control_num = 0;
  ASSERT_EQ(effects_loader_.FxSetControlValue(dfx_token, control_num,
                                              static_cast<float>(delay_samples) / kTestChans),
            ZX_OK);

  EXPECT_EQ(effects_loader_.FxProcessInPlace(dfx_token, 4, delay_buff_in_out), ZX_OK);
  EXPECT_EQ(effects_loader_.FxProcessInPlace(dfx_token, 4, delay_buff_in_out + (4 * kTestChans)),
            ZX_OK);
  EXPECT_EQ(effects_loader_.FxProcessInPlace(dfx_token, 4, delay_buff_in_out + (8 * kTestChans)),
            ZX_OK);

  for (uint32_t sample = 0; sample < num_samples; ++sample) {
    EXPECT_EQ(delay_buff_in_out[sample], expect[sample]) << sample;
  }
  EXPECT_EQ(effects_loader_.FxProcessInPlace(dfx_token, 0, delay_buff_in_out), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests cases in which we expect process_inplace to fail.
TEST_F(RechannelEffectTest, ProcessInPlace) {
  constexpr uint32_t kNumFrames = 1;
  float buff_in_out[kNumFrames * DfxRechannel::kNumChannelsIn] = {0};

  // Effects that change the channelization should not process in-place.
  fx_token_t dfx_token = effects_loader_.CreateFx(
      Effect::Rechannel, 48000, DfxRechannel::kNumChannelsIn, DfxRechannel::kNumChannelsOut);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_NE(effects_loader_.FxProcessInPlace(dfx_token, kNumFrames, buff_in_out), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests the process_inplace ABI, and that the test DFX behaves as expected.
TEST_F(SwapEffectTest, ProcessInPlace) {
  constexpr uint32_t kNumFrames = 4;
  float swap_buff_in_out[kNumFrames * kTestChans] = {1.0f, -1.0f, 1.0f, -1.0f,
                                                     1.0f, -1.0f, 1.0f, -1.0f};

  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Swap, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  EXPECT_EQ(effects_loader_.FxProcessInPlace(dfx_token, kNumFrames, swap_buff_in_out), ZX_OK);
  for (uint32_t sample_num = 0; sample_num < kNumFrames * kTestChans; ++sample_num) {
    EXPECT_EQ(swap_buff_in_out[sample_num], (sample_num % 2 ? 1.0f : -1.0f));
  }

  EXPECT_EQ(effects_loader_.FxProcessInPlace(dfx_token, 0, swap_buff_in_out), ZX_OK);

  // Calls with invalid token or null buff_ptr should fail.
  EXPECT_NE(effects_loader_.FxProcessInPlace(FUCHSIA_AUDIO_DFX_INVALID_TOKEN, kNumFrames,
                                             swap_buff_in_out),
            ZX_OK);
  EXPECT_NE(effects_loader_.FxProcessInPlace(dfx_token, kNumFrames, nullptr), ZX_OK);
  EXPECT_NE(effects_loader_.FxProcessInPlace(dfx_token, 0, nullptr), ZX_OK);

  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests cases in which we expect process to fail.
TEST_F(DelayEffectTest, Process) {
  constexpr uint32_t kNumFrames = 1;
  float audio_buff_in[kNumFrames * kTestChans] = {0.0f};
  float audio_buff_out[kNumFrames * kTestChans] = {0.0f};

  // These stereo-to-stereo effects should ONLY process in-place
  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Delay, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_NE(effects_loader_.FxProcess(dfx_token, kNumFrames, audio_buff_in, audio_buff_out), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests the process ABI, and that the test DFX behaves as expected.
TEST_F(RechannelEffectTest, Process) {
  constexpr uint32_t kNumFrames = 1;
  float audio_buff_in[kNumFrames * DfxRechannel::kNumChannelsIn] = {
      1.0f, -1.0f, 0.25f, -1.0f, 0.98765432f, -0.09876544f};
  float audio_buff_out[kNumFrames * DfxRechannel::kNumChannelsOut] = {0.0f};
  float expected[kNumFrames * DfxRechannel::kNumChannelsOut] = {0.799536645f, -0.340580851f};

  fx_token_t dfx_token = effects_loader_.CreateFx(
      Effect::Rechannel, 48000, DfxRechannel::kNumChannelsIn, DfxRechannel::kNumChannelsOut);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  EXPECT_EQ(effects_loader_.FxProcess(dfx_token, kNumFrames, audio_buff_in, audio_buff_out), ZX_OK);
  EXPECT_EQ(audio_buff_out[0], expected[0]) << std::setprecision(9) << audio_buff_out[0];
  EXPECT_EQ(audio_buff_out[1], expected[1]) << std::setprecision(9) << audio_buff_out[1];

  EXPECT_EQ(effects_loader_.FxProcess(dfx_token, 0, audio_buff_in, audio_buff_out), ZX_OK);

  // Test null token, buffer_in, buffer_out
  EXPECT_NE(effects_loader_.FxProcess(FUCHSIA_AUDIO_DFX_INVALID_TOKEN, kNumFrames, audio_buff_in,
                                      audio_buff_out),
            ZX_OK);
  EXPECT_NE(effects_loader_.FxProcess(dfx_token, kNumFrames, nullptr, audio_buff_out), ZX_OK);
  EXPECT_NE(effects_loader_.FxProcess(dfx_token, kNumFrames, audio_buff_in, nullptr), ZX_OK);
  EXPECT_NE(effects_loader_.FxProcess(dfx_token, 0, nullptr, audio_buff_out), ZX_OK);
  EXPECT_NE(effects_loader_.FxProcess(dfx_token, 0, audio_buff_in, nullptr), ZX_OK);

  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests cases in which we expect process to fail.
TEST_F(SwapEffectTest, Process) {
  constexpr uint32_t kNumFrames = 1;
  float audio_buff_in[kNumFrames * kTestChans] = {0.0f};
  float audio_buff_out[kNumFrames * kTestChans] = {0.0f};

  // These stereo-to-stereo effects should ONLY process in-place
  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Swap, 48000, kTestChans, kTestChans);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  EXPECT_NE(effects_loader_.FxProcess(dfx_token, kNumFrames, audio_buff_in, audio_buff_out), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Tests the process_inplace ABI thru successive in-place calls.
TEST_F(DelayEffectTest, ProcessInPlace_Chain) {
  constexpr uint32_t kNumFrames = 6;

  float buff_in_out[kNumFrames * kTestChans] = {1.0f,  -0.1f, -0.2f, 2.0f,  0.3f,  -3.0f,
                                                -4.0f, 0.4f,  5.0f,  -0.5f, -0.6f, 6.0f};
  float expected[kNumFrames * kTestChans] = {0.0f,  0.0f, 0.0f, 0.0f,  0.0f,  0.0f,
                                             -0.1f, 1.0f, 2.0f, -0.2f, -3.0f, 0.3f};

  fx_token_t delay1_token, swap_token, delay2_token;
  delay1_token = effects_loader_.CreateFx(Effect::Delay, 44100, kTestChans, kTestChans);
  swap_token = effects_loader_.CreateFx(Effect::Swap, 44100, kTestChans, kTestChans);
  delay2_token = effects_loader_.CreateFx(Effect::Delay, 44100, kTestChans, kTestChans);

  ASSERT_NE(delay1_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  ASSERT_NE(swap_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);
  ASSERT_NE(delay2_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  uint16_t control_num = 0;
  ASSERT_EQ(effects_loader_.FxSetControlValue(delay1_token, control_num, kTestDelay1), ZX_OK);
  ASSERT_EQ(effects_loader_.FxSetControlValue(delay2_token, control_num, kTestDelay2), ZX_OK);

  EXPECT_EQ(effects_loader_.FxProcessInPlace(delay1_token, kNumFrames, buff_in_out), ZX_OK);
  EXPECT_EQ(effects_loader_.FxProcessInPlace(swap_token, kNumFrames, buff_in_out), ZX_OK);
  EXPECT_EQ(effects_loader_.FxProcessInPlace(delay2_token, kNumFrames, buff_in_out), ZX_OK);
  for (uint32_t sample_num = 0; sample_num < kNumFrames * kTestChans; ++sample_num) {
    EXPECT_EQ(buff_in_out[sample_num], expected[sample_num]) << sample_num;
  }

  EXPECT_EQ(effects_loader_.FxProcessInPlace(delay2_token, 0, buff_in_out), ZX_OK);
  EXPECT_EQ(effects_loader_.FxProcessInPlace(swap_token, 0, buff_in_out), ZX_OK);
  EXPECT_EQ(effects_loader_.FxProcessInPlace(delay1_token, 0, buff_in_out), ZX_OK);

  EXPECT_EQ(effects_loader_.DeleteFx(delay2_token), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(swap_token), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(delay1_token), ZX_OK);
}

// Tests the flush ABI, and that DFX discards state but retains control values.
TEST_F(DelayEffectTest, Flush) {
  constexpr uint32_t kNumFrames = 1;
  float buff_in_out[kTestChans] = {1.0f, -1.0f};

  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Delay, 44100, kTestChans, kTestChans);

  float new_value;
  ASSERT_EQ(effects_loader_.FxGetControlValue(dfx_token, 0, &new_value), ZX_OK);
  EXPECT_NE(new_value, kTestDelay1);

  ASSERT_EQ(effects_loader_.FxSetControlValue(dfx_token, 0, kTestDelay1), ZX_OK);
  ASSERT_EQ(effects_loader_.FxGetControlValue(dfx_token, 0, &new_value), ZX_OK);
  ASSERT_EQ(new_value, kTestDelay1);

  ASSERT_EQ(effects_loader_.FxProcessInPlace(dfx_token, kNumFrames, buff_in_out), ZX_OK);
  ASSERT_EQ(buff_in_out[0], 0.0f);

  EXPECT_EQ(effects_loader_.FxFlush(dfx_token), ZX_OK);

  // Validate that settings are retained after Flush.
  EXPECT_EQ(effects_loader_.FxGetControlValue(dfx_token, 0, &new_value), ZX_OK);
  EXPECT_EQ(new_value, kTestDelay1);

  // Validate that cached samples are flushed.
  EXPECT_EQ(effects_loader_.FxProcessInPlace(dfx_token, kNumFrames, buff_in_out), ZX_OK);
  EXPECT_EQ(buff_in_out[0], 0.0f);

  // Verify invalid effect token
  EXPECT_NE(effects_loader_.FxFlush(FUCHSIA_AUDIO_DFX_INVALID_TOKEN), ZX_OK);
  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

//
// We use this subfunction to test the outer limits allowed by ProcessInPlace.
void DelayEffectTest::TestDelayBounds(uint32_t frame_rate, uint32_t channels,
                                      uint32_t delay_frames) {
  uint32_t delay_samples = delay_frames * channels;
  uint32_t num_frames = frame_rate;
  uint32_t num_samples = num_frames * channels;

  std::unique_ptr<float[]> delay_buff_in_out = std::make_unique<float[]>(num_samples);
  std::unique_ptr<float[]> expect = std::make_unique<float[]>(num_samples);

  fx_token_t dfx_token = effects_loader_.CreateFx(Effect::Delay, frame_rate, channels, channels);
  ASSERT_NE(dfx_token, FUCHSIA_AUDIO_DFX_INVALID_TOKEN);

  ASSERT_EQ(effects_loader_.FxSetControlValue(dfx_token, 0, static_cast<float>(delay_frames)),
            ZX_OK);

  for (uint32_t pass = 0; pass < 2; ++pass) {
    for (uint32_t i = 0; i < num_samples; ++i) {
      delay_buff_in_out[i] = static_cast<float>(i + pass * num_samples + 1);
      expect[i] = fmax(delay_buff_in_out[i] - delay_samples, 0.0f);
    }
    EXPECT_EQ(effects_loader_.FxProcessInPlace(dfx_token, num_frames, delay_buff_in_out.get()),
              ZX_OK);
    for (uint32_t sample = 0; sample < num_samples; ++sample) {
      EXPECT_EQ(delay_buff_in_out[sample], expect[sample]) << sample;
    }
  }

  EXPECT_EQ(effects_loader_.DeleteFx(dfx_token), ZX_OK);
}

// Verifies DfxDelay at the outer allowed bounds (largest delays and buffers).
TEST_F(DelayEffectTest, ProcessInPlace_Bounds) {
  TestDelayBounds(192000, 2, DfxDelay::kMaxDelayFrames);
  TestDelayBounds(2000, FUCHSIA_AUDIO_DFX_CHANNELS_MAX, DfxDelay::kMaxDelayFrames);
}

}  // namespace audio_dfx_test
}  // namespace media

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_sbc_encoder.h"

#include <lib/media/codec_impl/codec_buffer.h>

namespace {

// A client using the min shouldn't necessarily expect performance to be
// acceptable when running higher bit-rates.
constexpr uint32_t kInputPerPacketBufferBytesMin = SBC_MAX_PCM_BUFFER_SIZE;
// This is an arbitrary cap for now.
constexpr uint32_t kInputPerPacketBufferBytesMax = 4 * 1024 * 1024;

// For now, this is the forced packet count for output.
static constexpr uint32_t kOutputPacketCount = 21;

constexpr char kSbcMimeType[] = "audio/sbc";

}  // namespace

CodecAdapterSbcEncoder::CodecAdapterSbcEncoder(
    std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
    : CodecAdapterSW(lock, codec_adapter_events) {}

CodecAdapterSbcEncoder::~CodecAdapterSbcEncoder() = default;

void CodecAdapterSbcEncoder::ProcessInputLoop() {
  std::optional<CodecInputItem> maybe_input_item;
  while ((maybe_input_item = input_queue_.WaitForElement())) {
    CodecInputItem input_item = std::move(maybe_input_item.value());
    if (input_item.is_format_details()) {
      if (context_) {
        events_->onCoreCodecFailCodec(
            "Midstream input format change is not supported.");
        return;
      }

      if (CreateContext(std::move(input_item.format_details())) != kOk) {
        // Creation failed; a failure was reported through `events_`.
        return;
      }

      events_->onCoreCodecMidStreamOutputConstraintsChange(
          /*output_re_config_required=*/true);
    } else if (input_item.is_end_of_stream()) {
      ZX_DEBUG_ASSERT(context_);
      if (EncodeInput(nullptr) == kShouldTerminate) {
        // A failure was reported through `events_` or the stream was stopped.
        return;
      }
      events_->onCoreCodecOutputEndOfStream(/*error_detected_before=*/false);
    } else if (input_item.is_packet()) {
      ZX_DEBUG_ASSERT(context_);

      if (EncodeInput(input_item.packet()) == kShouldTerminate) {
        // A failure was reported through `events_` or the stream was stopped.
        return;
      }
    }
  }
}

void CodecAdapterSbcEncoder::CleanUpAfterStream() { context_ = std::nullopt; }

std::pair<fuchsia::media::FormatDetails, size_t>
CodecAdapterSbcEncoder::OutputFormatDetails() {
  FXL_DCHECK(context_);
  fuchsia::media::AudioCompressedFormatSbc sbc;
  fuchsia::media::AudioCompressedFormat compressed_format;
  compressed_format.set_sbc(std::move(sbc));

  fuchsia::media::AudioFormat audio_format;
  audio_format.set_compressed(std::move(compressed_format));

  fuchsia::media::FormatDetails format_details;
  format_details.set_mime_type(kSbcMimeType);
  format_details.mutable_domain()->set_audio(std::move(audio_format));

  return {std::move(format_details), context_->sbc_frame_length()};
}

fuchsia::sysmem::BufferCollectionConstraints
CodecAdapterSbcEncoder::CoreCodecGetBufferCollectionConstraints(
    CodecPort port,
    const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  std::lock_guard<std::mutex> lock(lock_);

  fuchsia::sysmem::BufferCollectionConstraints result;

  // For now, we didn't report support for single_buffer_mode, and CodecImpl
  // will have failed the codec already by this point if the client tried to
  // use single_buffer_mode.
  //
  // TODO(dustingreen): Support single_buffer_mode on input (only).
  ZX_DEBUG_ASSERT(!partial_settings.has_single_buffer_mode() ||
                  !partial_settings.single_buffer_mode());
  // The CodecImpl won't hand us the sysmem token, so we shouldn't expect to
  // have the token here.
  ZX_DEBUG_ASSERT(!partial_settings.has_sysmem_token());

  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_server());
  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_client());
  uint32_t packet_count = partial_settings.packet_count_for_server() +
                          partial_settings.packet_count_for_client();

  // For now this is true - when we plumb more flexible buffer count range this
  // will change to account for a range.
  ZX_DEBUG_ASSERT(port != kOutputPort || packet_count == kOutputPacketCount);

  // TODO(MTWN-250): plumb/permit range of buffer count from further down,
  // instead of single number frame_count, and set this to the actual
  // stream-required # of reference frames + # that can concurrently decode.
  // Packets and buffers are not the same thing, and we should permit the # of
  // packets to be >= the # of buffers.  We shouldn't be
  // allocating buffers on behalf of the client here, but until we plumb the
  // range of frame_count and are more flexible on # of allocated buffers, we
  // have to make sure there are at least as many buffers as packets.  We
  // categorize the buffers as for camping and for slack.  This should change to
  // be just the buffers needed for camping and maybe 1 for shared slack.  If
  // the client wants more buffers the client can demand buffers in its own
  // fuchsia::sysmem::BufferCollection::SetConstraints().
  result.min_buffer_count_for_camping =
      partial_settings.packet_count_for_server();
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_dedicated_slack == 0);
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_shared_slack == 0);
  // TODO: Uncap max_buffer_count, have both sides infer that packet count is
  // at least as many as buffer_count.
  result.max_buffer_count = packet_count;

  uint32_t per_packet_buffer_bytes_min;
  uint32_t per_packet_buffer_bytes_max;
  if (port == kInputPort) {
    per_packet_buffer_bytes_min = kInputPerPacketBufferBytesMin;
    per_packet_buffer_bytes_max = kInputPerPacketBufferBytesMax;
  } else {
    ZX_ASSERT(context_.has_value());

    ZX_DEBUG_ASSERT(port == kOutputPort);
    per_packet_buffer_bytes_min = context_->sbc_frame_length();
    // At least for now, don't cap the per-packet buffer size for output.
    per_packet_buffer_bytes_max = 0xFFFFFFFF;
  }

  result.has_buffer_memory_constraints = true;
  result.buffer_memory_constraints.min_size_bytes = per_packet_buffer_bytes_min;
  result.buffer_memory_constraints.max_size_bytes = per_packet_buffer_bytes_max;

  // These are all false because SW encode.
  result.buffer_memory_constraints.physically_contiguous_required = false;
  result.buffer_memory_constraints.secure_required = false;

  ZX_DEBUG_ASSERT(result.image_format_constraints_count == 0);

  // We don't have to fill out usage - CodecImpl takes care of that.
  ZX_DEBUG_ASSERT(!result.usage.cpu);
  ZX_DEBUG_ASSERT(!result.usage.display);
  ZX_DEBUG_ASSERT(!result.usage.vulkan);
  ZX_DEBUG_ASSERT(!result.usage.video);

  return result;
}

CodecAdapterSbcEncoder::InputLoopStatus CodecAdapterSbcEncoder::CreateContext(
    const fuchsia::media::FormatDetails& format_details) {
  if (!format_details.has_domain() || !format_details.domain().is_audio() ||
      !format_details.domain().audio().is_uncompressed() ||
      !format_details.domain().audio().uncompressed().is_pcm()) {
    events_->onCoreCodecFailCodec(
        "SBC Encoder received input that was not uncompressed pcm audio.");
    return kShouldTerminate;
  }
  auto& input_format = format_details.domain().audio().uncompressed().pcm();

  if (input_format.bits_per_sample != 16) {
    events_->onCoreCodecFailCodec(
        "SBC Encoder only encodes audio with signed 16 bit little endian "
        "linear samples.");
    return kShouldTerminate;
  }

  int16_t sampling_freq;
  switch (input_format.frames_per_second) {
    case 48000:
      sampling_freq = SBC_sf48000;
      break;
    case 44100:
      sampling_freq = SBC_sf44100;
      break;
    case 32000:
      sampling_freq = SBC_sf32000;
      break;
    case 16000:
      sampling_freq = SBC_sf16000;
      break;
    default:
      events_->onCoreCodecFailCodec(
          "SBC Encoder received input with unsupported frequency.");
      return kShouldTerminate;
  }

  if (!format_details.has_encoder_settings() ||
      !format_details.encoder_settings().is_sbc()) {
    events_->onCoreCodecFailCodec(
        "SBC Encoder received input without encoder settings.");
    return kShouldTerminate;
  }
  auto& settings = format_details.encoder_settings().sbc();

  if (settings.channel_mode == fuchsia::media::SbcChannelMode::MONO &&
      input_format.channel_map.size() != 1) {
    events_->onCoreCodecFailCodec(
        "SBC Encoder received request for MONO encoding, but input does "
        "not have exactly 1 channel.");
    return kShouldTerminate;
  }

  if (settings.channel_mode != fuchsia::media::SbcChannelMode::MONO &&
      input_format.channel_map.size() != 2) {
    events_->onCoreCodecFailCodec(
        "SBC Encoder received request for DUAL, STEREO, or JOINT_STEREO "
        "encoding, but input does not have exactly 2 channels.");
    return kShouldTerminate;
  }

  SBC_ENC_PARAMS params = {};
  params.s16SamplingFreq = sampling_freq;
  params.s16ChannelMode = static_cast<int16_t>(settings.channel_mode);
  params.s16NumOfSubBands = static_cast<int16_t>(settings.sub_bands);
  params.s16NumOfBlocks = static_cast<int16_t>(settings.block_count);
  params.s16AllocationMethod = static_cast<int16_t>(settings.allocation);
  SBC_Encoder_Init(&params);

  // The encoder will suggest a value for the bitpool, but since the client
  // provides that we ignore the suggestion and set it after
  // SBC_Encoder_Init.
  params.s16BitPool = settings.bit_pool;

  const uint64_t bytes_per_second = input_format.frames_per_second *
                                    sizeof(uint16_t) *
                                    input_format.channel_map.size();
  context_.emplace(
      Context{.input_format = std::move(input_format),
              .settings = std::move(settings),
              .params = params,
              .timestamp_extrapolator =
                  format_details.has_timebase()
                      ? TimestampExtrapolator(format_details.timebase(),
                                              bytes_per_second)
                      : TimestampExtrapolator()});

  return kOk;
}

// TODO(turnage): Store progress on an output buffer so it can be used across
//                multiple input packets if we're behind.
CodecAdapterSbcEncoder::InputLoopStatus CodecAdapterSbcEncoder::EncodeInput(
    CodecPacket* input_packet) {
  FXL_DCHECK(context_);
  FXL_DCHECK(context_->scratch_block.len < context_->pcm_batch_size());

  auto return_to_client = fit::defer([this, input_packet]() {
    if (input_packet) {
      events_->onCoreCodecInputPacketDone(input_packet);
    }
  });

  SetInputPacket(input_packet);

  const CodecBuffer* output_buffer = nullptr;
  size_t output_offset = 0;
  auto next_output_block = [this, &output_buffer,
                            &output_offset]() -> uint8_t* {
    if (!output_buffer) {
      output_buffer = output_buffer_pool_.AllocateBuffer();
      output_offset = 0;
    }

    if (!output_buffer) {
      return nullptr;
    }

    // We assume sysmem has enforced our minimum requested buffer size of at
    // least one sbc frame length.
    FXL_DCHECK(output_buffer->buffer_size() >= context_->sbc_frame_length());

    // Caller must set `output_buffer` to `nullptr` when space is insufficient.
    uint8_t* output = output_buffer->buffer_base() + output_offset;
    output_offset += context_->sbc_frame_length();
    return output;
  };

  std::pair<uint8_t*, InputLoopStatus> input_and_status;
  while ((input_and_status = EnsureOutputPacketIsFineAndGetNextInputBlock())
                 .second == kOk &&
         input_and_status.first != nullptr) {
    auto [input, status] = input_and_status;
    uint8_t* output = output = next_output_block();
    if (output == nullptr) {
      // The stream is ending.
      return kShouldTerminate;
    }
    FXL_DCHECK(output_buffer);

    SBC_Encode(&context_->params, reinterpret_cast<int16_t*>(input), output);

    if (output_offset + context_->sbc_frame_length() >
        output_buffer->buffer_size()) {
      FXL_DCHECK(context_->output_packet != nullptr);

      context_->output_packet->SetBuffer(output_buffer);
      context_->output_packet->SetValidLengthBytes(output_offset);
      context_->output_packet->SetStartOffset(0);
      SendOutputPacket(context_->output_packet);

      context_->output_packet = nullptr;
      output_buffer = nullptr;
    }
  }

  if (input_and_status.second != kOk) {
    return input_and_status.second;
  }

  // Flush the output if we didn't already by exceeding the output buffer size.
  if (output_buffer) {
    FXL_DCHECK(context_->output_packet != nullptr)
        << "If there are any bytes written, we should have already gotten an "
           "output packet.";
    SendOutputPacket(context_->output_packet);
    context_->output_packet = nullptr;
  }

  return kOk;
}

void CodecAdapterSbcEncoder::SendOutputPacket(CodecPacket* output_packet) {
  {
    fit::closure free_buffer = [this,
                                base = output_packet->buffer()->buffer_base()] {
      output_buffer_pool_.FreeBuffer(base);
    };
    std::lock_guard<std::mutex> lock(lock_);
    in_use_by_client_[output_packet] = fit::defer(std::move(free_buffer));
  }

  events_->onCoreCodecOutputPacket(output_packet,
                                   /*error_detected_before=*/false,
                                   /*error_detected_during=*/false);
}

void CodecAdapterSbcEncoder::SaveLeftovers() {
  FXL_DCHECK(context_->input_packet);
  FXL_DCHECK(context_->input_offset <=
             context_->input_packet->valid_length_bytes());
  if (context_->input_offset < context_->input_packet->valid_length_bytes()) {
    const size_t leftover =
        context_->input_packet->valid_length_bytes() - context_->input_offset;
    FXL_DCHECK(context_->scratch_block.len + leftover <
               context_->pcm_batch_size());

    memcpy(context_->scratch_block.buffer + context_->scratch_block.len,
           context_->input_packet->buffer()->buffer_base() +
               context_->input_packet->start_offset() + context_->input_offset,
           leftover);

    context_->scratch_block.len += leftover;
  }
}

void CodecAdapterSbcEncoder::SetInputPacket(CodecPacket* input_packet) {
  FXL_DCHECK(context_);
  FXL_DCHECK(context_->input_packet == nullptr);

  context_->input_packet = input_packet;
  context_->input_offset = 0;

  if (input_packet && input_packet->has_timestamp_ish()) {
    context_->timestamp_extrapolator.Inform(
        context_->input_stream_index + context_->scratch_block.len,
        input_packet->timestamp_ish());
  }

  if (context_->scratch_block.len > 0) {
    FXL_DCHECK(context_->output_packet != nullptr)
        << "If there are any bytes in the scratch block, we should already "
           "have gotten an output packet.";
    const uint32_t empty_scratch =
        context_->pcm_batch_size() - context_->scratch_block.len;

    if (input_packet) {
      const size_t n =
          std::min(input_packet->valid_length_bytes(), empty_scratch);
      memcpy(
          context_->scratch_block.buffer + context_->scratch_block.len,
          input_packet->buffer()->buffer_base() + input_packet->start_offset(),
          n);
      context_->scratch_block.len += n;
      context_->input_offset = n;
    } else {
      // There is no input; the stream is ending and we should flush what
      // we've got.
      memset(context_->scratch_block.buffer + context_->scratch_block.len, 0,
             empty_scratch);
      context_->scratch_block.len = context_->pcm_batch_size();
    }
  }
}

CodecAdapterSbcEncoder::InputLoopStatus
CodecAdapterSbcEncoder::EnsureOutputPacketIsSetIfAnyInputBytesRemain() {
  FXL_DCHECK(context_->output_packet || context_->scratch_block.len == 0)
      << "If there are any bytes in the scratch block, we should already "
         "have gotten an output packet.";
  if (!context_->output_packet && context_->input_packet &&
      context_->input_offset < context_->input_packet->valid_length_bytes()) {
    FXL_DCHECK(context_->scratch_block.len == 0);

    std::optional<CodecPacket*> maybe_output_packet =
        free_output_packets_.WaitForElement();
    if (!maybe_output_packet) {
      // The stream is ending.
      return kShouldTerminate;
    }
    FXL_DCHECK(*maybe_output_packet != nullptr);
    context_->output_packet = *maybe_output_packet;

    std::optional<uint64_t> maybe_timestamp;
    if (context_->timestamp_extrapolator.has_information() &&
        !(maybe_timestamp = context_->timestamp_extrapolator.Extrapolate(
              context_->input_stream_index))) {
      events_->onCoreCodecFailCodec(
          "Extrapolation was required for a timestamp because the input "
          "was unaligned, but no timebase is set.");
      return kShouldTerminate;
    }

    if (maybe_timestamp) {
      context_->output_packet->SetTimstampIsh(*maybe_timestamp);
    }
  }

  return kOk;
}

std::pair<uint8_t*, CodecAdapterSbcEncoder::InputLoopStatus>
CodecAdapterSbcEncoder::EnsureOutputPacketIsFineAndGetNextInputBlock() {
  FXL_DCHECK(context_);

  if (context_->scratch_block.len == context_->pcm_batch_size()) {
    FXL_DCHECK(context_->output_packet != nullptr);

    context_->scratch_block.len = 0;
    context_->input_stream_index += context_->pcm_batch_size();
    return {context_->scratch_block.buffer, kOk};
  }

  if (!context_->input_packet) {
    return {nullptr, kOk};
  }

  InputLoopStatus ensure_output_packet_status =
      EnsureOutputPacketIsSetIfAnyInputBytesRemain();
  if (ensure_output_packet_status != kOk) {
    return {nullptr, ensure_output_packet_status};
  }

  if (context_->input_offset + context_->pcm_batch_size() <=
      context_->input_packet->valid_length_bytes()) {
    uint8_t* block = context_->input_packet->buffer()->buffer_base() +
                     context_->input_packet->start_offset() +
                     context_->input_offset;
    context_->input_offset += context_->pcm_batch_size();
    context_->input_stream_index += context_->pcm_batch_size();
    return {block, kOk};
  }

  SaveLeftovers();
  FXL_DCHECK(context_->output_packet || context_->scratch_block.len == 0)
      << "If there are any bytes in the scratch block, we should already "
         "have gotten an output packet.";
  context_->input_packet = nullptr;
  context_->input_offset = 0;

  return {nullptr, kOk};
}

void CodecAdapterSbcEncoder::CoreCodecSetBufferCollectionInfo(
    CodecPort port,
    const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  // TODO: Should uncap max_buffer_count and stop asserting this, or assert
  // instead that buffer_count >= buffers for camping + dedicated slack.
  ZX_DEBUG_ASSERT(port != kOutputPort ||
                  buffer_collection_info.buffer_count == kOutputPacketCount);
}
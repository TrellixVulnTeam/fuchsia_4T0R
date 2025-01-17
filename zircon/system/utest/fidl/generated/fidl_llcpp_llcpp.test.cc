// WARNING: This file is machine generated by fidlgen.

#include <fidl_llcpp_llcpp.test.h>
#include <memory>

namespace llcpp {

namespace fidl {
namespace test {
namespace coding {

namespace {

[[maybe_unused]]
constexpr uint64_t kLlcpp_Action_Ordinal = 0x46bfc70900000000lu;
extern "C" const fidl_type_t fidl_test_coding_LlcppActionResponseTable;

}  // namespace
template <>
Llcpp::ResultOf::Action_Impl<Llcpp::ActionResponse>::Action_Impl(zx::unowned_channel _client_end) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<ActionRequest>();
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  uint8_t* _write_bytes = _write_bytes_array.view().data();
  memset(_write_bytes, 0, ActionRequest::PrimarySize);
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(ActionRequest));
  ::fidl::DecodedMessage<ActionRequest> _decoded_request(std::move(_request_bytes));
  Super::SetResult(
      Llcpp::InPlace::Action(std::move(_client_end), Super::response_buffer()));
}

Llcpp::ResultOf::Action Llcpp::SyncClient::Action() {
  return ResultOf::Action(zx::unowned_channel(this->channel_));
}

Llcpp::ResultOf::Action Llcpp::Call::Action(zx::unowned_channel _client_end) {
  return ResultOf::Action(std::move(_client_end));
}

template <>
Llcpp::UnownedResultOf::Action_Impl<Llcpp::ActionResponse>::Action_Impl(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(ActionRequest)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  memset(_request_buffer.data(), 0, ActionRequest::PrimarySize);
  _request_buffer.set_actual(sizeof(ActionRequest));
  ::fidl::DecodedMessage<ActionRequest> _decoded_request(std::move(_request_buffer));
  Super::SetResult(
      Llcpp::InPlace::Action(std::move(_client_end), std::move(_response_buffer)));
}

Llcpp::UnownedResultOf::Action Llcpp::SyncClient::Action(::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Action(zx::unowned_channel(this->channel_), std::move(_response_buffer));
}

Llcpp::UnownedResultOf::Action Llcpp::Call::Action(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Action(std::move(_client_end), std::move(_response_buffer));
}

zx_status_t Llcpp::SyncClient::Action_Deprecated(int32_t* out_v) {
  return Llcpp::Call::Action_Deprecated(zx::unowned_channel(this->channel_), out_v);
}

zx_status_t Llcpp::Call::Action_Deprecated(zx::unowned_channel _client_end, int32_t* out_v) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<ActionRequest>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _request = *reinterpret_cast<ActionRequest*>(_write_bytes);
  _request._hdr.ordinal = kLlcpp_Action_Ordinal;
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(ActionRequest));
  ::fidl::DecodedMessage<ActionRequest> _decoded_request(std::move(_request_bytes));
  auto _encode_request_result = ::fidl::Encode(std::move(_decoded_request));
  if (_encode_request_result.status != ZX_OK) {
    return _encode_request_result.status;
  }
  constexpr uint32_t _kReadAllocSize = ::fidl::internal::ClampedMessageSize<ActionResponse>();
  FIDL_ALIGNDECL uint8_t _read_bytes[_kReadAllocSize];
  ::fidl::BytePart _response_bytes(_read_bytes, _kReadAllocSize);
  auto _call_result = ::fidl::Call<ActionRequest, ActionResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(_response_bytes));
  if (_call_result.status != ZX_OK) {
    return _call_result.status;
  }
  auto _decode_result = ::fidl::Decode(std::move(_call_result.message));
  if (_decode_result.status != ZX_OK) {
    return _decode_result.status;
  }
  auto& _response = *_decode_result.message.message();
  *out_v = std::move(_response.v);
  return ZX_OK;
}

::fidl::DecodeResult<Llcpp::ActionResponse> Llcpp::SyncClient::Action_Deprecated(::fidl::BytePart _response_buffer, int32_t* out_v) {
  return Llcpp::Call::Action_Deprecated(zx::unowned_channel(this->channel_), std::move(_response_buffer), out_v);
}

::fidl::DecodeResult<Llcpp::ActionResponse> Llcpp::Call::Action_Deprecated(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer, int32_t* out_v) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(ActionRequest)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  auto& _request = *reinterpret_cast<ActionRequest*>(_request_buffer.data());
  _request._hdr.ordinal = kLlcpp_Action_Ordinal;
  _request_buffer.set_actual(sizeof(ActionRequest));
  ::fidl::DecodedMessage<ActionRequest> _decoded_request(std::move(_request_buffer));
  auto _encode_request_result = ::fidl::Encode(std::move(_decoded_request));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<ActionResponse>(_encode_request_result.status, _encode_request_result.error);
  }
  auto _call_result = ::fidl::Call<ActionRequest, ActionResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(_response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<ActionResponse>(_call_result.status, _call_result.error);
  }
  auto _decode_result = ::fidl::Decode(std::move(_call_result.message));
  if (_decode_result.status != ZX_OK) {
    return _decode_result;
  }
  auto& _response = *_decode_result.message.message();
  *out_v = std::move(_response.v);
  return _decode_result;
}

::fidl::DecodeResult<Llcpp::ActionResponse> Llcpp::InPlace::Action(zx::unowned_channel _client_end, ::fidl::BytePart response_buffer) {
  constexpr uint32_t _write_num_bytes = sizeof(ActionRequest);
  ::fidl::internal::AlignedBuffer<_write_num_bytes> _write_bytes;
  ::fidl::BytePart _request_buffer = _write_bytes.view();
  _request_buffer.set_actual(_write_num_bytes);
  ::fidl::DecodedMessage<ActionRequest> params(std::move(_request_buffer));
  params.message()->_hdr = {};
  params.message()->_hdr.ordinal = kLlcpp_Action_Ordinal;
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Llcpp::ActionResponse>::FromFailure(
        std::move(_encode_request_result));
  }
  auto _call_result = ::fidl::Call<ActionRequest, ActionResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<Llcpp::ActionResponse>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
}


bool Llcpp::TryDispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  if (msg->num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_INVALID_ARGS);
    return true;
  }
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  switch (hdr->ordinal) {
    case kLlcpp_Action_Ordinal:
    {
      auto result = ::fidl::DecodeAs<ActionRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->Action(
        Interface::ActionCompleter::Sync(txn));
      return true;
    }
    default: {
      return false;
    }
  }
}

bool Llcpp::Dispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  bool found = TryDispatch(impl, msg, txn);
  if (!found) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_NOT_SUPPORTED);
  }
  return found;
}


void Llcpp::Interface::ActionCompleterBase::Reply(int32_t v) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<ActionResponse>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _response = *reinterpret_cast<ActionResponse*>(_write_bytes);
  _response._hdr.ordinal = kLlcpp_Action_Ordinal;
  _response.v = std::move(v);
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof(ActionResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<ActionResponse>(std::move(_response_bytes)));
}

void Llcpp::Interface::ActionCompleterBase::Reply(::fidl::BytePart _buffer, int32_t v) {
  if (_buffer.capacity() < ActionResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  auto& _response = *reinterpret_cast<ActionResponse*>(_buffer.data());
  _response._hdr.ordinal = kLlcpp_Action_Ordinal;
  _response.v = std::move(v);
  _buffer.set_actual(sizeof(ActionResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<ActionResponse>(std::move(_buffer)));
}

void Llcpp::Interface::ActionCompleterBase::Reply(::fidl::DecodedMessage<ActionResponse> params) {
  params.message()->_hdr = {};
  params.message()->_hdr.ordinal = kLlcpp_Action_Ordinal;
  CompleterBase::SendReply(std::move(params));
}


}  // namespace coding
}  // namespace test
}  // namespace fidl
}  // namespace llcpp

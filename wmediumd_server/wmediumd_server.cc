/*
 *
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <android-base/strings.h>
#include <assert.h>
#include <gflags/gflags.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <sys/msg.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <string>

#include "wmediumd.grpc.pb.h"
#include "wmediumd/api.h"
#include "wmediumd/grpc.h"

using google::protobuf::Empty;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using wmediumdserver::SetPositionRequest;
using wmediumdserver::SetSnrRequest;
using wmediumdserver::StartPcapRequest;
using wmediumdserver::WmediumdService;

#define MAC_ADDR_LEN 6
#define STR_MAC_ADDR_LEN 17

static std::atomic<long> msg_type_response_increment{MSG_TYPE_RESPONSE_BASE};

template <class T>
static void AppendBinaryRepresentation(std::string& buf, const T& data) {
  std::copy(reinterpret_cast<const char*>(&data),
            reinterpret_cast<const char*>(&data) + sizeof(T),
            std::back_inserter(buf));
}

bool IsValidMacAddr(const std::string& mac_address) {
  if (mac_address.size() != STR_MAC_ADDR_LEN) {
    return false;
  }

  if (mac_address[2] != ':' || mac_address[5] != ':' || mac_address[8] != ':' ||
      mac_address[11] != ':' || mac_address[14] != ':') {
    return false;
  }

  for (int i = 0; i < STR_MAC_ADDR_LEN; ++i) {
    if ((i - 2) % 3 == 0) continue;
    char c = mac_address[i];

    if (isupper(c)) {
      c = tolower(c);
    }

    if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) return false;
  }

  return true;
}

static std::array<uint8_t, 6> ParseMacAddress(const std::string& mac_address) {
  auto split_mac = android::base::Split(mac_address, ":");
  std::array<uint8_t, 6> mac;
  for (int i = 0; i < 6; i++) {
    char* end_ptr;
    mac[i] = (uint8_t)strtol(split_mac[i].c_str(), &end_ptr, 16);
  }

  return mac;
}

class WmediumdServiceImpl final : public WmediumdService::Service {
 public:
  WmediumdServiceImpl(int event_fd, int msq_id)
      : event_fd_(event_fd), msq_id_(msq_id) {}

  Status SetPosition(ServerContext* context, const SetPositionRequest* request,
                     Empty* reply) override {
    // Validate parameters
    if (!IsValidMacAddr(request->mac_address())) {
      return Status(StatusCode::INVALID_ARGUMENT, "Got invalid mac address");
    }
    auto mac = ParseMacAddress(request->mac_address());

    // Construct request payload
    struct wmediumd_set_position request_data_payload;
    memcpy(request_data_payload.mac, &mac, sizeof(mac));
    request_data_payload.x = request->x_pos();
    request_data_payload.y = request->y_pos();

    struct wmediumd_grpc_response_message response_message;
    SendAndReceiveGrpcMessage(REQUEST_SET_POSITION,
                              sizeof(request_data_payload),
                              &request_data_payload, &response_message);
    if (response_message.data_type != RESPONSE_ACK) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Failed to execute SetPosition");
    }
    return Status::OK;
  }

  Status SetSnr(ServerContext* context, const SetSnrRequest* request,
                Empty* reply) override {
    // Validate parameters
    if (!IsValidMacAddr(request->mac_address_1()) ||
        !IsValidMacAddr(request->mac_address_2())) {
      return Status(StatusCode::INVALID_ARGUMENT, "Got invalid mac address");
    }
    auto mac_1 = ParseMacAddress(request->mac_address_1());
    auto mac_2 = ParseMacAddress(request->mac_address_2());

    // Construct request payload
    struct wmediumd_set_snr request_data_payload;
    memcpy(request_data_payload.node1_mac, &mac_1, sizeof(mac_1));
    memcpy(request_data_payload.node2_mac, &mac_2, sizeof(mac_2));
    request_data_payload.snr = request->snr();

    struct wmediumd_grpc_response_message response_message;
    SendAndReceiveGrpcMessage(REQUEST_SET_SNR, sizeof(request_data_payload),
                              &request_data_payload, &response_message);
    if (response_message.data_type != RESPONSE_ACK) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Failed to execute SetSnr");
    }
    return Status::OK;
  }

  Status StartPcap(ServerContext* context, const StartPcapRequest* request,
                   Empty* reply) override {
    // Construct request payload
    ssize_t size =
        sizeof(struct wmediumd_start_pcap) + (request->path().length() + 1);
    struct wmediumd_start_pcap* request_data_payload =
        (struct wmediumd_start_pcap*)malloc(size);
    strcpy(request_data_payload->pcap_path, request->path().c_str());

    struct wmediumd_grpc_response_message response_message;
    SendAndReceiveGrpcMessage(REQUEST_START_PCAP, size, request_data_payload,
                              &response_message);
    free(request_data_payload);
    if (response_message.data_type != RESPONSE_ACK) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Failed to execute StartPcap");
    }
    return Status::OK;
  }

  Status StopPcap(ServerContext* context, const Empty* request,
                  Empty* reply) override {
    struct wmediumd_grpc_response_message response_message;
    SendAndReceiveGrpcMessage(REQUEST_STOP_PCAP, &response_message);
    if (response_message.data_type != RESPONSE_ACK) {
      return Status(StatusCode::FAILED_PRECONDITION,
                    "Failed to execute StopPcap");
    }
    return Status::OK;
  }

 private:
  void SendAndReceiveGrpcMessage(
      enum wmediumd_grpc_request_data_type data_type, ssize_t data_size,
      void* request_data_payload,
      struct wmediumd_grpc_response_message* response_message) {
    long msg_type_response = msg_type_response_increment.fetch_add(1);

    // Send Request Message
    struct wmediumd_grpc_request_message request_message;
    request_message.msg_type_request = MSG_TYPE_REQUEST;
    request_message.msg_type_response = msg_type_response;
    request_message.data_type = data_type;
    request_message.data_size = data_size;
    assert(data_size <= GRPC_MSG_BUF_MAX);
    if (data_size > 0) {
      memcpy(request_message.data_payload, request_data_payload, data_size);
    }
    msgsnd(msq_id_, &request_message, MSG_TYPE_REQUEST_SIZE, 0);

    // Trigger Event
    uint64_t evt = 1;
    write(event_fd_, &evt, sizeof(evt));

    msgrcv(msq_id_, response_message, MSG_TYPE_RESPONSE_SIZE, msg_type_response,
           0);
  }

  void SendAndReceiveGrpcMessage(
      enum wmediumd_grpc_request_data_type data_type,
      struct wmediumd_grpc_response_message* response_message) {
    SendAndReceiveGrpcMessage(data_type, 0, NULL, response_message);
  }

  int event_fd_;
  int msq_id_;
};

void RunWmediumdServer(std::string grpc_uds_path, int event_fd, int msq_id) {
  std::string server_address("unix:" + grpc_uds_path);
  WmediumdServiceImpl service(event_fd, msq_id);

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

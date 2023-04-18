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

#include <string>
#include <thread>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include <wmediumd/wmediumd.h>
#include <wmediumd_server/wmediumd_server.h>

constexpr char kGrpcUdsPathOption[] = "--grpc_uds_path=";

int main(int argc, char* argv[]) {
    std::vector<char*> wmediumd_args;
    std::string grpc_uds_path;
    for (int i = 0; i < argc; i++) {
        if (android::base::StartsWith(argv[i], kGrpcUdsPathOption)) {
            std::string current_arg(argv[i]);
            grpc_uds_path = current_arg.substr(strlen(kGrpcUdsPathOption));
        } else {
            wmediumd_args.push_back(argv[i]);
        }
    }

    std::thread wmediumd_server_thread;
    if (!grpc_uds_path.empty()) {
        wmediumd_server_thread = std::thread(RunWmediumdServer, grpc_uds_path);
    }

    wmediumd_main(wmediumd_args.size(), wmediumd_args.data());

    if (!grpc_uds_path.empty()) {
        wmediumd_server_thread.join();
    }

    return 0;
}
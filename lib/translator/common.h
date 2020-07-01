// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LIB_COMMON_H
#define LIB_COMMON_H

#include "absl/strings/string_view.h"
#include "lib/scsi_defs.h"

namespace translator {

// Vendor Identification shall be set to "NVMe" followed by 4 spaces: "NVMe    "
// This value is not null terminated and should be size 8
constexpr absl::string_view kNvmeVendorIdentification = "NVMe    ";
static_assert(kNvmeVendorIdentification.size() == 8);

enum class StatusCode { kSuccess, kInvalidInput, kNoTranslation, kFailure };

void DebugLog(const char* format, ...);

void SetDebugCallback(void (*callback)(const char*));

absl::string_view ScsiOpcodeToString(scsi_defs::OpCode opcode);

}  // namespace translator
#endif
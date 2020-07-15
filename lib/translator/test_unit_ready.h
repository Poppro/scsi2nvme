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

#ifndef LIB_TRANSLATOR_test_unit_ready_H
#define LIB_TRANSLATOR_test_unit_ready_H

#include "absl/types/span.h"

#include "common.h"
#include "lib/scsi.h"

#include "third_party/spdk/nvme.h"

namespace translator {

StatusCode TestUnitReadyToNvme(absl::Span<const uint8_t> scsi_cmd);

StatusCode TestUnitReadyToScsi(bool& result);

}  // namespace translator
#endif
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

#ifndef LIB_TRANSLATOR_READ_H
#define LIB_TRANSLATOR_READ_H

#include "third_party/spdk_defs/nvme_defs.h"

#include "common.h"
#include "lib/scsi_defs.h"

namespace translator {

/**
 * SCSI has 5 Read commands: Read(6), Read(10), Read(12), Read(16), Read(32)
 * Each translation function takes in a raw SCSI command in bytes,
 * casts it to scsi_defs::Read[6,10,12,16,32]Command,
 * and builds an NVMe Read command
 *
 * Read(6) is obsolete, but may still be implemented on some devices.
 * As such, it will call the LegacyRead() translation function
 *
 * Read(10), Read(12), and Read(16) have essentially the same fields but with
 * different memory layouts. They all call the Read() translation function,
 * which calls LegacyRead() and handles some additional fields
 *
 * Read(32) calls Read() and also translates additional logical block
 * application tags
 */
StatusCode Read6ToNvme(absl::Span<const uint8_t> scsi_cmd,
                       nvme_defs::GenericQueueEntryCmd& nvme_cmd);

StatusCode Read10ToNvme(absl::Span<const uint8_t> scsi_cmd,
                        nvme_defs::GenericQueueEntryCmd& nvme_cmd);
StatusCode Read12ToNvme(absl::Span<const uint8_t> scsi_cmd,
                        nvme_defs::GenericQueueEntryCmd& nvme_cmd);

StatusCode Read16ToNvme(absl::Span<const uint8_t> scsi_cmd,
                        nvme_defs::GenericQueueEntryCmd& nvme_cmd);

StatusCode Read32ToNvme(absl::Span<const uint8_t> scsi_cmd,
                        nvme_defs::GenericQueueEntryCmd& nvme_cmd);
}  // namespace translator
#endif

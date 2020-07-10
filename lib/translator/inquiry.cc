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

#include "inquiry.h"
#include "string.h"

namespace translator {

// command specific helpers
namespace {
void TranslateStandardInquiry(const nvme::IdentifyControllerData& identify_ctrl,
                              const nvme::IdentifyNamespace& identify_ns,
                              absl::Span<uint8_t> buffer) {
  scsi::InquiryData result = {
      .version = scsi::Version::kSpc4,
      .response_data_format = scsi::ResponseDataFormat::kCompliant,
      .additional_length = 0x1f,
      .tpgs = scsi::TPGS::kNotSupported,
      .protect =
          (identify_ns.dps.pit == 0 && identify_ns.dps.md_start == 0) ? 0 : 1,
      .cmdque = 1};

  // Shall be set to “NVMe" followed by 4 spaces: “NVMe    “
  // Vendor Identification is not null terminated.
  static_assert(sizeof(result.vendor_identification) ==
                kNvmeVendorIdentification.size());

  memcpy(result.vendor_identification, kNvmeVendorIdentification.data(),
         kNvmeVendorIdentification.size());

  // Shall be set to the first 16 bytes of the Model Number (MN) field within
  // the Identify Controller Data Structure
  memcpy(result.product_identification, identify_ctrl.mn,
         sizeof(result.product_identification));

  // Shall be set to the last 4 ASCII graphic characters in the range of 21h-7Eh
  // (i.e. last 4 non-space characters) of the Firmware Revision (FR) field
  // within the Identify Controller Data Structure.
  int idx = 3;
  for (int i = 7; i >= 0; --i) {
    if (identify_ctrl.fr[i] != ' ' && identify_ctrl.fr[i] >= 0x21 &&
        identify_ctrl.fr[i] <= 0x7e) {
      result.product_revision_level[idx] = identify_ctrl.fr[i];
      if (idx-- == 0) break;
    }
  }
  WriteValue(result, buffer);
}

void TranslateSupportedVpdPages(absl::Span<uint8_t> buffer) {
  scsi::PageCode supported_page_list[7] = {
      scsi::PageCode::kSupportedVpd,
      scsi::PageCode::kUnitSerialNumber,
      scsi::PageCode::kDeviceIdentification,
      scsi::PageCode::kExtended,
      scsi::PageCode::kBlockLimitsVpd,
      scsi::PageCode::kBlockDeviceCharacteristicsVpd,
      scsi::PageCode::kLogicalBlockProvisioningVpd};

  scsi::SupportedVitalProductData result = {
      .page_length = sizeof(supported_page_list),
  };

  WriteValue(result, buffer);
  WriteValue(supported_page_list, buffer.subspan(sizeof(result)));
}

void TranslateUnitSerialNumberVpd(
    const nvme::IdentifyControllerData& identify_ctrl,
    const nvme::IdentifyNamespace& identify_ns, uint32_t nsid,
    absl::Span<uint8_t> buffer) {
  scsi::UnitSerialNumber result = {.page_code =
                                       scsi::PageCode::kUnitSerialNumber};

  // converts the nguid or eui64 into a formatter hex string
  // 0x0123456789ABCDEF would be converted to “0123_4567_89AB_CDEF."
  char product_serial_number[40] = {0}, hex_string[33];

  // TOOD: Create shared constants to be used in tests as well when merging all
  // Inquiry paths
  const size_t kNguidLen = 40, kEui64Len = 20, kV1SerialLen = 30;

  // check if nonzero
  bool nguid_nz = identify_ns.nguid[0] || identify_ns.nguid[1];
  bool eui64_nz = identify_ns.eui64;

  if (nguid_nz || eui64_nz) {
    if (nguid_nz) {
      // 6.1.3.1.1
      result.page_length = kNguidLen;

      // convert 128-bit number into hex string, 64-bits at a time
      sprintf(hex_string, "%08lx", identify_ns.nguid[0]);
      sprintf(&hex_string[16], "%08lx", identify_ns.nguid[1]);

    } else if (eui64_nz) {
      // 6.1.3.1.2
      result.page_length = kEui64Len;

      // convert 64-bit number into hex string
      sprintf(hex_string, "%08lx", identify_ns.eui64);
    }

    // insert _ and . in the correct positions
    for (int i = 4; i < result.page_length - 1; i += 5) {
      product_serial_number[i] = '_';
    }
    product_serial_number[result.page_length - 1] = '.';

    // insert numbers
    int pos = 0;
    for (int i = 0; i < result.page_length - 1; ++i) {
      if (product_serial_number[i] != '_') {
        product_serial_number[i] = hex_string[pos++];
      }
    }
  } else {
    // 6.1.3.1.3
    // valid for NVMe 1.0 devices only

    result.page_length = kV1SerialLen;

    // PRODUCT SERIAL NUMBER should be formed as follows:
    // Bits 239:80 20 bytes of Serial Number (bytes 23:04 of Identify
    // Controller data structure)
    static_assert(sizeof(identify_ctrl.sn) == 20);
    static_assert(sizeof(product_serial_number) >= 20);
    memcpy(&product_serial_number, identify_ctrl.sn, sizeof(identify_ctrl.sn));

    // Bits 79:72 ASCII representation of “_"
    product_serial_number[sizeof(identify_ctrl.sn)] = '_';

    // Bits 71:08 ASCII representation of 32 bit Namespace Identifier (NSID)
    sprintf(&product_serial_number[sizeof(identify_ctrl.sn) + 1], "%08x", nsid);

    // Bits 07:00 ASCII representation of “."
    product_serial_number[kV1SerialLen - 1] = '.';
  }

  WriteValue(result, buffer);
  WriteValue(product_serial_number, buffer.subspan(sizeof(result)));
}
}  // namespace

StatusCode InquiryToNvme(absl::Span<const uint8_t> raw_scsi,
                         nvme::GenericQueueEntryCmd& identify_ns,
                         nvme::GenericQueueEntryCmd& identify_ctrl,
                         uint32_t& alloc_len, scsi::LunAddress lun) {
  scsi::InquiryCommand cmd = {};
  if (!ReadValue(raw_scsi, cmd)) return StatusCode::kFailure;

  alloc_len = cmd.allocation_length;

  // Allocate prp & assign to command
  uint64_t ns_prp = AllocPages(1);
  if (!ns_prp) return StatusCode::kFailure;

  identify_ns = nvme::GenericQueueEntryCmd{
      .opc = static_cast<uint8_t>(nvme::AdminOpcode::kIdentify), .nsid = lun};
  identify_ns.dptr.prp.prp1 = ns_prp;
  identify_ns.cdw[0] = 0x0;  // cns val

  uint64_t ctrl_prp = AllocPages(1);
  if (!ctrl_prp) return StatusCode::kFailure;

  identify_ctrl = nvme::GenericQueueEntryCmd{
      .opc = static_cast<uint8_t>(nvme::AdminOpcode::kIdentify),
  };
  identify_ctrl.dptr.prp.prp1 = ctrl_prp;
  identify_ctrl.cdw[0] = 0x1;  // cns val
  return StatusCode::kSuccess;
}

// Main logic engine for the Inquiry command
StatusCode InquiryToScsi(
    absl::Span<const uint8_t> raw_scsi, absl::Span<uint8_t> buffer,
    absl::Span<const nvme::GenericQueueEntryCmd> nvme_cmds) {
  scsi::InquiryCommand inquiry_cmd = {};

  if (!ReadValue(raw_scsi, inquiry_cmd)) return StatusCode::kFailure;

  uint8_t* ctrl_dptr = reinterpret_cast<uint8_t*>(nvme_cmds[0].dptr.prp.prp1);
  auto ctrl_span =
      absl::MakeSpan(ctrl_dptr, sizeof(nvme::IdentifyControllerData));

  uint8_t* ns_dptr = reinterpret_cast<uint8_t*>(nvme_cmds[1].dptr.prp.prp1);
  auto ns_span = absl::MakeSpan(ns_dptr, sizeof(nvme::IdentifyNamespace));

  nvme::IdentifyControllerData identify_ctrl = {};
  if (!ReadValue(ctrl_span, identify_ctrl)) return StatusCode::kFailure;

  nvme::IdentifyNamespace identify_ns = {};
  if (!ReadValue(ns_span, identify_ns)) return StatusCode::kFailure;

  // nsid should come from Namespace
  uint32_t nsid = nvme_cmds[1].nsid;

  if (inquiry_cmd.evpd) {
    switch (inquiry_cmd.page_code) {
      case scsi::PageCode::kSupportedVpd:
        // Return Supported Vpd Pages data page to application client, refer
        // to 6.1.2.
        TranslateSupportedVpdPages(buffer);
        break;
      case scsi::PageCode::kUnitSerialNumber:
        // Return Unit Serial Number data page toapplication client.
        // Referto 6.1.3.
        TranslateUnitSerialNumberVpd(identify_ctrl, identify_ns, nsid, buffer);
        break;
      case scsi::PageCode::kDeviceIdentification:
        // TODO: Return Device Identification data page toapplication client,
        // refer to 6.1.4
        break;
      case scsi::PageCode::kExtended:
        // TODO: May optionally be supported by returning Extended INQUIRY data
        // page toapplication client, refer to 6.1.5.
        break;
      case scsi::PageCode::kBlockLimitsVpd:
        // TODO: May be supported by returning Block Limits VPD data page to
        // application client, refer to 6.1.6.
        break;
      case scsi::PageCode::kBlockDeviceCharacteristicsVpd:
        // TODO: Return Block Device Characteristics Vpd Page to application
        // client, refer to 6.1.7.
        break;
      case scsi::PageCode::kLogicalBlockProvisioningVpd:
      // May be supported by returning Logical Block Provisioning VPD Page to
      // application client, refer to 6.1.8.
      default:
        // TODO: Command may be terminated with CHECK CONDITION status, ILLEGAL
        // REQUEST dense key, and ILLEGAL FIELD IN CDB additional sense code
        return StatusCode::kInvalidInput;
    }
  } else {
    // Return Standard INQUIRY Data to application client
    TranslateStandardInquiry(identify_ctrl, identify_ns, buffer);
  }
  return StatusCode::kSuccess;
}

};  // namespace translator

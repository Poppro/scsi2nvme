#include "read.h"

#include "absl/types/span.h"

#include "common.h"

namespace translator {

namespace {  // anonymous namespace for helper functions

// Section 5.3
// https://www.nvmexpress.org/wp-content/uploads/NVM-Express-SCSI-Translation-Reference-1_1-Gold.pdf
StatusCode BuildPrinfo(uint8_t rdprotect, uint8_t& prinfo) {
  bool pract;     // Protection Information Action 1 bit
  uint8_t prchk;  // Protection Information Check 3 bits
  prinfo = 0;     // Protection Information field 4 bits

  switch (rdprotect) {  // 3 bits
    case 0b0:
      pract = 0b1;
      prchk = 0b111;
      break;
    case 0b001:
    case 0b101:
      pract = 0b0;
      prchk = 0b111;
      break;
    case 0b010:
      pract = 0b0;
      prchk = 0b011;
      break;
    case 0b011:
      pract = 0b0;
      prchk = 0b000;
      break;
    case 0b100:
      pract = 0b0;
      prchk = 0b100;
      break;
    default:
      // Should result in SCSI command termination with status: CHECK CONDITION,
      //  sense key: ILLEGAL REQUEST, additional sense code: LLEGAL FIELD IN CDB
      DebugLog("RDPROTECT with value %d has no translation to PRINFO",
               rdprotect);
      return StatusCode::kInvalidInput;
  }

  prinfo |= prchk;
  prinfo |= (pract << 3);

  return StatusCode::kSuccess;
}

void SetLbaTags(uint32_t eilbrt, uint16_t elbat, uint16_t elbatm,
                nvme_defs::GenericQueueEntryCmd& nvme_cmd) {
  // cdw14 expected initial logical block reference
  nvme_cmd.cdw[4] = eilbrt;
  // cdw15 bits 15:0 expected logical block application tag
  nvme_cmd.cdw[5] |= elbat;
  // cdw15 bits 31:16 expected logical block application tag mask
  nvme_cmd.cdw[5] |= (elbatm << 16);
}

StatusCode LegacyRead(uint32_t lba, uint16_t transfer_length,
                      nvme_defs::GenericQueueEntryCmd& nvme_cmd) {
  nvme_cmd = nvme_defs::GenericQueueEntryCmd{
      .opc = static_cast<uint8_t>(nvme_defs::NvmOpcode::kRead),
      .psdt = 0b00  // PRPs are used for data transfer
  };

  uint64_t mptr = AllocPages(1);
  uint64_t prp = AllocPages(1);
  if (mptr == 0 || prp == 0) {
    DebugLog("Error when requesting a page of memory");
    return StatusCode::kFailure;
  }

  nvme_cmd.mptr = mptr;
  nvme_cmd.dptr.prp.prp1 = prp;
  nvme_cmd.cdw[0] = lba;              // cdw10 Starting lba bits 31:00
  nvme_cmd.cdw[2] = transfer_length;  // cdw12 nlb bits 15:00

  return StatusCode::kSuccess;
}

StatusCode Read(uint8_t rdprotect, bool fua, uint32_t lba,
                uint16_t transfer_length,
                nvme_defs::GenericQueueEntryCmd& nvme_cmd) {
  StatusCode status = LegacyRead(lba, transfer_length, nvme_cmd);

  if (status != StatusCode::kSuccess) {
    return status;
  }

  uint8_t prinfo;  // Protection Information field 4 bits;
  status = BuildPrinfo(rdprotect, prinfo);

  if (status != StatusCode::kSuccess) {
    return status;
  }

  nvme_cmd.cdw[2] |= (prinfo << 26);  // cdw12 prinfo bits 29:26
  nvme_cmd.cdw[2] |= (fua << 30);     // cdw12 fua bit 30;

  return StatusCode::kSuccess;
}

}  // namespace

StatusCode Read6ToNvme(absl::Span<const uint8_t> scsi_cmd,
                       nvme_defs::GenericQueueEntryCmd& nvme_cmd) {
  if (scsi_cmd.size() != sizeof(scsi_defs::Read6Command)) {
    DebugLog("Malformed Read6 command");
    return StatusCode::kInvalidInput;
  }

  scsi_defs::Read6Command read_cmd;
  if (!ReadValue(scsi_cmd, read_cmd)) {
    DebugLog("Unable to cast raw bytes to Read6 command");
    return StatusCode::kInvalidInput;
  }

  return LegacyRead(read_cmd.logical_block_address, read_cmd.transfer_length,
                    nvme_cmd);
}

StatusCode Read10ToNvme(absl::Span<const uint8_t> scsi_cmd,
                        nvme_defs::GenericQueueEntryCmd& nvme_cmd) {
  if (scsi_cmd.size() != sizeof(scsi_defs::Read10Command)) {
    DebugLog("Malformed Read10 command");
    return StatusCode::kInvalidInput;
  }

  scsi_defs::Read10Command read_cmd;
  if (!ReadValue(scsi_cmd, read_cmd)) {
    DebugLog("Unable to cast raw bytes to Read10 command");
    return StatusCode::kInvalidInput;
  }

  return Read(read_cmd.rdprotect, read_cmd.fua, read_cmd.logical_block_address,
              read_cmd.transfer_length, nvme_cmd);
}

StatusCode Read12ToNvme(absl::Span<const uint8_t> scsi_cmd,
                        nvme_defs::GenericQueueEntryCmd& nvme_cmd) {
  if (scsi_cmd.size() != sizeof(scsi_defs::Read12Command)) {
    DebugLog("Malformed Read12 command");
    return StatusCode::kInvalidInput;
  }

  scsi_defs::Read12Command read_cmd;
  if (!ReadValue(scsi_cmd, read_cmd)) {
    DebugLog("Unable to cast raw bytes to Read12 command");
    return StatusCode::kInvalidInput;
  }

  return Read(read_cmd.rdprotect, read_cmd.fua, read_cmd.logical_block_address,
              read_cmd.transfer_length, nvme_cmd);
}

StatusCode Read16ToNvme(absl::Span<const uint8_t> scsi_cmd,
                        nvme_defs::GenericQueueEntryCmd& nvme_cmd) {
  if (scsi_cmd.size() != sizeof(scsi_defs::Read16Command)) {
    DebugLog("Malformed Read16 command");
    return StatusCode::kInvalidInput;
  }

  scsi_defs::Read16Command read_cmd;
  if (!ReadValue(scsi_cmd, read_cmd)) {
    DebugLog("Unable to cast raw bytes to Read16 command");
    return StatusCode::kInvalidInput;
  }

  return Read(read_cmd.rdprotect, read_cmd.fua, read_cmd.logical_block_address,
              read_cmd.transfer_length, nvme_cmd);
}

StatusCode Read32ToNvme(absl::Span<const uint8_t> scsi_cmd,
                        nvme_defs::GenericQueueEntryCmd& nvme_cmd) {
  if (scsi_cmd.size() != sizeof(scsi_defs::Read32Command)) {
    DebugLog("Malformed Read32 command");
    return StatusCode::kInvalidInput;
  }

  scsi_defs::Read32Command read_cmd;
  if (!ReadValue(scsi_cmd, read_cmd)) {
    DebugLog("Unable to cast raw bytes to Read32 command");
    return StatusCode::kInvalidInput;
  }

  StatusCode status =
      Read(read_cmd.rdprotect, read_cmd.fua, read_cmd.logical_block_address,
           read_cmd.transfer_length, nvme_cmd);

  if (status != StatusCode::kSuccess) {
    return status;
  }

  SetLbaTags(read_cmd.eilbrt, read_cmd.elbat, read_cmd.lbatm, nvme_cmd);
  return StatusCode::kSuccess;
}

}  // namespace translator

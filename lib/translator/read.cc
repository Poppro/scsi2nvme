#include "read.h"

#include <cmath>
#include <initializer_list>
#include <iterator>

#include "absl/types/span.h"

namespace translator {

namespace {  // anonymous namespace for helper functions

uint8_t GetPrinfo(uint8_t rd_protect) {
  bool pract;      // Protection Information Action 1 bit
  uint8_t prchk;   // Protection Information Check 3 bits
  uint8_t prinfo;  // 4 bits

  switch (rd_protect) {
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
      // shouldn't reach here because we covered all possible values of
      // rd_protect
      break;
  }

  prinfo |= prchk;
  prinfo |= (pract << 3);

  return prinfo;
}

void SetLogicalBlockTags(uint32_t eilbrt, uint16_t elbat, uint16_t elbatm,
                         nvme_defs::GenericQueueEntryCmd& queue_entry) {
  // expected initial logical block reference
  queue_entry.cdw[4] = eilbrt;
  // expected logical block application tag
  queue_entry.cdw[5] |= elbat;
  // bits  31:16 expected logical block application tag mask
  queue_entry.cdw[5] |= (elbatm << 16);
}

void LegacyRead(uint32_t lba, uint16_t transfer_length,
                nvme_defs::GenericQueueEntryCmd& queue_entry,
                uint64_t prp_ptr) {
  queue_entry.opc = static_cast<uint8_t>(nvme_defs::NvmOpcode::kRead);

  queue_entry.psdt = 0b00;  // PRPs are used for data transfer
  queue_entry.fuse = 0b00;  // Normal operation, not fused
  queue_entry.mptr;  // field is valid only if the command has metadata that is
                     // not interleaved with  the logical block data, as
                     // specified in the Format NVM command.
  queue_entry.nsid = 0x0;  // not used for this command, cleared to 0

  queue_entry.dptr.prp.prp1 = prp_ptr;

  // cdw10 Starting lba bits 31:00
  queue_entry.cdw[0] = lba;

  // cdw12 nlb bits 15:00
  queue_entry.cdw[2] |= transfer_length;

  // cd12 bit 31 limited retry controller should apply limited retry efforts or apply
  // all avialbe error recovery means to return the data to the host
  queue_entry.cdw[2] != 0 << 31;
}

void Read(uint8_t rd_protect, bool fua, uint32_t lba, uint16_t transfer_length,
          nvme_defs::GenericQueueEntryCmd queue_entry, uint64_t prp_ptr) {
  LegacyRead(lba, transfer_length, queue_entry, prp_ptr);

  // cdw12 prinfo bits 29:26
  queue_entry.cdw[2] |= (GetPrinfo(rd_protect) << 26);
  // cdw12 fua bit 30;
  queue_entry.cdw[2] |= (fua << 30);

  uint8_t dsm;  // indicates attributes for lbas being read, e.g. compressible,
                // sequential request, access latency, access frequency
  queue_entry.cdw[3] |= dsm;
}

}  // namespace

void Read6ToNvme(scsi_defs::Read6Command cmd,
                 nvme_defs::GenericQueueEntryCmd queue_entry,
                 uint64_t prp_ptr) {
  return LegacyRead(cmd.logical_block_address, cmd.transfer_length, queue_entry,
                    prp_ptr);
}

void Read10ToNvme(scsi_defs::Read10Command cmd,
                  nvme_defs::GenericQueueEntryCmd queue_entry,
                  uint64_t prp_ptr) {
  return Read(cmd.rd_protect, cmd.fua, cmd.logical_block_address,
              cmd.transfer_length, queue_entry, prp_ptr);
}

void Read12ToNvme(scsi_defs::Read12Command cmd,
                  absl::Span<nvme_defs::GenericQueueEntryCmd> queue_entries,
                  uint64_t prp_ptr) {
  uint32_t transfer_length = cmd.transfer_length;
  uint8_t num_calls = transfer_length / pow(2, 16);

  Read(cmd.rd_protect, cmd.fua, cmd.logical_block_address,
       transfer_length % (uint16_t)pow(2, 16), queue_entries[0], prp_ptr);

  for (int i = 1; i <= num_calls; i++) {
    Read(cmd.rd_protect, cmd.fua, cmd.logical_block_address, pow(2, 16),
         queue_entries[i], prp_ptr + i * pow(2, 16));
  }
}

void Read16ToNvme(scsi_defs::Read16Command cmd,
                  absl::Span<nvme_defs::GenericQueueEntryCmd> queue_entries,
                  uint64_t prp_ptr) {
  uint32_t transfer_length = cmd.transfer_length;
  uint8_t num_calls = transfer_length / pow(2, 16);

  Read(cmd.rd_protect, cmd.fua, cmd.logical_block_address,
       transfer_length % (uint16_t)pow(2, 16), queue_entries[0], prp_ptr);

  for (int i = 1; i <= num_calls; i++) {
    Read(cmd.rd_protect, cmd.fua, cmd.logical_block_address, pow(2, 16),
         queue_entries[i], prp_ptr + i * pow(2, 16));
  }
}

void Read32ToNvme(scsi_defs::Read32Command cmd,
                  absl::Span<nvme_defs::GenericQueueEntryCmd> queue_entries,
                  uint64_t prp_ptr) {
  uint32_t transfer_length = cmd.transfer_length;
  uint8_t num_calls = transfer_length / pow(2, 16);

  Read(cmd.rd_protect, cmd.fua, cmd.logical_block_address,
       transfer_length % (uint16_t)pow(2, 16), queue_entries[0], prp_ptr);

  for (int i = 1; i <= num_calls; i++) {
    Read(cmd.rd_protect, cmd.fua, cmd.logical_block_address, pow(2, 16),
         queue_entries[i], prp_ptr + i * pow(2, 16));
    SetLogicalBlockTags(cmd.eilbrt, cmd.elbat, cmd.lbatm, queue_entries[i]);
  }
}

}  // namespace translator

/*
 * Copyright (C) 2021 fleroviux
 *
 * Licensed under GPLv3 or any later version.
 * Refer to the included LICENSE file.
 */

#pragma once

#include <common/log.hpp>
#include <common/m4a.hpp>
#include <common/punning.hpp>
#include <emulator/cartridge/backup/backup.hpp>
#include <emulator/cartridge/gpio/gpio.hpp>
#include <emulator/cartridge/game_pak.hpp>
#include <emulator/config/config.hpp>
#include <memory>
#include <type_traits>

#include "arm/arm7tdmi.hpp"
#include "hw/apu/apu.hpp"
#include "hw/ppu/ppu.hpp"
#include "hw/dma.hpp"
#include "hw/interrupt.hpp"
#include "hw/serial.hpp"
#include "hw/timer.hpp"
#include "scheduler.hpp"

namespace nba::core {

struct CPU final : private arm::ARM7TDMI, private arm::MemoryBase {
  using Access = arm::MemoryBase::Access;

  CPU(std::shared_ptr<Config> config);

  void Reset();
  void RunFor(int cycles);

  enum MemoryRegion {
    REGION_BIOS  = 0,
    REGION_EWRAM = 2,
    REGION_IWRAM = 3,
    REGION_MMIO  = 4,
    REGION_PRAM  = 5,
    REGION_VRAM  = 6,
    REGION_OAM   = 7,
    REGION_ROM_W0_L = 8,
    REGION_ROM_W0_H = 9,
    REGION_ROM_W1_L = 0xA,
    REGION_ROM_W1_H = 0xB,
    REGION_ROM_W2_L = 0xC,
    REGION_ROM_W2_H = 0xD,
    REGION_SRAM_1 = 0xE,
    REGION_SRAM_2 = 0xF
  };

  enum class HaltControl {
    RUN,
    STOP,
    HALT
  };

  std::shared_ptr<Config> config;

  struct SystemMemory {
    u8 bios[0x04000];
    u8 wram[0x40000];
    u8 iram[0x08000];

    // TODO: remove this, it's obsolete.
    struct ROM {
      std::unique_ptr<uint8_t[]> data;
      size_t size;
      u32 mask = 0x1FFFFFF;
      std::unique_ptr<nba::GPIO> gpio;
      std::unique_ptr<nba::Backup> backup_sram;
      std::unique_ptr<nba::Backup> backup_eeprom;
    } rom;

    u32 bios_latch = 0;
  } memory;

  GamePak game_pak;

  struct MMIO {
    u16 keyinput = 0x3FF;
    u16 rcnt_hack = 0;
    u8 postflg = 0;
    HaltControl haltcnt = HaltControl::RUN;

    struct WaitstateControl {
      int sram = 0;
      int ws0_n = 0;
      int ws0_s = 0;
      int ws1_n = 0;
      int ws1_s = 0;
      int ws2_n = 0;
      int ws2_s = 0;
      int phi = 0;
      int prefetch = 0;
      int cgb = 0;
    } waitcnt;

    struct KeyControl {
      uint16_t input_mask = 0;
      bool interrupt = false;
      bool and_mode = false;
    } keycnt;

  } mmio;

  Scheduler scheduler;
  IRQ irq;
  DMA dma;
  APU apu;
  PPU ppu;
  Timer timer;
  SerialBus serial_bus;

private:
  bool IsGPIOAccess(u32 address) {
    // NOTE: we do not check if the address lies within ROM, since
    // it is not required in the context. This should be reflected in the name though.
    return memory.rom.gpio && address >= 0xC4 && address <= 0xC8;
  }

  bool IsEEPROMAccess(u32 address) {
    return memory.rom.backup_eeprom && ((~memory.rom.size & 0x02000000) || address >= 0x0DFFFF00);
  }

  auto ReadMMIO(u32 address) -> u8;
  void WriteMMIO(u32 address, u8 value);
  void WriteMMIO16(u32 address, u16 value);
  auto ReadBIOS(u32 address) -> u32;
  auto ReadUnused(u32 address) -> u32;

  template<typename T>
  auto Read(u32 address, Access access) -> T;

  template<typename T>
  void Write(u32 address, T value, Access access);

  auto ReadByte(u32 address, Access access) -> u8  final {
    return Read<u8>(address, access);
  }

  auto ReadHalf(u32 address, Access access) -> u16 final {
    return Read<u16>(address, access);
  }

  auto ReadWord(u32 address, Access access) -> u32 final {
    return Read<u32>(address, access);
  }

  void WriteByte(u32 address, u8  value, Access access) final {
    Write<u8>(address, value, access);
  }

  void WriteHalf(u32 address, u16 value, Access access) final {
    Write<u16>(address, value, access);
  }

  void WriteWord(u32 address, u32 value, Access access) final {
    Write<u32>(address, value, access);
  }

  void Tick(int cycles);
  void Idle() final;
  void PrefetchStepRAM(int cycles);
  void PrefetchStepROM(u32 address, int cycles);
  void UpdateMemoryDelayTable();

  void M4ASearchForSampleFreqSet();
  void M4ASampleFreqSetHook();
  void M4AFixupPercussiveChannels();

  void CheckKeypadInterrupt();
  void OnKeyPress();

  M4ASoundInfo* m4a_soundinfo;
  int m4a_original_freq = 0;
  u32 m4a_setfreq_address = 0;

  /* GamePak prefetch buffer state. */
  struct Prefetch {
    bool active = false;
    bool rom_code_access = false;
    u32 head_address;
    u32 last_address;
    int count = 0;
    int capacity = 8;
    int opcode_width = 4;
    int countdown;
    int duty;
  } prefetch;

  bool bus_is_controlled_by_dma;
  bool openbus_from_dma;

  int cycles16[2][256] {
    { 1, 1, 3, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1 },
    { 1, 1, 3, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1 },
  };

  int cycles32[2][256] {
    { 1, 1, 6, 1, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 1 },
    { 1, 1, 6, 1, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 1 }
  };

  static constexpr int s_ws_nseq[4] = { 4, 3, 2, 8 }; /* Non-sequential SRAM/WS0/WS1/WS2 */
  static constexpr int s_ws_seq0[2] = { 2, 1 };       /* Sequential WS0 */
  static constexpr int s_ws_seq1[2] = { 4, 1 };       /* Sequential WS1 */
  static constexpr int s_ws_seq2[2] = { 8, 1 };       /* Sequential WS2 */
};

#include "cpu-memory.inl"

} // namespace nba::core

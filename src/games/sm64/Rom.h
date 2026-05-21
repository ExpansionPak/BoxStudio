#pragma once

#include <string>
#include <vector>
#include <cstdint>

// -------------------------------------------------------
// The first 64 bytes of any SM64 ROM contain a header.
// This struct maps out the fields we care about.
// uint8_t  = 1 byte  (0-255)
// uint32_t = 4 bytes (0-4,294,967,295)
// -------------------------------------------------------
struct RomHeader
{
    uint8_t  pi_bsd_dom1_lat_r;   // byte 0   - always 0x80 for .z64
    uint8_t  pi_bsd_dom1_pd2_r;   // byte 1   - always 0x37
    uint8_t  pi_bsd_dom1_pgs_r;   // byte 2   - always 0x12
    uint8_t  pi_bsd_dom1_pwd_r;   // byte 3   - always 0x40
    uint32_t clock_rate;          // bytes 4-7
    uint32_t program_counter;     // bytes 8-11
    uint32_t release;             // bytes 12-15
    uint32_t crc1;                // bytes 16-19  - checksum part 1
    uint32_t crc2;                // bytes 20-23  - checksum part 2
    uint8_t  unknown[8];          // bytes 24-31  - unused
    char     title[20];           // bytes 32-51  - game title (e.g. "SUPER MARIO 64")
    uint8_t  unknown2[7];         // bytes 52-58  - unused
    uint8_t  media_format;        // byte 59
    char     game_id[4];          // bytes 60-63  - e.g. "SMSE" or "NSME"
};

// -------------------------------------------------------
// Rom - holds the entire ROM in memory and provides
// helper functions for reading data out of it.
// -------------------------------------------------------
struct Rom
{
    // --- Data ---

    std::vector<uint8_t> data;  // the raw ROM bytes, loaded entirely into memory
    RomHeader            header;
    std::string          filepath;
    bool                 loaded = false;

    // --- Functions ---

    // Load a .z64 ROM from disk.
    // Returns true on success, false if the file couldn't be opened
    // or doesn't look like a valid SM64 ROM.
    bool load(const std::string& path);

    // Unload the ROM and clear all data.
    void unload();

    // Read a single byte at a given offset.
    uint8_t  read_u8 (uint32_t offset) const;

    // Read a 2-byte unsigned integer (big endian).
    uint16_t read_u16(uint32_t offset) const;

    // Read a 4-byte unsigned integer (big endian).
    uint32_t read_u32(uint32_t offset) const;

    // Returns true if the offset + size doesn't go past the end of the ROM.
    // Always call this before reading if you're not 100% sure of the offset.
    bool in_bounds(uint32_t offset, uint32_t size = 1) const;
};
// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads the in-memory image size (SizeOfImage) of a Windows x64 PE file. The
// MSVC linker's 2 GiB limit (LNK1248) applies to this value, not to the file
// length, so the exporter reports it for the launcher and every module DLL.
//
// Header-only, standard library only, no Windows headers: it is used by the
// exporter and by tests on synthetic byte buffers.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace suyu::recomp {

// The largest image the MSVC x64 linker accepts.
inline constexpr std::uint64_t kPeImageLimitBytes = std::uint64_t{2} * 1024 * 1024 * 1024;

namespace pe_detail {

inline constexpr std::size_t kDosHeaderSize = 64;
inline constexpr std::size_t kDosLfanewOffset = 0x3C;
inline constexpr std::size_t kPeSignatureSize = 4;
inline constexpr std::size_t kCoffHeaderSize = 20;
inline constexpr std::size_t kCoffMachineOffset = 0;
inline constexpr std::size_t kCoffSizeOfOptionalHeaderOffset = 16;
inline constexpr std::size_t kOptionalMagicOffset = 0;
// Offset of SizeOfImage inside the optional header (same for PE32 and PE32+).
inline constexpr std::size_t kOptionalSizeOfImageOffset = 56;
inline constexpr std::uint16_t kDosMagic = 0x5A4D;      // "MZ"
inline constexpr std::uint16_t kMachineAmd64 = 0x8664;
inline constexpr std::uint16_t kOptionalMagicPe32Plus = 0x20B;

inline std::uint16_t Read16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

inline std::uint32_t Read32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

inline std::nullopt_t Fail(std::string* error, std::string message) {
    if (error) {
        *error = std::move(message);
    }
    return std::nullopt;
}

} // namespace pe_detail

// SizeOfImage of a PE32+ x64 image held in `bytes` (at least the headers). Returns
// nullopt with `error` set when the headers are truncated or not an x64 PE32+ image.
inline std::optional<std::uint64_t> ParsePeSizeOfImage(const std::vector<std::uint8_t>& bytes,
                                                       std::string* error) {
    using namespace pe_detail;
    if (bytes.size() < kDosHeaderSize || Read16(bytes, 0) != kDosMagic) {
        return Fail(error, "not a PE file (missing MZ header)");
    }
    const std::uint64_t pe_offset = Read32(bytes, kDosLfanewOffset);
    const std::uint64_t optional_offset = pe_offset + kPeSignatureSize + kCoffHeaderSize;
    if (pe_offset < kDosHeaderSize ||
        optional_offset + kOptionalSizeOfImageOffset + sizeof(std::uint32_t) > bytes.size()) {
        return Fail(error, "PE header offset (e_lfanew) is outside the file");
    }
    const std::size_t pe = static_cast<std::size_t>(pe_offset);
    if (bytes[pe] != 'P' || bytes[pe + 1] != 'E' || bytes[pe + 2] != 0 || bytes[pe + 3] != 0) {
        return Fail(error, "PE signature not found");
    }
    if (Read16(bytes, pe + kPeSignatureSize + kCoffMachineOffset) != kMachineAmd64) {
        return Fail(error, "PE machine is not x64");
    }
    // The COFF header declares how long the optional header is; SizeOfImage must be inside
    // that declared extent, not merely inside the buffer.
    const std::size_t declared_optional_size =
        Read16(bytes, pe + kPeSignatureSize + kCoffSizeOfOptionalHeaderOffset);
    if (declared_optional_size < kOptionalSizeOfImageOffset + sizeof(std::uint32_t)) {
        return Fail(error, "PE optional header is too short to hold SizeOfImage (declared " +
                               std::to_string(declared_optional_size) + " bytes)");
    }
    const std::size_t optional_header = static_cast<std::size_t>(optional_offset);
    if (optional_offset + declared_optional_size > bytes.size()) {
        return Fail(error, "PE optional header (declared " +
                               std::to_string(declared_optional_size) +
                               " bytes) extends past the end of the file");
    }
    if (Read16(bytes, optional_header + kOptionalMagicOffset) != kOptionalMagicPe32Plus) {
        return Fail(error, "PE optional header is not PE32+");
    }
    const std::uint32_t size_of_image = Read32(bytes, optional_header + kOptionalSizeOfImageOffset);
    if (size_of_image == 0) {
        return Fail(error, "PE SizeOfImage is zero");
    }
    return size_of_image;
}

// Largest e_lfanew the file reader supports. Linker-produced images put the PE header right
// after the DOS stub (a few hundred bytes); a larger offset is reported as unsupported
// instead of as a missing header.
inline constexpr std::uint32_t kPeMaxHeaderOffsetBytes = 1024 * 1024;

// SizeOfImage of the PE file at `path` (std::filesystem handles non-ASCII paths). Reads the
// DOS header, then the header region at the validated e_lfanew.
inline std::optional<std::uint64_t> ReadPeSizeOfImage(const std::filesystem::path& path,
                                                      std::string* error) {
    using namespace pe_detail;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Fail(error, "cannot open the file");
    }
    std::vector<std::uint8_t> bytes(kDosHeaderSize);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(file.gcount()));
    if (bytes.size() < kDosHeaderSize || Read16(bytes, 0) != kDosMagic) {
        return Fail(error, "not a PE file (missing MZ header)");
    }
    const std::uint32_t pe_offset = Read32(bytes, kDosLfanewOffset);
    if (pe_offset > kPeMaxHeaderOffsetBytes) {
        return Fail(error, "PE header offset (e_lfanew " + std::to_string(pe_offset) +
                               ") exceeds the supported limit of " +
                               std::to_string(kPeMaxHeaderOffsetBytes) + " bytes");
    }
    const auto read_prefix = [&](std::size_t length) {
        bytes.assign(length, 0);
        file.clear();
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));
        bytes.resize(static_cast<std::size_t>(file.gcount()));
    };
    // First the PE signature and COFF header, which declare the optional header's length;
    // then the bounded extent through the end of that declared header, so a header that
    // the file does not fully contain is refused by the parser.
    const std::size_t optional_start =
        static_cast<std::size_t>(pe_offset) + kPeSignatureSize + kCoffHeaderSize;
    read_prefix(optional_start);
    std::size_t declared_optional_size = 0;
    if (bytes.size() == optional_start) {
        declared_optional_size =
            Read16(bytes, static_cast<std::size_t>(pe_offset) + kPeSignatureSize +
                              kCoffSizeOfOptionalHeaderOffset);
    }
    // At least through SizeOfImage, so a too-short declaration is reported as such.
    constexpr std::size_t kMinOptionalBytes = kOptionalSizeOfImageOffset + sizeof(std::uint32_t);
    read_prefix(optional_start + std::max(declared_optional_size, kMinOptionalBytes));
    return ParsePeSizeOfImage(bytes, error);
}

} // namespace suyu::recomp

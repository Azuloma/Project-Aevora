# SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Script mode: cmake -DINPUT=<file> -DOUTPUT=<cpp> -DSYMBOL=<name> -P EmbedBinary.cmake
# Writes <SYMBOL>_words (little-endian uint64 words, zero padded) and <SYMBOL>_size_bytes.
foreach(_required INPUT OUTPUT SYMBOL)
    if (NOT DEFINED ${_required})
        message(FATAL_ERROR "EmbedBinary.cmake needs -D${_required}=")
    endif()
endforeach()

file(READ "${INPUT}" _hex HEX)
string(LENGTH "${_hex}" _hex_length)
if (_hex_length EQUAL 0)
    message(FATAL_ERROR "EmbedBinary.cmake: ${INPUT} is empty or unreadable")
endif()
math(EXPR _size_bytes "${_hex_length} / 2")

math(EXPR _padding_digits "(16 - ${_hex_length} % 16) % 16")
if (_padding_digits GREATER 0)
    string(REPEAT "0" ${_padding_digits} _padding)
    string(APPEND _hex "${_padding}")
endif()

set(_byte "([0-9a-f][0-9a-f])")
string(REGEX REPLACE "${_byte}${_byte}${_byte}${_byte}${_byte}${_byte}${_byte}${_byte}"
       "0x\\8\\7\\6\\5\\4\\3\\2\\1ULL," _words "${_hex}")

file(WRITE "${OUTPUT}"
    "#include <cstddef>\n#include <cstdint>\n"
    "extern \"C\" {\n"
    "extern const std::uint64_t ${SYMBOL}_words[];\n"
    "extern const std::size_t ${SYMBOL}_size_bytes;\n"
    "const std::uint64_t ${SYMBOL}_words[] = {${_words}};\n"
    "const std::size_t ${SYMBOL}_size_bytes = ${_size_bytes};\n"
    "}\n")

# Embed a SPIR-V .spv file as a uint32_t[] C header.
# Invoke with: cmake -DINPUT_FILE=<path> -DOUTPUT_FILE=<path> -DARRAY_NAME=<name> -P EmbedSpv.cmake

file(READ "${INPUT_FILE}" SPV_HEX HEX)
string(LENGTH "${SPV_HEX}" HEX_LEN)
math(EXPR WORD_COUNT "${HEX_LEN} / 8")

if(WORD_COUNT EQUAL 0)
    message(FATAL_ERROR "EmbedSpv: empty or malformed SPIR-V file: ${INPUT_FILE}")
endif()

# SPIR-V is little-endian on x86. CMake reads bytes left-to-right as hex pairs.
# For bytes b0,b1,b2,b3 in file order, the uint32_t literal must be 0xb3b2b1b0
# so that when the compiler stores it in memory (little-endian) it produces the
# original byte sequence that Vulkan expects.
set(words "")
math(EXPR LAST "${WORD_COUNT} - 1")
foreach(i RANGE 0 ${LAST})
    math(EXPR off "${i} * 8")
    string(SUBSTRING "${SPV_HEX}" ${off} 8 chunk)
    string(SUBSTRING "${chunk}" 0 2 b0)
    string(SUBSTRING "${chunk}" 2 2 b1)
    string(SUBSTRING "${chunk}" 4 2 b2)
    string(SUBSTRING "${chunk}" 6 2 b3)
    list(APPEND words "0x${b3}${b2}${b1}${b0}")
endforeach()

string(JOIN ", " words_str ${words})

file(WRITE "${OUTPUT_FILE}"
    "// Auto-generated from SPIR-V — do not edit\n"
    "#pragma once\n"
    "#include <cstdint>\n"
    "static const uint32_t ${ARRAY_NAME}[] = {${words_str}};\n"
    "static const uint32_t ${ARRAY_NAME}_size = sizeof(${ARRAY_NAME});\n"
)

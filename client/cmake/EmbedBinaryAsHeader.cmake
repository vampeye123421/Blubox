if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE are required")
endif()

file(READ "${INPUT_FILE}" FONT_HEX HEX)
string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1," FONT_BYTES "${FONT_HEX}")
file(WRITE "${OUTPUT_FILE}"
    "#pragma once\n"
    "#include <cstddef>\n"
    "namespace blubox {\n"
    "inline constexpr unsigned char kUiFontData[] = {${FONT_BYTES}};\n"
    "inline constexpr int kUiFontDataSize = static_cast<int>(sizeof(kUiFontData));\n"
    "}\n"
)

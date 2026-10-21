if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE are required")
endif()

file(READ "${INPUT_FILE}" FONT_HEX HEX)
string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1," FONT_BYTES "${FONT_HEX}")
if(NOT DEFINED SYMBOL_NAME)
    set(SYMBOL_NAME "kUiFontData")
endif()
if(NOT DEFINED SIZE_SYMBOL_NAME)
    set(SIZE_SYMBOL_NAME "kUiFontDataSize")
endif()

file(WRITE "${OUTPUT_FILE}"
    "#pragma once\n"
    "#include <cstddef>\n"
    "namespace blubox {\n"
    "inline constexpr unsigned char ${SYMBOL_NAME}[] = {${FONT_BYTES}};\n"
    "inline constexpr int ${SIZE_SYMBOL_NAME} = static_cast<int>(sizeof(${SYMBOL_NAME}));\n"
    "}\n"
)

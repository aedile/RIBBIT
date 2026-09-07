# Platform-independent Frogger emulation core (shared with the host harness in ../../host)
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../core)
idf_component_register(
    SRCS "${CORE_DIR}/frogger.c" "${CORE_DIR}/frogger_video.c" "${CORE_DIR}/ay8910.c" "${CORE_DIR}/z80/Z80.c"
    INCLUDE_DIRS "${CORE_DIR}" "${CORE_DIR}/z80"
    LDFRAGMENTS "linker.lf"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fjump-tables -ftree-switch-conversion -DLSB_FIRST)

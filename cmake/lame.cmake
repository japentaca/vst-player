# Fetches LAME 3.100 (MP3 encoder, LGPL 2.0+) and builds libmp3lame as a static library: the exe
# records MP3 without lame.exe or a DLL. LAME has no CMake build, so the library sources are listed
# here (same list as its Makefile.MSVC, without the frontend and the mpglib decoder) and its
# configMS.h becomes config.h. Third-party code: compiled without our warning flags.
include(FetchContent)

set(VSTPLAYER_LAME_VERSION "3.100")

FetchContent_Declare(lame
    URL "https://downloads.sourceforge.net/project/lame/lame/${VSTPLAYER_LAME_VERSION}/lame-${VSTPLAYER_LAME_VERSION}.tar.gz"
        "https://sourceforge.net/projects/lame/files/lame/${VSTPLAYER_LAME_VERSION}/lame-${VSTPLAYER_LAME_VERSION}.tar.gz/download"
    URL_HASH SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
    DOWNLOAD_NAME "lame-${VSTPLAYER_LAME_VERSION}.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(lame) # no CMakeLists.txt in the tarball: only downloads and extracts

set(_lame_dir "${lame_SOURCE_DIR}/libmp3lame")
configure_file("${lame_SOURCE_DIR}/configMS.h" "${CMAKE_CURRENT_BINARY_DIR}/lame-config/config.h" COPYONLY)

add_library(mp3lame STATIC
    "${_lame_dir}/bitstream.c"
    "${_lame_dir}/encoder.c"
    "${_lame_dir}/fft.c"
    "${_lame_dir}/gain_analysis.c"
    "${_lame_dir}/id3tag.c"
    "${_lame_dir}/lame.c"
    "${_lame_dir}/newmdct.c"
    "${_lame_dir}/psymodel.c"
    "${_lame_dir}/quantize.c"
    "${_lame_dir}/quantize_pvt.c"
    "${_lame_dir}/vector/xmm_quantize_sub.c"
    "${_lame_dir}/set_get.c"
    "${_lame_dir}/vbrquantize.c"
    "${_lame_dir}/reservoir.c"
    "${_lame_dir}/tables.c"
    "${_lame_dir}/takehiro.c"
    "${_lame_dir}/util.c"
    "${_lame_dir}/mpglib_interface.c"
    "${_lame_dir}/VbrTag.c"
    "${_lame_dir}/presets.c"
    "${_lame_dir}/version.c")

target_include_directories(mp3lame
    PUBLIC "${lame_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/lame-config" "${_lame_dir}" "${lame_SOURCE_DIR}")

target_compile_definitions(mp3lame PRIVATE HAVE_CONFIG_H _CRT_SECURE_NO_WARNINGS)
target_compile_options(mp3lame PRIVATE /W0 /MP)

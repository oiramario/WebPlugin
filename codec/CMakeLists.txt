# -- codec/CMakeLists.txt - WebPCodec static library -----------------------
#
# Links against vendored libwebp (codec/libwebp/). All libwebp examples,
# tools, and tests are disabled below - we only need the core decode libs.
#

# -- Suppress vendored libwebp targets -------------------------------------
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "")
set(WEBP_BUILD_CWEBP       OFF CACHE BOOL "")
set(WEBP_BUILD_DWEBP       OFF CACHE BOOL "")
set(WEBP_BUILD_GIF2WEBP    OFF CACHE BOOL "")
set(WEBP_BUILD_IMG2WEBP    OFF CACHE BOOL "")
set(WEBP_BUILD_VWEBP       OFF CACHE BOOL "")
set(WEBP_BUILD_WEBPINFO    OFF CACHE BOOL "")
set(WEBP_BUILD_WEBPMUX     OFF CACHE BOOL "")

set(WEBP_BUILD_LIBWEBPMUX  OFF CACHE BOOL "")
set(WEBP_BUILD_EXTRAS      OFF CACHE BOOL "")
set(WEBP_BUILD_TESTS       OFF CACHE BOOL "")
set(WEBP_BUILD_WEBP_JS     OFF CACHE BOOL "")
set(WEBP_USE_THREAD        ON  CACHE BOOL "")   # multi-threaded decode for larger stills
set(WEBP_NEAR_LOSSLESS     OFF CACHE BOOL "")   # encoding-only; not needed by decoder
set(WEBP_ENABLE_SIMD       ON  CACHE BOOL "")

# -- Vendored libwebp ------------------------------------------------------
add_subdirectory(libwebp)

# webpdemux carries target_link_libraries(webpdemux webp) from vendor code,
# which drags in encoder code + sharpyuv.  Redirect it to webpdecoder so
# those never get compiled.
set_target_properties(webpdemux PROPERTIES
    LINK_LIBRARIES "webpdecoder"
    INTERFACE_LINK_LIBRARIES "webpdecoder"
)
set_target_properties(sharpyuv webpencode webpdsp webputils webp PROPERTIES EXCLUDE_FROM_ALL TRUE)

# -- Our static library ----------------------------------------------------
add_library(WebPCodec STATIC
    WebPDecoder.cpp
)

# Use webpdecoder instead of webp: webp includes encoder code and links
# sharpyuv; webpdecoder compiles decode-only sources with no encoder/sharpyuv.
target_link_libraries(WebPCodec PRIVATE
    webpdecoder   # decoder-only core
    webpdemux     # animated frame metadata
)

# Propagate trace flag to our codec source
target_compile_definitions(WebPCodec PUBLIC
    $<$<BOOL:${DEBUG_WEBP_TRACE}>:DEBUG_WEBP_TRACE=1>
)

# Public include: consumers can #include "WebPDecoder.h"
target_include_directories(WebPCodec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# webpdecoder's INTERFACE include paths (added by libwebp) lack src/,
# and we redirect webpdemux away from webp (which has src/),
# so explicitly add the libwebp src include for webp/decode.h / webp/demux.h.
target_include_directories(WebPCodec PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libwebp/src
)

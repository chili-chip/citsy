# Download Bitsy 8.15's engine scripts and fonts into tests/parity/reference/bitsy-8.15.
#
# Usage: cmake -P scripts/fetch_bitsy.cmake
#
# The parity tests (tests/parity/bitsy_reference.mjs), the built-in font test
# (tests/unit/test_textbox.cpp) and scripts/gen_builtin_fonts.py read these
# files. They are not stored in this repository: each one is downloaded from
# the v8.15 commit on Codeberg and checked against its SHA-256. A file that is
# already there with the right hash is not downloaded again. Configuring with
# CITSY_BUILD_TESTS=ON runs this script.

set(BITSY_COMMIT "6c0238f6526c3a608bd711cf465bfe51f55573ad")  # tag v8.15
set(BITSY_URL "https://codeberg.org/adamledoux/bitsy/raw/commit/${BITSY_COMMIT}")
get_filename_component(BITSY_DIR "${CMAKE_CURRENT_LIST_DIR}/../tests/parity/reference/bitsy-8.15" ABSOLUTE)

# <name in BITSY_DIR> <path upstream> <sha256>
set(BITSY_FILES
    CREDITS.md CREDITS.md 8592dd7ea1e9a53e70812012304439e35da2a9055753c78dc3b862212566e5b4
    LICENSE.md LICENSE.md d680407dd64ddf37fd1b7aef13a628c27ee7a1e5da4f61514ef4495179399cf9
    world.js editor/script/engine/world.js 266b5e6d4f0bdad600c5986b7561d1427c2357c1803b8f0b0a6644c84ebfe556
    sound.js editor/script/engine/sound.js b1413f18e9d200494f79ee26d66306f2c21ab822bbd8fc5a41fc6e3975c17bf7
    font.js editor/script/engine/font.js 4c9c4ed2aa5b66714d5b7eb06597adf7d9679b7b4123fb5fe5f999c3419e457f
    transition.js editor/script/engine/transition.js cdff563e0c288caf443949935570dd2a16d872f9d5fe62023d3543c33b72f2af
    script.js editor/script/engine/script.js 875e2d31a40b01390de9a6c64a247b1d18c05787533e83e84f401ef19cb5acd0
    dialog.js editor/script/engine/dialog.js a4f90cbef51fa0fbd91c8d2bdf9cc1d8b66a309aa234ff541df30eabdffe4ae7
    renderer.js editor/script/engine/renderer.js 4a82fc5621e8e3f49ffe393977dc9780b30b2369eadace48c421dba710abc7ba
    bitsy.js editor/script/engine/bitsy.js 6299045324a83aeff605f63f9de225f8a5f2e98eb7a6079f7c881e29be4c76a4
    ascii_small.bitsyfont dev/resources/bitsyfont/ascii_small.bitsyfont 87f0a1972c5026497b83e98fc71a0b0952cbe818fa7c796315943c178becff44
    unicode_european_small.bitsyfont dev/resources/bitsyfont/unicode_european_small.bitsyfont a170ba3583cca3966fb08f3e1643122b15dc9939444502e7433d1b7eb9338408
    unicode_european_large.bitsyfont dev/resources/bitsyfont/unicode_european_large.bitsyfont e9ca0fb59df053f7b8c509d9b9190c05a63657a28a836679261e554b5d44ade8
    arabic.bitsyfont dev/resources/bitsyfont/arabic.bitsyfont 652fade8f063f8f2ebaa87b58b793c189ba9088f8f1875ac175b1a49bfbb2ee5
)

list(LENGTH BITSY_FILES _count)
math(EXPR _last "${_count} - 1")
foreach(_i RANGE 0 ${_last} 3)
    math(EXPR _j "${_i} + 1")
    math(EXPR _k "${_i} + 2")
    list(GET BITSY_FILES ${_i} _name)
    list(GET BITSY_FILES ${_j} _path)
    list(GET BITSY_FILES ${_k} _hash)
    set(_dest "${BITSY_DIR}/${_name}")
    if(EXISTS "${_dest}")
        file(SHA256 "${_dest}" _have)
        if(_have STREQUAL _hash)
            continue()
        endif()
    endif()
    message(STATUS "Downloading Bitsy 8.15 ${_path}")
    file(DOWNLOAD "${BITSY_URL}/${_path}" "${_dest}"
         EXPECTED_HASH SHA256=${_hash}
         TLS_VERIFY ON
         STATUS _status)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        file(REMOVE "${_dest}")
        list(GET _status 1 _error)
        message(FATAL_ERROR "Could not download ${BITSY_URL}/${_path}: ${_error}")
    endif()
endforeach()

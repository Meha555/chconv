set(MAGIC_LIB)
set(MAGIC_MGC_FILE)
if(WIN32)
    find_package(unofficial-libmagic REQUIRED)
    if(TARGET unofficial::libmagic::libmagic)
        set(MAGIC_LIB unofficial::libmagic::libmagic)
        set(MAGIC_MGC_FILE ${unofficial-libmagic_DICTIONARY})
    endif()
else()
    find_library(MAGIC_LIB NAMES magic)
    find_file(MAGIC_MGC_FILE
        NAMES magic.mgc
        PATHS /usr/share/libmagic /usr/local/share/libmagic
        NO_DEFAULT_PATH
    )
endif()

set(MAGIC_VERSION "5.46")
if(EMBED_MAGIC_MGC_FILE OR NOT MAGIC_MGC_FILE)
    message(NOTICE "magic.mgc not found, using internal version")
    set(EMBED_MAGIC_MGC_FILE ON CACHE BOOL "Embed magic.mgc file database" FORCE)
    set(MAGIC_MGC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/misc/magic.mgc)
endif()
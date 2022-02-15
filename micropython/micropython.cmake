# Create an INTERFACE library for our C module.
add_library(usermod_cexample INTERFACE)

# Add our source files to the lib
target_sources(usermod_cexample INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/mp_quirc.c
    #${CMAKE_CURRENT_LIST_DIR}/../lib/decode.c"
    #${CMAKE_CURRENT_LIST_DIR}/../lib/identify.c"
    #${CMAKE_CURRENT_LIST_DIR}/../lib/version_db.c"
    #${CMAKE_CURRENT_LIST_DIR}/../lib/quirc.c"
)

# Add the current directory as an include directory.
target_include_directories(usermod_cexample INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Link our INTERFACE library to the usermod target.
target_link_libraries(usermod INTERFACE usermod_cexample)

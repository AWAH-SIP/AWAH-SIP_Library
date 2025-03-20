cmake_minimum_required(VERSION 3.16)

include(ExternalProject)

# Create custom targets for PJSIP
ExternalProject_Add(pjproject_build
    SOURCE_DIR ${PJSIP_SOURCE_DIR}
    CONFIGURE_COMMAND sh -c "CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} ./configure --prefix=${PJSIP_INSTALL_DIR} --disable-video"
    BUILD_COMMAND make dep && make
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE 1
    LOG_OUTPUT_ON_FAILURE 1
)


message(STATUS "PJSIP_INSTALL_DIR: ${PJSIP_INSTALL_DIR}")
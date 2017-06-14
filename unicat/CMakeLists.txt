INCLUDE_DIRECTORIES(
${PROJECT_SOURCE_DIR}/unicat/include
${PROJECT_SOURCE_DIR}/unicat/src)

ADD_LIBRARY(unicat MODULE
    src/backends/fabric
    src/backends/backend
    src/backends/unicorn
    src/backends/storage
    src/unicat
    src/module)
    

TARGET_LINK_LIBRARIES(unicat
    blackhole
    cocaine-core
    cocaine-io-util
    ${Boost_LIBRARIES})

SET_TARGET_PROPERTIES(unicat PROPERTIES
    PREFIX ""
    SUFFIX "${COCAINE_PLUGIN_SUFFIX}"
    COMPILE_FLAGS "-std=c++11 -O2 -Wall -pedantic -Wnon-virtual-dtor")

INSTALL(TARGETS unicat
    LIBRARY DESTINATION lib/cocaine
    COMPONENT runtime)

INSTALL(
    DIRECTORY
        include/cocaine/idl
    DESTINATION include/cocaine
    COMPONENT development)

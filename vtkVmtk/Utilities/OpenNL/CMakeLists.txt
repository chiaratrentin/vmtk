SET (OPENNL_SRCS
    nl_single_file.c
    )

INCLUDE_DIRECTORIES(${OPENNL_SOURCE_DIR})

ADD_LIBRARY (nl STATIC ${OPENNL_SRCS})

IF(NOT WIN32)
  SET_TARGET_PROPERTIES(nl PROPERTIES COMPILE_FLAGS -fPIC)
ENDIF(NOT WIN32)

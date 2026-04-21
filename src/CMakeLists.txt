# SPDX-License-Identifier: Unlicense

target_sources( ${PROJECT_NAME}
    PRIVATE
        StreamWeaver.cpp
        StreamWeaver.h
        StreamWeaverFFT.cpp
        StreamWeaverFFT.h
        GrainsDatabaseResource.cpp
        GrainsDatabaseResource.h
        RegisterExtension.cpp
)

target_include_directories( ${PROJECT_NAME}
    PRIVATE
        "src"
)

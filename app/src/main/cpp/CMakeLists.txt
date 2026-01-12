cmake_minimum_required(VERSION 3.22.1)

project(u3d LANGUAGES C CXX)

# --------------------------------------------------
# Native App Glue
# --------------------------------------------------
set(NATIVE_APP_GLUE_DIR
        ${ANDROID_NDK}/sources/android/native_app_glue
)

add_library(
        native_app_glue
        STATIC
        ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c
)

target_include_directories(
        native_app_glue
        PUBLIC
        ${NATIVE_APP_GLUE_DIR}
)

# --------------------------------------------------
# Main native library
# --------------------------------------------------
add_library(
        u3d
        SHARED
        main.cpp
)

target_include_directories(
        u3d
        PRIVATE
        ${NATIVE_APP_GLUE_DIR}
)

# --------------------------------------------------
# System libraries
# --------------------------------------------------
find_library(android-lib android)
find_library(log-lib log)
find_library(egl-lib EGL)
find_library(glesv2-lib GLESv2)

# --------------------------------------------------
# Link
# --------------------------------------------------
target_link_libraries(
        u3d
        native_app_glue
        ${android-lib}
        ${egl-lib}
        ${glesv2-lib}
        ${log-lib}
)

# --------------------------------------------------
# REQUIRED for NativeActivity
# --------------------------------------------------
set_target_properties(
        u3d
        PROPERTIES
        LINK_FLAGS "-u ANativeActivity_onCreate"
)

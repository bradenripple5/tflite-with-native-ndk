### For more information about using CMake with Android Studio, read the
### documentation: https://d.android.com/studio/projects/add-native-code.html.
### For more examples on how to use CMake, see https://github.com/android/ndk-samples.
##
#### Sets the minimum CMake version required for this project.
##cmake_minimum_required(VERSION 3.22.1)
#
### Sets the minimum version of CMake required to build the native library. this line from tflite cpp api for android
##
##
#cmake_minimum_required(VERSION 3.10.2)
##
##
### Declares the project name. The project name can be accessed via ${ PROJECT_NAME},
### Since this is the top level CMakeLists.txt, the project name is also accessible
### with ${CMAKE_PROJECT_NAME} (both CMake variables are in-sync within the top level
### build script scope).
#project("myapplication")
##
### Creates and names a library, sets it as either STATIC
### or SHARED, and provides the relative paths to its source code.
### You can define multiple libraries, and CMake builds them for you.
### Gradle automatically packages shared libraries with your APK.
###
### In this top level CMakeLists.txt, ${CMAKE_PROJECT_NAME} is used to define
### the target library name; in the sub-module's CMakeLists.txt, ${PROJECT_NAME}
### is preferred for the same purpose.
###
### In order to load a library into your app from Java/Kotlin, you must call
### System.loadLibrary() and pass the name of the library defined here;
### for GameActivity/NativeActivity derived applications, the same library name must be
### used in the AndroidManifest.xml file.
##add_library(${CMAKE_PROJECT_NAME} SHARED
##        # List C/C++ source files with relative paths to this CMakeLists.txt.
##        native-lib.cpp)
##
### Specifies libraries CMake should link to your target library. You
### can link libraries from various origins, such as libraries defined in this
### build script, prebuilt third-party libraries, or Android system libraries.
##target_link_libraries(${CMAKE_PROJECT_NAME}
##        # List libraries link to the target library
##        android
##        log)
#
#
#
#
#cmake_minimum_required(VERSION 3.22)
##project(native-lib)
#include_directories(${CMAKE_SOURCE_DIR}/include)
#
#
#

#find_library(log-lib       log)
#find_library(android-lib   android)
#find_library(camera2ndk-lib camera2ndk)
#find_library(mediandk-lib   mediandk)
#
#add_library(tensorflow-lite SHARED IMPORTED)
#set_target_properties(tensorflow-lite PROPERTIES
#        IMPORTED_LOCATION     ${CMAKE_SOURCE_DIR}/tf-lite-api/generated-libs/${ANDROID_ABI}/libtensorflowlite.so
#
#        #        IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/generated-libs/${ANDROID_ABI}/libtensorflowlite.so
##        IMPORTED_LOCATION /home/brady/AndroidStudioProjects/MyApplication/tf-lite-api/generated-libs/arm64-v8a/libtensorflowlite.so
#)
#             # if you’re using TF-Lite C++ API
#target_link_libraries(native-lib
#        ${log-lib}
#        ${android-lib}
#        ${camera2ndk-lib}
#        ${mediandk-lib}
#        tensorflow-lite
#        EGL
#        GLESv2
#)
cmake_minimum_required(VERSION 3.10.2)
project(NdkCamera)

add_library(native-lib SHARED
        native-lib.cpp
        NativeCamera.cpp
         Renderer.cpp)

find_library(log-lib log)
find_library(android-lib android)
find_library(camera2-lib camera2ndk)
find_library(media-lib mediandk)
find_library(egl-lib EGL)
find_library(gles-lib GLESv2)

target_link_libraries(native-lib
        ${log-lib}
        ${android-lib}
        ${camera2-lib}
        ${media-lib}
        ${egl-lib}
        ${gles-lib})

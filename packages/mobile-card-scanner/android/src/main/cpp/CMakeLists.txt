cmake_minimum_required(VERSION 3.14)

# Set optimization flags for release builds
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG -ffast-math -funroll-loops")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG -ffast-math -funroll-loops")

# Enable NEON for ARM64
if(ANDROID_ABI STREQUAL "arm64-v8a")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a")
endif()

file(GLOB_RECURSE ANDROID_CPP_SOURCES CONFIGURE_DEPENDS "${ANDROID_CPP_DIR}/*.cpp")
file(GLOB_RECURSE COMMON_CPP_SOURCES CONFIGURE_DEPENDS
     "${COMMON_CPP_DIR}/rnbridge/*.cpp" "${COMMON_CPP_DIR}/rnbridge/*.c"
     "${COMMON_CPP_DIR}/backends/*.cpp" "${COMMON_CPP_DIR}/backends/*.c")

# Target name matches `androidCxxLibName` in nitro.json (required for Nitrogen,
# build.gradle Prefab export, and Kotlin System.loadLibrary).
add_library(CardScanner SHARED ${ANDROID_CPP_SOURCES} ${COMMON_CPP_SOURCES})

find_package(ReactAndroid REQUIRED CONFIG)
find_package(fbjni REQUIRED CONFIG)

# ------- Nitro & VisionCamera v5 Frame Processor -------
# VisionCamera prefab for Frame HybridObject access.
find_package(react-native-vision-camera REQUIRED CONFIG)
target_link_libraries(CardScanner react-native-vision-camera::VisionCamera)

target_include_directories(
  CardScanner
  PUBLIC
  "${COMMON_CPP_DIR}"
  "${COMMON_CPP_DIR}/rnbridge"
  "${COMMON_CPP_DIR}/rnbridge/jsi"
  "${COMMON_CPP_DIR}/backends"
  "${ANDROID_CPP_DIR}"
  "${INCLUDE_DIR}"
  "${INCLUDE_DIR}/cpuinfo"
  "${INCLUDE_DIR}/pthreadpool"
  "${REACT_NATIVE_DIR}/ReactCommon"
  "${REACT_NATIVE_DIR}/ReactAndroid/src/main/jni/react/turbomodule"
  "${REACT_NATIVE_DIR}/ReactCommon/callinvoker"
)

set(LINK_LIBRARIES
  ReactAndroid::jsi
  fbjni::fbjni
  android
  log
)

set(RN_VERSION_LINK_LIBRARIES
  ReactAndroid::reactnative
)

# ------- ObjectBox -------
add_library(objectbox-jni SHARED IMPORTED)
set_target_properties(objectbox-jni PROPERTIES
IMPORTED_LOCATION "${JNI_LIBS_DIR}/${ANDROID_ABI}/libobjectbox-jni.so")

# ------- Executorch -------
add_library(executorch SHARED IMPORTED)

set_target_properties(executorch PROPERTIES
                      IMPORTED_LOCATION "${LIBS_DIR}/executorch/${ANDROID_ABI}/libexecutorch.so")


# ------- OpenCV (common for all ABIs) -------
set(OPENCV_LIBS
  "${LIBS_DIR}/opencv/${ANDROID_ABI}/libopencv_core.a"
  "${LIBS_DIR}/opencv/${ANDROID_ABI}/libopencv_imgproc.a"
  "${LIBS_DIR}/opencv/${ANDROID_ABI}/libopencv_features2d.a"
  "${LIBS_DIR}/opencv/${ANDROID_ABI}/libopencv_highgui.a"
  "${LIBS_DIR}/opencv/${ANDROID_ABI}/libopencv_photo.a"
  "${LIBS_DIR}/opencv/${ANDROID_ABI}/libopencv_video.a"
)

if(ANDROID_ABI STREQUAL "arm64-v8a")
  target_compile_definitions(CardScanner PRIVATE ARCH_ARM64)

  # ------- pthreadpool -------
  add_library(pthreadpool SHARED IMPORTED)

  set_target_properties(pthreadpool PROPERTIES
                        IMPORTED_LOCATION "${LIBS_DIR}/pthreadpool/${ANDROID_ABI}/libpthreadpool.so")

  # ------- cpuinfo -------
  add_library(cpuinfo SHARED IMPORTED)

  # OpenCV third-party libs for arm64
  set(OPENCV_THIRD_PARTY_LIBS
    "${LIBS_DIR}/opencv-third-party/${ANDROID_ABI}/libkleidicv_hal.a"
    "${LIBS_DIR}/opencv-third-party/${ANDROID_ABI}/libkleidicv_thread.a"
    "${LIBS_DIR}/opencv-third-party/${ANDROID_ABI}/libkleidicv.a"
  )

  set_target_properties(cpuinfo PROPERTIES
                        IMPORTED_LOCATION "${LIBS_DIR}/cpuinfo/${ANDROID_ABI}/libcpuinfo.so")
  set(EXECUTORCH_LIBS
    "pthreadpool"
    "cpuinfo"
  )
elseif(ANDROID_ABI STREQUAL "x86_64")
  set(OPENCV_THIRD_PARTY_LIBS "")
  set(EXECUTORCH_LIBS "")
else()
  set(OPENCV_THIRD_PARTY_LIBS "")
  set(EXECUTORCH_LIBS "")
endif()

target_link_libraries(
  CardScanner
  cardscanner_core
  ${LINK_LIBRARIES}
  ${RN_VERSION_LINK_LIBRARIES}
  ${log-lib}
  objectbox-jni
  executorch
  ${EXECUTORCH_LIBS}
  ${OPENCV_LIBS}
  ${OPENCV_THIRD_PARTY_LIBS}
  )

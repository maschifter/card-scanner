require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

Pod::Spec.new do |s|
  s.name         = "react-native-card-scanner"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported }
  s.source       = { :git => package["repository"]["url"], :tag => "#{s.version}" }

  s.dependency "ObjectBox"
  s.dependency "opencv-rne", "~> 4.11.0"

  et_binaries_path = File.expand_path('$(PODS_TARGET_SRCROOT)/third-party/ios/libs/executorch', __dir__)
  pthreadpool_binaries_path = File.expand_path('$(PODS_TARGET_SRCROOT)/third-party/ios/libs/pthreadpool', __dir__)
  cpuinfo_binaries_path = File.expand_path('$(PODS_TARGET_SRCROOT)/third-party/ios/libs/cpuinfo', __dir__)

  s.user_target_xcconfig = {
    "HEADER_SEARCH_PATHS" => "$(PODS_TARGET_SRCROOT)/third-party/include",

    "OTHER_LDFLAGS[sdk=iphoneos*]" => [
      '$(inherited)',
      "-force_load \"#{et_binaries_path}/libbackend_xnnpack_ios.a\"",
      "-force_load \"#{et_binaries_path}/libexecutorch_ios.a\"",
      "-force_load \"#{et_binaries_path}/libkernels_optimized_ios.a\"",
      "-force_load \"#{et_binaries_path}/libthreadpool_ios.a\"",
      "\"#{pthreadpool_binaries_path}/physical-arm64-release/libpthreadpool.a\"",
      "\"#{cpuinfo_binaries_path}/libcpuinfo.a\"",
    ].join(' '),

    "OTHER_LDFLAGS[sdk=iphonesimulator*]" => [
      '$(inherited)',
      "-force_load \"#{et_binaries_path}/libbackend_xnnpack_simulator.a\"",
      "-force_load \"#{et_binaries_path}/libexecutorch_simulator.a\"",
      "-force_load \"#{et_binaries_path}/libkernels_optimized_simulator.a\"",
      "-force_load \"#{et_binaries_path}/libthreadpool_simulator.a\"",
      "\"#{pthreadpool_binaries_path}/simulator-arm64-debug/libpthreadpool.a\"",
      "\"#{cpuinfo_binaries_path}/libcpuinfo.a\"",
    ].join(' '),

    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'x86_64',
  }

  s.script_phase = {
    :name => 'Run CMake to Fetch C++ Headers',
    :script => <<-SCRIPT,
      echo "⚙️ Running CMake to configure and generate ObjectBox schema..."
      cd "${PODS_TARGET_SRCROOT}"
      # 1. Configure
      mkdir -p build/ios && cd build/ios
      cmake ../..
      
      # 2. Build (this runs add_obx_schema)
      cmake --build .  

      # 3. Create the stamp file to signal completion
      touch "${PODS_TARGET_SRCROOT}/build/ios/cmake_build_complete.stamp"
    SCRIPT
    :execution_position => :before_compile,
    
    # This tells Xcode to wait for the *entire script* to finish.
    :output_files => ["${PODS_TARGET_SRCROOT}/build/ios/cmake_build_complete.stamp"]
  }

  s.source_files = [
    "ios/CardScanner/**/*.{m,mm,h}",
    "cpp/**/*.{cpp,c,h,hpp}",
    "common/**/*.{cpp,c,h,hpp}",
  ]

  s.pod_target_xcconfig = {
    "USE_HEADERMAP" => "YES",
    "HEADER_SEARCH_PATHS" =>
      '"$(PODS_TARGET_SRCROOT)/ios" '+
      '"$(PODS_TARGET_SRCROOT)/common" '+
      '"$(PODS_TARGET_SRCROOT)/cpp" '+
      '"$(PODS_TARGET_SRCROOT)/third-party/include" '+
      '"$(PODS_TARGET_SRCROOT)/build/ios/_deps/objectbox-c-src/include" '+
      '"$(PODS_TARGET_SRCROOT)/build/ios/_deps/objectbox-c-src/external/" '+
      # Path to the ObjectBox.framework headers (from the pod)
      '"$(PODS_ROOT)/ObjectBox/ObjectBox.xcframework/ios-arm64/ObjectBox.framework/Headers" ',
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++20",
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'x86_64',
  }


  install_modules_dependencies(s)
end

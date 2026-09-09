require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

# Benchmark settings are opt-in per app, via app.json:
#   "extra": { "cardScanner": { "benchmark": true, "benchmarkVerbose": true } }
# Compile-time, so changing either needs `pod install` and a rebuild.
card_scanner_setting = lambda do |name|
  app_json = File.join(Pod::Config.instance.installation_root.parent, "app.json")
  next 0 unless File.exist?(app_json)

  JSON.parse(File.read(app_json)).dig("extra", "cardScanner", name) ? 1 : 0
rescue StandardError
  0
end

benchmark_enabled = card_scanner_setting.call("benchmark")
benchmark_verbose = card_scanner_setting.call("benchmarkVerbose")

Pod::Spec.new do |s|
  s.name         = "card-scanner"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported }
  s.source       = { :git => package["repository"]["url"], :tag => "#{s.version}" }

  s.dependency "ObjectBox"
  s.dependency "opencv-rne", "~> 4.11.0"
  s.dependency "VisionCamera"
  s.dependency "card-scanner-core"

  et_binaries_path = File.expand_path('$(PODS_TARGET_SRCROOT)/third-party/ios/libs/executorch', __dir__)
  pthreadpool_binaries_path = File.expand_path('$(PODS_TARGET_SRCROOT)/third-party/ios/libs/pthreadpool', __dir__)
  cpuinfo_binaries_path = File.expand_path('$(PODS_TARGET_SRCROOT)/third-party/ios/libs/cpuinfo', __dir__)

  s.user_target_xcconfig = {
    "HEADER_SEARCH_PATHS" =>
      '"$(PODS_TARGET_SRCROOT)/ios" '+
      '"$(PODS_TARGET_SRCROOT)/common" '+
      '"$(PODS_TARGET_SRCROOT)/common/rnbridge" '+
      '"$(PODS_TARGET_SRCROOT)/common/rnbridge/jsi" '+
      '"$(PODS_TARGET_SRCROOT)/common/backends" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/src" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/src/database" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/src/models" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/src/utils" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/third-party/include" '+
      '"$(PODS_TARGET_SRCROOT)/third-party/include" '+
      '"$(PODS_TARGET_SRCROOT)/third-party/include/cpuinfo" '+
      '"$(PODS_TARGET_SRCROOT)/third-party/include/pthreadpool"',

    "OTHER_LDFLAGS[sdk=iphoneos*]" => [
      '$(inherited)',
      "-force_load \"#{et_binaries_path}/libbackend_xnnpack_ios.a\"",
      "-force_load \"#{et_binaries_path}/libbackend_coreml_ios.a\"",
      "-force_load \"#{et_binaries_path}/libexecutorch_ios.a\"",
      "-force_load \"#{et_binaries_path}/libkernels_optimized_ios.a\"",
      "-force_load \"#{et_binaries_path}/libthreadpool_ios.a\"",
      "\"#{pthreadpool_binaries_path}/physical-arm64-release/libpthreadpool.a\"",
      "\"#{cpuinfo_binaries_path}/libcpuinfo.a\"",
      '-framework CoreML -framework Accelerate -lsqlite3',
    ].join(' '),

    "OTHER_LDFLAGS[sdk=iphonesimulator*]" => [
      '$(inherited)',
      "-force_load \"#{et_binaries_path}/libbackend_xnnpack_simulator.a\"",
      "-force_load \"#{et_binaries_path}/libbackend_coreml_simulator.a\"",
      "-force_load \"#{et_binaries_path}/libexecutorch_simulator.a\"",
      "-force_load \"#{et_binaries_path}/libkernels_optimized_simulator.a\"",
      "-force_load \"#{et_binaries_path}/libthreadpool_simulator.a\"",
      "\"#{pthreadpool_binaries_path}/simulator-arm64-debug/libpthreadpool.a\"",
      "\"#{cpuinfo_binaries_path}/libcpuinfo.a\"",
      '-framework CoreML -framework Accelerate -lsqlite3',
    ].join(' '),

    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'x86_64',
  }

  # ObjectBox schema generation moved to card-scanner-core along with the
  # CMakeLists.txt that drives it - this package no longer has one, and the old
  # script phase here pointed at it. See card-scanner-core.podspec.

  s.source_files = [
    "ios/CardScanner/**/*.{m,mm,h}",
    "common/rnbridge/**/*.{cpp,c,h,hpp}",
    "common/backends/**/*.{cpp,c,h,hpp}",
  ]

  s.public_header_files = [
    "common/rnbridge/**/*.h",
  ]

  s.pod_target_xcconfig = {
    "USE_HEADERMAP" => "YES",
    "ALWAYS_SEARCH_USER_PATHS" => "YES",
    "HEADER_SEARCH_PATHS" =>
      '"$(PODS_TARGET_SRCROOT)/ios" '+
      '"$(PODS_TARGET_SRCROOT)/common" '+
      '"$(PODS_TARGET_SRCROOT)/common/rnbridge" '+
      '"$(PODS_TARGET_SRCROOT)/common/rnbridge/jsi" '+
      '"$(PODS_TARGET_SRCROOT)/common/backends" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/src" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/src/database" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/src/models" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/src/utils" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/third-party/include" '+
      '"$(PODS_TARGET_SRCROOT)/third-party/include" '+
      '"$(PODS_TARGET_SRCROOT)/third-party/include/cpuinfo" '+
      '"$(PODS_TARGET_SRCROOT)/third-party/include/pthreadpool" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/build/_deps/objectbox-c-src/include" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/build/_deps/objectbox-c-src/external/" '+
      '"$(PODS_TARGET_SRCROOT)/../card-scanner-core/build" '+
      # Path to the ObjectBox.framework headers (from the pod)
      '"$(PODS_ROOT)/ObjectBox/ObjectBox.xcframework/ios-arm64/ObjectBox.framework/Headers" ',
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++20",
    "GCC_PREPROCESSOR_DEFINITIONS" =>
      "$(inherited) CARDSCANNER_BENCHMARK=#{benchmark_enabled} " +
      "CARDSCANNER_BENCHMARK_VERBOSE=#{benchmark_verbose}",
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'x86_64',
  }


  install_modules_dependencies(s)

  # Adds the nitrogen-generated sources and the react-native-nitro-modules
  # dependency for the HybridCardScannerPlugin frame processor.
  load File.join(__dir__, 'nitrogen/generated/ios/CardScanner+autolinking.rb')
  add_nitrogen_files(s)
end

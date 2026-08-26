require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

# BenchmarkCollector/BenchmarkRunner live in THIS package, so this pod target is
# the one that has to see the flag - CocoaPods compiles each pod separately, and
# a define on the wrapper's target does not reach here. Kept byte-identical to
# the lambda in card-scanner.podspec so both targets agree; if they disagree,
# the collector silently no-ops and every benchmark reports 0 scans.
# On Android the equivalent is add_compile_definitions() in
# mobile-card-scanner/android/CMakeLists.txt, which is directory-scoped and so
# already covers core via add_subdirectory.
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
  s.name         = "card-scanner-core"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported }
  s.source       = { :git => package["repository"]["url"], :tag => "#{s.version}" }

  # Core needs a database and an image library. It deliberately does NOT depend
  # on VisionCamera, NitroModules or any inference runtime - the wrapper package
  # links whichever backend defines inference::loadSession.
  s.dependency "ObjectBox"
  s.dependency "opencv-rne", "~> 4.11.0"

  # ObjectBox entity code is generated from database/schema.fbs at configure
  # time, and the generated headers land in ios/build.
  s.script_phase = {
    :name => 'Generate ObjectBox schema',
    :script => <<-SCRIPT,
      set -e
      cd "${PODS_TARGET_SRCROOT}"
      mkdir -p build && cd build
      cmake ..
      cmake --build . --target cardscanner_schema
      touch "${PODS_TARGET_SRCROOT}/build/cmake_build_complete.stamp"
    SCRIPT
    :execution_position => :before_compile,
    :output_files => ["${PODS_TARGET_SRCROOT}/build/cmake_build_complete.stamp"]
  }

  s.source_files = [
    "src/**/*.{cpp,c,h,hpp}",
  ]

  s.public_header_files = [
    "src/**/*.h",
  ]

  s.pod_target_xcconfig = {
    # card-scanner has Swift (nitrogen's autolinking shim) and depends on this
    # pod, so as a static library it needs a module map here.
    "DEFINES_MODULE" => "YES",
    "USE_HEADERMAP" => "YES",
    "ALWAYS_SEARCH_USER_PATHS" => "YES",
    "HEADER_SEARCH_PATHS" =>
      '"$(PODS_TARGET_SRCROOT)/src" '+
      '"$(PODS_TARGET_SRCROOT)/src/database" '+
      '"$(PODS_TARGET_SRCROOT)/src/models" '+
      '"$(PODS_TARGET_SRCROOT)/src/utils" '+
      '"$(PODS_TARGET_SRCROOT)/third-party/include" '+
      '"$(PODS_TARGET_SRCROOT)/build" '+
      '"$(PODS_TARGET_SRCROOT)/build/_deps/objectbox-c-src/include" '+
      '"$(PODS_TARGET_SRCROOT)/build/_deps/objectbox-c-src/external/" '+
      '"$(PODS_ROOT)/ObjectBox/ObjectBox.xcframework/ios-arm64/ObjectBox.framework/Headers" ',
    "GCC_PREPROCESSOR_DEFINITIONS" =>
      "$(inherited) CARDSCANNER_BENCHMARK=#{benchmark_enabled} " +
      "CARDSCANNER_BENCHMARK_VERBOSE=#{benchmark_verbose}",
    # Log.h uses C++20 concepts.
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++20",
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'x86_64',
  }

end

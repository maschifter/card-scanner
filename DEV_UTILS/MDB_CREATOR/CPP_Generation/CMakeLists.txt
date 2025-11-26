cmake_minimum_required(VERSION 3.15)
project(ObjectBoxJSONLoader CXX)

# Ustawiamy standard C++ na 17 (zgodnie z Twoim kodem C++)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. Pobieranie ObjectBox (Wersja Host) ---
# Tutaj NIE używamy hacków cross-compilation, bo ten program
# ma działać na Twoim Macu, żeby przetworzyć JSONa.
include(FetchContent)
FetchContent_Declare(
    objectbox
    GIT_REPOSITORY https://github.com/objectbox/objectbox-c.git
    GIT_TAG        v4.1.0
)
FetchContent_MakeAvailable(objectbox)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3 # Stabilna, popularna wersja
)
FetchContent_MakeAvailable(nlohmann_json)
# --- 2. Generator ---
find_package(ObjectBoxGenerator 4.0.0 REQUIRED)

# --- 3. Aplikacja ---
add_executable(json_loader main.cpp)

# --- 4. Schemat i Model ---
# KLUCZOWE: Używamy INSOURCE.
# Dzięki temu generator szuka 'objectbox-model.json' w tym samym katalogu co 'schema.fbs'.
# Upewnij się, że 'schema.fbs' i Twój 'objectbox-model.json' są w katalogu projektu!
add_obx_schema(
  TARGET json_loader
  SCHEMA_FILES schema.fbs
  INSOURCE 
  CXX_STANDARD 14
)

# --- 5. Linkowanie ---
target_link_libraries(
  json_loader
  PRIVATE
  objectbox # To linkuje bibliotekę dynamiczną (dylib/so) ObjectBox
)

# Dodatkowe includy (na wszelki wypadek, choć target 'objectbox' powinien to załatwić)
target_include_directories(
  json_loader
  PRIVATE
  "${objectbox_SOURCE_DIR}/include"
)

target_include_directories(
  json_loader
  PRIVATE
  ${nlohmann_json_SOURCE_DIR}/include
)

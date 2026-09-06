include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
  EnTT
  URL https://github.com/skypjack/entt/archive/d4014c74dc3793aba95ae354d6e23a026c2796db.tar.gz
  URL_HASH SHA256=9a6c0e1a7049615d40bbe56443d42a27703c6251a5be862de8e952b0f0f84f36
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/archive/55f93686c01528224f448c19128836e7df245f72.tar.gz
  URL_HASH SHA256=67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)

FetchContent_MakeAvailable(EnTT nlohmann_json)

if(BUILD_TESTING)
  FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/644821ce28cb25d7992a4d0375b1d83214392592.tar.gz
    URL_HASH SHA256=5536f5e936466e3d0ef6350630d17d976fee2dee3cc44940e77a769cafab1904
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  )
  FetchContent_MakeAvailable(Catch2)
endif()

if(ANT_BUILD_DESKTOP)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/dbc56a87da87d973a9c5baa4e7438a9d20121d28.tar.gz
    URL_HASH SHA256=81b06ce7c19cf3b634b0271c23c361ba6ad8bf45fb8b036abbfeb4260ec1e126
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  )
  FetchContent_MakeAvailable(raylib)
endif()

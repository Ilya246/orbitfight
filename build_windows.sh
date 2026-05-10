cmake -B build-win -S . \
      -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw64.cmake \
      -DCMAKE_BUILD_TYPE=Release

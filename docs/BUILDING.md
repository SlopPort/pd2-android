# Building

## Toolchain

- Android SDK (compileSdk 35) with NDK **27.2.12479018** and CMake **3.22.1**
  (`sdkmanager "ndk;27.2.12479018" "cmake;3.22.1"`).
- Gradle 8.10 with Android Gradle Plugin 8.8 (no wrapper is committed yet; use a local Gradle or
  open the project in Android Studio).
- `local.properties` with `sdk.dir=...` (not committed).
- JDK 17+ (AGP 8.8 refuses Java 8); Android Studio's bundled `jbr` works:
  `JAVA_HOME="C:\Program Files\Android\Android Studio\jbr"`.

```bash
gradle assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The native part is large (all of D2Common and D2Game, ~250k lines); a clean build takes a few
minutes, incremental ones seconds.

## Game data on the device

The app keeps its own copy of the user's MPQs in its private storage. For development there is a
shortcut: copy the archives to the app's external folder and start the app; they are imported and
put in PD2 order automatically (`DataImport.importPending`).

```bash
adb shell mkdir -p /sdcard/Android/data/pl.d2port/files/import
adb push d2data.mpq d2exp.mpq d2char.mpq /sdcard/Android/data/pl.d2port/files/import/
adb push pd2data.mpq pd2maps.mpq pd2assets.mpq /sdcard/Android/data/pl.d2port/files/import/
```

Needed for Act I: `d2data.mpq`, `d2exp.mpq`, `d2char.mpq`, `pd2data.mpq`, `pd2maps.mpq`,
`pd2assets.mpq`.

## Command-line test (no APK)

`app/src/main/cpp/d2test.cpp` is a phone-side test executable (tables, DRLG, collision, the D2Game
server). Build it with the NDK toolchain directly:

```bash
cmake -S app/src/main/cpp -B build-native -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=26 -DANDROID_STL=c++_static \
  -DD2NATIVE_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-native
```

It reads extracted files (lower-case paths) from `/data/local/tmp/d2data`;
`tools/d2data_extract.py` extracts them from the MPQs in PD2 order. See DEVELOPMENT.md.

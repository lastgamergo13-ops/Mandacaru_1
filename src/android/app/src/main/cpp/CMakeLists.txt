add_library(CemuAndroid SHARED
        AndroidFilesystemCallbacks.cpp
        AndroidInputHelpers.cpp
        CompressTitleCallbacks.cpp
        GameTitleLoader.cpp
        Image.cpp
        JNIUtils.cpp
        NativeAccount.cpp
        NativeActiveSettings.cpp
        NativeEmulatedUSBDevices.cpp
        NativeEmulation.cpp
        NativeGameTitles.cpp
        NativeGraphicPacks.cpp
        NativeInput.cpp
        NativeLib.cpp
        NativeLocalization.cpp
        NativeLogging.cpp
        NativeSettings.cpp
        NativeSwkbd.cpp
        WuaConverter.cpp
)

cemu_use_precompiled_header(CemuAndroid)

find_package(Stb REQUIRED)
target_include_directories(CemuAndroid PRIVATE ${Stb_INCLUDE_DIR})

target_link_libraries(CemuAndroid PRIVATE
        -landroid
        CemuCommon
        CemuAudio
        CemuComponents
        CemuCafe
        CemuGui
        ZArchive::zarchive
)

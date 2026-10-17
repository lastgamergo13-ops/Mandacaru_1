add_library(CemuAndroid SHARED
        ${CMAKE_SOURCE_DIR}/src/CemuCommonInit.cpp
        # V11.36: ZArchiveReader com OpenFromStream (a versao do vcpkg nao tem). Fica aqui, e nao no CemuCafe (lib estatica),
        # para o linker usar sempre esta versao e nunca puxar a antiga de libzarchive.a.
        ${CMAKE_SOURCE_DIR}/src/zarchive/zarchivereader.cpp
        AndroidFilesystemCallbacks.cpp
        AndroidInputHelpers.cpp
        AndroidMemoryManager.cpp
        AndroidOpenGLCanvas.cpp
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
        NativeRenderer.cpp
        NativeCheats.cpp
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

# libadrenotools: os .so de hook (usados ao carregar driver Vulkan customizado, ex. Turnip) precisam ser
# COMPILADOS e EMPACOTADOS no APK (lib/arm64-v8a). Sem esta dependencia o ninja nao os constroi.
foreach(_adrenotools_hook hook_impl main_hook file_redirect_hook)
    if(TARGET ${_adrenotools_hook})
        add_dependencies(CemuAndroid ${_adrenotools_hook})
        message(STATUS "libadrenotools: ${_adrenotools_hook} sera construido junto com o CemuAndroid")
    endif()
endforeach()

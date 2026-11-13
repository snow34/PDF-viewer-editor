cmake_minimum_required(VERSION 3.21)
project(PrepressPdf VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)

add_library(mupdf INTERFACE)
if(MINGW)
    # MSYS2 ships MuPDF as a DLL with a working pkg-config file.
    pkg_check_modules(MUPDF REQUIRED IMPORTED_TARGET mupdf)
    target_link_libraries(mupdf INTERFACE PkgConfig::MUPDF)
else()
    # Linux distributions ship static libraries and an unreliable pkg-config
    # file, so the library and what it was built with are found by hand.
    find_path(MUPDF_INCLUDE_DIR mupdf/fitz.h REQUIRED)
    find_library(MUPDF_LIBRARY mupdf REQUIRED)
    find_library(MUPDF_THIRD_LIBRARY mupdf-third)
    target_include_directories(mupdf INTERFACE ${MUPDF_INCLUDE_DIR})
    target_link_libraries(mupdf INTERFACE ${MUPDF_LIBRARY})
    if(MUPDF_THIRD_LIBRARY)
        target_link_libraries(mupdf INTERFACE ${MUPDF_THIRD_LIBRARY})
    endif()
    foreach(lib freetype harfbuzz openjp2 jbig2dec jpeg z mujs gumbo)
        find_library(MUPDF_DEP_${lib} ${lib})
        if(MUPDF_DEP_${lib})
            target_link_libraries(mupdf INTERFACE ${MUPDF_DEP_${lib}})
        endif()
    endforeach()
endif()

add_library(engine STATIC
    src/engine/Types.h
    src/engine/Document.cpp src/engine/Document.h
    src/engine/Compositor.cpp src/engine/Compositor.h
    src/engine/ProfileLibrary.cpp src/engine/ProfileLibrary.h
)
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC Qt6::Gui PRIVATE mupdf PkgConfig::LCMS2)
if(WIN32)
    target_link_libraries(engine PRIVATE mscms)
endif()

add_library(ui STATIC
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/PageView.cpp src/ui/PageView.h
    src/ui/OutputPreviewPanel.cpp src/ui/OutputPreviewPanel.h
    src/ui/ProfileDialog.cpp src/ui/ProfileDialog.h
    src/ui/RenderWorker.cpp src/ui/RenderWorker.h
)
target_link_libraries(ui PUBLIC engine Qt6::Widgets)

add_executable(prepresspdf WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(prepresspdf PRIVATE ui)

enable_testing()
add_executable(test_engine tests/test_engine.cpp)
target_link_libraries(test_engine PRIVATE engine Qt6::Test)
target_compile_definitions(test_engine PRIVATE TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
add_test(NAME test_engine COMMAND test_engine)
set_tests_properties(test_engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_ui tests/test_ui.cpp)
target_link_libraries(test_ui PRIVATE ui Qt6::Test)
target_compile_definitions(test_ui PRIVATE TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
add_test(NAME test_ui COMMAND test_ui)
set_tests_properties(test_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

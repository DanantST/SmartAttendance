idf_component_register()

set(lib_dir ${CMAKE_CURRENT_SOURCE_DIR}/lib/${IDF_TARGET})
set(include_dir ${CMAKE_CURRENT_SOURCE_DIR}/include/opencv4)

set(OPENCV_MODULES highgui imgproc core)

set(OPENCV_MODULE_BUILD_ARGS)
if(CONFIG_OPENCV_MODULE_FEATURES2D)
    list(INSERT OPENCV_MODULES 0 features2d)
endif()

if(CONFIG_OPENCV_MODULE_PHOTO)
    list(INSERT OPENCV_MODULES 0 photo)
endif()

if(CONFIG_OPENCV_MODULE_VIDEO)
    list(INSERT OPENCV_MODULES 0 video)
endif()

foreach(module ${OPENCV_MODULES})
    add_prebuilt_library(${module} ${lib_dir}/libopencv_${module}.a)
endforeach()

add_library(opencv INTERFACE)
target_link_libraries(opencv INTERFACE ${OPENCV_MODULES})
target_include_directories(opencv INTERFACE ${include_dir})

target_link_libraries(${COMPONENT_LIB} INTERFACE opencv)
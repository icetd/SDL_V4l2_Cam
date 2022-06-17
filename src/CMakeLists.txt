set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11" )

set(SDL2_libs_DIR /home/tiandeng/share/Dev/Thirlib/lib/SDL2/lib )
set(SDL2_headers_DIR /home/tiandeng/share/Dev/Thirlib/lib/SDL2/include )
set(V4l2_device_DIR /home/tiandeng/share/Dev/SDL_V4l2_Cam/src/v4l2_device )

add_library( SDL2 SHARED IMPORTED )
set_target_properties( SDL2 PROPERTIES IMPORTED_LOCATION ${SDL2_libs_DIR}/libSDL2.so )

include_directories(${SDL2_headers_DIR} ${V4l2_device_DIR})
link_directories(${SDL2_libs_DIR})

add_executable(SDL_V4l2_Cam
	SDL_V4l2_Cam.c
	v4l2_device/v4l2_device.c )
target_link_libraries(SDL_V4l2_Cam  SDL2 pthread jpeg)




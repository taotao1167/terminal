cmake_minimum_required(VERSION 2.9)
project(test)

SET(EXE_SRCLIST terminal_example.c terminal.c tt_buffer.c)
add_executable(example ${EXE_SRCLIST})

SET(TESTER_SRCLIST player_tester.c terminal.c tt_buffer.c pi_list.c)
# set SUBSYSTEM:WINDOWS for WinMain
# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")
add_executable(player_tester ${TESTER_SRCLIST})

add_library(PIMediaPlayer SHARED IMPORTED)
add_library(pi_mediacore SHARED IMPORTED)
add_library(PI_iLiveBase SHARED IMPORTED)
add_library(UAC SHARED IMPORTED)
add_library(PIAudioProcessing SHARED IMPORTED)
add_library(pi_ws SHARED IMPORTED)
add_library(pi_wt SHARED IMPORTED)

if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
	target_link_libraries(player_tester PUBLIC PIMediaPlayer pi_mediacore PI_iLiveBase UAC PIAudioProcessing pi_ws pi_wt)
	set(DEPLIBS_DIR "/home/www/gitlab/dep-libs")
	set(LIB_DIR "linux_x64_ubuntu1804")
	set_target_properties(PIMediaPlayer PROPERTIES IMPORTED_LOCATION ${DEPLIBS_DIR}/${LIB_DIR}/libPI_MediaPlayer.so)
	set_target_properties(pi_mediacore PROPERTIES IMPORTED_LOCATION ${DEPLIBS_DIR}/${LIB_DIR}/libPI_MediaCore.so)
	set_target_properties(PI_iLiveBase PROPERTIES IMPORTED_LOCATION ${DEPLIBS_DIR}/${LIB_DIR}/libPI_iLiveBase.so)
	set_target_properties(UAC PROPERTIES IMPORTED_LOCATION ${DEPLIBS_DIR}/${LIB_DIR}/libUAC.so)
	set_target_properties(PIAudioProcessing PROPERTIES IMPORTED_LOCATION ${DEPLIBS_DIR}/${LIB_DIR}/libPIAudioProcessing.so)
	set_target_properties(pi_ws PROPERTIES IMPORTED_LOCATION ${DEPLIBS_DIR}/${LIB_DIR}/libpi_ws.so)
	set_target_properties(pi_wt PROPERTIES IMPORTED_LOCATION ${DEPLIBS_DIR}/${LIB_DIR}/libpi_wt.so)
	# set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
	# set(CMAKE_INSTALL_RPATH "${DEPLIBS_DIR}/${LIB_DIR}")
elseif(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
	target_link_libraries(player_tester PUBLIC PIMediaPlayer pi_mediacore)
	set(DEPLIBS_DIR D:/code/dep-libs)
	if(CMAKE_CL_64)
		set(LIB_DIR "win64")
	else(CMAKE_CL_64)
		set(LIB_DIR "win")
		message(STATUS "Current Platform is ${CURRENT_PLATFORM}")
	endif(CMAKE_CL_64)

	set_target_properties(PIMediaPlayer PROPERTIES
			IMPORTED_LOCATION ${DEPLIBS_DIR}/${LIB_DIR}/PIMediaPlayer.dll
			IMPORTED_IMPLIB ${DEPLIBS_DIR}/${LIB_DIR}/PIMediaPlayer.lib
		)
	# add pi_mediacore for PIMC_SetAnalyzeCode
	set_target_properties(pi_mediacore PROPERTIES
			IMPORTED_LOCATION ${DEPLIBS_DIR}/${LIB_DIR}/pi_mediacore.dll
			IMPORTED_IMPLIB ${DEPLIBS_DIR}/${LIB_DIR}/pi_mediacore.lib
		)

	# cmake .. -G "Visual Studio 15 2017" -DCMAKE_BUILD_TYPE=Release
	# msbuild test.sln /p:Configuration=Release /p:Platform=win32 /t:Clean;Rebuild
	# cmake .. -G "Visual Studio 15 2017 Win64" -DCMAKE_BUILD_TYPE=Release
	# msbuild test.sln /p:Configuration=Release /p:Platform=x64 /t:Clean;Rebuild
else()
	message(FATAL_ERROR "unsupported platform: ${CMAKE_HOST_SYSTEM_NAME}")
endif()

target_include_directories(player_tester PUBLIC ${DEPLIBS_DIR}/include/PIMediaPlayer)

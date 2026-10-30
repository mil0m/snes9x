# The Snes9x emulation core plus the corruption engine, shared by the Qt front
# end and the headless tests. Defines the static library target snes9x-core.
set(SNES9X_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

set(SNES9X_CORE_SOURCES
    ${SNES9X_ROOT}/fxinst.cpp
    ${SNES9X_ROOT}/fxemu.cpp
    ${SNES9X_ROOT}/fxdbg.cpp
    ${SNES9X_ROOT}/c4.cpp
    ${SNES9X_ROOT}/c4emu.cpp
    ${SNES9X_ROOT}/apu/apu.cpp
    ${SNES9X_ROOT}/apu/bapu/dsp/sdsp.cpp
    ${SNES9X_ROOT}/apu/bapu/smp/smp.cpp
    ${SNES9X_ROOT}/apu/bapu/smp/smp_state.cpp
    ${SNES9X_ROOT}/msu1.cpp
    ${SNES9X_ROOT}/msu1.h
    ${SNES9X_ROOT}/dsp.cpp
    ${SNES9X_ROOT}/dsp1.cpp
    ${SNES9X_ROOT}/dsp2.cpp
    ${SNES9X_ROOT}/dsp3.cpp
    ${SNES9X_ROOT}/dsp4.cpp
    ${SNES9X_ROOT}/spc7110.cpp
    ${SNES9X_ROOT}/obc1.cpp
    ${SNES9X_ROOT}/seta.cpp
    ${SNES9X_ROOT}/seta010.cpp
    ${SNES9X_ROOT}/seta011.cpp
    ${SNES9X_ROOT}/seta018.cpp
    ${SNES9X_ROOT}/controls.cpp
    ${SNES9X_ROOT}/crosshairs.cpp
    ${SNES9X_ROOT}/cpu.cpp
    ${SNES9X_ROOT}/sa1.cpp
    ${SNES9X_ROOT}/debug.cpp
    ${SNES9X_ROOT}/sdd1.cpp
    ${SNES9X_ROOT}/tile.cpp
    ${SNES9X_ROOT}/tileimpl-n1x1.cpp
    ${SNES9X_ROOT}/tileimpl-n2x1.cpp
    ${SNES9X_ROOT}/tileimpl-h2x1.cpp
    ${SNES9X_ROOT}/srtc.cpp
    ${SNES9X_ROOT}/gfx.cpp
    ${SNES9X_ROOT}/memmap.cpp
    ${SNES9X_ROOT}/clip.cpp
    ${SNES9X_ROOT}/ppu.cpp
    ${SNES9X_ROOT}/dma.cpp
    ${SNES9X_ROOT}/snes9x.cpp
    ${SNES9X_ROOT}/globals.cpp
    ${SNES9X_ROOT}/stream.cpp
    ${SNES9X_ROOT}/conffile.cpp
    ${SNES9X_ROOT}/bsx.cpp
    ${SNES9X_ROOT}/snapshot.cpp
    ${SNES9X_ROOT}/screenshot.cpp
    ${SNES9X_ROOT}/movie.cpp
    ${SNES9X_ROOT}/statemanager.cpp
    ${SNES9X_ROOT}/sha256.cpp
    ${SNES9X_ROOT}/bml.cpp
    ${SNES9X_ROOT}/cpuops.cpp
    ${SNES9X_ROOT}/cpuexec.cpp
    ${SNES9X_ROOT}/sa1cpu.cpp
    ${SNES9X_ROOT}/cheats.cpp
    ${SNES9X_ROOT}/cheats2.cpp
    ${SNES9X_ROOT}/sdd1emu.cpp
    ${SNES9X_ROOT}/netplay.cpp
    ${SNES9X_ROOT}/server.cpp
    ${SNES9X_ROOT}/loadzip.cpp
    ${SNES9X_ROOT}/fscompat.cpp)
file(GLOB SNES9X_CORRUPT_SOURCES CONFIGURE_DEPENDS ${SNES9X_ROOT}/corrupt/*.cpp)
list(APPEND SNES9X_CORE_SOURCES ${SNES9X_CORRUPT_SOURCES})

add_library(snes9x-core STATIC ${SNES9X_CORE_SOURCES})
target_include_directories(snes9x-core PRIVATE ${SNES9X_ROOT} ${SNES9X_ROOT}/external)

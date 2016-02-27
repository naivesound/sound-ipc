WINCXX ?= $(CXX)

SRCS := main.cpp RtAudio.cpp RtMidi.cpp
STATIC := -static-libgcc -static-libstdc++

# Cross compile on Ubuntu: CXX=i686-w64-mingw32-g++ make sound.exe

all:
	@echo make sound.alsa
	@echo make sound.pulse
	@echo make sound.exe
	@echo make sound.mac

sound.alsa: $(SRCS)
	$(CXX) -std=c++0x -D__LINUX_ALSA__ $^ -o $@ -lasound -pthread

sound.exe: $(SRCS)
	$(WINCXX) -D__WINDOWS_WASAPI__ -I. $^ -o $@ -lole32 -lm -lksuser -lws2_32

sound.mac: $(SRCS)
	$(CXX) -D__MACOSX_CORE__ $^ -o $@ -framework CoreAudio -framework CoreMIDI -framework CoreFoundation -pthread

clean:
	-rm -f sound.alsa
	-rm -f sound.mac
	-rm -f sound.exe

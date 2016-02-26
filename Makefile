SRCS := main.cpp RtAudio.cpp RtMidi.cpp

# Cross compile on Ubuntu: CXX=i686-w64-mingw32-g++ make sound.exe

all:
	@echo make sound.alsa
	@echo make sound.pulse
	@echo make sound.exe
	@echo make sound.mac

sound.alsa: $(SRCS)
	$(CXX) -D__LINUX_ALSA__ $^ -o $@ -lasound -pthread

sound.exe: $(SRCS)
	$(CXX) -D__WINDOWS_WASAPI__ -I. $^ -o $@ -lole32 -lm -lksuser -lws2_32 -pthread

sound.mac: $(SRCS)
	$(CXX) -D__MACOSX_CORE__ $^ -o $@ -framework CoreAudio -framework CoreMIDI -framework CoreFoundation -pthread

clean:
	-rm sound.alsa
	-rm sound.mac
	-rm sound.exe

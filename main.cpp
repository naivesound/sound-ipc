#define _WIN32_WINNT 0x0600
#include <unistd.h>
#include <sys/types.h> 
#ifdef __WIN32__
# include <winsock2.h>
#else
# include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "RtAudio.h"

#define USE_PTHREAD !__WIN32__

#if !USE_PTHREAD
#include <winbase.h>
#endif

using namespace std;

struct audio_data {
	bool in;
	int fd;
#if USE_PTHREAD
	pthread_mutex_t mutex;
	pthread_cond_t produce;
	pthread_cond_t consume;
#else
	CRITICAL_SECTION mutex;
	CONDITION_VARIABLE produce;
	CONDITION_VARIABLE consume;
#endif
	bool ready;
	size_t bufsz;
	void *buffer;
};

static int audio_cb(void *out, void *in, unsigned int frames, double time, RtAudioStreamStatus status, void *context) {
	struct audio_data *data = (struct audio_data *) context;

#if USE_PTHREAD
	pthread_mutex_lock(&data->mutex);
	pthread_cond_signal(&data->produce);
	while (!data->ready) {
		pthread_cond_wait(&data->consume, &data->mutex);
	}
#else
	EnterCriticalSection(&data->mutex);
	WakeConditionVariable(&data->produce);
	while (!data->ready) {
		SleepConditionVariableCS(&data->consume, &data->mutex, INFINITE);
	}
#endif
	if (data->in) {
		memcpy(data->buffer, in, data->bufsz);
	} else {
		memcpy(out, data->buffer, data->bufsz);
	}
	data->ready = false;
#if USE_PTHREAD
	pthread_mutex_unlock(&data->mutex);
#else
	LeaveCriticalSection(&data->mutex);
#endif
	return 0;
}

static int usage() {
	cerr << "USAGE: sound <list|audio|midi> [options]" << endl;
	return 1;
}

static int list(int argc, char *argv[]) {
	RtAudio *audio = new RtAudio();
	if (audio != NULL) {
		for (int i = 0; i < audio->getDeviceCount(); i++) {
			RtAudio::DeviceInfo info = audio->getDeviceInfo(i);
			cout << i << ". " << info.name << endl;
		}
		delete audio;
	}
	return 0;
}

static int audio(bool in, int argc, char *argv[]) {
	int port = 0;
	int format = RTAUDIO_SINT16;
	int rate = 44100;
	unsigned int frames = 0;
	int bytesPerFrame = 2;

	RtAudio::StreamParameters *output = NULL;
	RtAudio::StreamParameters *input = NULL;
	RtAudio::StreamParameters *params;
	RtAudio::StreamOptions options;

	if (in) {
		params = input = new RtAudio::StreamParameters();
	} else {
		params = output = new RtAudio::StreamParameters();
	}

	params->deviceId = -1;
	params->nChannels = 1;

	int opt;

	while ((opt = getopt(argc, argv, "b:d:c:r:f:p:")) != -1) {
		switch (opt) {
			case 'd': params->deviceId = atoi(optarg); break;
			case 'c': params->nChannels = atoi(optarg); break;
			case 'r': rate = atoi(optarg); break;
			case 'b': frames = atoi(optarg); break;
			case 'f':
				if (strcmp("s8", optarg) == 0) {
					format = RTAUDIO_SINT8;
					bytesPerFrame = 1;
				} else if (strcmp("s16", optarg) == 0) {
					format = RTAUDIO_SINT16;
					bytesPerFrame = 2;
				} else if (strcmp("s24", optarg) == 0) {
					format = RTAUDIO_SINT24;
					bytesPerFrame = 3;
				} else if (strcmp("s32", optarg) == 0) {
					format = RTAUDIO_SINT32;
					bytesPerFrame = 4;
				} else if (strcmp("f32", optarg) == 0) {
					format = RTAUDIO_FLOAT32;
					bytesPerFrame = 4;
				} else if (strcmp("f64", optarg) == 0) {
					format = RTAUDIO_FLOAT64;
					bytesPerFrame = 8;
				}
				break;
			case 'p': 
				port = atoi(optarg);
				break;
			default: return usage();
		}
	}

	RtAudio *audio = new RtAudio();
	if (audio == NULL) {
		cerr << "failed to open RtAudio" << endl;
		return 1;
	}

	if (params->deviceId == -1) {
		if (in) {
			params->deviceId = audio->getDefaultInputDevice();
			options.flags |= RTAUDIO_ALSA_USE_DEFAULT;
		} else {
			params->deviceId = audio->getDefaultOutputDevice();
			options.flags |= RTAUDIO_ALSA_USE_DEFAULT;
		}
	}

	if (frames == 0) {
		frames = rate / 50; // 20ms
	}

	struct audio_data data;

	audio->openStream(output, input, format, rate, &frames, audio_cb, &data, &options);

	data.bufsz = frames * bytesPerFrame * params->nChannels;
	data.buffer = malloc(data.bufsz);
	data.in = in;
	data.fd = 0;
#if USE_PTHREAD
	pthread_mutex_init(&data.mutex, NULL);
	pthread_cond_init(&data.consume, NULL);
	pthread_cond_init(&data.produce, NULL);
#else
	InitializeCriticalSection(&data.mutex);
	InitializeConditionVariable(&data.consume);
	InitializeConditionVariable(&data.produce);
#endif

	if (port != 0) {
#ifdef __WIN32__
		WORD versionWanted = MAKEWORD(1, 1);
		WSADATA wsaData;
		WSAStartup(versionWanted, &wsaData);
#endif
		int srv = socket(AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in addr;

		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(12345);
		bind(srv, (struct sockaddr *) &addr, sizeof(addr));
		listen(srv, 1);
		data.fd = accept(srv, NULL, NULL);
		//close(srv);

		int n = data.bufsz*4;
#ifdef __WIN32__
		const char *sockopt = (const char *) &n;
		int optsz = sizeof(n);
		setsockopt(data.fd, SOL_SOCKET, SO_RCVBUF, sockopt, optsz);
#else
		setsockopt(data.fd, SOL_SOCKET, SO_RCVBUF, (const void *)&n, sizeof(n));
#endif
	}

	audio->startStream();

	char *buf = (char *) malloc(data.bufsz);

	for (;;) {
		int sz = data.bufsz;
		int offset = 0;
		char *p = buf;
		while (sz > 0) {
			int n;
			if (port == 0) {
				n = read(data.fd, p+offset, sz);
			} else {
				n = recv(data.fd, p+offset, sz, 0);
			}
			if (n <= 0) {
				exit(0);
			}
			sz = sz - n;
			offset = offset + n;
		}
#if USE_PTHREAD
		pthread_mutex_lock(&data.mutex);
#else
		EnterCriticalSection(&data.mutex);
#endif
		memcpy(data.buffer, buf, data.bufsz);
		data.ready = true;
#if USE_PTHREAD
		pthread_cond_signal(&data.consume);
		while (data.ready) {
			pthread_cond_wait(&data.produce, &data.mutex);
		}
		pthread_mutex_unlock(&data.mutex);
#else
		WakeConditionVariable(&data.consume);
		while (data.ready) {
			SleepConditionVariableCS(&data.produce, &data.mutex, INFINITE);
		}
		LeaveCriticalSection(&data.mutex);
#endif
	}

	free(buf);
	close(data.fd);
	free(data.buffer);
	audio->stopStream();
	audio->closeStream();
	delete audio;
	return 0;
}

static int midi(bool in, int argc, char *argv[]) {

}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		return usage();
	} else {
		if (strcmp("list", argv[1]) == 0) {
			return list(argc-1, argv+1);
		} else if (strcmp("in", argv[1]) == 0) {
			return audio(true, argc-1, argv+1);
		} else if (strcmp("out", argv[1]) == 0) {
			return audio(false, argc-1, argv+1);
		} else if (strcmp("midiin", argv[1]) == 0) {
			return midi(true, argc-1, argv+1);
		} else if (strcmp("midiout", argv[1]) == 0) {
			return midi(false, argc-1, argv+1);
		} else {
			return usage();
		}
	}
}

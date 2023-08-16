#pragma once

#include <stdint.h>

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746D66
#define ID_DATA 0x61746164

class WavWriter
{
public:

	WavWriter();
	~WavWriter();

	bool	Open(const char* sFilename, int samplingRate, int channelCount = 2);
	void	AddAudioData(const int16_t* data, int sampleCount);
	void	Close();

private:
	FILE*	m_h;
	int		m_sampleCount;
	int		m_channelCount;
	int		m_samplingRate;

	struct WAVHeader
	{
		unsigned int RIFFMagic;
		unsigned int FileLength;
		unsigned int FileType;
		unsigned int FormMagic;
		unsigned int FormLength;
		unsigned short	SampleFormat;
		unsigned short NumChannels;
		unsigned int PlayRate;
		unsigned int BytesPerSec;
		unsigned short Stride;
		unsigned short BitsPerSample;
		unsigned int DataMagic;
		unsigned int DataLength;
	};

};



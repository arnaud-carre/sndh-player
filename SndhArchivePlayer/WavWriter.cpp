#define	_CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include "WavWriter.h"

WavWriter::WavWriter()
{
	m_h = NULL;
}

WavWriter::~WavWriter()
{
	if (m_h)
		Close();
}

bool	WavWriter::Open(const char* sFilename, int samplingRate, int channelCount /* = 2 */)
{
	bool ret = false;
	m_h = fopen(sFilename, "wb");
	if (m_h)
	{
		WAVHeader dummy;
		fwrite(&dummy, sizeof(WAVHeader), 1, m_h);
		m_samplingRate = samplingRate;
		m_channelCount = channelCount;
		m_sampleCount = 0;
		ret = true;
	}
	else
	{
		printf("ERROR: Unable to write file \"%s\"\n", sFilename);
	}
	return ret;
}

void	WavWriter::AddAudioData(const int16_t* data, int sampleCount)
{
	if (m_h)
	{
		const int rawSize = sizeof(int16_t) * sampleCount * m_channelCount;
		fwrite(data, sizeof(int16_t)*m_channelCount, sampleCount, m_h);
		m_sampleCount += sampleCount;
	}
}

void	WavWriter::Close()
{
	if (m_h)
	{
		fseek(m_h, 0, SEEK_SET);
		WAVHeader head;
		head.RIFFMagic = ID_RIFF;
		head.FileType = ID_WAVE;
		head.FormMagic = ID_FMT;
		head.DataMagic = ID_DATA;
		head.FormLength = 0x10;
		head.SampleFormat = 1;
		head.NumChannels = (unsigned short)m_channelCount;
		head.PlayRate = m_samplingRate;
		head.BitsPerSample = 16;
		head.Stride = (head.BitsPerSample / 8) * head.NumChannels;
		head.BytesPerSec = head.PlayRate * head.Stride;
		head.DataLength = m_sampleCount * head.Stride;
		head.FileLength = head.DataLength + sizeof(WAVHeader) - 8;
		fwrite(&head, 1, sizeof(WAVHeader), m_h);
		fseek(m_h, 0, SEEK_END);
		fclose(m_h);
		m_h = NULL;
	}
}

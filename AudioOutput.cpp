/*
 *  Audio output handling for SoftFM
 *
 *  Copyright (C) 2013, Joris van Rantwijk.
 *
 *  .WAV file writing by Sidney Cadot,
 *  adapted for SoftFM by Joris van Rantwijk.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see http://www.gnu.org/licenses/gpl-2.0.html
 */

#define _FILE_OFFSET_BITS 64

#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <algorithm>

#include "SoftFM.h"
#include "AudioOutput.h"

/********** DEBUG SETUP **********/
#define ENABLE_SDEBUG
#define DEBUG_PREFIX "AudioOutput: "
#include "utils/singleton.h"
#include "utils/screenlogger.h"
/*********************************/

using namespace std;

#define assert(X)

/* ****************  class AudioOutput  **************** */

// Encode a list of samples as signed 16-bit little-endian integers.
void AudioOutput::samplesToInt16(const SampleVector& samples, vector<uint8_t>& bytes)
{
    bytes.resize(2 * samples.size());

    SampleVector::const_iterator i = samples.begin();
    SampleVector::const_iterator n = samples.end();
    vector<uint8_t>::iterator k = bytes.begin();

    while (i != n) {
        Sample s = *(i++);
        s = max(Sample(-1.0), min(Sample(1.0), s));
        long v = lrint(s * 32767);
        unsigned long u = v;
        *(k++) = u & 0xff;
        *(k++) = (u >> 8) & 0xff;
    }
}

void AudioOutput::samplesToInt16(const SampleVector& samples, LF::audio::AudioBuffer& bytes)
{
    uint32_t size = samples.size() * 2;
    for (auto ss : samples)
    {
        Sample s = max(Sample(-1.0), min(Sample(1.0), ss));
        long v = lrint(s * 32767);
        unsigned long u = v;
        uint8_t temp[2];
        temp[0] = u & 0xff;
        temp [1] = (u >> 8) & 0xff;
        bytes.PushFramesBytes(temp, 2);
    }
}


/* ****************  class RawAudioOutput  **************** */

// Construct raw audio writer.
RawAudioOutput::RawAudioOutput(const string& filename)
{
    if (filename == "-") {

        m_fd = STDOUT_FILENO;

    } else {

        m_fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (m_fd < 0) {
            m_error  = "can not open '" + filename + "' (" +
                       strerror(errno) + ")";
            m_zombie = true;
            return;
        }

    }
}


// Destructor.
RawAudioOutput::~RawAudioOutput()
{
    // Close file descriptor.
    if (m_fd >= 0 && m_fd != STDOUT_FILENO) {
        close(m_fd);
    }
}


// Write audio data.
bool RawAudioOutput::write(const SampleVector& samples)
{
    if (m_fd < 0)
        return false;

    // Convert samples to bytes.
    samplesToInt16(samples, m_bytebuf);

    // Write data.
    size_t p = 0;
    size_t n = m_bytebuf.size();
    while (p < n) {

        ssize_t k = ::write(m_fd, m_bytebuf.data() + p, n - p);
        if (k <= 0) {
            if (k == 0 || errno != EINTR) {
                m_error = "write failed (";
                m_error += strerror(errno);
                m_error += ")";
                return false;
            }
        } else {
            p += k;
        }
    }

    return true;
}


/* ****************  class WavAudioOutput  **************** */

// Construct .WAV writer.
WavAudioOutput::WavAudioOutput(const std::string& filename,
                               unsigned int samplerate,
                               bool stereo)
  : numberOfChannels(stereo ? 2 : 1)
  , sampleRate(samplerate)
{
    m_stream = fopen(filename.c_str(), "wb");
    if (m_stream == NULL) {
        m_error  = "can not open '" + filename + "' (" +
                   strerror(errno) + ")";
        m_zombie = true;
        return;
    }

    // Write initial header with a dummy sample count.
    // This will be replaced with the actual header once the WavFile is closed.
    if (!write_header(0x7fff0000)) {
        m_error = "can not write to '" + filename + "' (" +
                  strerror(errno) + ")";
        m_zombie = true;
    }
}


// Destructor.
WavAudioOutput::~WavAudioOutput()
{
    // We need to go back and fill in the header ...

    if (!m_zombie) {

        const unsigned bytesPerSample = 2;

        const long currentPosition = ftell(m_stream);

        assert((currentPosition - 44) % bytesPerSample == 0);

        const unsigned totalNumberOfSamples = (currentPosition - 44) / bytesPerSample;

        assert(totalNumberOfSamples % numberOfChannels == 0);

        // Put header in front

        if (fseek(m_stream, 0, SEEK_SET) == 0) {
            write_header(totalNumberOfSamples);
        }
    }

    // Done writing the file

    if (m_stream) {
        fclose(m_stream);
    }
}


// Write audio data.
bool WavAudioOutput::write(const SampleVector& samples)
{
    if (m_zombie)
        return false;

    // Convert samples to bytes.
    samplesToInt16(samples, m_bytebuf);

    // Write samples to file.
    size_t k = fwrite(m_bytebuf.data(), 1, m_bytebuf.size(), m_stream);
    if (k != m_bytebuf.size()) {
        m_error = "write failed (";
        m_error += strerror(errno);
        m_error += ")";
        return false;
    }

    return true;
}


// (Re)write .WAV header.
bool WavAudioOutput::write_header(unsigned int nsamples)
{
    const unsigned bytesPerSample = 2;
    const unsigned bitsPerSample  = 16;

    enum wFormatTagId
    {
        eWAVE_FORMAT_PCM        = 0x0001,
        eWAVE_FORMAT_IEEE_FLOAT = 0x0003
    };

    assert(nsamples % numberOfChannels == 0);

    // synthesize header

    uint8_t wavHeader[44];

    encode_chunk_id    (wavHeader +  0, "RIFF");
    set_value<uint32_t>(wavHeader +  4, 36 + nsamples * bytesPerSample);
    encode_chunk_id    (wavHeader +  8, "WAVE");
    encode_chunk_id    (wavHeader + 12, "fmt ");
    set_value<uint32_t>(wavHeader + 16, 16);
    set_value<uint16_t>(wavHeader + 20, eWAVE_FORMAT_PCM);
    set_value<uint16_t>(wavHeader + 22, numberOfChannels);
    set_value<uint32_t>(wavHeader + 24, sampleRate                                    ); // sample rate
    set_value<uint32_t>(wavHeader + 28, sampleRate * numberOfChannels * bytesPerSample); // byte rate
    set_value<uint16_t>(wavHeader + 32,              numberOfChannels * bytesPerSample); // block size
    set_value<uint16_t>(wavHeader + 34, bitsPerSample);
    encode_chunk_id    (wavHeader + 36, "data");
    set_value<uint32_t>(wavHeader + 40, nsamples * bytesPerSample);

    return fwrite(wavHeader, 1, 44, m_stream) == 44;
}


void WavAudioOutput::encode_chunk_id(uint8_t * ptr, const char * chunkname)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        assert(chunkname[i] != '\0');
        ptr[i] = chunkname[i];
    }
    assert(chunkname[4] == '\0');
}


template <typename T>
void WavAudioOutput::set_value(uint8_t * ptr, T value)
{
    for (size_t i = 0; i < sizeof(T); ++i)
    {
        ptr[i] = value & 0xff;
        value >>= 8;
    }
}


/* ****************  class RtAudioOutput  **************** */

RtAudioOutput::RtAudioOutput(unsigned int samplerate, bool stereo) :
    mAudioBuffer(LF::audio::AudioFormat_t::Sint16, static_cast<uint16_t>(samplerate), (stereo ? 2 : 1)),
    mParameters(LF::audio::AudioFormat_t::Sint16, static_cast<uint16_t>(samplerate), (stereo ? 2 : 1)),
    mPlayer(mParameters)
{
    mPlayer.SetEndOfEmptyBuffer(false);
    mPlayer.SetBuffer(&mAudioBuffer);
    auto api = LF::audio::AudioDevice::GetDefaultApi();
    auto id = LF::audio::AudioDevice::GetDefaultOutDeviceId(api);
#if M_OS == M_OS_LINUX
    mPlayer.SetOutputDevice(LF::audio::AudioApi::LINUX_ALSA, 1);
#else
     mPlayer.SetOutputDevice(api, id);
#endif
    mPlayer.Start();
}

RtAudioOutput::~RtAudioOutput()
{
    mPlayer.Stop();
}

bool RtAudioOutput::write(const SampleVector& samples)
{
    samplesToInt16(samples, mAudioBuffer);
    return true;
}

/* end */

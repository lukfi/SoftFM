
#include <climits>
#include <cstring>
#include <rtl-sdr.h>

#include "RtlSdrSource.h"

/********** DEBUG SETUP **********/
//#define ENABLE_SDEBUG
#define DEBUG_PREFIX "RtlSdrSource: "
#include "utils/singleton.h"
#include "utils/screenlogger.h"
/*********************************/

#define LF_ASSERT_ON
#include "utils/assert.h"

using namespace std;

void rtlsdrsrc_callback(unsigned char* buf, uint32_t len, void* ctx)
{
    RtlSdrSource* rtlsdr = reinterpret_cast<RtlSdrSource*>(ctx);
    rtlsdr->DongleCallback(buf, len);
}

// Open RTL-SDR device.
RtlSdrSource::RtlSdrSource(int dev_index, bool async) :
    mAsync(async),
    m_dev(0),
    m_block_length(default_block_length)
{
    int r;

    const char *devname = rtlsdr_get_device_name(dev_index);
    if (devname != NULL)
        m_devname = devname;

    r = rtlsdr_open(&m_dev, dev_index);
    if (r < 0) {
        m_error =  "Failed to open RTL-SDR device (";
        m_error += strerror(-r);
        m_error += ")";
    }

    if (mAsync)
    {
        mSampleBuffer = new LF::utils::SWSRLFList<SampleBufferBlock>("RtlSdrSourceSampleBuffer");
    }
}


// Close RTL-SDR device.
RtlSdrSource::~RtlSdrSource()
{
    if (m_dev)
        rtlsdr_close(m_dev);
}


// Configure RTL-SDR tuner and prepare for streaming.
bool RtlSdrSource::configure(uint32_t sample_rate,
                             uint32_t frequency,
                             int tuner_gain,
                             int block_length,
                             bool agcmode)
{
    int r;

    if (!m_dev)
        return false;

    r = rtlsdr_set_sample_rate(m_dev, sample_rate);
    if (r < 0) {
        m_error = "rtlsdr_set_sample_rate failed";
        return false;
    }

    r = rtlsdr_set_center_freq(m_dev, frequency);
    if (r < 0) {
        m_error = "rtlsdr_set_center_freq failed";
        return false;
    }

    if (tuner_gain == INT_MIN) {
        r = rtlsdr_set_tuner_gain_mode(m_dev, 0);
        if (r < 0) {
            m_error = "rtlsdr_set_tuner_gain_mode could not set automatic gain";
            return false;
        }
    } else {
        r = rtlsdr_set_tuner_gain_mode(m_dev, 1);
        if (r < 0) {
            m_error = "rtlsdr_set_tuner_gain_mode could not set manual gain";
            return false;
        }

        r = rtlsdr_set_tuner_gain(m_dev, tuner_gain);
        if (r < 0) {
            m_error = "rtlsdr_set_tuner_gain failed";
            return false;
        }
    }

    // set RTL AGC mode
    r = rtlsdr_set_agc_mode(m_dev, int(agcmode));
    if (r < 0) {
        m_error = "rtlsdr_set_agc_mode failed";
        return false;
    }

    // set block length
    m_block_length = (block_length < 4096) ? 4096 :
                     (block_length > 1024 * 1024) ? 1024 * 1024 :
                     block_length;
    m_block_length -= m_block_length % 4096;

    // reset buffer to start streaming
    if (rtlsdr_reset_buffer(m_dev) < 0) {
        m_error = "rtlsdr_reset_buffer failed";
        return false;
    }

    return true;
}

bool RtlSdrSource::StartAsync()
{
    bool ret = false;
    if (mAsync && !mThread)
    {
        mThread = new std::thread(&RtlSdrSource::DongleThread, this, nullptr);
        ret = true;
    }
    return ret;
}

bool RtlSdrSource::StopAsync()
{
    bool ret = false;
    if (mAsync && mThread)
    {
        rtlsdr_cancel_async(m_dev);
        mThread->join();
        delete mThread;
        mThread = nullptr;
        ret = true;
    }
    return ret;
}


// Return current sample frequency in Hz.
uint32_t RtlSdrSource::get_sample_rate()
{
    return rtlsdr_get_sample_rate(m_dev);
}


// Return current center frequency in Hz.
uint32_t RtlSdrSource::get_frequency()
{
    return rtlsdr_get_center_freq(m_dev);
}


// Return current tuner gain in units of 0.1 dB.
int RtlSdrSource::get_tuner_gain()
{
    return rtlsdr_get_tuner_gain(m_dev);
}


// Return a list of supported tuner gain settings in units of 0.1 dB.
vector<int> RtlSdrSource::get_tuner_gains()
{
    int num_gains = rtlsdr_get_tuner_gains(m_dev, NULL);
    if (num_gains <= 0)
        return vector<int>();

    vector<int> gains(num_gains);
    if (rtlsdr_get_tuner_gains(m_dev, gains.data()) != num_gains)
        return vector<int>();

    return gains;
}


// Fetch a bunch of samples from the device.
bool RtlSdrSource::get_samples(IQSampleVector& samples)
{
    int r, n_read;

    if (!m_dev || mAsync)
        return false;

    vector<uint8_t> buf(2 * m_block_length);

    r = rtlsdr_read_sync(m_dev, buf.data(), 2 * m_block_length, &n_read);
    if (r < 0) {
        m_error = "rtlsdr_read_sync failed";
        return false;
    }

    if (n_read != 2 * m_block_length) {
        m_error = "short read, samples lost";
        return false;
    }

    samples.resize(m_block_length);
    for (int i = 0; i < m_block_length; i++) {
        int32_t re = buf[2*i];
        int32_t im = buf[2*i+1];
        samples[i] = IQSample( (re - 128) / IQSample::value_type(128),
                               (im - 128) / IQSample::value_type(128) );
    }

    return true;
}


// Return a list of supported devices.
vector<string> RtlSdrSource::get_device_names()
{
    vector<string> result;

    int device_count = rtlsdr_get_device_count();
    if (device_count <= 0)
        return result;

    result.reserve(device_count);
    for (int i = 0; i < device_count; i++) {
        result.push_back(string(rtlsdr_get_device_name(i)));
    }

    return result;
}

void RtlSdrSource::DongleThread(void*)
{
    SDEB("Started DongleThread");
    rtlsdr_read_async(m_dev, rtlsdrsrc_callback, this, 0, 0);
    SDEB("Stopped DongleThread");
}

void RtlSdrSource::DongleCallback(uint8_t* buf, size_t len)
{
    SDEB("+");
//    auto* s = &mDongleState;
//    struct demod_state *d = s->demod_target;

    size_t iqSamples = len / 2;
    SampleBufferBlock* block = mSampleBuffer->GetBlockToWrite();
    if (block)
    {
        for (size_t i = 0; i < iqSamples; ++i)
        {
            int32_t re = buf[2 * i];
            int32_t im = buf[2 * i + 1];
            block->samples[i] = IQSample((re - 128) / IQSample::value_type(128),
                                         (im - 128) / IQSample::value_type(128));
        }
//        if (s->mute)
//        {
//            for (i=0; i<s->mute; i++)
//            {
//                buf[i] = 127;
//            }
//            s->mute = 0;
//        }
//        if (!s->offset_tuning)
//        {
//            rotate_90(buf, len);
//        }
//        for (int i = 0; i < (int)len; i++)
//        {
//            block->buf16[i] = (int16_t)buf[i] - 127;
//        }
//        block->len = len;
        mSampleBuffer->UpdateWriteState();
    }
    else
    {
        SWAR("SampleBuffer is full");
    }
    NEW_DATA.Emit(this);
}

/* end */

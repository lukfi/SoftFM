#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <thread>

#include "threads/signals.h"
#include "utils/swsrlflist.h"

#include "SoftFM.h"

#define MAXIMUM_OVERSAMPLE 16
#define DEFAULT_BUF_LENGTH (1 * 16384)
#define MAXIMUM_BUF_LENGTH (MAXIMUM_OVERSAMPLE * DEFAULT_BUF_LENGTH)

class SampleBufferBlock : public LF::utils::SWSRLFListBlock
{
public:
    IQSample samples[MAXIMUM_BUF_LENGTH];
    int size;
};

//class IQSampleSource
//{
//public:
//    virtual ~SampleSource() {}
//    virtual bool Start() = 0;
////    SampleSource();
//    Signal<void(SampleSource*)> NEW_DATA;

//    virtual BufferBlock* GetBlockToRead() = 0;
//    virtual void UpdateReadState() = 0;
//};

class RtlSdrSource
{
public:

    Signal<void(RtlSdrSource*)> NEW_DATA;

    static const int default_block_length = 65536;

    /** Open RTL-SDR device. */
    RtlSdrSource(int dev_index, bool async = false);

    /** Close RTL-SDR device. */
    ~RtlSdrSource();

    /**
     * Configure RTL-SDR tuner and prepare for streaming.
     *
     * sample_rate  :: desired sample rate in Hz.
     * frequency    :: desired center frequency in Hz.
     * tuner_gain   :: desired tuner gain in 0.1 dB, or INT_MIN for auto-gain.
     * block_length :: preferred number of samples per block.
     *
     * Return true for success, false if an error occurred.
     */
    bool configure(std::uint32_t sample_rate,
                   std::uint32_t frequency,
                   int tuner_gain,
                   int block_length=default_block_length,
                   bool agcmode=false);

    bool StartAsync();
    bool StopAsync();

    /** Return current sample frequency in Hz. */
    std::uint32_t get_sample_rate();

    /** Return current center frequency in Hz. */
    std::uint32_t get_frequency();

    /** Return current tuner gain in units of 0.1 dB. */
    int get_tuner_gain();

    /** Return a list of supported tuner gain settings in units of 0.1 dB. */
    std::vector<int> get_tuner_gains();

    /** Return name of opened RTL-SDR device. */
    std::string get_device_name() const
    {
        return m_devname;
    }

    /**
     * Fetch a bunch of samples from the device.
     *
     * This function must be called regularly to maintain streaming.
     * Return true for success, false if an error occurred.
     */
    bool get_samples(IQSampleVector& samples);

    /** Return the last error, or return an empty string if there is no error. */
    std::string error()
    {
        std::string ret(m_error);
        m_error.clear();
        return ret;
    }

    /** Return true if the device is OK, return false if there is an error. */
    operator bool() const
    {
        return m_dev && m_error.empty();
    }

    /** Return a list of supported devices. */
    static std::vector<std::string> get_device_names();

private:
    const bool mAsync;
    LF::utils::SWSRLFList<SampleBufferBlock>* mSampleBuffer { nullptr };
    std::thread* mThread { nullptr };

    void DongleThread(void*);
    void DongleCallback(uint8_t* buf, size_t len);

    struct rtlsdr_dev * m_dev;
    int                 m_block_length;
    std::string         m_devname;
    std::string         m_error;

    friend void rtlsdrsrc_callback(unsigned char* buf, uint32_t len, void* ctx);
};

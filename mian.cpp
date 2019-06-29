#include <climits>
#include <getopt.h>
#include <string>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

#include "AudioOutput.h"
#include "RtlSdrSource.h"
#include "FmDecode.h"

#include "threads/threadutils.h"

/********** DEBUG SETUP **********/
#define ENABLE_SDEBUG
#define DEBUG_PREFIX "SoftFM: "
#include "utils/singleton.h"
#include "utils/screenlogger.h"
/*********************************/

//#define USE_OLD_MAIN

extern int oldmain(int argc, char **argv);
extern bool parse_dbl(const char *s, double& v);
extern void badarg(const char *label);
extern bool parse_int(const char *s, int& v, bool allow_unit=false);
extern bool parse_dbl(const char *s, double& v);

static void usage()
{
    fprintf(stderr,
    "Usage: softfm -f freq [options]\n"
            "  -f freq       Frequency of radio station in Hz\n"
            "  -d devidx     RTL-SDR device index, 'list' to show device list (default 0)\n"
            "  -g gain       Set LNA gain in dB, or 'auto' (default auto)\n"
            "  -a            Enable RTL AGC mode (default disabled)\n"
            "  -s ifrate     IF sample rate in Hz (default 1200000)\n"
            "                (valid ranges: [225001, 300000], [900001, 3200000]))\n"
            "  -r pcmrate    Audio sample rate in Hz (default 48000 Hz)\n"
            "  -M            Disable stereo decoding\n"
            "  -R filename   Write audio data as raw S16_LE samples\n"
            "                use filename '-' to write to stdout\n"
            "  -W filename   Write audio data to .WAV file\n"
            "  -P [device]   Play audio via RTAudio device (default 'default')\n"
            "  -T filename   Write pulse-per-second timestamps\n"
            "                use filename '-' to write to stdout\n"
            "  -b seconds    Set audio buffer size in seconds\n"
            "\n");
}

int main(int argc, char **argv)
{
#ifdef USE_OLD_MAIN
    oldmain(argc, argv);
#else
    double  freq    = -1;
    int     devidx  = 0;
    int     lnagain = INT_MIN;
    bool    agcmode = false;
    double  ifrate  = 1.2e6;
    int     pcmrate = 48000;
    bool    stereo  = true;
    enum OutputMode { MODE_RAW, MODE_WAV, MODE_RTAUDIO };
    OutputMode outmode = MODE_RTAUDIO;
    std::string  filename;
    std::string  ppsfilename;
    FILE*  ppsfile = nullptr;
    double  bufsecs = -1;

    SDEB("SoftFM - Software decoder for FM broadcast radio with RTL-SDR");

    const struct option longopts[] = {
        { "freq",       1, nullptr, 'f' },
        { "dev",        1, nullptr, 'd' },
        { "gain",       1, nullptr, 'g' },
        { "ifrate",     1, nullptr, 's' },
        { "pcmrate",    1, nullptr, 'r' },
        { "agc",        0, nullptr, 'a' },
        { "mono",       0, nullptr, 'M' },
        { "raw",        1, nullptr, 'R' },
        { "wav",        1, nullptr, 'W' },
        { "play",       2, nullptr, 'P' },
        { "pps",        1, nullptr, 'T' },
        { "buffer",     1, nullptr, 'b' },
        { nullptr,      0, nullptr, 0 } };

    int c, longindex;
    while ((c = getopt_long(argc, argv, "f:d:g:s:r:MR:W:P::T:b:a", longopts, &longindex)) >= 0)
    {
        switch (c)
        {
            case 'f':
                if (!parse_dbl(optarg, freq) || freq <= 0)
                {
                    badarg("-f");
                }
                break;
            case 'd':
                if (!parse_int(optarg, devidx))
                {
                    devidx = -1;
                }
                break;
            case 'g':
                if (strcasecmp(optarg, "auto") == 0)
                {
                    lnagain = INT_MIN;
                }
                else if (strcasecmp(optarg, "list") == 0)
                {
                    lnagain = INT_MIN + 1;
                }
                else
                {
                    double tmpgain;
                    if (!parse_dbl(optarg, tmpgain))
                    {
                        badarg("-g");
                    }
                    long int tmpgain2 = lrint(tmpgain * 10);
                    if (tmpgain2 <= INT_MIN || tmpgain2 >= INT_MAX)
                    {
                        badarg("-g");
                    }
                    lnagain = tmpgain2;
                }
                break;
            case 's':
                // NOTE: RTL does not support some sample rates below 900 kS/s
                // Also, max sampling rate is 3.2 MS/s
                if (!parse_dbl(optarg, ifrate))
                {
                    badarg("-s");
                }
                if ((ifrate < 225001) || (ifrate > 3200000) || ((ifrate > 300000) && (ifrate < 900001)))
                {
                    badarg("-s");
                }
                break;
            case 'r':
                if (!parse_int(optarg, pcmrate, true) || pcmrate < 1)
                {
                    badarg("-r");
                }
                break;
            case 'M':
                stereo = false;
                break;
            case 'R':
                outmode = MODE_RAW;
                filename = optarg;
                break;
            case 'W':
                outmode = MODE_WAV;
                filename = optarg;
                break;
            case 'P':
                outmode = MODE_RTAUDIO;
//                if (optarg != NULL)
//                    alsadev = optarg;
                break;
            case 'T':
                ppsfilename = optarg;
                break;
            case 'b':
                if (!parse_dbl(optarg, bufsecs) || bufsecs < 0)
                {
                    badarg("-b");
                }
                break;
            case 'a':
                agcmode = true;
                break;
            default:
                usage();
                SERR("Invalid command line options");
                exit(1);
        }
    }

    if (optind < argc)
    {
        usage();
        fprintf(stderr, "ERROR: Unexpected command line options\n");
        exit(1);
    }

    std::vector<std::string> devnames = RtlSdrSource::get_device_names();
    if (devidx < 0 || (unsigned int)devidx >= devnames.size())
    {
        if (devidx != -1)
        {
            SERR("invalid device index %d", devidx);
        }
        SDEB("Found %u devices: ", (unsigned int)devnames.size());
        for (unsigned int i = 0; i < devnames.size(); i++)
        {
            SDEB("%2u: %s", i, devnames[i].c_str());
        }
        exit(1);
    }
    SDEB("using device %d: %s", devidx, devnames[devidx].c_str());

    if (freq <= 0)
    {
        usage();
        SERR("ERROR: Specify a tuning frequency");
        exit(1);
    }

    // Intentionally tune at a higher frequency to avoid DC offset.
    double tuner_freq = freq + 0.25 * ifrate;

    // Open RTL-SDR device.
    RtlSdrSource rtlsdr(devidx, true);
    if (!rtlsdr)
    {
        SERR("RtlSdr: %s", rtlsdr.error().c_str());
        exit(1);
    }

    // Check LNA gain.
    if (lnagain != INT_MIN)
    {
        std::vector<int> gains = rtlsdr.get_tuner_gains();
        if (std::find(gains.begin(), gains.end(), lnagain) == gains.end())
        {
            if (lnagain != INT_MIN + 1)
            {
                SERR("LNA gain %.1f dB not supported by tuner", lnagain * 0.1);
            }
            SDEB("Supported LNA gains: ");
            for (int g: gains)
            {
                SDEB("\t%.1f dB ", 0.1 * g);
            }
            exit(1);
        }
    }

    // Configure RTL-SDR device and start streaming.
    rtlsdr.configure(ifrate, tuner_freq, lnagain, RtlSdrSource::default_block_length, agcmode);
    if (!rtlsdr)
    {
        SERR("RtlSdr: %s", rtlsdr.error().c_str());
        exit(1);
    }

    tuner_freq = rtlsdr.get_frequency();
    SDEB("device tuned for: %.6f MHz", tuner_freq * 1.0e-6);

    if (lnagain == INT_MIN)
    {
        SDEB("LNA gain: auto");
    }
    else
    {
        SDEB("LNA gain: %.1f dB", 0.1 * rtlsdr.get_tuner_gain());
    }

    ifrate = rtlsdr.get_sample_rate();
    SDEB("IF sample rate: %.0f Hz", ifrate);

    SDEB("RTL AGC mode: %s", agcmode ? "enabled" : "disabled");

    // The baseband signal is empty above 100 kHz, so we can
    // downsample to ~ 200 kS/s without loss of information.
    // This will speed up later processing stages.
    unsigned int downsample = std::max(1, int(ifrate / 215.0e3));
    SDEB("baseband downsampling factor %u", downsample);

    // Prevent aliasing at very low output sample rates.
    double bandwidth_pcm = std::min(FmDecoder::default_bandwidth_pcm, 0.45 * pcmrate);
    SDEB("audio sample rate: %u Hz", pcmrate);
    SDEB("audio bandwidth: %.3f kHz", bandwidth_pcm * 1.0e-3);

    // Open PPS file.
    if (!ppsfilename.empty())
    {
        if (ppsfilename == "-")
        {
            SDEB("writing pulse-per-second markers to stdout");
            ppsfile = stdout;
        }
        else
        {
            SDEB("writing pulse-per-second markers to '%s'", ppsfilename.c_str());
            ppsfile = fopen(ppsfilename.c_str(), "w");
            if (ppsfile == nullptr)
            {
                SERR("can not open '%s' (%s)", ppsfilename.c_str(), strerror(errno));
                exit(1);
            }
        }
        fprintf(ppsfile, "#pps_index sample_index   unix_time\n");
        fflush(ppsfile);
    }

    // Prepare output writer.
    std::unique_ptr<AudioOutput> audio_output;
    switch (outmode)
    {
        case MODE_RAW:
            SDEB("writing raw 16-bit audio samples to '%s'", filename.c_str());
            audio_output.reset(new RawAudioOutput(filename));
            break;
        case MODE_WAV:
            SDEB("writing audio samples to '%s'", filename.c_str());
            audio_output.reset(new WavAudioOutput(filename, pcmrate, stereo));
            break;
        case MODE_RTAUDIO:
            SDEB("playing audio to RTAudio default device");
            audio_output.reset(new RtAudioOutput(pcmrate, stereo));
            break;
    }

    if (!(*audio_output))
    {
        SERR("AudioOutput: %s", audio_output->error().c_str());
        exit(1);
    }

    FmDecoderThread dec(&rtlsdr, audio_output.get());
    dec.CreateDecoder(ifrate,                            // sample_rate_if
                      freq - tuner_freq,                 // tuning_offset
                      pcmrate,                           // sample_rate_pcm
                      stereo,                            // stereo
                      FmDecoder::default_deemphasis,     // deemphasis,
                      FmDecoder::default_bandwidth_if,   // bandwidth_if
                      FmDecoder::default_freq_dev,       // freq_dev
                      bandwidth_pcm,                     // bandwidth_pcm
                      downsample);
    rtlsdr.StartAsync();

    LF::threads::SleepSec(100);
#endif
}

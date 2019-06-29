#ifndef SOFTFM_H
#define SOFTFM_H

#include <complex>
#include <vector>

//typedef double Sample;
typedef float Sample;
typedef std::vector<Sample> SampleVector;

typedef std::complex<Sample> IQSample;
typedef std::vector<IQSample> IQSampleVector;

/** Compute mean and RMS over a sample vector. */
inline void samples_mean_rms(const SampleVector& samples,
                             double& mean, double& rms)
{
    Sample vsum = 0;
    Sample vsumsq = 0;

    unsigned int n = samples.size();
    for (unsigned int i = 0; i < n; i++) {
        Sample v = samples[i];
        vsum   += v;
        vsumsq += v * v;
    }

    mean = vsum / n;
    rms  = sqrt(vsumsq / n);
}

#endif

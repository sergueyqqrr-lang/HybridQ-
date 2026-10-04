#include "BandProcessor.h"

//==============================================================================
BandProcessor::BandProcessor() = default;

void BandProcessor::prepare (const juce::dsp::ProcessSpec& spec)
{
    preparedSampleRate = spec.sampleRate;

    juce::dsp::ProcessSpec monoSpec = spec;
    monoSpec.numChannels = 1;

    chainA.prepare (monoSpec);
    chainB.prepare (monoSpec);

    midSideScratch.setSize (2, (int) spec.maximumBlockSize);

    reset();
}

void BandProcessor::reset()
{
    chainA.reset();
    chainB.reset();
}

//==============================================================================
void BandProcessor::makeCoefficientsForType (double sampleRate, FilterType type,
                                              float frequencyHz, float gainDb, float q,
                                              Coeffs::Ptr& outMain, Coeffs::Ptr& outTilt2)
{
    frequencyHz = juce::jlimit (20.0f, (float) (sampleRate * 0.49), frequencyHz);
    q           = juce::jmax (0.05f, q);

    const auto gainLinear = juce::Decibels::decibelsToGain (gainDb);

    switch (type)
    {
        case FilterType::Bell:
            outMain = Coeffs::makePeakFilter (sampleRate, frequencyHz, q, gainLinear);
            break;

        case FilterType::LowShelf:
            outMain = Coeffs::makeLowShelf (sampleRate, frequencyHz, q, gainLinear);
            break;

        case FilterType::HighShelf:
            outMain = Coeffs::makeHighShelf (sampleRate, frequencyHz, q, gainLinear);
            break;

        case FilterType::LowCut:
            outMain = Coeffs::makeHighPass (sampleRate, frequencyHz, q);
            break;

        case FilterType::HighCut:
            outMain = Coeffs::makeLowPass (sampleRate, frequencyHz, q);
            break;

        case FilterType::Notch:
            outMain = Coeffs::makeNotch (sampleRate, frequencyHz, q);
            break;

        case FilterType::BandPass:
            outMain = Coeffs::makeBandPass (sampleRate, frequencyHz, q);
            break;

        case FilterType::TiltShelf:
        {
            const auto halfGainLinear    = juce::Decibels::decibelsToGain (gainDb * 0.5f);
            const auto halfGainLinearInv = juce::Decibels::decibelsToGain (gainDb * -0.5f);

            outMain  = Coeffs::makeLowShelf  (sampleRate, frequencyHz, q, halfGainLinearInv);
            outTilt2 = Coeffs::makeHighShelf (sampleRate, frequencyHz, q, halfGainLinear);
            break;
        }

        default:
            jassertfalse;
            break;
    }
}

//==============================================================================
void BandProcessor::updateCoefficients (double sampleRate, FilterType type,
                                         float frequencyHz, float gainDb, float q, float phaseBlend)
{
    currentType = type;
    preparedSampleRate = sampleRate;
    currentBlend = juce::jlimit (0.0f, 1.0f, phaseBlend);

    makeCoefficientsForType (sampleRate, type, frequencyHz, gainDb, q,
                              currentCoeffs, currentCoeffsTilt2);

    // Los coeficientes IIR se copian (por valor) a las CUATRO instancias de
    // filtro (dos chains x dos lanes) -- cada Filter mantiene su propia
    // memoria interna pero comparten la misma respuesta en frecuencia.
    *chainA.laneA.coefficients = *currentCoeffs;
    *chainA.laneB.coefficients = *currentCoeffs;
    *chainB.laneA.coefficients = *currentCoeffs;
    *chainB.laneB.coefficients = *currentCoeffs;

    if (type == FilterType::TiltShelf)
    {
        *chainA.laneATilt2.coefficients = *currentCoeffsTilt2;
        *chainA.laneBTilt2.coefficients = *currentCoeffsTilt2;
        *chainB.laneATilt2.coefficients = *currentCoeffsTilt2;
        *chainB.laneBTilt2.coefficients = *currentCoeffsTilt2;
    }

    const bool paramsChanged = (frequencyHz != lastDesignFreq || gainDb != lastDesignGain
                                 || q != lastDesignQ || type != lastDesignType
                                 || currentBlend != lastDesignBlend || sampleRate != lastDesignSampleRate);

    if (currentBlend > 0.0f && paramsChanged)
    {
        designBlendedFIR (sampleRate, type, frequencyHz, gainDb, q, currentBlend);

        lastDesignFreq = frequencyHz;
        lastDesignGain = gainDb;
        lastDesignQ = q;
        lastDesignType = type;
        lastDesignBlend = currentBlend;
        lastDesignSampleRate = sampleRate;
    }
}

//==============================================================================
// NOTA DE REAL-TIME-SAFETY: ver la nota extensa que ya existía en la
// versión de Fase 2 de este método -- sigue aplicando igual aquí. Lo único
// que cambia en Fase 3 es que el FIR resultante se comparte (vía el mismo
// puntero con conteo de referencias) entre las CUATRO instancias de FIR
// Filter de ambas chains, en vez de solo dos.
void BandProcessor::designBlendedFIR (double sampleRate, FilterType type, float frequencyHz,
                                       float gainDb, float q, float blend)
{
    constexpr int N = firDesignSize;
    const int half = N / 2;

    for (int k = 0; k <= half; ++k)
    {
        const double binFreq = juce::jlimit (1.0, sampleRate * 0.499,
                                              (double) k * sampleRate / (double) N);
        const auto db = computeMagnitudeForFrequencyDb (sampleRate, type, frequencyHz, gainDb, q,
                                                         (float) binFreq);
        fullMagnitude[(size_t) k] = juce::Decibels::decibelsToGain (db);
    }

    for (int k = 1; k < half; ++k)
        fullMagnitude[(size_t) (N - k)] = fullMagnitude[(size_t) k];

    for (int k = 0; k < N; ++k)
        logMagnitude[(size_t) k] = std::log (juce::jmax (1.0e-6f, fullMagnitude[(size_t) k]));

    for (int k = 0; k < N; ++k)
        fftBufferA[(size_t) k] = std::complex<float> (logMagnitude[(size_t) k], 0.0f);

    fft.perform (fftBufferA.data(), fftBufferB.data(), true); // true = inversa

    windowedCepstrum[0] = fftBufferB[0].real();
    for (int n = 1; n < half; ++n)
        windowedCepstrum[(size_t) n] = 2.0f * fftBufferB[(size_t) n].real();
    windowedCepstrum[(size_t) half] = fftBufferB[(size_t) half].real();
    for (int n = half + 1; n < N; ++n)
        windowedCepstrum[(size_t) n] = 0.0f;

    for (int k = 0; k < N; ++k)
        fftBufferA[(size_t) k] = std::complex<float> (windowedCepstrum[(size_t) k], 0.0f);

    fft.perform (fftBufferA.data(), fftBufferB.data(), false); // false = directa

    for (int k = 0; k < N; ++k)
        minPhase[(size_t) k] = fftBufferB[(size_t) k].imag();

    for (int k = 0; k < N; ++k)
    {
        const float linPhase = -juce::MathConstants<float>::pi * (float) k;
        const float blendedPhase = (1.0f - blend) * minPhase[(size_t) k] + blend * linPhase;

        if (k == 0 || k == half)
            fftBufferA[(size_t) k] = std::complex<float> (fullMagnitude[(size_t) k]
                                                                * std::cos (blendedPhase), 0.0f);
        else
            fftBufferA[(size_t) k] = std::polar (fullMagnitude[(size_t) k], blendedPhase);
    }

    for (int k = 1; k < half; ++k)
        fftBufferA[(size_t) (N - k)] = std::conj (fftBufferA[(size_t) k]);

    fft.perform (fftBufferA.data(), fftBufferB.data(), true);

    for (int n = 0; n < N; ++n)
        newTaps[(size_t) n] = fftBufferB[(size_t) n].real();

    // Un único objeto de coeficientes, compartido (vía conteo de
    // referencias) por las cuatro instancias de FIR::Filter -- es de solo
    // lectura durante el procesamiento, así que compartirlo es seguro y
    // evita cuadruplicar la memoria de los 512 taps.
    auto* firCoeffs = new FIRCoeffs (newTaps.data(), (size_t) N);
    chainA.firA.coefficients = firCoeffs;
    chainA.firB.coefficients = firCoeffs;
    chainB.firA.coefficients = firCoeffs;
    chainB.firB.coefficients = firCoeffs;
}

//==============================================================================
namespace
{
    void processSingleChannel (juce::dsp::IIR::Filter<float>& filter,
                                juce::AudioBuffer<float>& buffer, int channel)
    {
        auto block = juce::dsp::AudioBlock<float> (buffer).getSingleChannelBlock ((size_t) channel);
        juce::dsp::ProcessContextReplacing<float> context (block);
        filter.process (context);
    }

    void processSingleChannel (juce::dsp::FIR::Filter<float>& filter,
                                juce::AudioBuffer<float>& buffer, int channel)
    {
        auto block = juce::dsp::AudioBlock<float> (buffer).getSingleChannelBlock ((size_t) channel);
        juce::dsp::ProcessContextReplacing<float> context (block);
        filter.process (context);
    }

    void processSingleChannel (juce::dsp::DelayLine<float>& delay,
                                juce::AudioBuffer<float>& buffer, int channel)
    {
        auto block = juce::dsp::AudioBlock<float> (buffer).getSingleChannelBlock ((size_t) channel);
        juce::dsp::ProcessContextReplacing<float> context (block);
        delay.process (context);
    }
}

void BandProcessor::processWithChain (DspChain& chain, juce::AudioBuffer<float>& buffer,
                                       ChannelMode channelMode, juce::AudioBuffer<float>& scratch)
{
    const auto numSamples = buffer.getNumSamples();
    const bool isTilt = (currentType == FilterType::TiltShelf);
    const bool useFIR = currentBlend > 0.0f;

    auto runLane = [&] (Filter& lane, Filter& laneTilt2, FIRFilter& fir,
                         juce::dsp::DelayLine<float>& delay, juce::AudioBuffer<float>& buf, int channel)
    {
        if (useFIR)
        {
            processSingleChannel (fir, buf, channel);
        }
        else
        {
            processSingleChannel (lane, buf, channel);
            if (isTilt)
                processSingleChannel (laneTilt2, buf, channel);
            processSingleChannel (delay, buf, channel);
        }
    };

    switch (channelMode)
    {
        case ChannelMode::Stereo:
        {
            runLane (chain.laneA, chain.laneATilt2, chain.firA, chain.delayA, buffer, 0);
            if (buffer.getNumChannels() > 1)
                runLane (chain.laneB, chain.laneBTilt2, chain.firB, chain.delayB, buffer, 1);
            break;
        }

        case ChannelMode::Left:
        {
            runLane (chain.laneA, chain.laneATilt2, chain.firA, chain.delayA, buffer, 0);
            break;
        }

        case ChannelMode::Right:
        {
            if (buffer.getNumChannels() > 1)
                runLane (chain.laneA, chain.laneATilt2, chain.firA, chain.delayA, buffer, 1);
            break;
        }

        case ChannelMode::Mid:
        case ChannelMode::Side:
        {
            if (buffer.getNumChannels() < 2)
            {
                runLane (chain.laneA, chain.laneATilt2, chain.firA, chain.delayA, buffer, 0);
                break;
            }

            auto* left  = buffer.getWritePointer (0);
            auto* right = buffer.getWritePointer (1);
            auto* mid   = scratch.getWritePointer (0);
            auto* side  = scratch.getWritePointer (1);

            for (int i = 0; i < numSamples; ++i)
            {
                mid[i]  = 0.5f * (left[i] + right[i]);
                side[i] = 0.5f * (left[i] - right[i]);
            }

            const int laneChannel = (channelMode == ChannelMode::Mid) ? 0 : 1;
            runLane (chain.laneA, chain.laneATilt2, chain.firA, chain.delayA, scratch, laneChannel);

            for (int i = 0; i < numSamples; ++i)
            {
                left[i]  = mid[i] + side[i];
                right[i] = mid[i] - side[i];
            }
            break;
        }

        default:
            jassertfalse;
            break;
    }
}

//==============================================================================
void BandProcessor::process (juce::AudioBuffer<float>& fullBuffer,
                              juce::AudioBuffer<float>* transientBuffer,
                              juce::AudioBuffer<float>* sustainBuffer,
                              ChannelMode channelMode, SplitMode splitMode)
{
    if (! isActive)
        return;

    if (transientBuffer == nullptr || sustainBuffer == nullptr)
    {
        // Optimización: ninguna banda del plugin necesita el split, así
        // que el processor ni siquiera construyó los buffers. Se procesa
        // directo, comportamiento idéntico a Fases 1-2.
        jassert (splitMode == SplitMode::Full);
        processWithChain (chainA, fullBuffer, channelMode, midSideScratch);
        return;
    }

    switch (splitMode)
    {
        case SplitMode::Full:
            // Linealidad: aplicar el mismo filtro a transient y sustain por
            // separado y sumar después es exactamente equivalente a
            // aplicarlo a la señal combinada (ver nota de clase).
            processWithChain (chainA, *transientBuffer, channelMode, midSideScratch);
            processWithChain (chainB, *sustainBuffer, channelMode, midSideScratch);
            break;

        case SplitMode::Transient:
            processWithChain (chainA, *transientBuffer, channelMode, midSideScratch);
            break;

        case SplitMode::Sustain:
            processWithChain (chainB, *sustainBuffer, channelMode, midSideScratch);
            break;

        default:
            jassertfalse;
            break;
    }
}

//==============================================================================
float BandProcessor::computeMagnitudeForFrequencyDb (double sampleRate, FilterType type,
                                                      float bandFreqHz, float gainDb, float q,
                                                      float evaluateAtHz)
{
    Coeffs::Ptr localCoeffs, localCoeffsTilt2;
    makeCoefficientsForType (sampleRate, type, bandFreqHz, gainDb, q, localCoeffs, localCoeffsTilt2);

    if (localCoeffs == nullptr)
        return 0.0f;

    const auto clampedEvalFreq = juce::jlimit (20.0f, (float) (sampleRate * 0.49), evaluateAtHz);
    auto magnitude = localCoeffs->getMagnitudeForFrequency (clampedEvalFreq, sampleRate);

    if (type == FilterType::TiltShelf && localCoeffsTilt2 != nullptr)
        magnitude *= localCoeffsTilt2->getMagnitudeForFrequency (clampedEvalFreq, sampleRate);

    return juce::Decibels::gainToDecibels ((float) magnitude, -100.0f);
}

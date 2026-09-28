#include "BandProcessor.h"

//==============================================================================
void BandProcessor::prepare (const juce::dsp::ProcessSpec& spec)
{
    preparedSampleRate = spec.sampleRate;

    // Cada lane procesa UN canal, así que su spec interno es mono.
    juce::dsp::ProcessSpec monoSpec = spec;
    monoSpec.numChannels = 1;

    laneA.prepare (monoSpec);
    laneB.prepare (monoSpec);
    laneATilt2.prepare (monoSpec);
    laneBTilt2.prepare (monoSpec);

    // Scratch de 2 canales (mid, side) del tamaño máximo de bloque esperado.
    midSideScratch.setSize (2, (int) spec.maximumBlockSize);

    reset();
}

void BandProcessor::reset()
{
    laneA.reset();
    laneB.reset();
    laneATilt2.reset();
    laneBTilt2.reset();
}

//==============================================================================
void BandProcessor::makeCoefficientsForType (double sampleRate, FilterType type,
                                              float frequencyHz, float gainDb, float q,
                                              Coeffs::Ptr& outMain, Coeffs::Ptr& outTilt2)
{
    // Clamps defensivos -- protegen contra automatización de host con
    // valores fuera de rango o Q=0 (que rompería las fórmulas RBJ).
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
            // Fase 1: pendiente única de 12 dB/oct (una sola sección biquad).
            // Pendientes seleccionables (24/48 dB/oct via cascada) quedan
            // para el pulido de Fase 4/5, no son parte del criterio de
            // salida de esta fase.
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
            // Aproximación estándar de un tilt EQ: un low-shelf y un
            // high-shelf con la misma frecuencia de esquina y ganancias
            // opuestas de magnitud gainDb/2, en cascada. El resultado
            // "inclina" el espectro alrededor de `frequencyHz` sin
            // necesitar una topología de filtro dedicada.
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
                                         float frequencyHz, float gainDb, float q)
{
    currentType = type;
    preparedSampleRate = sampleRate;

    makeCoefficientsForType (sampleRate, type, frequencyHz, gainDb, q,
                              currentCoeffs, currentCoeffsTilt2);

    *laneA.coefficients = *currentCoeffs;
    *laneB.coefficients = *currentCoeffs;

    if (type == FilterType::TiltShelf)
    {
        *laneATilt2.coefficients = *currentCoeffsTilt2;
        *laneBTilt2.coefficients = *currentCoeffsTilt2;
    }
}

//==============================================================================
namespace
{
    /** Aplica un juce::dsp::IIR::Filter a un único canal de un AudioBuffer. */
    void processSingleChannel (juce::dsp::IIR::Filter<float>& filter,
                                juce::AudioBuffer<float>& buffer, int channel)
    {
        auto block = juce::dsp::AudioBlock<float> (buffer).getSingleChannelBlock ((size_t) channel);
        juce::dsp::ProcessContextReplacing<float> context (block);
        filter.process (context);
    }
}

void BandProcessor::process (juce::AudioBuffer<float>& buffer, ChannelMode channelMode)
{
    if (! isActive)
        return;

    const auto numSamples = buffer.getNumSamples();
    const bool isTilt = (currentType == FilterType::TiltShelf);

    auto runLane = [&] (Filter& lane, Filter& laneTilt2, juce::AudioBuffer<float>& buf, int channel)
    {
        processSingleChannel (lane, buf, channel);
        if (isTilt)
            processSingleChannel (laneTilt2, buf, channel);
    };

    switch (channelMode)
    {
        case ChannelMode::Stereo:
        {
            runLane (laneA, laneATilt2, buffer, 0);
            if (buffer.getNumChannels() > 1)
                runLane (laneB, laneBTilt2, buffer, 1);
            break;
        }

        case ChannelMode::Left:
        {
            runLane (laneA, laneATilt2, buffer, 0);
            break;
        }

        case ChannelMode::Right:
        {
            if (buffer.getNumChannels() > 1)
                runLane (laneA, laneATilt2, buffer, 1);
            break;
        }

        case ChannelMode::Mid:
        case ChannelMode::Side:
        {
            if (buffer.getNumChannels() < 2)
            {
                // Sin canal derecho no hay M/S real; se degrada a Stereo/Left.
                runLane (laneA, laneATilt2, buffer, 0);
                break;
            }

            auto* left  = buffer.getWritePointer (0);
            auto* right = buffer.getWritePointer (1);
            auto* mid   = midSideScratch.getWritePointer (0);
            auto* side  = midSideScratch.getWritePointer (1);

            // Encode: mid = (L+R)/2, side = (L-R)/2. Con esta normalización
            // la reconstrucción es exacta: L = mid+side, R = mid-side.
            for (int i = 0; i < numSamples; ++i)
            {
                mid[i]  = 0.5f * (left[i] + right[i]);
                side[i] = 0.5f * (left[i] - right[i]);
            }

            const int laneChannel = (channelMode == ChannelMode::Mid) ? 0 : 1;
            runLane (laneA, laneATilt2, midSideScratch, laneChannel);

            // Decode de vuelta a L/R.
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
float BandProcessor::computeMagnitudeForFrequencyDb (double sampleRate, FilterType type,
                                                      float bandFreqHz, float gainDb, float q,
                                                      float evaluateAtHz)
{
    // Todo aquí es 100% local: Coeffs::Ptr recién creados, nunca tocan
    // currentCoeffs/laneA/laneB de ninguna instancia real. Puede llamarse
    // con total seguridad desde el hilo de UI mientras el hilo de audio
    // está simultáneamente actualizando los filtros reales de la banda.
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

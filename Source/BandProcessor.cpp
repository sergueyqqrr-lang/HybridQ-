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
void BandProcessor::updateCoefficients (double sampleRate, FilterType type,
                                         float frequencyHz, float gainDb, float q)
{
    currentType = type;
    preparedSampleRate = sampleRate;

    // Clamps defensivos -- protegen contra automatización de host con
    // valores fuera de rango o Q=0 (que rompería las fórmulas RBJ).
    frequencyHz = juce::jlimit (20.0f, (float) (sampleRate * 0.49), frequencyHz);
    q           = juce::jmax (0.05f, q);

    const auto gainLinear = juce::Decibels::decibelsToGain (gainDb);

    switch (type)
    {
        case FilterType::Bell:
            currentCoeffs = Coeffs::makePeakFilter (sampleRate, frequencyHz, q, gainLinear);
            break;

        case FilterType::LowShelf:
            currentCoeffs = Coeffs::makeLowShelf (sampleRate, frequencyHz, q, gainLinear);
            break;

        case FilterType::HighShelf:
            currentCoeffs = Coeffs::makeHighShelf (sampleRate, frequencyHz, q, gainLinear);
            break;

        case FilterType::LowCut:
            // Fase 1: pendiente única de 12 dB/oct (una sola sección biquad).
            // Pendientes seleccionables (24/48 dB/oct via cascada) quedan
            // para el pulido de Fase 4/5, no son parte del criterio de
            // salida de esta fase.
            currentCoeffs = Coeffs::makeHighPass (sampleRate, frequencyHz, q);
            break;

        case FilterType::HighCut:
            currentCoeffs = Coeffs::makeLowPass (sampleRate, frequencyHz, q);
            break;

        case FilterType::Notch:
            currentCoeffs = Coeffs::makeNotch (sampleRate, frequencyHz, q);
            break;

        case FilterType::BandPass:
            currentCoeffs = Coeffs::makeBandPass (sampleRate, frequencyHz, q);
            break;

        case FilterType::TiltShelf:
        {
            // Aproximación estándar de un tilt EQ: un low-shelf y un
            // high-shelf con la misma frecuencia de esquina y ganancias
            // opuestas de magnitud gainDb/2, en cascada. El resultado
            // "inclina" el espectro alrededor de `frequencyHz` sin
            // necesitar una topología de filtro dedicada.
            const auto halfGainLinear = juce::Decibels::decibelsToGain (gainDb * 0.5f);
            const auto halfGainLinearInv = juce::Decibels::decibelsToGain (gainDb * -0.5f);

            currentCoeffs      = Coeffs::makeLowShelf  (sampleRate, frequencyHz, q, halfGainLinearInv);
            currentCoeffsTilt2 = Coeffs::makeHighShelf (sampleRate, frequencyHz, q, halfGainLinear);
            break;
        }

        default:
            jassertfalse;
            break;
    }

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
float BandProcessor::getMagnitudeForFrequencyDb (double sampleRate, float frequencyHz) const
{
    if (! isActive || currentCoeffs == nullptr)
        return 0.0f;

    frequencyHz = juce::jlimit (20.0f, (float) (sampleRate * 0.49), frequencyHz);

    auto magnitude = currentCoeffs->getMagnitudeForFrequency (frequencyHz, sampleRate);

    if (currentType == FilterType::TiltShelf && currentCoeffsTilt2 != nullptr)
        magnitude *= currentCoeffsTilt2->getMagnitudeForFrequency (frequencyHz, sampleRate);

    return juce::Decibels::gainToDecibels ((float) magnitude, -100.0f);
}

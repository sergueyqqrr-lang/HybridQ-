# HybridQ — Fase 0 (esqueleto funcional)

Este es el punto de partida del roadmap: un plugin que compila, carga en un
DAW/host, pasa audio sin artefactos, y expone **un** parámetro automatizable
(`Test Gain`) de punta a punta (host ⇄ APVTS ⇄ audio thread ⇄ UI).

No hay todavía: bandas, EQ real, Transient/Sustain, Phase Blend, ni Character.
Eso llega en las Fases 1-5. El objetivo de esta fase es exclusivamente
**validar que la fontanería del plugin es sólida** antes de meter DSP complejo.

## CI en GitHub

El repo incluye `.github/workflows/build.yml`: compila automáticamente en
Ubuntu, macOS y Windows en cada push/PR a `main`, y deja el VST3 (y el
Standalone) descargable como artifact desde la pestaña **Actions** de
GitHub — útil para probar un build sin tener que compilar en local.

Para que funcione tal cual, sube el proyecto a un repo con esta estructura
en la raíz (`CMakeLists.txt`, `Source/`, `.github/workflows/build.yml`).
No hace falta ninguna configuración adicional: JUCE se descarga solo vía
`FetchContent` la primera vez que corre el workflow (y queda cacheado
para los siguientes runs).

## Requisitos

- CMake ≥ 3.22
- Un compilador C++20 (Clang/MSVC/GCC recientes)
- macOS: Xcode command line tools instaladas
- Windows: Visual Studio 2022 (con el workload de desarrollo de escritorio C++)
- Linux: paquetes de desarrollo de JUCE (ALSA/X11/etc. — ver la documentación
  de JUCE, `docs/README.md` en el repo de JUCE, sección Linux Dependencies)

No necesitas descargar JUCE a mano: `CMakeLists.txt` usa `FetchContent` para
traerlo automáticamente desde GitHub la primera vez que configures el proyecto.
Si prefieres fijar JUCE como submódulo local (recomendado para iteración diaria,
evita re-descargar y te permite pinnear un commit exacto), comenta el bloque
`FetchContent` en `CMakeLists.txt` y usa en su lugar:

```bash
git submodule add https://github.com/juce-framework/JUCE.git JUCE
```

junto con `add_subdirectory(JUCE)`.

## Compilar

```bash
cmake -B build -S .
cmake --build build --config Release
```

Los binarios (VST3 y Standalone en esta fase; AU requiere macOS y AAX requiere
el SDK de Avid, ambos se añaden en fases posteriores) quedarán copiados
automáticamente en las carpetas estándar de plugins de tu sistema gracias a
`COPY_PLUGIN_AFTER_BUILD TRUE`.

## Validar (criterio de salida de Fase 0)

1. Carga el VST3 en tu DAW de prueba (Reaper es el más rápido para iterar).
2. Pasa audio real por el plugin: debe sonar **idéntico** a bypass.
3. Automatiza el knob "Test Gain" desde el DAW: debe responder sin clicks ni
   glitches, y el valor debe recuperarse correctamente al guardar/recargar
   el proyecto del DAW (prueba `getStateInformation`/`setStateInformation`).
4. Corre `pluginval` (https://github.com/Tracktion/pluginval) sobre el binario
   generado — debe pasar sin warnings ni crashes:
   ```bash
   pluginval --validate build/HybridQ_artefacts/Release/VST3/HybridQ.vst3
   ```

Si los 4 puntos pasan, Fase 0 está cerrada y se puede avanzar a Fase 1
(EQ paramétrico clásico — bandas reales, formas de filtro, display de
espectro) siguiendo el roadmap.

## Fase 1 — EQ paramétrico clásico (ya incluida en este código)

Añadido sobre la Fase 0:

- Hasta 24 bandas (`maxBands` en `BandModel.h`), cada una con:
  - Forma de filtro: Bell, Low/High Shelf, Low/High Cut, Notch, Band Pass, Tilt.
  - Routing de canal: Stereo, Mid, Side, Left, Right (encode/decode M-S por banda).
  - Procesamiento minimum-phase (biquad, `juce::dsp::IIR`) — Linear Phase y
    Phase Blend llegan en la Fase 2.
- Display central con grid, espectro FFT en tiempo real (2048 puntos, ventana
  Hann) y curva de EQ combinada.
- Handles arrastrables por banda (drag = freq/gain, rueda = Q, doble click =
  reset gain a 0 dB). Doble click en zona vacía del display crea una banda
  nueva en esa frecuencia/gain.
- Panel contextual de banda (freq/gain/Q/shape/channel/active/delete). Se
  dejó reservado el espacio de layout para el selector Full/Transient/Sustain
  y el slider Phase Blend de las Fases 2-3, para no tener que rehacer el
  panel cuando lleguen.
- Master gain de salida y save/load de presets básico (`.hybridq`, XML del
  estado del APVTS).

### Simplificaciones conscientes de esta fase (documentadas en el código)

- Low Cut / High Cut son de una sola sección biquad (12 dB/oct). Pendientes
  más pronunciadas (24/48 dB/oct vía cascada) quedan para el pulido de fases
  posteriores, no son parte del criterio de salida de Fase 1.
- Tilt se implementa como low-shelf + high-shelf en cascada con ganancias
  opuestas (técnica estándar), no como una topología de filtro dedicada.
- La curva de EQ del display suma en dB la respuesta de cada banda de forma
  independiente (no multiplica respuestas complejas) — aproximación visual
  estándar, no afecta al audio real.
- El acceso desde la UI a los coeficientes de cada `BandProcessor` (para
  dibujar la curva) no está protegido por un lock ni un double-buffer
  explícito; en el peor caso un frame de dibujo puede leer un coeficiente a
  medio actualizar (glitch visual de un frame, nunca de audio). Aceptable
  para MVP; si se quiere blindar del todo, la Fase 2 es buen momento para
  introducir un mecanismo de snapshot lock-free ya que de todos modos habrá
  que rediseñar esta parte para Phase Blend.

## Validar Fase 1

1. Todo lo de la Fase 0 (pluginval, pass-through, automatización, estado).
2. Doble click en el display crea una banda; arrastrar el handle cambia
   freq/gain en tiempo real y se refleja en la curva.
3. Cambiar el "Shape" de una banda a Low Cut / High Cut / Notch / Tilt y
   confirmar auditivamente que el comportamiento es coherente con la curva
   dibujada.
4. Cambiar "Channel" a Mid o Side en una banda y confirmar que solo afecta a
   esa parte de la imagen estéreo (prueba con una señal con contenido L/R
   diferenciado).
5. Guardar un preset, cerrar y volver a abrir el plugin en el DAW, cargar el
   preset: el estado de todas las bandas debe recuperarse exactamente.

## Fase 2 — Linear Phase + Phase Blend continuo (ya incluida en este código)

Añadido sobre la Fase 1:

- Nuevo parámetro `phaseBlend` por banda (0% = 100% minimum-phase/analógico,
  idéntico a la Fase 1; 100% = 100% linear-phase). Slider dedicado en el
  panel de banda con etiquetas "Analog"/"Linear" (sección 4.2 del brief).
- `BandProcessor::designBlendedFIR()` implementa el algoritmo completo de la
  sección 3.2: muestrea la magnitud exacta del biquad, reconstruye la fase
  mínima equivalente vía el método homomórfico/cepstrum (Oppenheim &
  Schafer), interpola esa fase con la fase lineal pura según `blend`, y
  reconstruye un FIR de 512 taps vía IFFT que reproduce la magnitud
  original con el comportamiento temporal interpolado.
- Codificación visual pasiva en el handle (sección 4.2): un halo alrededor
  del punto cuya opacidad crece con el blend — sin halo = 100% analógico,
  halo marcado = hacia linear-phase.
- Latencia reportada al host vía `setLatencySamples()`.

### Simplificaciones conscientes de esta fase (documentadas en el código)

- **Latencia global fija, no dinámica**: el plugin siempre reporta
  `firDesignSize / 2` (256 samples) de latencia, exista o no una banda con
  `blend > 0` en este momento, y las bandas en `blend == 0` pasan por una
  línea de delay que las realinea a esa misma latencia. Evita renegociar
  PDC con el host cada vez que se mueve un Phase Blend (bastante más
  invasivo) a cambio de "regalar" latencia cuando nadie usa Linear Phase.
  Optimizar esto a latencia adaptativa queda para un pulido posterior.
- **Rediseño del FIR en el hilo de audio**: `designBlendedFIR()` hace dos
  FFTs de 512 puntos y corre desde `processBlock` (solo cuando los
  parámetros de esa banda cambiaron desde el bloque anterior, no en cada
  bloque). No hay allocs de heap durante el rediseño (todos los buffers son
  `std::array` fijos), pero el costo de CPU en sí no es cero. Con una sola
  banda en blend > 0 siendo arrastrada es inaudible en hardware moderno;
  con muchas bandas a la vez podría notarse. Mover esto a un hilo de
  background con doble buffer (como indica la sección 3.2 del roadmap)
  queda para el pulido de Fase 5.
- Todavía no hay oversampling en este camino -- si en la práctica se
  escuchan artefactos de aliasing con blends intermedios en frecuencias
  altas, seria el primer punto a revisar.

## Validar Fase 2

1. Con una banda en `blend = 0%`, confirmar que suena y se comporta
   exactamente igual que en Fase 1 (biquad puro).
2. Subir `blend` a 100% en una banda con boost pronunciado (Q alto, gain
   alto): la curva de magnitud en el display NO debe cambiar de forma al
   mover el blend -- solo cambia el comportamiento temporal (fase). Si la
   curva se deforma al mover blend, hay un bug en el muestreo de magnitud.
3. Confirmar que el DAW reporta/compensa la latencia del plugin (debería
   verse como ~256 samples a la sample rate del proyecto, o el valor
   correspondiente de `firDesignSize/2`).
4. Probar valores intermedios de blend (25%, 50%, 75%) y escuchar que la
   transición entre carácter analógico y transparencia quirúrgica es
   gradual, sin clicks ni artefactos al mover el slider.
5. Automatizar `phaseBlend` desde el DAW mientras suena audio: no debe
   haber clicks notorios en la transición (el rediseño del FIR se dispara
   en cada cambio detectado, bloque a bloque).

## Fase 3 — Transient/Sustain Split (ya incluida en este código)

Añadido sobre la Fase 2:

- `TransientSustainSplitter` (sección 3.1, Opción A del brief): dos envelope
  followers por canal (rápido/lento) sobre la señal rectificada, cuya
  diferencia (Transient Detection Function) se convierte en una máscara
  suavizada `g[n] ∈ [0,1]`. Split aditivo puro: `transient[n] = in[n]*g[n]`,
  `sustain[n] = in[n]*(1-g[n])` -- `transient + sustain == input` siempre,
  sin pérdida de energía.
- Nuevo parámetro `splitMode` por banda (Full / Transient / Sustain).
  Selector de 3 botones con radio-group nativo en el panel de banda, en el
  espacio que se había dejado reservado desde la Fase 1.
- `BandProcessor` ahora mantiene DOS cadenas de filtro completas e
  independientes (`chainA`/`chainB`) en vez de una sola, porque Transient y
  Sustain nunca pueden compartir la memoria interna de un mismo filtro (ver
  comentario extenso en `BandProcessor.h`). Una banda en modo Full, cuando
  el split está activo en el plugin, se aplica de forma independiente a
  `transientBuffer` y a `sustainBuffer` (vía `chainA`/`chainB`
  respectivamente) -- esto es matemáticamente EXACTO (no una aproximación)
  gracias a la linealidad del filtro: `filtro(T) + filtro(S) ==
  filtro(T+S)`. Evita necesitar un tercer buffer "full" por separado.
- Optimización: el split (y el procesamiento duplicado que conlleva) solo
  se activa si AL MENOS una banda activa del plugin está en modo Transient o
  Sustain. Con todas las bandas en Full, el costo es idéntico a Fase 2.
- Codificación visual pasiva en el handle (sección 4.2): anilla partida --
  arco superior naranja = Transient, arco inferior azul = Sustain.

### Simplificaciones conscientes de esta fase (documentadas en el código)

- **Detección de banda completa, no multibanda**: el brief (sección 3.1)
  recomienda dividir la señal en 3-4 sub-bandas antes de detectar
  transientes, para no confundir un golpe de bombo grave con un hi-hat
  agudo. Esta versión detecta en banda completa (un único par de envelope
  followers). Más simple y barato; en mezclas completas muy densas la
  separación será menos precisa. Es el candidato más claro de pulido
  posterior a este MVP.
- **Coeficientes de ataque/release sin afinar por oído**: los valores
  (0.5ms/15ms rápido, 15ms/150ms lento, sensibilidad=6.0) son puntos de
  partida razonables según la teoría, pero no han sido validados escuchando
  material real -- esto solo se puede hacer con el plugin compilado y
  sonando.
- **Sin linking L/R en la detección**: cada canal detecta transientes de
  forma independiente. Para la mayoría del material esto es inaudible, pero
  en mezclas con paneo extremo podría percibirse una separación ligeramente
  distinta entre L y R.
- **Pequeño artefacto al cambiar splitMode en vivo**: cuando una banda deja
  de usar una chain (p. ej. pasa de Full a solo Transient), la chain que
  deja de usarse congela su memoria interna en vez de resetearse. Si más
  tarde se vuelve a usar, puede haber un transitorio muy breve mientras el
  filtro se re-asienta. No afecta el uso normal (cambiar de modo no es algo
  que se haga constantemente), pero es un detalle a pulir más adelante.

## Validar Fase 3

1. Con todas las bandas en Full, confirmar que el plugin suena y se
   comporta exactamente igual que en Fase 2 (optimización de "sin split").
2. Poner una banda en modo Sustain, cortar una resonancia: el ataque de la
   nota debe quedar intacto, solo se afecta el cuerpo/cola. Esta es la
   prueba de fuego del producto (sección 1.2 del brief).
3. Poner una banda en modo Transient con boost de presencia: debe
   escucharse más brillo en los ataques sin "embarrar" el sustain.
4. Confirmar visualmente que el handle muestra el arco naranja (Transient)
   o azul (Sustain) correctamente, y que el selector de 3 botones del panel
   refleja el modo real de la banda seleccionada.
5. Probar con batería real, voces, y una mezcla completa -- el brief pide
   explícitamente validar que la separación "suena natural, no artificial"
   en estos tres tipos de material.

## Siguiente paso sugerido

Fase 4 del roadmap: Character analógico (Console/Tape/Tube, waveshaping +
oversampling condicional) y Dynamic EQ por banda (sección 2.3 y "resto de
funcionalidades esperadas" de la sección 2.4).

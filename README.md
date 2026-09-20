# PengPlayer

PengPlayer es un reproductor de musica nativo y personalizable para PlayStation Vita, desarrollado con C++ y VitaSDK.

## Estado actual: 0.2.3

La version 0.2 introduce la primera reproduccion real de audio mediante `libsndfile` + `SceAudioOut`.

### 0.2.3 - estabilidad de audio

- Buffer de salida aumentado a 4096 frames para dar mas margen al decodificador.
- Hilo de audio con prioridad superior al hilo generico de la interfaz.
- Clipping seguro al convertir muestras a PCM signed 16-bit.
- Escalado correcto para WAV/AIFF de punto flotante.
- ALC del puerto BGM desactivado explicitamente para evitar procesamiento dinamico durante la reproduccion.

### Funciones

- Navegacion por `ux0:/music/` y subcarpetas.
- Deteccion de archivos de audio.
- Reproduccion en streaming, sin cargar la cancion completa en RAM.
- Puerto `SCE_AUDIO_OUT_PORT_TYPE_BGM` preparado para la futura reproduccion en segundo plano.
- MP3, FLAC y WAV mediante libsndfile (ademas de otros formatos que libsndfile pueda abrir).
- Play/Pause.
- Seek de -5/+5 segundos.
- Cancion anterior/siguiente dentro de la carpeta actual.
- Tiempo actual y duracion.
- Frecuencia de muestreo y canales.
- Orden A-Z / Z-A.

### Limitaciones de 0.2

- Solo mono y estereo.
- La salida directa admite las frecuencias compatibles con `SceAudioOut` hasta 48 kHz.
- Archivos de 88.2/96/192 kHz requieren resampling, que se agregara en una version posterior.
- Todavia no hay metadata, caratulas, biblioteca SQLite ni visualizadores.

## Controles

| Control | Accion |
|---|---|
| Arriba / Abajo | Navegar |
| X | Abrir carpeta / reproducir; sobre la cancion activa alterna pausa |
| Cuadrado | Play / Pause |
| Izquierda / Derecha | -5 / +5 segundos |
| L / R | Cancion anterior / siguiente |
| Circulo | Carpeta anterior |
| Triangulo | A-Z / Z-A |
| START | Salir |

## Dependencias

```bash
vdpm install libvita2d libsndfile lame mpg123 opus libvorbis libogg flac
```

En las versiones actuales de VitaSDK, `vdpm` resuelve automaticamente las dependencias de los paquetes.

## Compilacion

```bash
rm -rf build
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build build -j4
```

El VPK queda en `build/PengPlayer.vpk`.


## 0.2.3

- Reduce el bloque de AudioOut a 2048 frames para disminuir la latencia de inicio y seek.
- Mantiene clipping seguro y ALC desactivado de 0.2.3.


## Correccion 0.2.3

- Elimina el escaneo completo del archivo provocado por `SFC_SET_SCALE_FLOAT_INT_READ`.
- Mantiene el buffer de 2048 frames, ALC desactivado y el hilo de audio prioritario.
- WAV/AIFF float se convierten a PCM16 manualmente con clipping seguro, sin analizar toda la cancion antes de reproducir.
- Objetivo: inicio casi inmediato sin recuperar los clicks/pops de la 0.2.

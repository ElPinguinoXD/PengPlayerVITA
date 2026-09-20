# PengPlayer

PengPlayer es un reproductor de musica nativo y personalizable para PlayStation Vita, desarrollado en C++ con VitaSDK y libvita2d.

## Estado actual - v0.3

La version 0.3 mantiene el motor de audio estable de 0.2 y agrega la primera experiencia visual de reproduccion.

### Biblioteca

- Explora `ux0:/music/` y sus subcarpetas.
- Detecta MP3, FLAC, WAV, OGG, Opus y AIFF.
- Orden A-Z / Z-A.
- Navegacion por carpetas.
- Mini reproductor persistente.

### Reproduccion

- MP3 / FLAC / WAV / OGG / Opus / AIFF mediante libsndfile.
- Salida por `SCE_AUDIO_OUT_PORT_TYPE_BGM`.
- Play / pausa.
- Seek +/- 5 segundos.
- Cancion anterior / siguiente.
- Reproduccion continua: al terminar una cancion avanza automaticamente a la siguiente del mismo contexto de reproduccion.
- Inicio de reproduccion inmediato.
- Buffer de 2048 frames y reproduccion en hilo independiente.
- Clipping seguro y ALC desactivado.

### Nuevo en 0.3

- Lectura de titulo, artista, album, genero, fecha y numero de pista cuando el formato lo expone.
- Fallback ID3v2 para metadata MP3 comun.
- Informacion tecnica: formato, frecuencia, canales, bit depth y bitrate aproximado.
- Caratulas embebidas en MP3 (APIC) y FLAC (PICTURE).
- Fallback automatico a `cover.jpg/png`, `folder.jpg/png`, `front.jpg/png` o `album.jpg/png` dentro de la carpeta.
- Mini caratula en el reproductor inferior.
- Nueva pantalla **Ahora suena** con caratula grande, metadata y progreso.
- `SELECT` alterna Biblioteca / Ahora suena.
- La cola temporal de la carpeta se conserva aunque el usuario navegue por otras carpetas.

## Controles

### Biblioteca

- `Arriba / Abajo`: navegar.
- `X`: entrar en carpeta / reproducir / pausar la cancion activa.
- `O`: volver a la carpeta anterior.
- `Triangulo`: alternar A-Z / Z-A.
- `Cuadrado`: pausa / continuar.
- `Izquierda / Derecha`: -5 / +5 segundos.
- `L / R`: cancion anterior / siguiente.
- `SELECT`: abrir **Ahora suena**.
- `START`: salir.

### Ahora suena

- `X` o `Cuadrado`: pausa / continuar.
- `Izquierda / Derecha`: -5 / +5 segundos.
- `L / R`: cancion anterior / siguiente.
- `O` o `SELECT`: volver a Biblioteca.
- `START`: salir.

## Compilacion

```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"

cmake --build build -j4
```

El VPK resultante se genera en `build/PengPlayer.vpk`.

## Roadmap

- 0.4: biblioteca indexada y categorias (Canciones / Artistas / Albumes / Generos / Carpetas).
- 0.5: selector de color y temas.
- 0.6: visualizadores FFT / waveform.
- 0.7: playlists, cola, shuffle y repeat.
- 0.8: caratula personalizada elegida por el usuario desde un selector de imagen, guardada por cancion sin modificar el archivo original.
- 0.9: background robusto en LiveArea.
- 1.0: interfaz estable y experiencia completa.
- 1.1+: plugin para reproduccion durante juegos.

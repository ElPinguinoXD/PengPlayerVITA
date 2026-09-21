# PengPlayer 0.5

Reproductor de musica nativo y personalizable para PlayStation Vita, construido con VitaSDK, C++ y libvita2d.

## Novedades de la version 0.5

### Color HUE libre

La apariencia ya no se limita a una lista de colores predeterminados. En:

`START -> Ajustes -> Apariencia`

se muestra una barra HUE con todo el espectro de color.

Controles:

- `Izquierda / Derecha`: cambia 1 grado de HUE para ajuste fino.
- `L / R`: cambia 10 grados para recorrer el espectro rapidamente.
- `X`: guarda el color elegido.
- `O`: cancela los cambios no guardados.

El color se previsualiza en tiempo real y se guarda en:

`ux0:data/PengPlayer/preferences.cfg`

La configuracion anterior basada en presets se migra automaticamente al nuevo sistema HUE.

### Caratulas personalizadas

Desde `Ahora suena`, pulsa `TRIANGULO` y selecciona `Cambiar caratula` para elegir una imagen JPG, JPEG o PNG desde `ux0:`, `uma0:` o `imc0:`.

PengPlayer guarda la asociacion sin modificar el archivo de audio original:

`ux0:data/PengPlayer/custom_covers.dat`

Tambien puedes restaurar la caratula original desde el mismo menu.

### Crop cuadrado centrado

Todas las caratulas se muestran ahora con un recorte cuadrado centrado. Las imagenes horizontales o verticales llenan por completo el cuadro sin deformarse ni dejar bandas vacias. El archivo de imagen original no se modifica: el crop se realiza solamente al renderizar.

La prioridad de caratulas es:

1. Caratula personalizada.
2. Caratula embebida en el archivo.
3. `cover.jpg`, `folder.jpg`, `front.jpg`, `album.jpg` y equivalentes PNG/JPEG.
4. Placeholder de PengPlayer.

## Funciones heredadas

- Biblioteca: Canciones, Artistas, Albumes y Carpetas.
- Multiples rutas de musica configurables.
- Biblioteca persistente.
- Metadata y caratulas embebidas.
- Pantalla Ahora suena.
- Reproduccion automatica de la siguiente cancion.
- MP3, FLAC, WAV, OGG, OPUS y AIFF.
- Play, pausa, seek y anterior/siguiente.
- Audio estable y de inicio inmediato mediante SceAudioOut BGM.

## Controles principales

- `X`: seleccionar / reproducir / confirmar.
- `O`: volver.
- `TRIANGULO`: cambiar seccion o abrir opciones segun la pantalla.
- `SELECT`: Biblioteca / Ahora suena.
- `START`: Ajustes.
- `CUADRADO`: pausa / continuar.
- `Izquierda / Derecha`: seek de -5 / +5 segundos durante la reproduccion.
- `L / R`: anterior / siguiente durante la reproduccion.

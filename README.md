# PengPlayer 0.8

Reproductor de musica nativo y personalizable para PlayStation Vita, desarrollado con VitaSDK.

## Novedades de 0.8

### Biblioteca inteligente

La Biblioteca ahora se recorre con `△` entre:

`Canciones → Artistas → Albumes → Carpetas → Playlists → Favoritos → Categorias → Recientes → Mas reproducidas → Buscar`

- **Favoritos:** marca o desmarca la cancion actual desde `Ahora suena > △ > Opciones de cancion`.
- **Categorias:** crea categorias con nombres personalizados como `Para manejar`, `Noche`, `Acusticas` o cualquier otra.
- Dentro de una categoria, `△` abre `Anadir canciones`, `□` quita una cancion con confirmacion y `X` reproduce.
- **Recientes:** conserva las ultimas 50 canciones reproducidas.
- **Mas reproducidas:** cuenta las reproducciones y ordena las canciones por uso.
- **Buscar:** usa el teclado nativo de PS Vita para buscar por titulo, artista, album, genero o ruta.

Los datos inteligentes se guardan en:

`ux0:data/PengPlayer/smart_library.dat`

### Restauracion de sesion

PengPlayer guarda periodicamente:

- Cancion actual.
- Posicion aproximada.
- Cola activa.
- Elemento actual de la cola.

Al volver a abrir PengPlayer, la sesion se restaura **en pausa** para evitar que la musica empiece a sonar por sorpresa. El archivo se guarda en:

`ux0:data/PengPlayer/session.dat`

### Escala de interfaz

`START > Ajustes > Apariencia` ahora contiene dos ajustes:

- **Color HUE** libre de 0 a 359 grados.
- **Escala de interfaz** entre 80% y 120%.

En Apariencia:

- `↑/↓`: elegir Color o Escala.
- En Color: `←/→` cambia 1 grado y `L/R` cambia 10 grados.
- En Escala: `←/→` cambia 5% y `L/R` cambia 10%.
- `X`: guardar.
- `O`: cancelar cambios no guardados.

La escala afecta los textos y elementos tipograficos de toda la aplicacion y se guarda en `preferences.cfg`.

## Funciones conservadas

PengPlayer 0.8 conserva todo lo implementado anteriormente: audio estable mediante SceAudioOut BGM, MP3/FLAC/WAV/OGG/Opus/AIFF, metadata, caratulas embebidas y personalizadas, crop cuadrado, visualizadores, rutas de musica configurables, playlists, cola editable, shuffle, repeticion, auto-siguiente y confirmaciones de eliminacion.

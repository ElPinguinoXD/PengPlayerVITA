# PengPlayer 0.7

Reproductor de musica nativo y personalizable para PlayStation Vita, desarrollado con VitaSDK.

## Novedades de 0.7

- Cola de reproduccion visible y editable.
- Reproduccion aleatoria (shuffle) persistente.
- Modos de repeticion: desactivado, repetir toda la cola y repetir una cancion.
- Playlists persistentes guardadas en `ux0:data/PengPlayer/playlists.dat`.
- Crear playlists directamente desde la Vita usando el teclado nativo para elegir el nombre.
- Anadir la cancion actual a una playlist desde `Ahora suena > Triangulo`.
- Reproducir una playlist completa y usar auto-siguiente dentro de ella.
- Eliminar canciones de una playlist sin borrar el archivo original.
- Reordenar elementos de la cola con L/R.
- Eliminar elementos de la cola con Cuadrado (excepto la cancion que esta sonando).
- Los estados Aleatorio y Repetir se guardan en `preferences.cfg`.

## Controles nuevos

### Ahora suena
- `Cuadrado`: activar/desactivar aleatorio.
- `START`: cambiar entre Repetir Off / Todo / Una.
- `Triangulo > Ver cola`: abrir la cola actual.
- `Triangulo > Anadir a playlist`: guardar la cancion actual en una playlist.

### Cola
- `Arriba/Abajo`: seleccionar.
- `X`: reproducir el elemento seleccionado.
- `Cuadrado`: retirar de la cola.
- `L/R`: mover el elemento seleccionado.
- `O`: volver a Ahora suena.

### Playlists
- `START > Ajustes > Playlists`.
- `X`: abrir una playlist o crear una nueva.
- Al crear una playlist se abre el teclado nativo de PS Vita para escribir el nombre.
- Si el nombre ya existe, PengPlayer crea una variante unica como `Mi Playlist (2)`.
- `Cuadrado`: eliminar playlist.
- `SELECT`: abrir `Ahora suena` sin salir del apartado Playlists.
- Dentro de una playlist, `X` reproduce y `Cuadrado` retira la cancion de esa playlist.
- Dentro de una playlist, `SELECT` tambien abre `Ahora suena` y `O` regresa a la playlist.

## Funciones anteriores

PengPlayer conserva biblioteca por canciones/artistas/albumes/carpetas, rutas configurables, metadata, caratulas embebidas y personalizadas, crop cuadrado centrado, selector HUE, visualizadores, reproduccion automatica, seek y audio estable mediante SceAudioOut BGM.

### Playlists dentro de la Biblioteca

La Biblioteca ahora tiene cinco secciones al pulsar `△`:

`Canciones → Artistas → Álbumes → Carpetas → Playlists`

Desde `Playlists` puedes abrir una lista con `X`, crear una nueva usando el teclado nativo y eliminar una lista con `□`. Entrar a una playlist desde esta pestaña y pulsar `O` vuelve directamente a la Biblioteca.

## Ajustes finales de la 0.7

- `SELECT` funciona correctamente desde la lista de playlists y desde el interior de una playlist para abrir `Ahora suena` cuando hay una pista activa.
- `START` abre Ajustes desde Playlists, desde una playlist y desde la pantalla de añadir canciones.
- Dentro de una playlist, `△` abre `Añadir canciones`.
- La pantalla `Añadir canciones` muestra toda la biblioteca; `X` añade la pista seleccionada y `[OK]` indica las que ya pertenecen a esa playlist.
- `O` vuelve a la playlist sin detener la reproducción.

## Confirmaciones de eliminacion

Para evitar borrados accidentales, PengPlayer pide confirmacion antes de:

- Eliminar una playlist.
- Quitar una cancion de una playlist.
- Quitar una ruta de musica de la biblioteca.

La opcion segura `No` aparece seleccionada por defecto. Con `Izquierda/Derecha` se cambia entre `No` y `Si, eliminar`, `X` confirma y `O` cancela. Quitar una cancion de una playlist o una ruta de musica no borra los archivos originales.

# hex_editor
Hex editor for my OS class 

NavVis — Navegador y Visor de Archivos
Práctica de Sistemas Operativos: utilería TUI para navegar directorios y ver archivos en modo texto o hexadecimal.
Compilación
bashgcc -o navvis navvis.c -lncurses
Requiere la biblioteca ncurses:
bash# Ubuntu / Debian
sudo apt install libncurses-dev
Uso
bash./navvis [directorio]   # abre el navegador en el directorio dado (default: .)
./navvis archivo.txt    # abre directamente el visor de texto

Navegador de archivos
Muestra nombre, tamaño y fecha de modificación de cada entrada.
Los directorios aparecen primero, con prefijo /.
TeclaAcción↑ / ↓Moverse por la listaPgUp / PgDnPágina anterior / siguienteHome / EndPrimera / última entradaEnter / →Entrar al directorio o abrir en visor texto← / BackspaceSubir al directorio padreF2Abrir archivo seleccionado en visor textoF3Abrir archivo seleccionado en visor hexadecimalq / F10 / EscSalir

Visor de Texto
Muestra el archivo línea a línea usando mmap (soporta archivos tan grandes como permita la memoria virtual).
TeclaAcción↑ / ↓Línea anterior / siguientePgUp / PgDnPágina anterior / siguienteHome / gInicio del archivoEnd / GFinal del archivoF2 / wAlternar ajuste de línea (wrap)F4 / hCambiar a vista hexadecimalF5 / :Ir a número de líneaq / F10 / EscRegresar al navegador

Visor Hexadecimal
Muestra el archivo en formato offset  XX XX XX XX ...  ASCII, igual que el ejemplo de la práctica.
TeclaAcción↑ / ↓Fila anterior / siguientePgUp / PgDnPágina anterior / siguienteHome / gInicio del archivoEnd / GFinal del archivoF5 / :Ir a offset hexadecimal (p.ej. 1A2F)F2 / tCambiar a vista textoq / F10 / EscRegresar al navegador

Implementación

mmap — el archivo completo se mapea a memoria virtual; el SO maneja la paginación por bloques automáticamente, lo que permite manejar archivos arbitrariamente grandes.
Índice de líneas — el visor de texto construye un arreglo de offsets de inicio de línea para navegación O(1).
ncurses — toda la interfaz TUI usa colores, atributos y teclas especiales via ncurses.

# hex_editor
Hex editor for my OS class 
NavVis - Navegador y Visor de Archivos
# CLASE -> Sistemas Operativos

  Compilar: gcc -o navvis navvis.c -lncurses       -> en referencia a lncursers
  Uso: ./navvis [dir name any]
 
  CONTROLES :
  * Flechas Arriba/Abajo  — seleccionar archivo
  * Enter                 — entrar a directorio / abrir visor
  * Flecha Izquierda / u  — subir directorio padre
  * q / ESC               — salir

  CONTROLES VISOR (texto y hex):
  * Flechas Arriba/Abajo  — línea anterior / siguiente
  * PgUp / PgDn           — página anterior / siguiente
  * < / Home              — inicio del archivo
  * > / End               — final del archivo
  * g                     — ir a línea/offset específico
  * t / h                 — alternar entre vista Texto y Hex
  * q / ESC               — volver al navegador

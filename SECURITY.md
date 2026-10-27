# Seguridad

## Garantías

1. **Código abierto completo:** el `.exe` se compila solo a partir de `src/` con `build.ps1`, y la compilación es reproducible (mismo código, mismo SHA256).
2. **Sin red ni telemetría.**
3. **Sin administrador:** manifiesto `asInvoker`. Solo escribe en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (si activas el inicio con Windows) y en `%APPDATA%\TranslucentBar\`.
4. **Puente en Explorer acotado:** la DLL que carga la API de diagnóstico XAML solo cambia los pinceles del fondo de la barra y los restaura al salir.

## Verificar una descarga

- Descarga solo desde las [Releases](../../releases) de este repositorio. Cualquier copia de otra web, empaquetada con instalador o en un `.zip` con contraseña **no es oficial**.
- Compara el hash: `Get-FileHash .\TranslucentBar.exe -Algorithm SHA256`.
- O compílalo tú (`.\build.ps1`) y compara.

## Informar de un problema

Usa **Security → Report a vulnerability** (aviso privado) en GitHub, o abre un *issue* si no es sensible.

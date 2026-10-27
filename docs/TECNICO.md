# TranslucentBar · documentación técnica

Barra de tareas translúcida o con cristal desenfocado para Windows 11. C++20 y Win32, sin .NET, Electron, Qt, instalador, administrador, red ni telemetría. Código original de zevidaun, bajo **GPL-3.0-or-later**.

**Estado: prototipo funcional, con validación local en Windows 11 26H2, compilación 26300.9711, dos pantallas.** Está dirigido a la barra XAML de 24H2/25H2, pero esas dos versiones necesitan pruebas en sus propias máquinas. El objetivo de menos de 5 MB de working set **todavía no se cumple**. No se presenta esta versión como una entrega que haya satisfecho todos los objetivos.

## Uso

Ejecuta `out\TranslucentBar.exe`. El icono azul de la bandeja permite elegir:

- **Translúcido:** fondo transparente con tinte RGB y opacidad de 0 a 100 %.
- **Glass:** pincel acrílico de Windows, con desenfoque y el mismo tinte.
- **Normal:** recupera los pinceles originales de Windows.
- **Color del tinte…:** selector nativo de color.
- **Opacidad del tinte:** pasos de 5 % desde el menú; el JSON admite cualquier entero de 0 a 100.
- **Iniciar con Windows:** valor `TranslucentBar` en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, con la ruta del EXE entre comillas. No modifica HKLM ni pide elevación. Inicialmente desactivado.
- **Estado y diagnóstico…:** conexión XAML, fondos aplicados y último HRESULT.
- **Salir y restaurar la barra:** restaura antes de terminar.

El icono puede quedar dentro del desplegable de iconos ocultos de Windows. Los iconos, botones, reloj e interacción de la barra siguen siendo los de Explorer. El tinte elegido se mantiene al cambiar de tema; Windows sigue controlando el tema de los iconos.

También se puede controlar una instancia existente:

```powershell
.\out\TranslucentBar.exe --modo translucido
.\out\TranslucentBar.exe --modo glass
.\out\TranslucentBar.exe --modo normal
.\out\TranslucentBar.exe --menu
.\out\TranslucentBar.exe --estado
.\out\TranslucentBar.exe --salir
```

`--modo` también inicia la aplicación si está cerrada. En PowerShell, un ejecutable gráfico puede devolver el control antes de que termine el comando; para automatización usa `Start-Process ... -Wait` y espera después el evento de aplicación del efecto. `--attach` es un comando interno y transitorio.

## Aspecto comprobado

Las capturas de prueba muestran franjas de color situadas detrás de la barra real, para distinguir transparencia de desenfoque. No son recreaciones de la interfaz.

**Translúcido:** franjas nítidas, con tinte oscuro al 25 %.

![Barra real en modo Translúcido (recorte sin iconos)](real-translucido.png)

**Glass:** las mismas franjas, desenfocadas por el compositor.

![Barra real en modo Glass (recorte sin iconos)](real-glass.png)

**Normal:** material original de Windows restaurado.

![Barra real restaurada (recorte sin iconos)](real-normal.png)

## Compilar en un comando

Herramientas: PowerShell 5.1 o posterior y **LLVM-MinGW UCRT x64**. No se necesita Windows SDK separado, Visual Studio, CMake ni paquetes de C++ adicionales.

Instalación sin administrador, una sola vez, desde el paquete portable de WinGet:

```powershell
winget install --id MartinStorsjo.LLVM-MinGW.UCRT --exact --scope user --accept-package-agreements --accept-source-agreements
```

Desde la raíz del repositorio:

```powershell
.\build.ps1
```

Resultado: **`out\TranslucentBar.exe`**, un único archivo distribuible. El script busca la instalación de WinGet sin exigir reiniciar la consola. Con otra ubicación: `.\build.ps1 -Toolchain 'C:\herramientas\llvm-mingw\bin'`. Los scripts llevan UTF-8 con BOM para PowerShell 5.1. Si la política local impide ejecutar scripts, debe aplicarse la política que autorice el usuario; el proyecto no la cambia.

La compilación usa C++20, optimización de tamaño, LTO, bibliotecas C++ estáticas, avisos tratados como errores, manifiesto `asInvoker` y DPI `PerMonitorV2`. Las DLL de Windows y UCRT son componentes del sistema; no hay que instalar un runtime adicional. El EXE mide aproximadamente **433 KiB**. La versión de la herramienta usada aquí es LLVM-MinGW `23.1.4-20261020`.

## Cómo funciona y por qué

La barra moderna contiene un árbol XAML, además de las ventanas `Shell_TrayWnd` y `Shell_SecondaryTrayWnd`. Cambiar el backdrop de la ventana anfitriona no controla necesariamente lo que pinta ese árbol.

Se evaluaron estas rutas:

| Ruta | Decisión |
| --- | --- |
| `SetWindowCompositionAttribute`, `ACCENT_ENABLE_TRANSPARENTGRADIENT` y `ACCENT_ENABLE_ACRYLICBLURBEHIND` | No se usan como solución de la barra XAML. El atributo del HWND no garantiza modificar su relleno XAML; Microsoft desaconseja la API y no publica cabecera ni biblioteca de importación para ella. |
| `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)` | API documentada para el material de una ventana. No sustituye los pinceles que pinta XAML, por lo que tampoco se usa como solución completa de la barra. |
| `InitializeXamlDiagnosticsEx` y el servicio del árbol visual | Ruta elegida. La API de diagnóstico es pública; la estructura y nombres de los elementos internos de Explorer no son un contrato de compatibilidad de Microsoft. |

Un proceso auxiliar del mismo EXE carga `Windows.UI.Xaml.dll`, conecta con los endpoints XAML disponibles del proceso dueño de la barra y termina. Explorer puede tener varios núcleos XAML: detenerse en el primer endpoint válido no siempre alcanza las barras. Así, el controlador residente no carga todo XAML. El puente COM original se carga en Explorer, mantiene una suscripción por sesión diagnóstica y localiza los rectángulos `BackgroundFill` y `BackgroundStroke`. Se conservan los valores locales y los pinceles efectivos originales. El modo Translúcido coloca un `SolidColorBrush`; Glass coloca un `Windows.UI.Xaml.Media.AcrylicBrush`. No se modifica ningún archivo del sistema ni se parchea código de Explorer.

Glass utiliza `BackgroundSource=Backdrop`: la ventana de la barra ya permite componer el contenido situado detrás. En la prueba local, `HostBackdrop` se convertía en un color sólido cuando la barra no estaba activa. La opacidad de Glass es `TintOpacity`, no una garantía de transmisión lineal: el material de Windows añade sus propias capas y ruido. Windows puede reducir o desactivar el acrílico por accesibilidad, ahorro de energía o limitaciones gráficas.

Los índices de las propiedades se obtienen de `GetPropertyValuesChain`. En esta compilación de Explorer, `IVisualTreeService2::GetPropertyIndex` devolvía `E_INVALIDARG` incluso para `Color`; la cadena documentada sí funciona. Los contratos COM usados provienen de las cabeceras de MinGW. Se declara únicamente el primer miembro necesario del ABI público `IShapeStatics`; no hay código de TranslucentTB ni de otras utilidades en este proyecto.

### Un EXE y una DLL extraída

El archivo distribuible es un solo EXE, pero la API de diagnóstico exige una DLL que Explorer pueda cargar. Por eso se incrusta el puente y se extrae, verificando sus bytes, como `%APPDATA%\TranslucentBar\puente-<hash>.dll`. **No es una ejecución que permanezca exclusivamente en un único archivo en disco.** La DLL ocupa aproximadamente 193 KiB y solo usa bibliotecas estáticas y componentes de Windows.

Al salir, se restauran los pinceles y se cierran las esperas del proceso. La DLL y la suscripción diagnóstica quedan inactivas en Explorer hasta que éste se reinicie. Se reutilizan al volver a abrir la app; no se inyecta otra copia por cada ajuste. No se fuerza la descarga de código que Explorer puede estar ejecutando. Una actualización del puente requiere reiniciar Explorer para usar la nueva versión. Pueden quedar DLL antiguas en la carpeta; se pueden borrar después de cerrar la app y reiniciar Explorer.

## Eventos y reposo

- El controlador espera en `GetMessage`; no hay `SetTimer`, temporizadores periódicos, sondeo ni hilos en bucle de comprobación.
- `SetWinEventHook`, fuera de proceso, detecta creación, aparición y cambios de posición de las barras. Se filtran ventanas ajenas y se agrupan solicitudes mediante un mensaje pendiente.
- `TaskbarCreated` recupera el icono, descubre los nuevos HWND/PID y conecta de nuevo tras reiniciar Explorer.
- `WM_DISPLAYCHANGE`, `WM_DPICHANGED`, `WM_SETTINGCHANGE`, `WM_THEMECHANGED` y `WM_DWMCOMPOSITIONCHANGED` provocan una nueva aplicación.
- El puente recibe las mutaciones del árbol XAML y registra `RegisterPropertyChangedCallback` para `Fill`. Si Explorer sustituye el pincel, guarda el nuevo original y solicita reaplicar. Hay protección contra reentrada y se reutiliza el pincel mientras no cambie la configuración.
- Una espera del sistema con `RegisterWaitForSingleObject(INFINITE)` detecta la muerte del controlador y envía un mensaje al hilo XAML para restaurar, incluso después de `Stop-Process`. Esa espera no despierta periódicamente.
- La búsqueda de endpoints de diagnóstico es finita: hasta 64 nombres, solamente al conectar con una barra nueva. No es un bucle de reposo.

El sondeo y las pausas de los scripts de **prueba externos** sirven para esperar capturas y medir; no forman parte de la app.

## Configuración

Archivo: `%APPDATA%\TranslucentBar\config.json`, normalmente de unos 80 bytes:

```json
{
  "modo": "translucido",
  "color": "#172535",
  "opacidad": 25
}
```

Modos aceptados: `normal`, `translucido`, `glass`. Color: `#RRGGBB`. Opacidad: entero entre 0 y 100. Se admiten las tres claves en cualquier orden y un BOM UTF-8 opcional. No se admiten comentarios, claves adicionales o duplicadas, números decimales ni archivos de más de 1024 bytes. Un JSON inválido se conserva y se avisa; el siguiente cambio desde el menú lo sustituye con valores válidos. Los guardados usan un temporal y sustitución del archivo. Para editar a mano, cierra la aplicación, modifica el JSON y vuelve a abrirla; no hay vigilancia del archivo en reposo.

Si activas el inicio con Windows, conserva el EXE en una ruta estable. Después de moverlo, desactiva y vuelve a activar la opción. Para desinstalar: desactiva el inicio, sal de la app, borra el EXE y, después de reiniciar Explorer, elimina su carpeta de configuración si no quieres conservarla.

## Consumo medido

Mediciones reales con `Get-Process`, sin vaciar ni recortar artificialmente el working set. Los resultados completos y la versión de Windows están en [mediciones.json](mediciones.json). La CPU se calcula con la diferencia de segundos de CPU durante el intervalo; una lectura aislada de `CPU` es tiempo acumulado y no un porcentaje.

Medición del 27 de octubre de 2026, Windows 11 **26H2 26300.9711**, modo Translúcido, dos barras, 32 procesadores lógicos y **30,040 s** sin interacción:

| Indicador del proceso residente | Medido | Objetivo | Resultado |
| --- | ---: | ---: | --- |
| Tiempo de CPU añadido | 0,000 s | 0 s en reposo | Cumplido en esta muestra |
| CPU media de un núcleo / del equipo | 0 % / 0 % | 0 % | Cumplido en esta muestra |
| Working set inicial → final | 10,53 → **10,48 MiB** (10.993.664 bytes al final) | Menos de unos 5 MB | **Pendiente** |
| Memoria privada inicial → final | 1,74 → 1,66 MiB | Informativo | No sustituye al working set |

Un intervalo sin tiempo de CPU contabilizado no garantiza un cero matemático permanente: los eventos de Windows y los cambios del usuario sí requieren trabajo. No hay medición de GPU ni comparación experimental con otras utilidades.

El objetivo de 5 MB se refiere al working set, que incluye páginas compartidas de las DLL de Windows. La memoria privada no es equivalente a ese indicador. Además, el puente, la infraestructura diagnóstica y el material gráfico tienen coste dentro de **Explorer/DWM**, fuera del proceso `TranslucentBar`; las cifras del controlador no representan todo el coste del efecto. No se ha aislado ese incremento en esta máquina ni se afirma que sea menor que el de otras herramientas. El objetivo de memoria sigue pendiente.

Para repetirlo, deja una sola instancia ya conectada, cierra menús y diálogos y no interactúes durante la muestra:

```powershell
.\scripts\medir.ps1 -Segundos 30 -Salida build\mi-medicion.json
Get-Process TranslucentBar | Select-Object Id,CPU,WorkingSet64,PrivateMemorySize64
```

## Pruebas y límites

```powershell
.\test.ps1
# Cierra antes cualquier instancia de TranslucentBar:
.\scripts\integracion.ps1
```

Las pruebas de configuración incluyen 16 casos y una comprobación reversible del valor de inicio en HKCU. La prueba de integración crea fondos de franjas temporales, captura ambas barras reales, compara transparencia con Glass, verifica la identidad de los pinceles originales y comprueba la salida normal y la terminación forzada. Los artefactos se guardan en `build/`.

Para comprobar también los cambios de tema y el reinicio real de Explorer:

```powershell
.\scripts\integracion.ps1 -ProbarTemas -ReiniciarExplorer
```

Esa variante alterna temporalmente el tema del sistema y reinicia Explorer; las ventanas del explorador pueden cerrarse. Recupera el tema anterior al finalizar. No altera el tema de las aplicaciones. No ejecutes las pruebas visuales mientras estés usando otras ventanas sobre las barras.

Los resultados locales están en [integracion.json](integracion.json): dos barras con efecto visible, cuatro pinceles con identidad original recuperada, ambos temas con dos fondos aplicados, mismo controlador después de reiniciar Explorer y restauración comprobada por píxeles al salir y al forzar el cierre. La escena inicial puede necesitar un primer refresco del backdrop de Windows; por eso se comprueban tanto la identidad COM como las capturas de salida sobre una referencia ya estable.

Queda por verificar:

- Windows 11 24H2 y 25H2 reales, además de la compilación 26300 usada aquí. La compatibilidad no puede deducirse solo de que las API existan.
- Conexión/desconexión física de monitores, cambio real de DPI, barra con autoocultación, escritorio remoto y suspensión prolongada. El código tiene manejadores para los eventos, pero eso no sustituye la prueba de hardware.
- Validación manual completa del menú, del selector de color y del inicio en un nuevo inicio de sesión. El registro HKCU sí se prueba automáticamente.
- Reducir el working set por debajo de 5 MB y medir por separado el incremento atribuible al puente en Explorer/DWM.
- Otras arquitecturas: esta compilación es solo **x64**; no se ofrece un binario ARM64.

No se soporta combinarla con otra utilidad que escriba simultáneamente los mismos pinceles, ni con barras clásicas reemplazadas por terceros. Los nombres internos de XAML pueden cambiar con Windows; si el diagnóstico no conecta o no encuentra elementos, la barra puede permanecer normal. Consulta el diagnóstico y el HRESULT. No se pide administrador para sortear una denegación. Una actualización de Windows puede requerir adaptar el puente.

La restauración requiere que Explorer siga procesando mensajes. En un bloqueo de Explorer o del sistema no se puede prometer una restauración inmediata; al reiniciar Explorer se crean barras nuevas con sus valores de Windows.

## Referencias técnicas

- Microsoft: [InitializeXamlDiagnosticsEx](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/nf-xamlom-initializexamldiagnosticsex), [IVisualTreeService](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/nn-xamlom-ivisualtreeservice) y [GetPropertyValuesChain](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/nf-xamlom-ivisualtreeservice-getpropertyvalueschain).
- Microsoft: [RegisterPropertyChangedCallback](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.dependencyobject.registerpropertychangedcallback), [AcrylicBrush](https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.media.acrylicbrush) y [SetWinEventHook](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook).
- Microsoft: [SetWindowCompositionAttribute](https://learn.microsoft.com/en-us/windows/win32/dwm/setwindowcompositionattribute) y [DWM_SYSTEMBACKDROP_TYPE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type).
- Herramientas: [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) y [MinGW-w64](https://www.mingw-w64.org/). Las cabeceras del SDK/MinGW son contratos de plataforma y tienen sus propias licencias; no se redistribuye el compilador.

## Licencia

Copyright © 2026 zevidaun. [GNU GPL versión 3 o posterior](../LICENSE), identificador SPDX `GPL-3.0-or-later`. Sin garantías. La GPL permite el uso comercial y exige conservar sus condiciones al distribuir el programa o versiones derivadas; no es una prohibición de venta.

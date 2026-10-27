# Contribuir

¡Gracias! Para que TranslucentBar siga siendo ligero, cualquier cambio debe respetar estas reglas:

- **Nada de sondeo:** ni temporizadores ni bucles en reposo. Todo por eventos de Windows.
- **Nada de runtimes ni dependencias pesadas:** C++20 + Win32 y bibliotecas del sistema.
- **Sin red, sin telemetría, sin administrador.**
- **Reversible:** si cambias algo de la barra, guarda el original y restáuralo.
- **Mide:** si tu cambio afecta al consumo, adjunta la salida de `scripts\medir.ps1` antes y después.

## Probar

```powershell
.\build.ps1
.\test.ps1
# Con TranslucentBar cerrado:
.\scripts\integracion.ps1
```

Muy útil: probar en **Windows 11 24H2 o 25H2** y contarlo en un *issue*. Si adjuntas capturas, **recórtalas para no mostrar tus iconos ni datos personales**.

## Licencia

Al enviar una contribución aceptas que se publique bajo [GPL-3.0-or-later](LICENSE).

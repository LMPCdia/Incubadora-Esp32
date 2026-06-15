# Incubadora microbiológica — Web UI

Frontend en HTML/CSS/JS puro (sin build step) para controlar la incubadora ESP32.

## Modos de uso

**1. Embebido en el ESP32 (uso normal):**
El firmware sirve este mismo `index.html` desde flash. Conectás el celular al SoftAP `Incubadora-ESP32` y abrís `http://192.168.4.1`. Anda offline.

**2. Hosteado en Vercel (acceso desde cualquier lado):**
La UI se sirve desde `https://tu-app.vercel.app` y le pega vía fetch al endpoint `/api` del ESP32. Para esto necesitás:

- El ESP32 conectado a tu WiFi local en modo STA (no SoftAP) con IP alcanzable
- Configurar la URL base del ESP32 desde el botón ⚙ (se guarda en localStorage)
- Permitir mixed content en el browser (HTTPS de Vercel → HTTP del ESP32 lo bloquea por defecto)

## Modo demo

Si no hay API alcanzable, la UI arranca en **modo demo** con datos simulados (temperatura que tiende al setpoint con ruido). Útil para mostrar la interfaz sin hardware.

## Deploy en Vercel

```bash
cd web/
npx vercel --prod
```

O conectá el repo en https://vercel.com/new y elegí `web/` como root directory.

## Estructura

```
web/
├── index.html      # Todo: HTML + CSS + JS en un solo archivo
├── vercel.json     # Headers / config
└── README.md
```

## Aviso mixed-content

Cuando la UI corre en `https://tu-app.vercel.app` y querés que llame al ESP32 en `http://192.168.x.x/api`, el browser bloquea el fetch por mixed content. Soluciones:

- **Recomendado:** servir la UI directamente desde el ESP32 (modo embebido). Es lo que pasa al abrir `http://192.168.4.1` o la IP en STA.
- **Alternativa:** habilitar "insecure content" para el sitio de Vercel en la config del browser.
- **Avanzado:** poner un proxy HTTPS frente al ESP32 (nginx con cert, ngrok, cloudflared).

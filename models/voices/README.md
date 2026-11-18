# Piper local voice

Install the local engine and Brazilian Portuguese voice from the project root:

```powershell
.\scripts\install-piper-voice.ps1
```

The setup creates `.venv-piper`, installs the pinned `piper-tts[http]==1.8.0`
package, and downloads the pinned `pt_BR-faber-medium` model and its JSON
configuration. The ONNX model is 63,201,294 bytes (about 60.3 MiB); it is
intentionally excluded from Git. The installer checks the model SHA-256 and
the configuration file's Git blob hash before accepting either download.

The selected model is published in the
[rhasspy/piper-voices catalog](https://huggingface.co/rhasspy/piper-voices/tree/c10ece1aade47bb51c153c893d14e5bf8e5b7117/pt/pt_BR/faber/medium).
That repository lists an MIT license, and the model card identifies its
training dataset as CC0. The catalog does not state a distinct license for
the generated ONNX artifact, so review the source metadata before any
redistribution. The `piper-tts` engine package is distributed under
GPL-3.0-or-later.

The backend uses Piper's documented local
[HTTP server interface](https://github.com/OHF-Voice/piper1-gpl#http-api).
After installation, restart the backend and use **Testar voz local** in the
conversation panel. If Piper or the model is unavailable, TecBot retains the
browser's existing speech synthesis as a clearly identified fallback.

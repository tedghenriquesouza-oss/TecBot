import os
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

import requests


class ServiceError(Exception):
    def __init__(self, message, status_code=None, music_search_failure=False):
        super().__init__(message)
        self.status_code = status_code
        self.music_search_failure = music_search_failure


VOICE_ID = "pt_BR-faber-medium"
MAX_TEXT_LENGTH = 2000
MAX_AUDIO_BYTES = 15 * 1024 * 1024
PIPER_PORT = 5158
PIPER_URL = f"http://127.0.0.1:{PIPER_PORT}"
ALLOWED_ENVIRONMENT = (
    "PATH",
    "SYSTEMROOT",
    "WINDIR",
    "TEMP",
    "TMP",
    "USERPROFILE",
    "APPDATA",
    "LOCALAPPDATA",
    "PROGRAMDATA",
)
EMOJI_PATTERN = re.compile(
    r"(?:[#*0-9]\uFE0F?\u20E3)|"
    r"[\U0001F000-\U0001FAFF\U0001FC00-\U0001FFFD"
    r"\U0001F1E6-\U0001F1FF\u2600-\u27BF\uFE0E\uFE0F\u200D]"
)


def clean_speech_text(text):
    return re.sub(
        r"\s{2,}",
        " ",
        re.sub(r"\s+([,.;:!?])", r"\1", EMOJI_PATTERN.sub(" ", text)),
    ).strip()


class PiperService:
    def __init__(self, root):
        self.voice_directory = Path(root) / "models" / "voices"
        self.voice_model = self.voice_directory / f"{VOICE_ID}.onnx"
        self.python = self._find_python(root)
        self.process = None
        self.startup_lock = threading.Lock()
        self.synthesis_lock = threading.Lock()

    @staticmethod
    def _find_python(root):
        configured = os.getenv("PIPER_PYTHON")
        if configured:
            return configured
        windows_python = Path(root) / ".venv-piper" / "Scripts" / "python.exe"
        unix_python = Path(root) / ".venv-piper" / "bin" / "python"
        if windows_python.is_file():
            return str(windows_python)
        if unix_python.is_file():
            return str(unix_python)
        return sys.executable

    @staticmethod
    def _environment():
        return {key: os.environ[key] for key in ALLOWED_ENVIRONMENT if key in os.environ}

    def _files_ready(self):
        return self.voice_model.is_file() and Path(f"{self.voice_model}.json").is_file()

    def _package_ready(self):
        try:
            result = subprocess.run(
                [
                    self.python,
                    "-c",
                    "import importlib.util, sys; "
                    "sys.exit(0 if importlib.util.find_spec('piper') "
                    "and importlib.util.find_spec('flask') else 1)",
                ],
                env=self._environment(),
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=8,
                check=False,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
        except (OSError, subprocess.TimeoutExpired):
            return False
        return result.returncode == 0

    def get_status(self):
        model_ready = self._files_ready()
        package_ready = self._package_ready()
        running = self.process is not None and self.process.poll() is None
        return {
            "available": model_ready and package_ready,
            "engine": "piper-local" if model_ready and package_ready else "browser-fallback",
            "modelReady": model_ready,
            "packageReady": package_ready,
            "serverRunning": running,
        }

    def _stop_process(self):
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        self.process = None

    def _start_server(self):
        with self.startup_lock:
            if self.process is not None and self.process.poll() is None:
                return
            if not self._files_ready() or not self._package_ready():
                raise ServiceError("Piper local ou o modelo de voz não está instalado.")
            try:
                self.process = subprocess.Popen(
                    [
                        self.python,
                        "-m",
                        "piper.http_server",
                        "--model",
                        str(self.voice_model),
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(PIPER_PORT),
                        "--data-dir",
                        str(self.voice_directory),
                        "--download-dir",
                        str(self.voice_directory),
                    ],
                    cwd=self.voice_directory,
                    env=self._environment(),
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                )
            except OSError as error:
                raise ServiceError("O motor Piper local não pôde ser iniciado.") from error
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                if self.process.poll() is not None:
                    self.process = None
                    raise ServiceError("O motor Piper não iniciou corretamente.")
                try:
                    response = requests.get(f"{PIPER_URL}/info", timeout=1)
                    if response.ok:
                        info = response.json()
                        if (
                            not isinstance(info, dict)
                            or not isinstance(info.get("voice"), dict)
                            or info["voice"].get("name") != VOICE_ID
                        ):
                            self._stop_process()
                            raise ServiceError(
                                "O serviço local não corresponde à voz Piper configurada."
                            )
                        return
                except ValueError as error:
                    self._stop_process()
                    raise ServiceError(
                        "O serviço local não retornou informações válidas."
                    ) from error
                except requests.RequestException:
                    pass
                time.sleep(0.25)
            self._stop_process()
            raise ServiceError("O motor Piper não respondeu dentro do tempo esperado.")

    @staticmethod
    def _is_valid_wav(audio):
        return (
            44 < len(audio) <= MAX_AUDIO_BYTES
            and audio[:4] == b"RIFF"
            and audio[8:12] == b"WAVE"
            and audio[12:16] == b"fmt "
        )

    def synthesize(self, text, voice_rate=1):
        if not isinstance(text, str) or not text.strip():
            raise ServiceError("Informe um texto para gerar o áudio.", 400)
        text = clean_speech_text(text)
        if not text:
            raise ServiceError("O texto não contém conteúdo para gerar fala.", 400)
        if len(text) > MAX_TEXT_LENGTH:
            raise ServiceError(
                f"O texto excede o limite de {MAX_TEXT_LENGTH} caracteres.", 400
            )
        if (
            not isinstance(voice_rate, (int, float))
            or isinstance(voice_rate, bool)
            or not 0.5 <= voice_rate <= 2
        ):
            raise ServiceError("A velocidade da voz deve estar entre 0,5 e 2.", 400)
        if not self.synthesis_lock.acquire(blocking=False):
            raise ServiceError("O serviço de voz já está processando outro áudio.", 429)
        try:
            self._start_server()
            try:
                response = requests.post(
                    f"{PIPER_URL}/synthesize",
                    json={
                        "text": text,
                        "voice": VOICE_ID,
                        "length_scale": 1 / voice_rate,
                    },
                    timeout=45,
                )
            except requests.Timeout as error:
                raise ServiceError("A geração de voz excedeu o tempo limite.") from error
            except requests.RequestException as error:
                raise ServiceError("O motor Piper não está acessível.") from error
            if not response.ok:
                raise ServiceError("O motor Piper não conseguiu gerar um arquivo WAV.")
            audio = response.content
            if not self._is_valid_wav(audio):
                raise ServiceError("O motor Piper retornou um arquivo WAV inválido.")
            return audio
        finally:
            self.synthesis_lock.release()

    def stop(self):
        self._stop_process()


_service = None


def stop_piper_at_exit():
    if _service is not None:
        _service.stop()

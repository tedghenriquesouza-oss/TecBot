import copy
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch


_test_directory = tempfile.TemporaryDirectory()
os.environ["TECBOT_DATABASE"] = str(Path(_test_directory.name) / "tecbot-test.sqlite3")

import app as tecbot


class ConversationApiTests(unittest.TestCase):
    def setUp(self):
        original_settings = tecbot.settings.copy()
        original_provider_config = copy.deepcopy(tecbot.provider_config)

        def restore_settings():
            with tecbot.settings_lock:
                tecbot.settings.clear()
                tecbot.settings.update(original_settings)
                tecbot.provider_config = original_provider_config

        self.addCleanup(restore_settings)
        if tecbot.DATABASE_FILE.exists():
            tecbot.DATABASE_FILE.unlink()
        tecbot.initialize_database()
        self.client = tecbot.app.test_client()
        self.provider_history = []

        def fake_provider(message, provider=None, history=None):
            self.provider_history.append(history or [])
            return f"Resposta: {message}"

        self.provider_patch = patch.object(tecbot, "ask_provider", side_effect=fake_provider)
        self.provider_patch.start()
        self.addCleanup(self.provider_patch.stop)

    def test_chat_is_persisted_and_shared_history_is_sent_to_the_provider(self):
        first = self.client.post("/api/chat", json={"mensagem": "Lembre que gosto de robótica"})
        self.assertEqual(first.status_code, 200)
        first_id = first.json["conversationId"]

        second = self.client.post("/api/chat", json={"mensagem": "O que eu gosto?"})
        self.assertEqual(second.status_code, 200)
        self.assertNotEqual(first_id, second.json["conversationId"])
        self.assertEqual(
            self.provider_history[1],
            [
                {"role": "user", "content": "Lembre que gosto de robótica"},
                {"role": "assistant", "content": "Resposta: Lembre que gosto de robótica"},
            ],
        )

        detail = self.client.get(f"/api/conversations/{first_id}")
        self.assertEqual(detail.status_code, 200)
        self.assertEqual(len(detail.json["messages"]), 2)
        self.assertEqual(detail.json["conversation"]["title"], "Lembre que gosto de robótica")

        listing = self.client.get("/api/conversations")
        self.assertEqual(listing.status_code, 200)
        self.assertEqual(len(listing.json["conversations"]), 2)

    def test_existing_conversation_resumes_and_can_be_deleted(self):
        first = self.client.post("/api/chat", json={"mensagem": "Primeira mensagem"})
        conversation_id = first.json["conversationId"]

        resumed = self.client.post(
            "/api/chat",
            json={"mensagem": "Continuação", "conversationId": conversation_id},
        )
        self.assertEqual(resumed.status_code, 200)
        self.assertEqual(resumed.json["conversationId"], conversation_id)
        self.assertEqual(
            self.provider_history[1][-2:],
            [
                {"role": "user", "content": "Primeira mensagem"},
                {"role": "assistant", "content": "Resposta: Primeira mensagem"},
            ],
        )

        deleted = self.client.delete(f"/api/conversations/{conversation_id}")
        self.assertEqual(deleted.status_code, 200)
        self.assertEqual(self.client.get(f"/api/conversations/{conversation_id}").status_code, 404)

    def test_settings_are_readable_without_an_admin_token(self):
        response = self.client.get("/api/settings")
        self.assertEqual(response.status_code, 200)
        self.assertIn("configuracoes", response.json)

    def test_provider_config_saves_keys_without_exposing_them(self):
        config_file = Path(_test_directory.name) / "provider-config-test.json"
        request_body = {
            "geminiApiKey": "gemini-test-secret",
            "nvidiaApiKey": "",
            "customProviders": [
                {
                    "id": "my-provider",
                    "name": "Meu provedor",
                    "baseUrl": "https://api.example.com/v1",
                    "apiKey": "custom-test-secret",
                }
            ],
        }
        with patch.object(tecbot, "PROVIDER_CONFIG_FILE", config_file):
            response = self.client.put("/api/provider-config", json=request_body)
            self.assertEqual(response.status_code, 200)
            self.assertNotIn("gemini-test-secret", response.get_data(as_text=True))
            self.assertNotIn("custom-test-secret", response.get_data(as_text=True))
            self.assertTrue(
                next(
                    item
                    for item in response.json["provedores"]["builtInProviders"]
                    if item["id"] == "gemini"
                )["hasApiKey"]
            )
            self.assertTrue(
                response.json["provedores"]["customProviders"][0]["hasApiKey"]
            )
            self.assertIn("custom-test-secret", config_file.read_text(encoding="utf-8"))
            edited = {
                **request_body,
                "geminiApiKey": "",
                "customProviders": [
                    {
                        "id": "my-provider",
                        "name": "Meu provedor atualizado",
                        "baseUrl": "https://api.example.com/v2",
                        "apiKey": "",
                    }
                ],
            }
            updated = self.client.put("/api/provider-config", json=edited)
            self.assertEqual(updated.status_code, 200)
            self.assertIn(
                "custom-test-secret", config_file.read_text(encoding="utf-8")
            )
            self.assertNotIn(
                "custom-test-secret", updated.get_data(as_text=True)
            )
            listed = self.client.get("/api/provider-config")
            self.assertEqual(listed.status_code, 200)
            self.assertNotIn("gemini-test-secret", listed.get_data(as_text=True))
            self.assertNotIn("custom-test-secret", listed.get_data(as_text=True))

    def test_custom_openai_compatible_provider_uses_saved_token_and_model(self):
        custom_provider = {
            "id": "my-provider",
            "name": "Meu provedor",
            "baseUrl": "https://api.example.com/v1",
            "apiKey": "custom-test-secret",
        }
        configured_settings = {
            **tecbot.settings,
            "provider": "my-provider",
            "model": "my-model",
        }
        response_body = MagicMock()
        response_body.ok = True
        response_body.json.return_value = {
            "choices": [{"message": {"content": "Resposta personalizada"}}]
        }
        with patch.object(
            tecbot,
            "provider_config",
            {
                "keys": {},
                "customProviders": [custom_provider],
            },
        ), patch.object(tecbot, "settings", configured_settings), patch.object(
            tecbot.requests, "post", return_value=response_body
        ) as post:
            answer = tecbot.ask_openai_compatible("my-provider", "Olá")

        self.assertEqual(answer, "Resposta personalizada")
        post.assert_called_once()
        args, kwargs = post.call_args
        self.assertEqual(args[0], "https://api.example.com/v1/chat/completions")
        self.assertEqual(kwargs["headers"]["Authorization"], "Bearer custom-test-secret")
        self.assertEqual(kwargs["json"]["model"], "my-model")

    def test_music_command_searches_youtube_without_claiming_esp32_playback(self):
        resultado = {
            "titulo": "Minha música",
            "videoId": "abcdefghijk",
            "url": "https://www.youtube.com/watch?v=abcdefghijk",
        }
        with patch.object(tecbot, "search_youtube", return_value=resultado) as busca:
            response = self.client.post(
                "/api/chat",
                json={"mensagem": "tocar música Minha música"},
            )

        self.assertEqual(response.status_code, 200)
        busca.assert_called_once_with("Minha música")
        self.assertIn("Encontrei Minha música no YouTube", response.json["resposta"])
        self.assertIn("não reproduz áudio diretamente", response.json["resposta"])
        self.assertEqual(response.json["youtube"]["url"], resultado["url"])

    def test_device_voice_command_exposes_transcript_for_existing_speech_route(self):
        transcript = "TecBot, tudo bem?"
        question = "tudo bem?"
        cliente_genai = MagicMock()
        cliente_genai.models.generate_content.return_value.text = transcript
        with patch.dict(
            os.environ,
            {"ESP32_DEVICE_TOKEN": "test-token", "GEMINI_API_KEY": "test-key"},
        ), patch.object(
            tecbot, "latest_device_message", {"id": 0, "texto": ""}
        ), patch.object(
            tecbot, "decode_ima_adpcm_wav", return_value=b"WAVE"
        ), patch.object(tecbot.genai, "Client") as cliente, patch.object(
            tecbot.piper_service, "synthesize", return_value=b"WAV"
        ) as sintetizar:
            cliente.return_value.__enter__.return_value = cliente_genai
            response = self.client.post(
                "/api/device/voice-command",
                data=b"audio",
                content_type="audio/wav",
                headers={"Authorization": "Bearer test-token"},
            )
            self.assertEqual(response.status_code, 200)
            self.assertGreater(response.json["id"], 0)

            message = self.client.get(
                "/api/device/message",
                headers={"Authorization": "Bearer test-token"},
            )
            self.assertEqual(message.status_code, 200)
            self.assertEqual(message.json["texto"], f"Resposta: {question}")

            task_logs = self.client.get("/api/devices/task-logs")
            self.assertEqual(task_logs.status_code, 200)
            self.assertEqual(len(task_logs.json["logs"]), 1)
            self.assertEqual(task_logs.json["logs"][0]["tipo"], "question")
            self.assertEqual(
                task_logs.json["logs"][0]["detalhes"],
                {
                    "pergunta": question,
                    "resposta": f"Resposta: {question}",
                },
            )
            conversations = self.client.get("/api/conversations")
            self.assertEqual(conversations.status_code, 200)
            self.assertEqual(len(conversations.json["conversations"]), 1)
            self.assertEqual(
                conversations.json["conversations"][0]["title"], question
            )

            speech = self.client.post(
                "/api/device/speech",
                json={"id": response.json["id"]},
                headers={"Authorization": "Bearer test-token"},
            )
            self.assertEqual(speech.status_code, 200)
            sintetizar.assert_called_once_with(
                f"Resposta: {question}", tecbot.settings["voiceRate"]
            )
            task_logs = self.client.get("/api/devices/task-logs")
            self.assertEqual(task_logs.json["logs"][0]["tipo"], "speech")

    def test_device_voice_command_ignores_audio_without_wake_word(self):
        cliente_genai = MagicMock()
        cliente_genai.models.generate_content.return_value.text = "Qual é a previsão?"
        with patch.dict(
            os.environ,
            {"ESP32_DEVICE_TOKEN": "test-token", "GEMINI_API_KEY": "test-key"},
        ), patch.object(
            tecbot, "decode_ima_adpcm_wav", return_value=b"WAVE"
        ), patch.object(tecbot.genai, "Client") as cliente, patch.object(
            tecbot, "ask_provider"
        ) as perguntar:
            cliente.return_value.__enter__.return_value = cliente_genai
            response = self.client.post(
                "/api/device/voice-command",
                data=b"audio",
                content_type="audio/wav",
                headers={"Authorization": f"Bearer {os.environ['ESP32_DEVICE_TOKEN']}"},
            )

        self.assertEqual(response.status_code, 204)
        perguntar.assert_not_called()
        self.assertEqual(
            self.client.get("/api/conversations").json["conversations"], []
        )
        self.assertEqual(
            self.client.get("/api/devices/task-logs").json["logs"], []
        )

    def test_device_voice_errors_are_returned_for_audio_playback(self):
        with patch.dict(
            os.environ, {"ESP32_DEVICE_TOKEN": "test-token"}
        ), patch.object(
            tecbot, "latest_device_message", {"id": 0, "texto": ""}
        ), patch.object(
            tecbot, "provider_api_key", return_value=""
        ), patch.object(
            tecbot, "decode_ima_adpcm_wav", return_value=b"WAVE"
        ), patch.object(
            tecbot.piper_service, "synthesize", return_value=b"WAV"
        ) as sintetizar:
            response = self.client.post(
                "/api/device/voice-command",
                data=b"audio",
                content_type="audio/wav",
                headers={"Authorization": "Bearer test-token"},
            )
            self.assertEqual(response.status_code, 200)
            self.assertGreater(response.json["id"], 0)

            message = self.client.get(
                "/api/device/message",
                headers={"Authorization": "Bearer test-token"},
            )
            self.assertIn("não consigo transcrever", message.json["texto"].lower())
            logs = self.client.get("/api/devices/task-logs").json["logs"]
            self.assertEqual(len(logs), 1)
            self.assertEqual(
                logs[0]["detalhes"]["pergunta"],
                "Áudio recebido; transcrição indisponível.",
            )
            self.assertEqual(logs[0]["detalhes"]["resposta"], message.json["texto"])
            self.assertIn("erro", logs[0]["detalhes"])

            speech = self.client.post(
                "/api/device/speech",
                json={"id": response.json["id"]},
                headers={"Authorization": "Bearer test-token"},
            )

        self.assertEqual(speech.status_code, 200)
        self.assertEqual(speech.data, b"WAV")
        sintetizar.assert_called_once_with(
            message.json["texto"], tecbot.settings["voiceRate"]
        )

    def test_ai_processing_error_is_queued_as_spoken_device_response(self):
        cliente_genai = MagicMock()
        cliente_genai.models.generate_content.return_value.text = (
            "TecBot, me conte uma curiosidade."
        )
        with patch.dict(
            os.environ, {"ESP32_DEVICE_TOKEN": "test-token"}
        ), patch.object(
            tecbot, "latest_device_message", {"id": 0, "texto": ""}
        ), patch.object(
            tecbot, "provider_api_key", return_value="gemini-test-key"
        ), patch.object(
            tecbot, "decode_ima_adpcm_wav", return_value=b"WAVE"
        ), patch.object(tecbot.genai, "Client") as cliente, patch.object(
            tecbot,
            "ask_provider",
            side_effect=tecbot.ServiceError("API indisponível", 502),
        ), patch.object(
            tecbot.piper_service, "synthesize", return_value=b"WAV"
        ) as sintetizar:
            cliente.return_value.__enter__.return_value = cliente_genai
            response = self.client.post(
                "/api/device/voice-command",
                data=b"audio",
                content_type="audio/wav",
                headers={"Authorization": "Bearer test-token"},
            )
            self.assertEqual(response.status_code, 200)
            message = self.client.get(
                "/api/device/message",
                headers={"Authorization": "Bearer test-token"},
            )
            self.assertIn("não consegui processar", message.json["texto"].lower())
            logs = self.client.get("/api/devices/task-logs").json["logs"]
            self.assertEqual(logs[0]["detalhes"]["pergunta"], "me conte uma curiosidade.")
            self.assertEqual(logs[0]["detalhes"]["resposta"], message.json["texto"])
            self.assertEqual(logs[0]["tipo"], "question")
            speech = self.client.post(
                "/api/device/speech",
                json={"id": response.json["id"]},
                headers={"Authorization": "Bearer test-token"},
            )

        self.assertEqual(speech.status_code, 200)
        sintetizar.assert_called_once_with(
            message.json["texto"], tecbot.settings["voiceRate"]
        )

    def test_settings_can_be_updated_without_an_admin_token(self):
        settings_file = Path(_test_directory.name) / "settings-test.json"
        updated_settings = tecbot.settings.copy()
        updated_settings["assistantName"] = "TecBot de teste"
        with patch.object(tecbot, "SETTINGS_FILE", settings_file):
            response = self.client.put("/api/settings", json=updated_settings)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(
            response.json["configuracoes"]["assistantName"], "TecBot de teste"
        )

    def test_device_task_logs_are_paginated_and_validate_parameters(self):
        for index in range(3):
            tecbot.save_device_task_log(
                "esp32-1", "control", f"Atividade {index}", {"indice": index}
            )

        first_page = self.client.get("/api/devices/task-logs?limit=2")
        self.assertEqual(first_page.status_code, 200)
        self.assertEqual(
            [entry["titulo"] for entry in first_page.json["logs"]],
            ["Atividade 2", "Atividade 1"],
        )
        self.assertTrue(first_page.json["hasMore"])

        second_page = self.client.get(
            f"/api/devices/task-logs?limit=2&before={first_page.json['nextBefore']}"
        )
        self.assertEqual(second_page.status_code, 200)
        self.assertEqual(
            [entry["titulo"] for entry in second_page.json["logs"]],
            ["Atividade 0"],
        )
        self.assertFalse(second_page.json["hasMore"])
        self.assertEqual(
            self.client.get("/api/devices/task-logs?limit=0").status_code, 400
        )

    def test_device_audio_controls_proxy_and_validate_settings(self):
        device = {"deviceId": "esp32-test"}
        with patch.object(tecbot, "get_registered_device", return_value=device), patch.object(
            tecbot,
            "proxy_device_request",
            return_value={"mensagem": "Microfone desativado; som ativado."},
        ) as proxy:
            response = self.client.post(
                "/api/devices/control",
                json={
                    "deviceId": "esp32-test",
                    "action": "audio",
                    "microphoneActive": False,
                    "speakerActive": True,
                },
            )
            self.assertEqual(response.status_code, 200)
            proxy.assert_called_once_with(
                device,
                "/controle/audio",
                {"microfone": "0", "som": "1"},
            )

            invalid = self.client.post(
                "/api/devices/control",
                json={"deviceId": "esp32-test", "action": "audio"},
            )
            self.assertEqual(invalid.status_code, 400)
            invalid_type = self.client.post(
                "/api/devices/control",
                json={
                    "deviceId": "esp32-test",
                    "action": "audio",
                    "microphoneActive": "false",
                },
            )
            self.assertEqual(invalid_type.status_code, 400)

    def test_separate_frontend_pages_are_served(self):
        for path, marker in (
            ("/", "TecBot | Chat"),
            ("/dashboard.html", "TecBot | Dashboard"),
            ("/esp-logs.html", "TecBot | Registros do ESP32"),
            ("/settings.html", "TecBot | Configurações"),
        ):
            with self.subTest(path=path):
                response = self.client.get(path)
                self.assertEqual(response.status_code, 200)
                self.assertIn(marker.encode("utf-8"), response.data)
                response.close()


if __name__ == "__main__":
    unittest.main()

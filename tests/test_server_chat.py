import unittest

from fastapi import HTTPException
from fastapi.testclient import TestClient

from copilot import server


class TestCopilotChatErrorMapping(unittest.TestCase):
    def setUp(self):
        self.client = TestClient(server.app)
        self._original_create = server._create_llm_adapter
        self._original_build_messages = server._build_messages
        self._original_generate_with_fallback = server._generate_with_fallback

    def tearDown(self):
        server._create_llm_adapter = self._original_create
        server._build_messages = self._original_build_messages
        server._generate_with_fallback = self._original_generate_with_fallback

    def _patch_common(self):
        server._create_llm_adapter = lambda: object()
        server._build_messages = lambda req: [{"role": "user", "content": req.message}]

    def test_chat_maps_runtime_config_error_to_400(self):
        self._patch_common()

        def _raise(*args, **kwargs):
            raise RuntimeError("LLM 配置错误：缺少 API Key")

        server._generate_with_fallback = _raise

        resp = self.client.post("/chat", json={"message": "hi", "history": [], "images": []})
        self.assertEqual(resp.status_code, 400)
        self.assertIn("LLM 配置错误", resp.json().get("detail", ""))

    def test_chat_maps_timeout_like_error_to_503_instead_of_500(self):
        self._patch_common()

        def _raise(*args, **kwargs):
            raise Exception("ReadTimeout: upstream gateway timed out")

        server._generate_with_fallback = _raise

        resp = self.client.post("/chat", json={"message": "hi", "history": [], "images": []})
        self.assertEqual(resp.status_code, 503)

    def test_chat_keeps_unknown_error_as_500(self):
        self._patch_common()

        def _raise(*args, **kwargs):
            raise Exception("totally unknown internal crash")

        server._generate_with_fallback = _raise

        resp = self.client.post("/chat", json={"message": "hi", "history": [], "images": []})
        self.assertEqual(resp.status_code, 500)


class TestCopilotModelFallback(unittest.TestCase):
    def test_route_unavailable_runtime_error_uses_fallback_model(self):
        class FakeConfig:
            model = "primary-model"

        class FakeLLM:
            config = FakeConfig()

            def __init__(self):
                self.models = []

            def generate(self, messages, model=None):
                self.models.append(model or self.config.model)
                if model is None:
                    raise RuntimeError("model_not_found: no route")
                return "fallback response"

        llm = FakeLLM()
        response = server._generate_with_fallback(llm, [{"role": "user", "content": "hi"}])

        self.assertEqual(response, "fallback response")
        self.assertEqual(llm.models, ["primary-model", server.FALLBACK_MODELS[0]])

    def test_non_route_runtime_error_does_not_use_fallback_model(self):
        class FakeConfig:
            model = "primary-model"

        class FakeLLM:
            config = FakeConfig()

            def __init__(self):
                self.models = []

            def generate(self, messages, model=None):
                self.models.append(model or self.config.model)
                raise RuntimeError("LLM 配置错误：缺少 API Key")

        llm = FakeLLM()
        with self.assertRaisesRegex(RuntimeError, "LLM 配置错误"):
            server._generate_with_fallback(llm, [{"role": "user", "content": "hi"}])

        self.assertEqual(llm.models, ["primary-model"])


if __name__ == "__main__":
    unittest.main()

"""Offline tests for imagegen.py: no network, no credentials, no spend."""

import base64
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

import imagegen
from imagegen import (
    PROTECTED, build_requests, check_output_dir, describe_image, extract_images,
    main, redact, requested_models, save_images, slug, write_new,
)


def one_request(provider, argv):
    """Build the single request a test expects, asserting there is exactly one."""
    args = imagegen.build_parser().parse_args(argv)
    requests = build_requests(provider, args)
    assert len(requests) == 1, requests
    return requests[0]


def png_bytes(size=(8, 8), mode="RGBA", colour=(10, 20, 30, 255)) -> bytes:
    buffer = io.BytesIO()
    Image.new(mode, size, colour).save(buffer, format="PNG")
    return buffer.getvalue()


def encoded(size=(8, 8)) -> str:
    return base64.b64encode(png_bytes(size)).decode("ascii")


class RequestShapeTests(unittest.TestCase):
    def test_pixellab_payload_uses_description_and_image_size(self):
        _, url, payload = one_request("pixellab", [
            "generate", "--provider", "pixellab", "--prompt", "mossy floor",
            "--width", "240", "--height", "240", "--out", "scratch",
        ])
        self.assertEqual(url, "https://api.pixellab.ai/v2/create-image-pixen")
        self.assertEqual(payload["description"], "mossy floor")
        self.assertEqual(payload["image_size"], {"width": 240, "height": 240})
        # Optional fields stay absent rather than being sent as null.
        for key in ("seed", "detail", "view", "direction", "outline", "no_background"):
            self.assertNotIn(key, payload)

    def test_pixellab_optional_fields_are_forwarded(self):
        _, _, payload = one_request("pixellab", [
            "generate", "--provider", "pixellab", "--prompt", "x", "--out", "s",
            "--seed", "7", "--detail", "low detail", "--view", "side",
            "--direction", "east", "--outline", "lineless", "--no-background",
        ])
        self.assertEqual(payload["seed"], 7)
        self.assertEqual(payload["detail"], "low detail")
        self.assertEqual(payload["view"], "side")
        self.assertEqual(payload["direction"], "east")
        self.assertEqual(payload["outline"], "lineless")
        self.assertTrue(payload["no_background"])

    def test_openrouter_payload_uses_model_prompt_and_size(self):
        model, url, payload = one_request("openrouter", [
            "generate", "--provider", "openrouter", "--model", "vendor/model",
            "--prompt", "mossy floor", "--width", "1024", "--height", "1024",
            "--out", "scratch", "--no-background", "--output-format", "png",
        ])
        self.assertEqual(model, "vendor/model")
        self.assertEqual(url, "https://openrouter.ai/api/v1/images")
        self.assertEqual(payload["model"], "vendor/model")
        self.assertEqual(payload["prompt"], "mossy floor")
        self.assertEqual(payload["size"], "1024x1024")
        self.assertEqual(payload["background"], "transparent")
        self.assertEqual(payload["output_format"], "png")
        self.assertNotIn("n", payload)

    def test_openrouter_requires_a_model_because_slugs_change(self):
        args = imagegen.build_parser().parse_args([
            "generate", "--provider", "openrouter", "--prompt", "x", "--out", "s",
        ])
        with self.assertRaisesRegex(ValueError, "needs --model"):
            build_requests("openrouter", args)

    def test_openrouter_n_is_only_sent_above_one(self):
        _, _, payload = one_request("openrouter", [
            "generate", "--provider", "openrouter", "--model", "m", "--prompt", "x",
            "--out", "s", "--n", "3",
        ])
        self.assertEqual(payload["n"], 3)


class MultiModelTests(unittest.TestCase):
    def test_repeated_model_flags_run_every_model_in_order(self):
        args = imagegen.build_parser().parse_args([
            "generate", "--provider", "openrouter", "--prompt", "x", "--out", "s",
            "--model", "vendor/a", "--model", "vendor/b",
        ])
        self.assertEqual(requested_models("openrouter", args), ["vendor/a", "vendor/b"])

    def test_comma_separated_slugs_are_split(self):
        args = imagegen.build_parser().parse_args([
            "generate", "--provider", "openrouter", "--prompt", "x", "--out", "s",
            "--model", "vendor/a, vendor/b ,vendor/c",
        ])
        self.assertEqual(requested_models("openrouter", args),
                         ["vendor/a", "vendor/b", "vendor/c"])

    def test_duplicate_models_are_collapsed_so_nothing_is_paid_twice(self):
        args = imagegen.build_parser().parse_args([
            "generate", "--provider", "openrouter", "--prompt", "x", "--out", "s",
            "--model", "vendor/a,vendor/a", "--model", "vendor/a",
        ])
        self.assertEqual(requested_models("openrouter", args), ["vendor/a"])

    def test_each_model_gets_its_own_payload(self):
        args = imagegen.build_parser().parse_args([
            "generate", "--provider", "openrouter", "--prompt", "moss", "--out", "s",
            "--model", "vendor/a,vendor/b",
        ])
        requests = build_requests("openrouter", args)
        self.assertEqual([model for model, _, _ in requests], ["vendor/a", "vendor/b"])
        self.assertEqual([payload["model"] for _, _, payload in requests],
                         ["vendor/a", "vendor/b"])
        self.assertEqual({payload["prompt"] for _, _, payload in requests}, {"moss"})

    def test_pixellab_ignores_model_and_stays_a_single_request(self):
        args = imagegen.build_parser().parse_args([
            "generate", "--provider", "pixellab", "--prompt", "x", "--out", "s",
            "--model", "vendor/a",
        ])
        self.assertEqual(requested_models("pixellab", args), [""])
        self.assertEqual(len(build_requests("pixellab", args)), 1)


class ResponseParsingTests(unittest.TestCase):
    def test_pixellab_response_yields_one_image(self):
        result = {"image": {"type": "base64", "base64": encoded(), "format": "png"}}
        images = extract_images("pixellab", result)
        self.assertEqual(len(images), 1)
        self.assertEqual(images[0][1], "png")

    def test_pixellab_response_without_image_fails_loudly(self):
        with self.assertRaisesRegex(RuntimeError, "no image"):
            extract_images("pixellab", {})

    def test_openrouter_response_yields_every_image_with_media_type(self):
        result = {"data": [
            {"b64_json": encoded(), "media_type": "image/png"},
            {"b64_json": encoded(), "media_type": "image/webp"},
        ]}
        images = extract_images("openrouter", result)
        self.assertEqual([fmt for _, fmt in images], ["png", "webp"])

    def test_openrouter_media_type_defaults_to_png(self):
        images = extract_images("openrouter", {"data": [{"b64_json": encoded()}]})
        self.assertEqual(images[0][1], "png")

    def test_openrouter_response_without_data_fails_loudly(self):
        with self.assertRaisesRegex(RuntimeError, "no images"):
            extract_images("openrouter", {"data": []})

    def test_describe_image_decodes_real_bytes(self):
        info = describe_image(png_bytes((16, 12)))
        self.assertEqual(info["format"], "PNG")
        self.assertEqual(info["mode"], "RGBA")
        self.assertEqual(info["size"], [16, 12])
        self.assertEqual(info["visible_colors"], 1)

    def test_corrupt_bytes_are_rejected_not_written(self):
        with self.assertRaises(Exception):
            describe_image(b"not an image")


class SafetyTests(unittest.TestCase):
    def test_assets_directory_is_refused(self):
        with self.assertRaisesRegex(ValueError, "protected source art"):
            check_output_dir(PROTECTED)
        with self.assertRaisesRegex(ValueError, "protected source art"):
            check_output_dir(PROTECTED / "nested")

    def test_scratch_directory_is_allowed(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(check_output_dir(Path(directory)), Path(directory).resolve())

    def test_write_new_never_overwrites(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.png"
            write_new(path, b"first")
            with self.assertRaises(FileExistsError):
                write_new(path, b"second")
            self.assertEqual(path.read_bytes(), b"first")

    def test_redact_removes_the_credential(self):
        self.assertEqual(redact("token abc123 rejected", "abc123"),
                         "token <redacted> rejected")
        self.assertEqual(redact("nothing to hide", ""), "nothing to hide")

    def test_slug_is_filesystem_safe(self):
        self.assertEqual(slug("Mossy Stone Floor!"), "mossy-stone-floor")
        self.assertEqual(slug("///"), "image")
        self.assertLessEqual(len(slug("x" * 200)), 40)

    def test_missing_credential_names_the_variable_and_never_echoes_a_key(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "PIXELLAB_API_KEY"):
                imagegen.api_key("pixellab")
            with self.assertRaisesRegex(ValueError, "OPENROUTER_API_KEY"):
                imagegen.api_key("openrouter")


class CliTests(unittest.TestCase):
    def test_dry_run_makes_no_request_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "scratch"
            with patch.dict(os.environ, {}, clear=True), \
                 patch("imagegen.http_json") as http:
                code = main(["generate", "--provider", "pixellab", "--prompt", "x",
                             "--out", str(out)])
            self.assertEqual(code, 0)
            http.assert_not_called()
            self.assertFalse(out.exists())

    def test_dry_run_does_not_require_a_credential(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.dict(os.environ, {}, clear=True):
            code = main(["generate", "--provider", "openrouter", "--model", "m",
                         "--prompt", "x", "--out", str(Path(directory) / "s")])
            self.assertEqual(code, 0)

    def test_submit_without_a_credential_exits_before_any_request(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.dict(os.environ, {}, clear=True), \
             patch("imagegen.http_json") as http:
            with self.assertRaises(SystemExit) as error:
                main(["generate", "--provider", "pixellab", "--prompt", "x",
                      "--out", str(Path(directory) / "s"), "--submit"])
            self.assertEqual(error.exception.code, 1)
            http.assert_not_called()

    def test_submit_writes_images_and_a_receipt(self):
        result = {"image": {"base64": encoded((8, 8)), "format": "png"},
                  "usage": {"type": "usd", "usd": 0.02}}
        secret = "sk-or-v1-DO-NOT-LEAK-0123456789"
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "scratch"
            with patch.dict(os.environ, {"PIXELLAB_API_KEY": secret}, clear=True), \
                 patch("imagegen.http_json", return_value=result) as http:
                code = main(["generate", "--provider", "pixellab", "--prompt", "mossy floor",
                             "--out", str(out), "--submit"])
            self.assertEqual(code, 0)
            http.assert_called_once()
            images = list(out.glob("*.png"))
            receipts = list(out.glob("*.json"))
            self.assertEqual(len(images), 1)
            self.assertEqual(len(receipts), 1)
            text = receipts[0].read_text()
            receipt = json.loads(text)
            self.assertFalse(receipt["installed"])
            self.assertEqual(receipt["usage"]["usd"], 0.02)
            self.assertEqual(receipt["images"][0]["decoded"]["size"], [8, 8])
            self.assertEqual(len(receipt["images"][0]["sha256"]), 64)
            self.assertNotIn(secret, text)

    def test_submit_refuses_assets_before_calling_the_api(self):
        with patch.dict(os.environ, {"PIXELLAB_API_KEY": "k"}, clear=True), \
             patch("imagegen.http_json") as http:
            with self.assertRaises(SystemExit) as error:
                main(["generate", "--provider", "pixellab", "--prompt", "x",
                      "--out", str(PROTECTED), "--submit"])
            self.assertEqual(error.exception.code, 1)
            http.assert_not_called()

    def test_models_lists_openrouter_image_models(self):
        result = {"data": [
            {"id": "vendor/a", "name": "A", "architecture": {"output_modalities": ["image"]}},
            {"id": "vendor/b", "name": "B", "architecture": {"output_modalities": ["image"]}},
        ]}
        with patch.dict(os.environ, {"OPENROUTER_API_KEY": "k"}, clear=True), \
             patch("imagegen.http_json", return_value=result) as http:
            code = main(["models", "--provider", "openrouter"])
        self.assertEqual(code, 0)
        self.assertIn("/images/models", http.call_args[0][0])

    def test_models_works_without_a_key(self):
        result = {"data": [{"id": "vendor/a", "name": "A",
                            "architecture": {"output_modalities": ["image"]}}]}
        with patch.dict(os.environ, {}, clear=True), \
             patch("imagegen.http_json", return_value=result) as http:
            self.assertEqual(main(["models", "--provider", "openrouter"]), 0)
        self.assertIsNone(http.call_args[0][1])

    def test_balance_is_pixellab_only(self):
        result = {"credits": {"type": "usd", "usd": 10.5},
                  "subscription": {"type": "generations", "status": "active",
                                   "plan": "Tier 2: Pixel Artisan",
                                   "generations": 450, "total": 2000}}
        with patch.dict(os.environ, {"PIXELLAB_API_KEY": "k"}, clear=True), \
             patch("imagegen.http_json", return_value=result) as http:
            self.assertEqual(main(["balance", "--provider", "pixellab"]), 0)
        self.assertIn("/balance", http.call_args[0][0])
        with self.assertRaises(SystemExit):
            main(["balance", "--provider", "openrouter"])

    def test_balance_falls_back_to_raw_json_for_an_unknown_shape(self):
        with patch.dict(os.environ, {"PIXELLAB_API_KEY": "k"}, clear=True), \
             patch("imagegen.http_json", return_value={"unexpected": True}):
            self.assertEqual(main(["balance", "--provider", "pixellab"]), 0)


class MultiModelCliTests(unittest.TestCase):
    def test_dry_run_reports_every_model_and_spends_nothing(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.dict(os.environ, {}, clear=True), \
             patch("imagegen.http_json") as http:
            code = main(["generate", "--provider", "openrouter", "--prompt", "x",
                         "--out", str(Path(directory) / "s"),
                         "--model", "vendor/a,vendor/b"])
            self.assertEqual(code, 0)
            http.assert_not_called()

    def test_submit_calls_each_model_once_and_writes_a_receipt_per_model(self):
        result = {"data": [{"b64_json": encoded((8, 8)), "media_type": "image/png"}],
                  "usage": {"cost": 0.01}}
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "scratch"
            with patch.dict(os.environ, {"OPENROUTER_API_KEY": "k"}, clear=True), \
                 patch("imagegen.http_json", return_value=result) as http:
                code = main(["generate", "--provider", "openrouter", "--prompt", "moss",
                             "--out", str(out), "--model", "vendor/a,vendor/b",
                             "--submit"])
            self.assertEqual(code, 0)
            self.assertEqual(http.call_count, 2)
            self.assertEqual([call[0][2]["model"] for call in http.call_args_list],
                             ["vendor/a", "vendor/b"])
            receipts = sorted(out.glob("*.json"))
            self.assertEqual(len(receipts), 2)
            models = {json.loads(path.read_text())["model"] for path in receipts}
            self.assertEqual(models, {"vendor/a", "vendor/b"})
            # Filenames carry the model so candidates stay distinguishable.
            self.assertTrue(any("vendor-a" in path.name for path in receipts))
            self.assertTrue(any("vendor-b" in path.name for path in receipts))

    def test_one_failing_model_does_not_discard_the_others(self):
        good = {"data": [{"b64_json": encoded((8, 8)), "media_type": "image/png"}]}

        def fake(url, key, payload, timeout=300):
            if payload["model"] == "vendor/bad":
                raise RuntimeError("vendor/bad -> HTTP 400: unsupported size")
            return good

        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "scratch"
            with patch.dict(os.environ, {"OPENROUTER_API_KEY": "k"}, clear=True), \
                 patch("imagegen.http_json", side_effect=fake):
                code = main(["generate", "--provider", "openrouter", "--prompt", "moss",
                             "--out", str(out), "--model", "vendor/bad,vendor/good",
                             "--submit"])
            # A partial failure is reported as a failure, but the paid image survives.
            self.assertEqual(code, 1)
            self.assertEqual(len(list(out.glob("*.png"))), 1)
            self.assertEqual(len(list(out.glob("*.json"))), 1)

    def test_all_models_failing_reports_failure_without_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "scratch"
            with patch.dict(os.environ, {"OPENROUTER_API_KEY": "k"}, clear=True), \
                 patch("imagegen.http_json", side_effect=RuntimeError("boom")):
                code = main(["generate", "--provider", "openrouter", "--prompt", "moss",
                             "--out", str(out), "--model", "vendor/a", "--submit"])
            self.assertEqual(code, 1)
            self.assertEqual(list(out.glob("*.png")), [])

    def test_missing_model_still_fails_before_any_request(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.dict(os.environ, {"OPENROUTER_API_KEY": "k"}, clear=True), \
             patch("imagegen.http_json") as http:
            with self.assertRaises(SystemExit) as error:
                main(["generate", "--provider", "openrouter", "--prompt", "x",
                      "--out", str(Path(directory) / "s"), "--submit"])
            self.assertEqual(error.exception.code, 1)
            http.assert_not_called()


if __name__ == "__main__":
    unittest.main()

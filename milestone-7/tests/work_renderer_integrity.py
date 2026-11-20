"""Check original Work sources, allowing only the documented AndroidX adapter extension."""
import hashlib
import json

def assert_work_sources(test, root):
    manifest = json.loads((root/'docs/6wb-work-renderer-manifest.json').read_text(encoding='utf-8'))
    for name, expected in manifest['normalized_sha256'].items():
        text = (root/name).read_text(encoding='utf-8').replace('\r', '')
        if name == 'src/cluster/hud6wb_kit.cpp':
            overlay = json.loads((root/'docs/6wb-androidx-adapter-overlay.json').read_text(encoding='utf-8'))
            for new, original in overlay['replacements']:
                test.assertEqual(text.count(new), 1, new)
                text = text.replace(new, original)
        test.assertEqual(hashlib.sha256(text.encode()).hexdigest(), expected, name)

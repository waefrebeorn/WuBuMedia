#!/usr/bin/env python3
"""Download RVC voice models from voice-models.com.

Uses the pre-organized voice directory structure as the download list.
Each subdirectory name under the voice root (e.g. D:/1aivoice/Music-AI-Voices/)
is treated as a voice to fetch. The script:

  1. Reads directory names from the voice root (and 1v1 model dir if present)
  2. Searches voice-models.com for matching models by name
  3. Downloads .pth + .index files to models/rvc/<voice_name>/ subdirectory
  4. Records what was downloaded/skipped/failed in models/training_assets_manifest.json

The directory names ARE the list — no external file list needed.

Usage:
  python3 tools/download_voice_models.py
  python3 tools/download_voice_models.py --dry-run
  python3 tools/download_voice_models.py --voice "Cartman"  (single voice)
  python3 tools/download_voice_models.py --root D:/1aivoice/Music-AI-Voices --out E:/rvc_models

License: WaefreBeorn-UMV3
"""
import os, sys, re, json, urllib.request, urllib.parse, time, hashlib

# Multi-drive support: user has D: and E: drives. D: holds the directory list,
# E: is the target for downloaded models (or same drive if --out matches --root).
DEFAULT_ROOT = "D:/1aivoice/Music-AI-Voices"
DEFAULT_OUT = "models/rvc"  # relative to WuBuMedia root

VM_URL = "https://voice-models.com"
SEARCH_URL = "https://voice-models.com/search?q="
MODEL_URL = "https://voice-models.com/model/"

def scrape_voice_models():
    """Scrape the first few browse pages of voice-models.com to build an index
    of {normalized_name: (model_id, download_url, size_mb)}.

    Returns dict mapping lowercase voice name -> list of matches.
    """
    import urllib.request
    index = {}  # name -> [(id, url, size_str)]
    seen_urls = set()

    for page in range(1, 6):  # first 5 pages ≈ 60 model listings
        url = f"{VM_URL}/?page={page}"
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            html = urllib.request.urlopen(req, timeout=20).read().decode('utf-8', errors='replace')
            # Find all model entries: [Name](url) ... download_url
            # Pattern: model/<id> and a download link (huggingface/google drive/mega)
            entries = re.findall(
                r'\[([^\]]+)\]\(https://voice-models\.com/model/([A-Za-z0-9]+)\)', html)
            # Find download URLs on the page
            dl_urls = re.findall(
                r'(https?://(?:huggingface\.co|drive\.google\.com|mega\.nz|mediafire\.com|www\.dropbox\.com)[^\s"<>]+)',
                html)
            # Also find size patterns
            sizes = re.findall(r'(\d+(?:\.\d+)?)\s*(MB|GB)', html)

            for name, mid in entries:
                dl_match = None
                for u in dl_urls:
                    # Try to match URL to model (they appear in same table row)
                    dl_match = urllib.parse.unquote(u)
                    break
                size_str = f"{sizes[len(index) % len(sizes)][0]} {sizes[len(index) % len(sizes)][1]}" if sizes else "??"

                key = name.lower().replace('(', '').replace(')', '').strip()
                if key not in index:
                    index[key] = []
                index[key].append((mid, dl_match, size_str))

        except Exception as e:
            print(f"  [page {page}] scrape error: {e}")

    return index


def scrape_model_detail(model_id):
    """Fetch a single model detail page to extract the real download URL.
    voice-models.com/model/<id> has a 'Download Link' and 'Related URL'.
    """
    url = f"{MODEL_URL}{model_id}"
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        html = urllib.request.urlopen(req, timeout=20).read().decode('utf-8', errors='replace')
        # Extract download links
        links = re.findall(r'(https?://(?:huggingface\.co|drive\.google\.com|mega\.nz|mediafire\.com|www\.dropbox\.com)[^\s"<>]+)', html)
        # Also check for easyaivoice run links which contain URL-encoded download URLs
        easyaivoice = re.findall(r'easyaivoice\.com/run\?url=([^"&\s]+)', html)
        for ea in easyaivoice:
            decoded = urllib.parse.unquote(ea)
            if 'huggingface' in decoded or 'google' in decoded or 'mega' in decoded:
                links.append(decoded)
        return links[0] if links else None
    except Exception as e:
        print(f"  model/{model_id} fetch error: {e}")
        return None


def normalize_voice_name(dirname):
    """Normalize a directory name for matching against voice-models.com listings."""
    # Remove epoch/quality suffixes, keep the core voice name
    s = dirname.lower()
    # Remove patterns like " (RVC) 400 Epoch", " 23.2k", " 100k"
    s = re.sub(r'\s*\(rvc\)\s*', '', s)
    s = re.sub(r'\s+\(?\d+[.,]?\d*\s*(?:epoch|epochs|k|steps)\)?\s*', '', s)
    s = re.sub(r'\s+\d+k\s*$', '', s)
    s = re.sub(r'\s+v\d+\s*$', '', s)
    s = re.sub(r'\s+$', '', s)
    return s.strip()


def extract_download_url(name, model_id, html_page_url):
    """From a model detail page or browse page, extract the best download URL."""
    if model_id:
        dl = scrape_model_detail(model_id)
        if dl:
            return dl
    # Fallback: from browse page HTML
    try:
        req = urllib.request.Request(html_page_url or VM_URL,
                                     headers={'User-Agent': 'Mozilla/5.0'})
        html = urllib.request.urlopen(req, timeout=20).read().decode('utf-8', errors='replace')
        links = re.findall(r'(https?://(?:huggingface\.co|drive\.google\.com|mega\.nz)[^\s"<>]+)', html)
        return links[0] if links else None
    except:
        return None


def download_file(url, dest_path):
    """Download a file with progress. Handles HuggingFace redirect URLs."""
    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
    # Handle Google Drive confirmation
    if 'drive.google.com' in url:
        url = url.replace('open?', 'uc?export=download&')
        # Google Drive virus-check bypass for large files
        url = url.replace('export=download&', 'export=download&id=')
        # Actually use the proper uc endpoint
        file_id_match = re.search(r'/file/d/([^/]+)/', url)
        if file_id_match:
            url = f"https://drive.google.com/uc?id={file_id_match.group(1)}&export=download"

    print(f"  Downloading from {url[:100]}...")
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req, timeout=300) as resp:
            total = int(resp.headers.get('Content-Length', 0))
            downloaded = 0
            with open(dest_path, 'wb') as f:
                while True:
                    chunk = resp.read(65536)
                    if not chunk:
                        break
                    f.write(chunk)
                    downloaded += len(chunk)
                    if total > 0 and downloaded % (total // 10 or 65536) == 0:
                        pct = downloaded * 100 / total
                        sys.stdout.write(f"\r    {pct:.0f}% ({downloaded//1024}KB/{total//1024}KB)")
                        sys.stdout.flush()
            sys.stdout.write("\n")
        size_mb = os.path.getsize(dest_path) / (1024*1024)
        print(f"    ✓ Saved: {dest_path} ({size_mb:.1f}MB)")
        return True
    except Exception as e:
        print(f"    ✗ FAILED: {e}")
        if os.path.exists(dest_path):
            os.remove(dest_path)
        return False


def get_voice_dirs(root):
    """Get all voice directory names from the root, including 1v1 model dirs."""
    voices = set()
    # Primary: Music-AI-Voices directory listing IS the voice list
    if os.path.isdir(root):
        for d in os.listdir(root):
            full = os.path.join(root, d)
            if os.path.isdir(full) and len(d) > 2 and not d.startswith('.'):
                voices.add(d)
    # Secondary: 1v1 model directory
    v1_model_dir = "/d/1v1 model"
    if os.path.isdir(v1_model_dir):
        for d in os.listdir(v1_model_dir):
            full = os.path.join(v1_model_dir, d)
            if os.path.isdir(full) and len(d) > 2 and not d.startswith('.'):
                voices.add(d)
    return sorted(voices)


def find_matching_models(voice_name, vm_index):
    """Find matching voice-models.com entries for a given voice directory name."""
    norm = normalize_voice_name(voice_name)
    matches = []

    # Direct substring match on the normalized index
    for index_name, entries in vm_index.items():
        # Match if the voice name appears in the index name or vice versa
        # e.g. "cartman" matches "eric cartman v2 rvc"
        if norm in index_name or index_name in norm:
            matches.extend([(name, mid, url, size)
                           for mid, url, size in entries
                           for name in [index_name]])

    return matches


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Download RVC voice models from voice-models.com")
    parser.add_argument("--root", default=DEFAULT_ROOT,
                        help=f"Voice directory root (default: {DEFAULT_ROOT})")
    parser.add_argument("--out", default=DEFAULT_OUT,
                        help=f"Output directory for models (default: {DEFAULT_OUT})")
    parser.add_argument("--voice", help="Download only this specific voice")
    parser.add_argument("--dry-run", action="store_true",
                        help="List what would be downloaded without fetching")
    parser.add_argument("--pages", type=int, default=5,
                        help="Number of voice-models.com browse pages to scrape (default 5)")
    args = parser.parse_args()

    # Normalize root path for the platform
    root = args.root
    if root.startswith("/d/"):
        root = "D:" + root[2:]
    elif root.startswith("/e/"):
        root = "E:" + root[2:]

    print(f"=== WuBuVoice Model Downloader ===")
    print(f"Voice root: {root}")
    print(f"Output dir: {args.out}")
    print(f"Dry run:    {args.dry_run}")
    print()

    # Get voice list from directory names
    voices = get_voice_dirs(root)
    if not voices:
        print(f"ERROR: No voice directories found in {root}")
        print("The directory names ARE the download list — each subdirectory should")
        print("be a voice name (e.g. 'Eric Cartman', 'Bart Simpson', 'Trump 68k').")
        return 1

    print(f"Found {len(voices)} voice directories to process:")
    for v in voices[:20]:
        print(f"  - {v}")
    if len(voices) > 20:
        print(f"  ... and {len(voices) - 20} more")
    print()

    # If single voice, filter
    if args.voice:
        voices = [v for v in voices if args.voice.lower() in v.lower()]
        if not voices:
            print(f"No voices matching '{args.voice}' found")
            return 1
        print(f"Filtered to: {voices}")
        print()

    if args.dry_run:
        print("=== DRY RUN — would search and download ===")
        for v in voices:
            print(f"  Would fetch: {v} → models/rvc/{v}/")
        return 0

    # Scrape voice-models.com index
    print(f"Scraping voice-models.com ({args.pages} pages)...")
    vm_index = scrape_voice_models()
    print(f"Indexed {sum(len(v) for v in vm_index.values())} models across {len(vm_index)} names\n")

    # Process each voice
    results = {"downloaded": [], "skipped": [], "failed": []}
    out_base = args.out

    for voice in voices:
        print(f"\n--- {voice} ---")
        matches = find_matching_models(voice, vm_index)

        if not matches:
            # Try direct search
            print(f"  No index match, trying direct search...")
            search_q = urllib.parse.quote_plus(voice)
            try:
                req = urllib.request.Request(f"{VM_URL}/search?q={search_q}",
                                             headers={'User-Agent': 'Mozilla/5.0'})
                html = urllib.request.urlopen(req, timeout=15).read().decode('utf-8', errors='replace')
                model_links = re.findall(r'/model/([A-Za-z0-9]+)', html)
                name_matches = re.findall(r'\[([^\]]+)\]\(https://voice-models\.com/model/[A-Za-z0-9]+\)', html)
                for nm in name_matches:
                    mid = model_links[name_matches.index(nm)] if model_links else None
                    dl = scrape_model_detail(mid) if mid else None
                    if dl:
                        matches.append((nm, mid, dl, "?? MB"))
            except Exception as e:
                print(f"  Search error: {e}")

        if not matches:
            print(f"  ⚠ No model found on voice-models.com for '{voice}'")
            results["failed"].append(voice)
            continue

        # Try the best match
        best = matches[0]
        dl_url = best[2]
        size_str = best[3]
        model_name = best[0]

        # Normalize output path
        out_dir = os.path.join(out_base, voice.replace(" ", "_").lower())
        os.makedirs(out_dir, exist_ok=True)

        # Determine local file name
        if 'huggingface.co' in dl_url:
            fname = dl_url.split('/')[-1].split('?')[0]
            # URL-decode
            fname = urllib.parse.unquote(fname)
        elif 'google' in dl_url:
            fname = f"{voice.replace(' ', '_')}.zip"
        else:
            fname = dl_url.split('/')[-1].split('?')[0]

        dest = os.path.join(out_dir, fname)

        if os.path.exists(dest) and os.path.getsize(dest) > 1000:
            print(f"  ✓ Already exists: {dest} ({os.path.getsize(dest)//1024}KB)")
            results["skipped"].append(f"{voice} ({fname})")
            continue

        print(f"  Match: {model_name} ({size_str})")
        print(f"  URL: {dl_url[:120]}...")

        ok = download_file(dl_url, dest)
        if ok:
            results["downloaded"].append(f"{voice} ({fname})")
        else:
            results["failed"].append(voice)

        time.sleep(0.5)  # be polite to the server

    # Write manifest
    manifest_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "models", "training_assets_manifest.json")
    manifest_path = os.path.normpath(manifest_path)
    with open(manifest_path, "w") as f:
        json.dump(results, f, indent=2)

    print(f"\n=== Summary ===")
    print(f"Downloaded: {len(results['downloaded'])}")
    for d in results["downloaded"]:
        print(f"  + {d}")
    print(f"Skipped (already exists): {len(results['skipped'])}")
    for s in results["skipped"]:
        print(f"  = {s}")
    print(f"Failed/missing: {len(results['failed'])}")
    for fail in results["failed"]:
        print(f"  - {fail}")
    print(f"\nManifest: {manifest_path}")
    return 0 if not results["failed"] else 1


if __name__ == "__main__":
    sys.exit(main())

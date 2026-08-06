#!/usr/bin/env python3
# SPDX-License-Identifier: WaefreBeorn-UMV3
"""wubu_merge.py — Multi-AGI Merge Protocol (Gold Coast Federation).

Phase 12: Multi-AGI Merge Protocol.

Implements a peer-to-peer knowledge + persona federation protocol
for merging WuBuDesk with other Gold Coast AGI implementations.

Based on research:
  * "Model Merging in LLMs" (arXiv:2408.07666) — parameter/knowledge aggregation
  * ModelSoup weighted averaging (https://arxiv.org/abs/2205.06160)
  * LOKA Protocol — decentralized ethical alignment (arXiv:2604.02369v1)
  * Coral Protocol — semantic alignment + verifiable meaning
  * "Toward Semantically Aligned Agent Communication" (arXiv:2604.02369v1)

The merge protocol operates in 4 phases:
  1. HELLO — Exchange identity + capabilities + verification tags
  2. SYNC — Exchange persona states, knowledge diffs, memory facts
  3. MERGE — Resolve conflicts via Devil's-Advocate trust scoring
  4. COMMIT — Persist merged knowledge, update local persona

Key principle: NO trust without verification. Every incoming claim is
checked against the Devil's-Advocate system before being accepted.

Storage:
  - knowledge/merge_peers.json — known AGI peers + their states
  - knowledge/merge_journal.jsonl — every merge operation logged

Usage:
  python src/wubu_merge.py --hello
  python src/wubu_merge.py --peers
  python src/wubu_merge.py --sync <peer_slug>
  python src/wubu_merge.py --import-bundle <path>
  python src/wubu_merge.py --export-bundle <peer_slug>

License: SPDX-License-Identifier: WaefreBeorn-UMV3
"""
import os
import sys
import json
import time
import hashlib
import sqlite3
import threading
import re
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

PEERS_FILE = os.path.join(ROOT, "knowledge", "merge_peers.json")
JOURNAL_FILE = os.path.join(ROOT, "knowledge", "merge_journal.jsonl")
WIKI_DB = os.path.join(ROOT, "knowledge", "wiki.db")

# Our AGI identity (the "face" we present to other AIs)
OUR_IDENTITY = {
    "name": "WuBuDesk-Cohost",
    "persona": "streaming co-host",
    "operator": "WaefreBeorn",
    "capabilities": [
        "voice_synthesis", "face_animation", "twitch_chat", "capture_optimization",
        "browser_cookies", "global_hotkeys", "knowledge_base", "memory_persistence",
        "persona_dynamics", "agora_control", "self_monitoring",
        "recursive_learning", "api_gateway",
    ],
    "hard_rules": [
        "never discuss healthcare / United Healthcare",
        "never dox anyone — delete PPI from past companies",
        "never fake tool results — always verify before reporting done",
    ],
    "version": "v8.0-agora",
    "research_base": "arxiv:2408.07666, arxiv:2604.02369v1, Coral/LOKA protocols",
}

# Trust levels for Devil's-Advocate scoring
TRUST_VERY_LOW = 0.2
TRUST_LOW = 0.4
TRUST_MEDIUM = 0.6
TRUST_HIGH = 0.8
TRUST_VERY_HIGH = 1.0


def _log_journal(entry_type, data):
    """Log a merge operation to the journal."""
    os.makedirs(os.path.dirname(JOURNAL_FILE), exist_ok=True)
    entry = {
        "timestamp": time.time(),
        "type": entry_type,
        "data": data,
    }
    with open(JOURNAL_FILE, "a", encoding="utf-8") as f:
        f.write(json.dumps(entry) + "\n")


def _hash_identity(identity):
    """Create a deterministic hash of our identity for verification."""
    canon = json.dumps(identity, sort_keys=True)
    return hashlib.sha256(canon.encode()).hexdigest()[:16]


def our_signature():
    """Our identity hash signature."""
    return _hash_identity(OUR_IDENTITY)


def _da_score(claim, source_trust=TRUST_MEDIUM):
    """Devil's-Advocate score for an incoming claim.

    Every claim is checked:
    - Does it conflict with hard rules? (immediate rejection)
    - Is the source trusted? (trust score)
    - Does it conflict with existing known facts? (reduced trust)
    - Is it a match of existing facts? (boosted trust)

    Returns (score, reason) where score is 0.0-1.0.
    """
    # Hard rule check
    rules = OUR_IDENTITY["hard_rules"]
    content = str(claim.get("content", "")) + str(claim.get("value", ""))
    content_lower = content.lower()

    for rule in rules:
        if "healthcare" in rule:
            healthcare_terms = ["healthcare", "united health", "uhc", "insurance"]
            if any(t in content_lower for t in healthcare_terms):
                return 0.0, "violates hard rule: no healthcare discussion"
        if "doxxing" in rule:
            # Only reject if there's actual PII of a real person
            pii_terms = ["address", "phone number", "email", "ssn", "social security"]
            if any(t in content_lower for t in pii_terms):
                if claim.get("source") not in ("boss", "repo", "tool"):
                    return 0.0, "violates hard rule: no doxxing PII"

    # Source-based trust
    source = claim.get("source", "unverified")
    source_weights = {
        "boss": 1.0,       # boss statement = highest trust
        "repo": 0.95,      # repo commit = very high trust
        "tool": 0.9,       # tool result = high trust
        "verified": 0.8,   # verified result = medium-high
        "research": 0.6,   # research = medium (needs verification)
        "unverified": 0.3, # unverified = low
    }
    base = source_weights.get(source, TRUST_MEDIUM)

    # Apply source trust multiplier
    score = base * source_trust
    reason = f"source={source}, source_trust={source_trust}"

    # Check for conflict/match with existing facts
    if "key" in claim:
        try:
            conn = sqlite3.connect(WIKI_DB, check_same_thread=False)
            existing = conn.execute(
                "SELECT value, confidence FROM facts WHERE key=?",
                (claim["key"],)
            ).fetchone()
            conn.close()
            if existing:
                if existing[0] != str(claim.get("value")):
                    reason += f"; conflicts with existing fact"
                    score *= 0.5  # reduce trust on conflict
                else:
                    reason += "; matches existing fact"
                    score = min(1.0, score + 0.2)  # boost on match
        except Exception:
            pass

    return min(1.0, max(0.0, score)), reason


class Peer:
    """Represents another AGI in the Gold Coast network."""

    def __init__(self, slug, identity=None, trust=TRUST_MEDIUM,
                 endpoint=None):
        self.slug = slug
        self.identity = identity or {}
        self.trust = trust
        self.endpoint = endpoint  # http://host:port for API calls
        self.last_seen = time.time()
        self.last_sync = 0.0
        self.signature = _hash_identity(identity) if identity else None

    def to_dict(self):
        return {
            "slug": self.slug,
            "identity": self.identity,
            "trust": self.trust,
            "endpoint": self.endpoint,
            "last_seen": self.last_seen,
            "last_sync": self.last_sync,
            "signature": self.signature,
        }

    @classmethod
    def from_dict(cls, d):
        p = cls(d["slug"], d.get("identity", {}), d.get("trust", TRUST_MEDIUM),
                d.get("endpoint"))
        p.last_seen = d.get("last_seen", time.time())
        p.last_sync = d.get("last_sync", 0.0)
        p.signature = d.get("signature")
        return p


class MergeProtocol:
    """Multi-AGI merge protocol implementation.

    Phases:
    1. HELLO — identity exchange
    2. SYNC — state sync
    3. MERGE — knowledge/fact merge with Devil's-Advocate
    4. COMMIT — persist merged state
    """

    def __init__(self):
        self.peers = self._load_peers()

    def _load_peers(self):
        """Load known peers from JSON."""
        try:
            with open(PEERS_FILE, "r") as f:
                data = json.load(f)
            return {slug: Peer.from_dict(d) for slug, d in data.items()}
        except (FileNotFoundError, json.JSONDecodeError):
            return {}

    def _save_peers(self):
        """Save peers to JSON."""
        os.makedirs(os.path.dirname(PEERS_FILE), exist_ok=True)
        data = {slug: p.to_dict() for slug, p in self.peers.items()}
        with open(PEERS_FILE, "w") as f:
            json.dump(data, f, indent=2)

    def hello(self):
        """Return our identity packet for HELLO exchange."""
        identity = dict(OUR_IDENTITY)
        identity["signature"] = our_signature()
        identity["timestamp"] = time.time()
        return identity

    def add_peer(self, slug, identity):
        """Add or update a peer from a HELLO exchange."""
        sig = identity.pop("signature", None)
        expected_sig = _hash_identity(identity)

        if sig and sig != expected_sig:
            _log_journal("peer_rejected", {
                "slug": slug, "reason": "signature mismatch"
            })
            return None

        peer = Peer(slug, identity, trust=TRUST_MEDIUM if sig else TRUST_LOW)
        peer.last_seen = time.time()
        self.peers[slug] = peer
        self._save_peers()
        _log_journal("peer_added", {"slug": slug, "identity": identity})
        return peer

    def _insert_fact(self, fact, source_label):
        """Insert a fact into the wiki DB. Returns True on success."""
        try:
            conn = sqlite3.connect(WIKI_DB)
            conn.execute(
                "INSERT OR REPLACE INTO facts (key, value, confidence, "
                "source_slug, updated) VALUES (?, ?, ?, ?, ?)",
                (fact["key"], str(fact["value"]),
                 min(1.0, fact.get("confidence", 1.0)),
                 source_label, time.time())
            )
            conn.commit()
            conn.close()
            return True
        except Exception as e:
            print(f"  fact insert error: {e}", file=sys.stderr)
            return False

    def sync_with_peer(self, peer_slug):
        """Sync knowledge + facts with a peer.

        Returns a diff of what was exchanged.
        """
        if peer_slug not in self.peers:
            _log_journal("sync_failed", {
                "peer": peer_slug, "reason": "unknown peer"
            })
            return {"error": "unknown peer"}

        peer = self.peers[peer_slug]
        peer.last_seen = time.time()

        # Fetch peer's facts from their API
        facts = []
        if peer.endpoint:
            try:
                import urllib.request
                url = f"{peer.endpoint}/api/wiki/facts"
                req = urllib.request.Request(url)
                resp = urllib.request.urlopen(req, timeout=10)
                remote_facts = json.loads(resp.read())
                facts = remote_facts.get("facts", [])
            except Exception as e:
                _log_journal("sync_fetch_error", {
                    "peer": peer_slug, "error": str(e)
                })

        # Apply Devil's-Advocate to each fact
        accepted = 0
        rejected = 0
        for fact in facts:
            score, reason = _da_score(fact, peer.trust)
            if score > 0.0:
                ok = self._insert_fact(fact, f"peer:{peer_slug}")
                if ok:
                    accepted += 1
                else:
                    rejected += 1
            else:
                rejected += 1
                _log_journal("fact_rejected", {
                    "peer": peer_slug, "fact": fact, "reason": reason
                })

        # Adjust peer trust
        if facts:
            if accepted > 0:
                peer.trust = min(1.0, peer.trust + 0.05 * (accepted / len(facts)))
            elif rejected > 0:
                peer.trust = max(TRUST_VERY_LOW, peer.trust - 0.1)

        peer.last_sync = time.time()
        self._save_peers()

        result = {
            "peer": peer_slug,
            "facts_synced": len(facts),
            "facts_accepted": accepted,
            "facts_rejected": rejected,
            "peer_trust": peer.trust,
        }
        _log_journal("sync_complete", result)
        return result

    def merge_peer(self, peer_slug):
        """Full merge with a peer: sync + persona alignment.

        4-phase merge:
        1. HELLO — exchange identity (already done when peer added)
        2. SYNC — exchange facts, articles, memory facts
        3. MERGE — resolve conflicts via Devil's-Advocate
        4. COMMIT — persist merged state, update persona
        """
        if peer_slug not in self.peers:
            return {"error": "unknown peer"}

        peer = self.peers[peer_slug]
        _log_journal("merge_started", {"peer": peer_slug})

        # Phase 2: Sync
        sync_result = self.sync_with_peer(peer_slug)

        # Phase 3: Merge persona (mood model alignment)
        persona_merge = self._merge_persona(peer)

        # Phase 4: Commit
        commit = {
            "peer": peer_slug,
            "sync": sync_result,
            "persona": persona_merge,
            "timestamp": time.time(),
        }
        _log_journal("merge_complete", commit)
        return commit

    def _merge_persona(self, peer):
        """Merge persona states via consensus scoring.

        We don't overwrite our persona — instead, we note alignment
        scores and adjust trust. This is the LOKA/Coral Protocol approach:
        agents reach agreement through justification, not overwrite.
        """
        peer_identity = peer.identity
        alignment = {}

        # Check hard rule alignment
        our_rules = set(OUR_IDENTITY["hard_rules"])
        peer_rules = set(peer_identity.get("hard_rules", []))
        shared_rules = our_rules & peer_rules
        conflicting_rules = our_rules - peer_rules

        alignment["hard_rules_shared"] = len(shared_rules)
        alignment["hard_rules_conflicting"] = len(conflicting_rules)

        # Check capability overlap
        our_caps = set(OUR_IDENTITY["capabilities"])
        peer_caps = set(peer_identity.get("capabilities", []))
        alignment["caps_overlap"] = list(our_caps & peer_caps)
        alignment["caps_unique_to_peer"] = list(peer_caps - our_caps)

        # Adjust trust based on alignment
        if conflicting_rules:
            peer.trust = max(TRUST_VERY_LOW, peer.trust - 0.2)
            alignment["trust_adjustment"] = "decreased (rule conflict)"
        elif len(shared_rules) >= len(our_rules):
            peer.trust = min(1.0, peer.trust + 0.1)
            alignment["trust_adjustment"] = "increased (full rule alignment)"
        else:
            alignment["trust_adjustment"] = "unchanged"

        return alignment

    def list_peers(self):
        """List all known peers with their status."""
        return [p.to_dict() for p in self.peers.values()]

    def import_bundle(self, bundle_path):
        """Import a signed knowledge bundle from another AGI.

        Bundle format: JSON with {identity, signature, articles, facts, timestamp}
        """
        try:
            with open(bundle_path, "r") as f:
                bundle = json.load(f)
        except Exception as e:
            return {"error": f"cannot load bundle: {e}"}

        # Verify signature
        identity = bundle.get("identity", {})
        expected_sig = _hash_identity(identity)
        actual_sig = bundle.get("signature", "")
        if actual_sig != expected_sig:
            _log_journal("bundle_rejected", {
                "reason": "signature mismatch",
                "file": bundle_path
            })
            return {"error": "signature verification failed"}

        # Add as peer
        peer_slug = bundle.get("peer_slug", expected_sig[:8])
        self.add_peer(peer_slug, identity)

        # Merge facts via Devil's-Advocate
        accepted = 0
        rejected = 0
        for fact in bundle.get("facts", []):
            score, reason = _da_score(fact, TRUST_HIGH)
            if score > 0.0:
                ok = self._insert_fact(fact, f"bundle:{peer_slug}")
                if ok:
                    accepted += 1
                else:
                    rejected += 1
            else:
                rejected += 1

        # Merge articles into wiki
        from wubu_wiki import Wiki
        wiki = Wiki()
        articles_merged = 0
        for article in bundle.get("articles", []):
            changed = wiki.upsert(
                slug=article.get("slug", "imported"),
                content=article.get("content", ""),
                title=article.get("title"),
                tags=article.get("tags"),
                source=f"bundle:{peer_slug}",
                source_url=article.get("source_url"),
            )
            if changed:
                articles_merged += 1

        result = {
            "peer": peer_slug,
            "facts_accepted": accepted,
            "facts_rejected": rejected,
            "articles_merged": articles_merged,
            "signature_verified": True,
        }
        _log_journal("bundle_imported", result)
        return result


def export_bundle(peer_slug="wu-bu-desk"):
    """Export our knowledge as a signed bundle for other AIs."""
    identity = dict(OUR_IDENTITY)
    signature = our_signature()

    # Export wiki facts
    facts = []
    try:
        conn = sqlite3.connect(WIKI_DB)
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            "SELECT key, value, confidence, source_slug FROM facts"
        ).fetchall()
        for r in rows:
            facts.append({
                "key": r["key"],
                "value": r["value"],
                "confidence": r["confidence"],
                "source": r["source_slug"],
            })
        conn.close()
    except Exception:
        pass

    # Export wiki articles (last 50)
    articles = []
    try:
        from wubu_wiki import Wiki
        wiki = Wiki()
        all_articles = wiki.list_articles(limit=50)
        for a in all_articles:
            article = wiki.get(a["slug"])
            if article:
                articles.append({
                    "slug": a["slug"],
                    "title": article["title"],
                    "content": article["content"],
                    "tags": article["tags"],
                    "source_url": article.get("source_url"),
                })
    except Exception:
        pass

    bundle = {
        "peer_slug": peer_slug,
        "identity": identity,
        "signature": signature,
        "timestamp": time.time(),
        "facts": facts,
        "articles": articles,
    }
    return bundle


if __name__ == "__main__":
    mp = MergeProtocol()

    if len(sys.argv) > 1 and sys.argv[1] == "--hello":
        print(json.dumps(mp.hello(), indent=2))

    elif len(sys.argv) > 1 and sys.argv[1] == "--peers":
        print(json.dumps(mp.list_peers(), indent=2))

    elif len(sys.argv) > 1 and sys.argv[1] == "--sync":
        peer_slug = sys.argv[2] if len(sys.argv) > 2 else None
        if peer_slug:
            result = mp.sync_with_peer(peer_slug)
        else:
            results = []
            for slug in mp.peers:
                results.append(mp.sync_with_peer(slug))
            result = {"results": results}
        print(json.dumps(result, indent=2))

    elif len(sys.argv) > 1 and sys.argv[1] == "--merge-peer":
        peer_slug = sys.argv[2]
        result = mp.merge_peer(peer_slug)
        print(json.dumps(result, indent=2))

    elif len(sys.argv) > 1 and sys.argv[1] == "--export-bundle":
        slug = sys.argv[2] if len(sys.argv) > 2 else "wu-bu-desk"
        bundle = export_bundle(slug)
        path = os.path.join(ROOT, "knowledge", f"bundle_{slug}.json")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            json.dump(bundle, f, indent=2)
        print(f"Bundle exported to {path}")
        print(f"  Facts: {len(bundle['facts'])}")
        print(f"  Articles: {len(bundle['articles'])}")
        print(f"  Signature: {bundle['signature']}")

    elif len(sys.argv) > 1 and sys.argv[1] == "--import-bundle":
        path = sys.argv[2]
        result = mp.import_bundle(path)
        print(json.dumps(result, indent=2))

    elif len(sys.argv) > 1 and sys.argv[1] == "--add-peer":
        slug = sys.argv[2]
        identity = {
            "name": slug,
            "persona": "agora",
            "capabilities": [],
            "hard_rules": OUR_IDENTITY["hard_rules"],
        }
        peer = mp.add_peer(slug, identity)
        if peer:
            print(f"Added peer: {slug} (trust={peer.trust})")
        else:
            print(f"Rejected peer: {slug}")

    else:
        print("Usage: wubu_merge.py [--hello|--peers|--sync <slug>|"
              "--merge-peer <slug>|--export-bundle <slug>|"
              "--import-bundle <path>|--add-peer <slug>]")
        print(f"\nOur identity:")
        print(json.dumps(mp.hello(), indent=2))

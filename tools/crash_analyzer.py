#!/usr/bin/env python3
"""
Reactor Crash Dump Analyzer

Decodes MicroReactor black box dumps and generates human-readable reports.
Can parse ELF files for symbol resolution.

Usage:
    crash_analyzer.py dump.bin                     # Analyze binary dump
    crash_analyzer.py dump.bin --elf firmware.elf # With symbol resolution
    crash_analyzer.py dump.bin --output report.md # Generate markdown report
    crash_analyzer.py dump.bin --mermaid          # Generate Mermaid sequence diagram
"""

import argparse
import struct
import json
from dataclasses import dataclass
from typing import List, Dict, Optional
from datetime import datetime

# Black box entry structure (must match ur_types.h)
# typedef struct {
#     uint16_t entity_id;
#     uint16_t signal_id;
#     uint16_t src_id;
#     uint16_t state;
#     uint32_t timestamp;
# } ur_blackbox_entry_t;

BLACKBOX_ENTRY_SIZE = 12

# System signals
SYSTEM_SIGNALS = {
    0x0000: "SIG_NONE",
    0x0001: "SIG_SYS_INIT",
    0x0002: "SIG_SYS_ENTRY",
    0x0003: "SIG_SYS_EXIT",
    0x0004: "SIG_SYS_TICK",
    0x0005: "SIG_SYS_TIMEOUT",
    0x0006: "SIG_SYS_DYING",
    0x0007: "SIG_SYS_REVIVE",
    0x0008: "SIG_SYS_RESET",
    0x0009: "SIG_SYS_SUSPEND",
    0x000A: "SIG_SYS_RESUME",
    0x0020: "SIG_PARAM_CHANGED",
    0x0021: "SIG_PARAM_READY",
    0x0100: "SIG_USER_BASE",
}


@dataclass
class BlackboxEntry:
    """Single black box entry"""
    entity_id: int
    signal_id: int
    src_id: int
    state: int
    timestamp: int

    @classmethod
    def from_bytes(cls, data: bytes) -> 'BlackboxEntry':
        """Parse from binary data"""
        if len(data) < BLACKBOX_ENTRY_SIZE:
            raise ValueError(f"Insufficient data: {len(data)} < {BLACKBOX_ENTRY_SIZE}")

        entity_id, signal_id, src_id, state, timestamp = struct.unpack(
            '<HHHHI', data[:BLACKBOX_ENTRY_SIZE]
        )

        return cls(
            entity_id=entity_id,
            signal_id=signal_id,
            src_id=src_id,
            state=state,
            timestamp=timestamp
        )

    def signal_name(self, signal_map: Dict[int, str] = None) -> str:
        """Get signal name"""
        if signal_map and self.signal_id in signal_map:
            return signal_map[self.signal_id]
        if self.signal_id in SYSTEM_SIGNALS:
            return SYSTEM_SIGNALS[self.signal_id]
        return f"SIG_0x{self.signal_id:04X}"


@dataclass
class CrashDump:
    """Complete crash dump"""
    entries: List[BlackboxEntry]
    metadata: Dict

    @classmethod
    def from_hex(cls, hex_string: str) -> 'CrashDump':
        """Parse from hex string"""
        data = bytes.fromhex(hex_string.replace(' ', '').replace('\n', ''))
        return cls.from_bytes(data)

    @classmethod
    def from_bytes(cls, data: bytes) -> 'CrashDump':
        """Parse from binary data"""
        entries = []
        offset = 0

        while offset + BLACKBOX_ENTRY_SIZE <= len(data):
            entry = BlackboxEntry.from_bytes(data[offset:])
            # Skip empty entries
            if entry.entity_id != 0 or entry.signal_id != 0:
                entries.append(entry)
            offset += BLACKBOX_ENTRY_SIZE

        return cls(entries=entries, metadata={
            "total_entries": len(entries),
            "data_size": len(data),
            "parsed_at": datetime.now().isoformat()
        })

    @classmethod
    def from_file(cls, filepath: str) -> 'CrashDump':
        """Parse from file (binary or hex)"""
        with open(filepath, 'rb') as f:
            data = f.read()

        # Try to detect if it's hex
        try:
            text = data.decode('ascii').strip()
            if all(c in '0123456789abcdefABCDEF \n' for c in text):
                return cls.from_hex(text)
        except:
            pass

        return cls.from_bytes(data)


class ELFSymbols:
    """ELF symbol resolver (simplified)"""

    def __init__(self, elf_path: str):
        self.symbols: Dict[int, str] = {}
        self.signal_names: Dict[int, str] = {}
        self.entity_names: Dict[int, str] = {}
        self.state_names: Dict[int, str] = {}

        try:
            self._parse_elf(elf_path)
        except Exception as e:
            print(f"Warning: Could not parse ELF: {e}")

    def _parse_elf(self, path: str):
        """Parse ELF for symbols (basic implementation)"""
        try:
            from elftools.elf.elffile import ELFFile
            from elftools.elf.sections import SymbolTableSection

            with open(path, 'rb') as f:
                elf = ELFFile(f)

                for section in elf.iter_sections():
                    if isinstance(section, SymbolTableSection):
                        for symbol in section.iter_symbols():
                            name = symbol.name
                            addr = symbol['st_value']

                            # Look for signal definitions
                            if name.startswith('SIG_'):
                                self.signal_names[addr] = name
                            # Look for entity IDs
                            elif name.startswith('ID_') or name.endswith('_ID'):
                                self.entity_names[addr] = name
                            # Look for state IDs
                            elif name.startswith('STATE_'):
                                self.state_names[addr] = name

                            self.symbols[addr] = name

        except ImportError:
            print("Note: pyelftools not installed, symbol resolution disabled")
        except Exception as e:
            print(f"Warning: ELF parsing failed: {e}")

    def get_signal_name(self, sig_id: int) -> Optional[str]:
        return self.signal_names.get(sig_id)

    def get_entity_name(self, ent_id: int) -> Optional[str]:
        return self.entity_names.get(ent_id)

    def get_state_name(self, state_id: int) -> Optional[str]:
        return self.state_names.get(state_id)


class CrashAnalyzer:
    """Crash dump analyzer"""

    def __init__(self, dump: CrashDump, symbols: ELFSymbols = None):
        self.dump = dump
        self.symbols = symbols
        self.signal_map: Dict[int, str] = {}

        if symbols:
            self.signal_map.update(symbols.signal_names)

    def get_entity_name(self, ent_id: int) -> str:
        """Get entity name"""
        if self.symbols:
            name = self.symbols.get_entity_name(ent_id)
            if name:
                return name
        return f"Entity_{ent_id}"

    def get_signal_name(self, sig_id: int) -> str:
        """Get signal name"""
        if self.symbols:
            name = self.symbols.get_signal_name(sig_id)
            if name:
                return name
        if sig_id in SYSTEM_SIGNALS:
            return SYSTEM_SIGNALS[sig_id]
        return f"SIG_0x{sig_id:04X}"

    def get_state_name(self, state_id: int) -> str:
        """Get state name"""
        if self.symbols:
            name = self.symbols.get_state_name(state_id)
            if name:
                return name
        return f"State_{state_id}"

    def analyze(self) -> Dict:
        """Perform analysis and return results"""
        results = {
            "summary": {
                "total_events": len(self.dump.entries),
                "unique_entities": len(set(e.entity_id for e in self.dump.entries)),
                "unique_signals": len(set(e.signal_id for e in self.dump.entries)),
            },
            "timeline": [],
            "entities": {},
            "potential_issues": [],
        }

        # Build timeline
        for i, entry in enumerate(self.dump.entries):
            event = {
                "index": i,
                "timestamp_ms": entry.timestamp,
                "entity": self.get_entity_name(entry.entity_id),
                "entity_id": entry.entity_id,
                "signal": self.get_signal_name(entry.signal_id),
                "signal_id": entry.signal_id,
                "source": self.get_entity_name(entry.src_id),
                "source_id": entry.src_id,
                "state": self.get_state_name(entry.state),
                "state_id": entry.state,
            }
            results["timeline"].append(event)

            # Track per-entity stats
            ent_name = event["entity"]
            if ent_name not in results["entities"]:
                results["entities"][ent_name] = {
                    "signal_count": 0,
                    "state_changes": 0,
                    "last_state": None,
                    "signals_received": []
                }

            ent_stats = results["entities"][ent_name]
            ent_stats["signal_count"] += 1
            ent_stats["signals_received"].append(event["signal"])

            if ent_stats["last_state"] != entry.state:
                ent_stats["state_changes"] += 1
                ent_stats["last_state"] = entry.state

        # Detect potential issues
        self._detect_issues(results)

        return results

    def _detect_issues(self, results: Dict):
        """Detect potential problems from the trace"""
        issues = results["potential_issues"]

        # Check for rapid state changes
        for ent_name, stats in results["entities"].items():
            if stats["state_changes"] > len(self.dump.entries) / 4:
                issues.append({
                    "type": "rapid_state_changes",
                    "entity": ent_name,
                    "message": f"{ent_name} had {stats['state_changes']} state changes "
                              f"in {len(self.dump.entries)} events - possible thrashing"
                })

        # Check for SIG_SYS_DYING
        dying_events = [e for e in self.dump.entries if e.signal_id == 0x0006]
        for event in dying_events:
            issues.append({
                "type": "entity_dying",
                "entity": self.get_entity_name(event.entity_id),
                "timestamp": event.timestamp,
                "message": f"{self.get_entity_name(event.entity_id)} "
                          f"reported dying at t={event.timestamp}ms"
            })

        # Check for timeout signals
        timeout_events = [e for e in self.dump.entries if e.signal_id == 0x0005]
        if len(timeout_events) > 3:
            issues.append({
                "type": "multiple_timeouts",
                "count": len(timeout_events),
                "message": f"Multiple timeout events ({len(timeout_events)}) "
                          "- possible stuck operations"
            })

        # Check for signal storm (many signals in short time)
        if len(self.dump.entries) >= 2:
            first_ts = self.dump.entries[0].timestamp
            last_ts = self.dump.entries[-1].timestamp
            duration = last_ts - first_ts if last_ts > first_ts else 1

            rate = len(self.dump.entries) * 1000 / duration  # signals per second
            if rate > 1000:
                issues.append({
                    "type": "signal_storm",
                    "rate": rate,
                    "message": f"Very high signal rate ({rate:.0f}/sec) - "
                              "possible infinite loop"
                })

    def generate_text_report(self) -> str:
        """Generate text report"""
        results = self.analyze()
        lines = []

        lines.append("=" * 60)
        lines.append("MicroReactor Crash Dump Analysis")
        lines.append("=" * 60)
        lines.append("")

        # Summary
        lines.append("## Summary")
        lines.append(f"Total events: {results['summary']['total_events']}")
        lines.append(f"Unique entities: {results['summary']['unique_entities']}")
        lines.append(f"Unique signals: {results['summary']['unique_signals']}")
        lines.append("")

        # Issues
        if results["potential_issues"]:
            lines.append("## Potential Issues")
            for issue in results["potential_issues"]:
                lines.append(f"  - [{issue['type']}] {issue['message']}")
            lines.append("")

        # Timeline
        lines.append("## Event Timeline (last 50 events)")
        lines.append("-" * 60)

        for event in results["timeline"][-50:]:
            lines.append(
                f"[{event['timestamp_ms']:8d}ms] "
                f"{event['entity']:15s} <- {event['signal']:25s} "
                f"from {event['source']:15s} (state={event['state']})"
            )

        lines.append("")

        # Entity stats
        lines.append("## Entity Statistics")
        for ent_name, stats in results["entities"].items():
            lines.append(f"  {ent_name}:")
            lines.append(f"    Signals received: {stats['signal_count']}")
            lines.append(f"    State changes: {stats['state_changes']}")

        return "\n".join(lines)

    def generate_mermaid(self) -> str:
        """Generate Mermaid sequence diagram"""
        results = self.analyze()
        lines = []

        lines.append("```mermaid")
        lines.append("sequenceDiagram")

        # Add participants
        entities = sorted(results["entities"].keys())
        for ent in entities:
            lines.append(f"    participant {ent}")

        # Add signals (last 30 to keep diagram readable)
        for event in results["timeline"][-30:]:
            src = event["source"]
            dst = event["entity"]
            sig = event["signal"]

            if src != dst:
                lines.append(f"    {src}->>+{dst}: {sig}")
            else:
                lines.append(f"    Note right of {dst}: {sig}")

        lines.append("```")

        return "\n".join(lines)

    def generate_json(self) -> str:
        """Generate JSON report"""
        return json.dumps(self.analyze(), indent=2)


def main():
    parser = argparse.ArgumentParser(description='MicroReactor Crash Dump Analyzer')
    parser.add_argument('dump_file', help='Crash dump file (binary or hex)')
    parser.add_argument('--elf', help='ELF file for symbol resolution')
    parser.add_argument('--output', '-o', help='Output file')
    parser.add_argument('--format', '-f', choices=['text', 'json', 'mermaid'],
                       default='text', help='Output format')
    parser.add_argument('--mermaid', action='store_true',
                       help='Generate Mermaid diagram')

    args = parser.parse_args()

    # Load dump
    try:
        dump = CrashDump.from_file(args.dump_file)
        print(f"Loaded {len(dump.entries)} events from {args.dump_file}")
    except Exception as e:
        print(f"Error loading dump: {e}")
        return 1

    # Load symbols if provided
    symbols = None
    if args.elf:
        symbols = ELFSymbols(args.elf)

    # Analyze
    analyzer = CrashAnalyzer(dump, symbols)

    # Generate output
    if args.mermaid or args.format == 'mermaid':
        output = analyzer.generate_mermaid()
    elif args.format == 'json':
        output = analyzer.generate_json()
    else:
        output = analyzer.generate_text_report()

    # Write output
    if args.output:
        with open(args.output, 'w') as f:
            f.write(output)
        print(f"Report written to {args.output}")
    else:
        print(output)

    return 0


if __name__ == '__main__':
    exit(main())

#!/usr/bin/env python3

import argparse
import csv
import sys
from collections import defaultdict
from pathlib import Path


DEFAULT_TRACE = Path(__file__).resolve().parents[1] / "results" / "latency_trace.csv"

REQUIRED_FIELDS = (
    "initiator_id",
    "target_id",
    "command",
    "address",
    "data",
    "start_time_ns",
    "delay_ns",
    "end_time_ns",
    "decoded_port",
    "masked_address",
    "data_length",
    "response_status",
)

FLOAT_FIELDS = ("start_time_ns", "delay_ns", "end_time_ns")
INT_FIELDS = ("data_length", "decoded_port")
DEDUP_IDENTICAL_FIELDS = (
    "initiator_id",
    "target_id",
    "command",
    "address",
    "masked_address",
    "data",
    "start_time_ns",
    "delay_ns",
    "end_time_ns",
    "decoded_port",
    "data_length",
    "response_status",
)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate an architecture-oriented latency report for examples/lt."
    )
    parser.add_argument(
        "--trace",
        default=DEFAULT_TRACE,
        type=Path,
        help=f"CSV trace path. Defaults to {DEFAULT_TRACE}",
    )
    parser.add_argument(
        "--initiator",
        action="append",
        default=[],
        help="Keep only rows with this initiator_id. Can be repeated.",
    )
    parser.add_argument(
        "--exclude-initiator",
        action="append",
        default=[],
        help="Exclude rows with this initiator_id. Can be repeated.",
    )
    parser.add_argument(
        "--target",
        action="append",
        default=[],
        help="Keep only rows with this target_id. Can be repeated.",
    )
    parser.add_argument(
        "--command",
        action="append",
        default=[],
        help="Keep only rows with this command, e.g. READ or WRITE. Can be repeated.",
    )
    parser.add_argument(
        "--max-start-time-ns",
        type=float,
        help="Keep only rows with start_time_ns <= this value.",
    )
    parser.add_argument(
        "--min-start-time-ns",
        type=float,
        help="Keep only rows with start_time_ns >= this value.",
    )
    parser.add_argument(
        "--dedup-identical",
        action="store_true",
        help="Remove fully identical transaction rows before filtering and reporting.",
    )
    return parser.parse_args()


def parse_hex(value):
    if value is None:
        return None

    text = str(value).strip()
    if not text:
        return None

    try:
        return int(text, 16)
    except ValueError:
        return None


def parse_float(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def parse_int(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def load_rows(trace_path):
    if not trace_path.exists():
        print(f"error: CSV trace not found: {trace_path}", file=sys.stderr)
        print(
            "hint: run `renode-test examples/lt/lt.robot` first.",
            file=sys.stderr,
        )
        raise SystemExit(1)

    with trace_path.open(newline="") as csv_file:
        reader = csv.DictReader(csv_file)
        fieldnames = reader.fieldnames or []
        missing_fields = [field for field in REQUIRED_FIELDS if field not in fieldnames]
        if missing_fields:
            print(
                "error: CSV trace is missing required fields: "
                + ", ".join(missing_fields),
                file=sys.stderr,
            )
            raise SystemExit(1)

        rows = []
        for row_number, row in enumerate(reader, start=2):
            row["_row_number"] = row_number

            for field in FLOAT_FIELDS:
                row[field] = parse_float(row.get(field))

            for field in INT_FIELDS:
                row[field] = parse_int(row.get(field))

            rows.append(row)

    return rows


def format_number(value):
    if value is None:
        return "NA"
    return f"{value:.3f}"


def format_hex(value):
    if value is None:
        return "NA"
    return f"0x{value:016X}"


def filter_rows(rows, args):
    initiators = {str(value) for value in args.initiator}
    excluded_initiators = {str(value) for value in args.exclude_initiator}
    targets = {str(value) for value in args.target}
    commands = {str(value).upper() for value in args.command}

    filtered = []
    for row in rows:
        start_time = row.get("start_time_ns")

        if initiators and row.get("initiator_id") not in initiators:
            continue
        if excluded_initiators and row.get("initiator_id") in excluded_initiators:
            continue
        if targets and row.get("target_id") not in targets:
            continue
        if commands and str(row.get("command", "")).upper() not in commands:
            continue
        if args.min_start_time_ns is not None and (
            start_time is None or start_time < args.min_start_time_ns
        ):
            continue
        if args.max_start_time_ns is not None and (
            start_time is None or start_time > args.max_start_time_ns
        ):
            continue

        filtered.append(row)

    return filtered


def dedup_identical_rows(rows):
    seen = set()
    deduplicated = []
    for row in rows:
        key = tuple(row.get(field) for field in DEDUP_IDENTICAL_FIELDS)
        if key in seen:
            continue

        seen.add(key)
        deduplicated.append(row)

    return deduplicated


def active_filters(args):
    filters = []
    if args.initiator:
        filters.append("initiator_id in [" + ", ".join(args.initiator) + "]")
    if args.exclude_initiator:
        filters.append(
            "initiator_id not in [" + ", ".join(args.exclude_initiator) + "]"
        )
    if args.target:
        filters.append("target_id in [" + ", ".join(args.target) + "]")
    if args.command:
        filters.append("command in [" + ", ".join(value.upper() for value in args.command) + "]")
    if args.min_start_time_ns is not None:
        filters.append(f"start_time_ns >= {args.min_start_time_ns:g}")
    if args.max_start_time_ns is not None:
        filters.append(f"start_time_ns <= {args.max_start_time_ns:g}")

    return "; ".join(filters) if filters else "none"


def summarize_numeric(rows, group_keys):
    groups = defaultdict(list)
    for row in rows:
        delay = row.get("delay_ns")
        if delay is None:
            continue

        key = tuple(row.get(group_key, "") for group_key in group_keys)
        groups[key].append(delay)

    summary_rows = []
    for key, values in sorted(groups.items(), key=lambda item: tuple(str(v) for v in item[0])):
        summary_rows.append(
            list(key)
            + [
                len(values),
                format_number(sum(values) / len(values)),
                format_number(min(values)),
                format_number(max(values)),
            ]
        )

    return summary_rows


def print_table(title, headers, rows):
    print(f"\n== {title} ==")
    if not rows:
        print("(none)")
        return

    string_rows = [[str(value) for value in row] for row in rows]
    widths = [
        max(len(str(header)), *(len(row[index]) for row in string_rows))
        for index, header in enumerate(headers)
    ]

    header_line = "  ".join(str(header).ljust(widths[index]) for index, header in enumerate(headers))
    divider = "  ".join("-" * width for width in widths)
    print(header_line)
    print(divider)

    for row in string_rows:
        print("  ".join(value.ljust(widths[index]) for index, value in enumerate(row)))


def print_overview(rows, raw_count, analyzed_count, deduplicated_count, filters):
    starts = [row["start_time_ns"] for row in rows if row["start_time_ns"] is not None]
    ends = [row["end_time_ns"] for row in rows if row["end_time_ns"] is not None]
    delays = [row["delay_ns"] for row in rows if row["delay_ns"] is not None]

    first_start = min(starts) if starts else None
    last_end = max(ends) if ends else None
    observed_time = last_end - first_start if first_start is not None and last_end is not None else None
    avg_delay = sum(delays) / len(delays) if delays else None

    print_table(
        "Overview",
        ("metric", "value"),
        (
            ("raw_transactions", raw_count),
            ("analyzed_transactions", analyzed_count),
            ("deduplicated_transactions", deduplicated_count),
            ("filtered_transactions", len(rows)),
            ("active_filters", filters),
            ("total_transactions", len(rows)),
            ("first_start_time_ns", format_number(first_start)),
            ("last_end_time_ns", format_number(last_end)),
            ("total_observed_time_ns", format_number(observed_time)),
            ("avg_delay_ns", format_number(avg_delay)),
            ("min_delay_ns", format_number(min(delays) if delays else None)),
            ("max_delay_ns", format_number(max(delays) if delays else None)),
        ),
    )


def count_by(rows, field):
    counts = defaultdict(int)
    for row in rows:
        counts[row.get(field, "")] += 1
    return [[key, count] for key, count in sorted(counts.items(), key=lambda item: str(item[0]))]


def print_address_range_summary(rows):
    groups = defaultdict(list)
    for row in rows:
        address = parse_hex(row.get("address"))
        if address is None:
            continue
        groups[row.get("target_id", "")].append(address)

    table_rows = []
    for target_id, addresses in sorted(groups.items(), key=lambda item: str(item[0])):
        table_rows.append(
            [
                target_id,
                format_hex(min(addresses)),
                format_hex(max(addresses)),
                len(addresses),
            ]
        )

    print_table(
        "Address Range Summary By target_id",
        ("target_id", "min_address", "max_address", "count"),
        table_rows,
    )


def print_data_length_summary(rows):
    print_table(
        "Data Length Summary",
        ("data_length", "count"),
        count_by(rows, "data_length"),
    )


def sanity_row(row):
    return [
        row.get("_row_number", ""),
        row.get("initiator_id", ""),
        row.get("target_id", ""),
        row.get("command", ""),
        row.get("address", ""),
        row.get("data_length", ""),
        format_number(row.get("start_time_ns")),
        format_number(row.get("end_time_ns")),
        format_number(row.get("delay_ns")),
        row.get("response_status", ""),
    ]


def print_sanity_block(title, rows):
    print_table(
        title,
        (
            "csv_row",
            "initiator_id",
            "target_id",
            "command",
            "address",
            "data_length",
            "start_time_ns",
            "end_time_ns",
            "delay_ns",
            "response_status",
        ),
        [sanity_row(row) for row in rows[:20]],
    )
    if len(rows) > 20:
        print(f"... {len(rows) - 20} more rows omitted")


def print_sanity_checks(rows):
    checks = (
        (
            "Sanity: response_status != TLM_OK_RESPONSE",
            [row for row in rows if row.get("response_status") != "TLM_OK_RESPONSE"],
        ),
        (
            "Sanity: data_length != 4",
            [row for row in rows if row.get("data_length") != 4],
        ),
        (
            "Sanity: end_time_ns < start_time_ns",
            [
                row
                for row in rows
                if row.get("end_time_ns") is not None
                and row.get("start_time_ns") is not None
                and row["end_time_ns"] < row["start_time_ns"]
            ],
        ),
        (
            "Sanity: delay_ns < 0",
            [row for row in rows if row.get("delay_ns") is not None and row["delay_ns"] < 0],
        ),
    )

    for title, failing_rows in checks:
        if failing_rows:
            print_sanity_block(title, failing_rows)
        else:
            print_table(title, ("status",), (("OK",),))


def timeline_row(row):
    return [
        format_number(row.get("start_time_ns")),
        format_number(row.get("end_time_ns")),
        row.get("initiator_id", ""),
        row.get("target_id", ""),
        row.get("command", ""),
        row.get("address", ""),
        row.get("masked_address", ""),
        row.get("data", ""),
        format_number(row.get("delay_ns")),
        row.get("response_status", ""),
    ]


def print_timeline(rows, title):
    print_table(
        title,
        (
            "start_time_ns",
            "end_time_ns",
            "initiator_id",
            "target_id",
            "command",
            "address",
            "masked_address",
            "data",
            "delay_ns",
            "response_status",
        ),
        [timeline_row(row) for row in rows],
    )


def timeline_sort_key(row):
    start = row.get("start_time_ns")
    end = row.get("end_time_ns")
    return (
        start if start is not None else float("inf"),
        end if end is not None else float("inf"),
        row.get("_row_number", 0),
    )


def main():
    args = parse_args()
    raw_rows = load_rows(args.trace)
    analyzed_rows = dedup_identical_rows(raw_rows) if args.dedup_identical else raw_rows
    deduplicated_count = len(raw_rows) - len(analyzed_rows)
    filters = active_filters(args)
    rows = filter_rows(analyzed_rows, args)
    if not rows:
        print(
            "error: no transactions remain after filtering. "
            f"raw_transactions={len(raw_rows)} "
            f"analyzed_transactions={len(analyzed_rows)} active_filters={filters}",
            file=sys.stderr,
        )
        raise SystemExit(1)

    sorted_rows = sorted(rows, key=timeline_sort_key)

    print(f"Trace: {args.trace}")
    print_overview(rows, len(raw_rows), len(analyzed_rows), deduplicated_count, filters)
    print_table(
        "By initiator_id",
        ("initiator_id", "count", "avg_delay_ns", "min_delay_ns", "max_delay_ns"),
        summarize_numeric(rows, ("initiator_id",)),
    )
    print_table(
        "By target_id, command",
        ("target_id", "command", "count", "avg_delay_ns", "min_delay_ns", "max_delay_ns"),
        summarize_numeric(rows, ("target_id", "command")),
    )
    print_table(
        "By initiator_id, target_id, command",
        (
            "initiator_id",
            "target_id",
            "command",
            "count",
            "avg_delay_ns",
            "min_delay_ns",
            "max_delay_ns",
        ),
        summarize_numeric(rows, ("initiator_id", "target_id", "command")),
    )
    print_table("By response_status", ("response_status", "count"), count_by(rows, "response_status"))
    print_table("By decoded_port", ("decoded_port", "count"), count_by(rows, "decoded_port"))
    print_address_range_summary(rows)
    print_data_length_summary(rows)
    print_sanity_checks(rows)
    print_timeline(sorted_rows[:10], "First 10 Timeline Rows")
    print_timeline(sorted_rows[-10:], "Last 10 Timeline Rows")


if __name__ == "__main__":
    main()

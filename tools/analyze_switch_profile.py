"""Extract comparable intervals; never interpret aggregate fence waits as GPU time."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def analyze(profile, log=''):
    # Application logs append across runs; the runtime profile may be truncated.
    # Do not let old failures, samples or notifications contaminate the last run.
    starts = list(re.finditer(r'^.*FNV INI applied: fnv_fixed_render_mode=', log, re.MULTILINE))
    session_count = len(starts)
    if starts: log = log[starts[-1].start():]
    profile_starts = list(re.finditer(r'^image 0x', profile, re.MULTILINE))
    if profile_starts: profile = profile[profile_starts[-1].start():]
    rows = []
    for block in re.split(r'(?=^==== )', profile, flags=re.MULTILINE):
        match = re.search(r'^==== ([\d.]+) s .*?game ([\d.]+) fps .*?CPU total ([\d.]+)%', block, re.MULTILINE)
        if not match or float(match[1]) < 0.5: continue  # Startup has a near-zero divisor.
        row = dict(interval_seconds=float(match[1]), fps=float(match[2]), cpu_total_percent=float(match[3]))
        clocks = re.search(r'clocks: CPU ([\d.]+) MHz, GPU ([\d.]+) MHz, memory ([\d.]+) MHz', block)
        if clocks:
            row['clocks_mhz'] = dict(zip(('cpu','gpu','memory'), map(float, clocks.groups())))
        patterns = {
            'fence_wait_ms_per_second': r'waited \d+ \(([\d.]+) ms\)',
            'draws_per_frame': r'per frame: ([\d.]+) draws',
            'submits_per_frame': r'screens \| ([\d.]+) submits',
            'resolves_per_frame': r'submits \| ([\d.]+) resolves',
            'shared_upload_mb_per_frame': r'([\d.]+) MB of shared memory uploaded',
            'protection_failures_cumulative': r'protect runs \d+, pages \d+, failures (\d+)',
            'main_cpu_percent': r'"Main XThread[^\n]*CPU ([\d.]+)%',
        }
        for name, pattern in patterns.items():
            m = re.search(pattern, block)
            if m: row[name] = float(m[1])
        scheduling = re.search(r'FNV FSI scheduling: late_opaque=(\d+) original=(\d+)', block)
        if scheduling:
            row['fsi_late_draw_attempts'] = int(scheduling[1])
            row['fsi_original_draw_attempts'] = int(scheduling[2])
        exports = re.search(r'FNV depth exports: ([^\n]+)', block)
        if exports:
            row['depth_exports'] = {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',exports[1])}
        topology = re.search(r'FNV render topology: (.*?) \| passes/frame=([\d.]+) barriers/frame=([\d.]+) changes/frame=([\d.]+)', block)
        if topology:
            row['render_topology'] = {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',topology[1])}
            row['render_topology'].update(passes_per_frame=float(topology[2]),barriers_per_frame=float(topology[3]),changes_per_frame=float(topology[4]))
        row['call_timings'] = []
        for m in re.finditer(r'^       ([\w-]+): (\d+) calls \| ([\d.]+) calls/frame \| ([\d.]+) ms/frame \| ([\d.]+) ms/s \| mean ([\d.]+) ms \| max ([\d.]+) ms', block, re.MULTILINE):
            row['call_timings'].append(dict(name=m[1], calls=int(m[2]), calls_per_frame=float(m[3]),
                ms_per_frame=float(m[4]), ms_per_second=float(m[5]), mean_ms=float(m[6]), maximum_ms=float(m[7])))
        rows.append(row)
    gpu_samples = [dict(submission=int(m[1]), groups=int(m[2]), draw_ms=float(m[3]),
                        resolve_ms=float(m[4]), other_ms=float(m[5]), total_ms=float(m[6]))
                   for m in re.finditer(r'FNV GPU sample: submission=(\d+) groups=(\d+) draw_ms=([\d.]+) '
                                        r'resolve_ms=([\d.]+) other_ms=([\d.]+) total_ms=([\d.]+)', log)]
    ring_waits = [dict(pointer=m[1], interval_seconds=float(m[2]), progress=int(m[3]),
                       timeouts=int(m[4]), fallback_sleeps=int(m[5]))
                  for m in re.finditer(r'FNV GPU ring wait: caller=8224AD30 pointer=([0-9A-F]+) '
                                       r'interval_s=([\d.]+) progress=(\d+) timeout250us=(\d+) fallback50us=(\d+)', log)]
    return dict(intervals=rows, gpu_samples=gpu_samples, ring_waits=ring_waits,
                graphics_snapshots=[l for l in log.splitlines() if re.search(r'FNV (attachment|target|image|resolve) snapshot',l)],
                gpu_boundary_spans=[l for l in log.splitlines() if 'FNV GPU boundary span:' in l],
                application_sessions_detected=session_count,
                profile_sessions_detected=len(profile_starts),
                selected_application_header=log.splitlines()[0] if starts else None,
                fatal_lines=[l for l in log.splitlines() if '[FATAL]' in l],
                protect_failure_lines=[l for l in log.splitlines() if 'Protect: failure' in l],
                stack_sampling_present='image+' in profile,
                limits=['inclusive host timings overlap: do not sum nested rows',
                        'latest marked log/profile sessions selected independently; confirm they belong to the same run',
                        'different clocks and scenes cannot establish an optimization speedup or regression',
                        'fence waits are not GPU pass timestamps',
                        'startup near-zero intervals excluded',
                        'scissor area and surface pitch do not measure shaded pixels',
                        'sampling/timers affect performance; this is a diagnostic build'])


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--profile', type=Path, required=True)
    p.add_argument('--log', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    data = analyze(args.profile.read_text(), args.log.read_text() if args.log else '')
    data['profile_sha256'] = hashlib.sha256(args.profile.read_bytes()).hexdigest()
    args.output.write_text(json.dumps(data, indent=2) + '\n')
    print('Analyzed', len(data['intervals']), 'non-startup intervals; fatal lines:', len(data['fatal_lines']))

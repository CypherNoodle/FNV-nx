"""Summarize cumulative engine counters without treating host timings as GPU time.

Output can contain private addresses/paths from the supplied log. Keep reports
local. Presentation rates do not identify gameplay or measure GPU utilization.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re


def summarize(path):
    data = path.read_bytes()
    lines = data.decode('utf-8', errors='replace').splitlines()
    windows, failures, markers, samples = [], Counter(), [], []
    previous = None
    heartbeat = None
    for line in lines:
        if line.startswith(('FNV PC engine ', 'SoftFloat state=')):
            markers.append(line)
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        if line.startswith('Heartbeat '):
            heartbeat = {k: int(fields[k]) for k in
                         ('elapsed_ms', 'fence_calls', 'fence_ms',
                          'submit_calls', 'submit_ms')}
            samples.append(dict(elapsed_ms=heartbeat['elapsed_ms'],groups={}))
        group=line.split(' ',1)[0]
        if samples and group in ('Reuse','Draw','HostPhases','Audio','Audout','CPU','IO'):
            values={}
            for k,v in fields.items():
                try:values[k]=int(v,16 if k in ('main_ticks_rc','affinity') else 10)
                except ValueError:pass
            samples[-1]['groups'][group]=values
        if 'hr' in fields and fields['hr'].lower() != '00000000':
            failures[(line.split()[0], fields['hr'].lower())] += 1
        if not line.startswith('Progress '):
            continue
        current = {k: int(fields[k]) for k in
                   ('elapsed_ms', 'draws', 'submissions', 'presents', 'gpu_alloc')}
        # A snapshot can be omitted while busy. Difference adjacent successful
        # snapshots, never an assumed ten-second interval.
        if heartbeat and heartbeat['elapsed_ms'] == current['elapsed_ms']:
            current.update({k: v for k, v in heartbeat.items()
                            if k != 'elapsed_ms'})
        if previous:
            dt = current['elapsed_ms'] - previous['elapsed_ms']
            keys = ('draws', 'submissions', 'presents')
            if dt > 0 and all(current[k] >= previous[k] for k in keys):
                delta = {k: current[k] - previous[k] for k in keys}
                row = dict(start_ms=previous['elapsed_ms'],
                           end_ms=current['elapsed_ms'], duration_ms=dt,
                           **delta,
                           presentations_per_second=delta['presents'] * 1000 / dt,
                           draws_per_present=(delta['draws'] / delta['presents']
                                              if delta['presents'] else None))
                for k in ('fence_ms', 'submit_ms'):
                    if k in current and k in previous and current[k] >= previous[k]:
                        row['host_' + k] = current[k] - previous[k]
                windows.append(row)
        previous = current
    cpu_windows=[]
    for before,after in zip(samples,samples[1:]):
        a,b=before['groups'].get('CPU',{}),after['groups'].get('CPU',{})
        dt=after['elapsed_ms']-before['elapsed_ms']
        if dt<=0 or not a or not b or a.get('main_ticks_rc')!=0 or b.get('main_ticks_rc')!=0:continue
        hz=b.get('ticks_hz',0)
        if not hz or hz!=a.get('ticks_hz') or a.get('affinity')!=b.get('affinity'):continue
        keys=['core'+str(i) for i in range(4)]
        if any(k not in a or k not in b or b[k]<a[k] for k in keys):continue
        rates={k:(b[k]-a[k])*100000/(hz*dt) for k in keys}
        cpu_windows.append(dict(start_ms=before['elapsed_ms'],end_ms=after['elapsed_ms'],
                                main_thread_percent_by_core=rates,
                                main_thread_percent=sum(rates.values())))
    return dict(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                markers=markers, last_snapshot=previous, intervals=windows,
                metric_samples=samples,main_thread_cpu_intervals=cpu_windows,
                logged_failures=[dict(operation=k[0], hr=k[1], count=v)
                                 for k, v in failures.most_common()],
                caveats=['Trace counts may be capped; they are logged counts only.',
                         'Host fence/submit durations are not GPU timestamps.',
                         'Scene and debugger pauses are not labelled by these counters.'])


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    report = summarize(a.log)
    a.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(markers=report['markers'],
                          intervals=len(report['intervals']),
                          last_snapshot=report['last_snapshot'],
                          logged_failures=report['logged_failures'][:8]), indent=2))

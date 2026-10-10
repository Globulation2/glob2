"""Read-only NVML interval evidence, launched outside the benchmark CPU partition.

NVML distinguishes unsupported/error from no nonzero utilization samples. See
https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html
ABI: NVIDIA/go-nvml pkg/nvml/nvml.h, ProcessInfo_v2 and ProcessUtilizationSample.
No hardware settings are changed. This collector alone does not qualify a run.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import time


SUCCESS, NOT_FOUND = 0, 6
CAPACITY = 1024


class UtilSample(C.Structure):
    _fields_ = [('pid', C.c_uint), ('timeStamp', C.c_ulonglong),
                ('smUtil', C.c_uint), ('memUtil', C.c_uint),
                ('encUtil', C.c_uint), ('decUtil', C.c_uint)]


class ProcessInfo(C.Structure):
    _fields_ = [('pid', C.c_uint), ('usedGpuMemory', C.c_ulonglong),
                ('gpuInstanceId', C.c_uint), ('computeInstanceId', C.c_uint)]


class Utilization(C.Structure):
    _fields_ = [('gpu', C.c_uint), ('memory', C.c_uint)]


def identity(pid):
    try:
        text = Path(f'/proc/{pid}/stat').read_text()
        end = text.rfind(')')
        return dict(pid=pid, name=text[text.find('(')+1:end][:64],
                    start_ticks=int(text[end+2:].split()[19]))
    except (OSError, ValueError, IndexError):
        return dict(pid=pid, identity_unavailable=True)


def utilization_record(status, samples, cursor, supported):
    """Keep no-sample/error distinct; errors never advance the driver cursor."""
    rows = []
    if status == SUCCESS:
        for sample in samples:
            row = {name: getattr(sample, name) for name, _ in UtilSample._fields_}
            if row['timeStamp'] <= cursor or any(row[k] > 100 for k in ('smUtil', 'memUtil', 'encUtil', 'decUtil')):
                raise ValueError('stale or invalid NVML utilization sample')
            rows.append(row)
        cursor = max([cursor] + [r['timeStamp'] for r in rows])
        supported = True
    return dict(status=status, samples=rows, next_cursor_us=cursor,
                supported_observed=supported,
                no_nonzero_samples_reported=status == NOT_FOUND,
                query_error=status not in (SUCCESS, NOT_FOUND))


class Monitor:
    def __init__(self, uuid):
        self.lib = C.CDLL('libnvidia-ml.so.1')
        self.handle, self.cursor, self.supported = C.c_void_p(), 0, False
        self.bind('nvmlInit_v2', [])
        self.bind('nvmlShutdown', [])
        self.bind('nvmlDeviceGetHandleByUUID', [C.c_char_p, C.POINTER(C.c_void_p)])
        self.bind('nvmlDeviceGetProcessUtilization', [C.c_void_p, C.POINTER(UtilSample), C.POINTER(C.c_uint), C.c_ulonglong])
        for kind in ('Compute', 'Graphics'):
            self.bind(f'nvmlDeviceGet{kind}RunningProcesses_v2', [C.c_void_p, C.POINTER(C.c_uint), C.POINTER(ProcessInfo)])
        self.bind('nvmlDeviceGetUtilizationRates', [C.c_void_p, C.POINTER(Utilization)])
        self.bind('nvmlDeviceGetTemperature', [C.c_void_p, C.c_uint, C.POINTER(C.c_uint)])
        self.bind('nvmlDeviceGetClockInfo', [C.c_void_p, C.c_uint, C.POINTER(C.c_uint)])
        for name in ('nvmlDeviceGetPowerUsage', 'nvmlDeviceGetEnforcedPowerLimit'):
            self.bind(name, [C.c_void_p, C.POINTER(C.c_uint)])
        self.bind('nvmlDeviceGetCurrentClocksThrottleReasons', [C.c_void_p, C.POINTER(C.c_ulonglong)])
        if self.lib.nvmlInit_v2() != SUCCESS:
            raise RuntimeError('NVML initialization failed')
        if self.lib.nvmlDeviceGetHandleByUUID(uuid.encode(), C.byref(self.handle)) != SUCCESS:
            self.lib.nvmlShutdown()
            raise RuntimeError('explicit GPU UUID not found')
        self.samples = (UtilSample * CAPACITY)()
        self.processes = (ProcessInfo * CAPACITY)()

    def bind(self, name, arguments):
        function = getattr(self.lib, name)
        function.argtypes, function.restype = arguments, C.c_int

    def scalar(self, name, *arguments, wide=False):
        value = C.c_ulonglong() if wide else C.c_uint()
        status = getattr(self.lib, name)(self.handle, *arguments, C.byref(value))
        return dict(status=status, value=value.value if status == SUCCESS else None)

    def snapshot(self):
        started, count = time.monotonic_ns(), C.c_uint(CAPACITY)
        since = self.cursor
        status = self.lib.nvmlDeviceGetProcessUtilization(self.handle, self.samples, C.byref(count), since)
        if status == SUCCESS and count.value > CAPACITY:
            raise ValueError('NVML returned an oversized utilization array')
        utilization = utilization_record(status, self.samples[:count.value] if status == SUCCESS else [], self.cursor, self.supported)
        self.cursor, self.supported = utilization['next_cursor_us'], utilization['supported_observed']
        inventories = {}
        for kind in ('Compute', 'Graphics'):
            count = C.c_uint(CAPACITY)
            status = getattr(self.lib, f'nvmlDeviceGet{kind}RunningProcesses_v2')(self.handle, C.byref(count), self.processes)
            if status == SUCCESS and count.value > CAPACITY:
                raise ValueError('NVML returned an oversized process array')
            inventories[kind.lower()] = dict(status=status, processes=[dict(identity(p.pid), used_gpu_memory_bytes=p.usedGpuMemory)
                for p in self.processes[:count.value]] if status == SUCCESS else [])
        rates = Utilization()
        status = self.lib.nvmlDeviceGetUtilizationRates(self.handle, C.byref(rates))
        return dict(time_ns=time.time_ns(), monotonic_ns=started, cursor_since_us=since,
            process_utilization=utilization, inventories=inventories,
            gpu_utilization=dict(status=status, gpu=rates.gpu if status == SUCCESS else None, memory=rates.memory if status == SUCCESS else None),
            temperature_c=self.scalar('nvmlDeviceGetTemperature', 0),
            power_mw=self.scalar('nvmlDeviceGetPowerUsage'), power_limit_mw=self.scalar('nvmlDeviceGetEnforcedPowerLimit'),
            sm_clock_mhz=self.scalar('nvmlDeviceGetClockInfo', 1), memory_clock_mhz=self.scalar('nvmlDeviceGetClockInfo', 2),
            throttle_reasons=self.scalar('nvmlDeviceGetCurrentClocksThrottleReasons', wide=True),
            query_duration_ns=time.monotonic_ns()-started)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gpu-uuid', required=True)
    parser.add_argument('--seconds', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--benchmark-cpus', required=True, help='comma separated logical CPU IDs the collector must exclude')
    args = parser.parse_args()
    if args.seconds <= 0:
        raise ValueError('positive resource-pilot duration required')
    cpus = {int(cpu) for cpu in args.benchmark_cpus.split(',')}
    affinity = sorted(os.sched_getaffinity(0))
    if cpus.intersection(affinity):
        raise ValueError('collector must run outside the reserved benchmark CPU set')
    monitor = Monitor(args.gpu_uuid)
    metadata = dict(gpu_uuid=args.gpu_uuid, process_id=os.getpid(), allowed_cpus=affinity,
                    cgroup=Path('/proc/self/cgroup').read_text().strip(), seconds=args.seconds,
                    source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    sample_size=C.sizeof(UtilSample), process_info_size=C.sizeof(ProcessInfo),
                    note='One-second sampled resource evidence; absent idle processes require a successful supported query. No exclusive GPU or subsecond interference claim.')
    try:
        with args.output.open('x') as output:
            output.write(json.dumps(dict(metadata=metadata))+'\n'); output.flush()
            started = time.monotonic()
            for index in range(args.seconds):
                output.write(json.dumps(monitor.snapshot())+'\n'); output.flush()
                time.sleep(max(0, started+index+1-time.monotonic()))
    finally:
        monitor.lib.nvmlShutdown()


if __name__ == '__main__':
    main()

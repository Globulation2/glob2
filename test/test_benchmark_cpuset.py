import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('benchmark_cpuset', Path(__file__).with_name('run_with_benchmark_cpuset.py'))
m = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m)


class FakeSystem:
    def __init__(self):
        self.path = None
        self.files = {}
        self.operations = []
        self.members = []
        self.invalid = False
        self.fail_write = None
        self.fail_remove = False
        self.controller = 'cpuset cpu'
        self.online = '0-31'
        self.parent_cpus = '0-31'

    def read(self, path):
        if path == Path('/proc/self/mountinfo'):
            return '1 0 0:1 / /sys/fs/cgroup rw - cgroup2 cgroup rw'
        if path == m.CPU_ROOT / 'online':
            return self.online
        if path.name == 'thread_siblings_list':
            cpu = int(path.parent.parent.name[3:]) % 16
            return f'{cpu},{cpu+16}'
        if path.parent == m.CGROUP_ROOT:
            return {'cgroup.subtree_control': self.controller, 'cpuset.cpus.effective': self.parent_cpus,
                    'cpuset.mems.effective': '0'}[path.name]
        if path.name == 'cgroup.procs':
            return '\n'.join(map(str, self.members))
        if path.name == 'cgroup.events':
            return f'populated {int(bool(self.members))}\nfrozen 0'
        if path.name == 'cpuset.cpus.partition' and self.invalid:
            return 'root invalid (external change)'
        if path.name == 'cpuset.mems.effective':
            return '0'
        if path.name in ('cpuset.cpus.effective', 'cpuset.cpus.exclusive.effective'):
            return self.files.get('cpuset.cpus', '')
        return self.files.get(path.name, 'member')

    def write(self, path, value):
        self.operations.append(('write', path, value))
        if path.name == self.fail_write:
            raise OSError('injected write failure')
        self.files[path.name] = value
        if path.name == 'cpuset.cpus.partition':
            self.parent_cpus = '8-15,24-31' if value == 'root' else '0-31'
        if path.name == 'cgroup.procs':
            self.members = [int(value)]
        if path.name == 'cgroup.kill':
            self.members.clear()

    def mkdir(self, path):
        self.path = path
        self.operations.append(('mkdir', path))

    def rmdir(self, path):
        if self.fail_remove:
            raise OSError('injected removal failure')
        self.operations.append(('rmdir', path))
        self.path = None

    def exists(self, path):
        return self.path == path

    def namespace(self, pid):
        return 'cgroup:[123]'


class CpusetTests(unittest.TestCase):
    def partition(self, system=None):
        return m.Partition(system or FakeSystem(), set(range(8)), set(range(8)) | set(range(16,24)),
                           m.CGROUP_ROOT / 'glob2-benchmark-test')

    def test_cpu_lists(self):
        self.assertEqual(m.cpu_list('0-2,16,20-21'), {0,1,2,16,20,21})
        for value in ('', '1-0', '1,1', '-1', '0-1-2', '1,', '1.5'):
            with self.assertRaises(ValueError):
                m.cpu_list(value)

    def test_namespace_empty_direct_read_uses_validated_privileged_fallback(self):
        system = m.System()
        with patch.object(m.os,'readlink',return_value=''),patch.object(m.subprocess,'check_output',return_value='cgroup:[42]\n') as fallback:
            self.assertEqual(system.namespace('1'),'cgroup:[42]')
            fallback.assert_called_once_with(['sudo','-n','/usr/bin/readlink','/proc/1/ns/cgroup'],text=True)
        for value in ('', 'not-a-namespace', 'cgroup:[]'):
            with patch.object(m.os,'readlink',return_value=''),patch.object(m.subprocess,'check_output',return_value=value):
                with self.assertRaises(RuntimeError):system.namespace('1')
        with patch.object(m.os,'readlink',return_value='cgroup:[42]'),patch.object(m.subprocess,'check_output') as fallback:
            self.assertEqual(system.namespace('self'),'cgroup:[42]')
            fallback.assert_not_called()

    def test_preflight_requires_complete_smt_and_spare_cpu(self):
        p = self.partition()
        p.reserved = set(range(8))
        with self.assertRaisesRegex(ValueError, 'SMT'):
            p.preflight()
        p.reserved = set(range(32))
        with self.assertRaises(ValueError):
            p.preflight()

    def test_preflight_refuses_disabled_controller_and_namespace(self):
        p = self.partition()
        p.system.controller = 'cpu'
        with self.assertRaisesRegex(RuntimeError, 'already be enabled'):
            p.preflight()
        p.system.controller = 'cpuset cpu'
        with patch.object(p.system, 'namespace', side_effect=['one','two']):
            with self.assertRaisesRegex(RuntimeError, 'namespaces'):
                p.preflight()
        self.assertEqual(p.system.operations, [])

    def test_partition_validity_and_restoration(self):
        p = self.partition()
        p.preflight(); p.create()
        self.assertEqual(p.system.parent_cpus, '8-15,24-31')
        p.system.invalid = True
        with self.assertRaisesRegex(RuntimeError, 'not a valid'):
            p.validate()
        p.system.invalid = False
        p.remove()
        self.assertEqual(p.system.parent_cpus, '0-31')
        self.assertFalse(p.system.exists(p.path))
        self.assertTrue(all(op[1] == p.path or op[1].parent == p.path for op in p.system.operations))

    def run_main(self, mode):
        system = FakeSystem()
        if mode == 'partial-setup': system.fail_write = 'cpuset.cpus.exclusive'
        if mode == 'migration-failure': system.fail_write = 'cgroup.procs'
        if mode == 'remove-failure': system.fail_remove = True
        child = unittest.mock.Mock(pid=12345)
        child.poll.return_value = None
        calls = 0
        def wait(timeout):
            nonlocal calls
            calls += 1
            if calls == 1:
                if mode == 'interrupt': signal.getsignal(signal.SIGINT)(signal.SIGINT, None)
                if mode == 'invalidated':
                    system.invalid = True
                    raise subprocess.TimeoutExpired('fake', timeout)
                if mode == 'timeout': raise TimeoutError('fake timeout')
                if mode not in ('force-kill','denied-signal','descendants'):
                    system.members.clear()
                if mode in ('descendants','denied-signal'): child.poll.return_value = 0
            return 7 if mode == 'child-failure' else 0
        child.wait.side_effect = wait
        def graceful(*args):
            if mode == 'denied-signal': raise PermissionError('root helper')
            if mode != 'force-kill': system.members.clear()
        def proof(fd, count):
            return json.dumps({'uid':os.getuid() + (1 if mode == 'wrong-identity' else 0),'gid':os.getgid(),'groups':os.getgroups(),
                               'affinity':list(range(8)), 'cgroup':'0::/'+system.path.name}).encode()
        def launch(*args, **kwargs):
            if mode == 'startup-interrupt':
                signal.getsignal(signal.SIGINT)(signal.SIGINT,None)
            return child
        # Advance a fake clock quickly so force-kill cleanup tests never wait.
        ticks = iter(range(0, 100000, 100)) if mode in ('force-kill','denied-signal') else iter(range(100000))
        with tempfile.TemporaryDirectory() as tmp:
            audit = Path(tmp)/'audit.json'
            args = ['cpuset','--audit',str(audit),'--','fake-benchmark']
            if mode == 'dry-run': args.insert(3,'--dry-run')
            old = signal.getsignal(signal.SIGINT)
            with patch.object(m.sys,'argv',args),patch.object(m,'System',return_value=system),patch.object(m.subprocess,'Popen',side_effect=launch) as popen,patch.object(m.os,'pipe',side_effect=[(100,101),(102,103)]),patch.object(m.os,'close'),patch.object(m.os,'write',return_value=1),patch.object(m.os,'read',side_effect=proof),patch.object(m.select,'select',return_value=([102],[],[])),patch.object(m.os,'killpg',side_effect=graceful),patch.object(m.os,'kill',side_effect=graceful),patch.object(m.time,'monotonic',side_effect=lambda: next(ticks)),patch.object(m.time,'sleep'),patch.object(m.os,'sched_getaffinity',side_effect=lambda pid: set(range(8)) if pid else set(range(32))):
                if mode in ('partial-setup','migration-failure','remove-failure','interrupt','invalidated','timeout','force-kill','denied-signal','startup-interrupt','wrong-identity'):
                    with self.assertRaises((RuntimeError,InterruptedError,TimeoutError,OSError)):
                        m.main()
                else:
                    self.assertEqual(m.main(), 7 if mode=='child-failure' else 0)
                if mode in ('dry-run','partial-setup'): popen.assert_not_called()
            self.assertEqual(signal.getsignal(signal.SIGINT),old)
            if mode == 'startup-interrupt':
                child.wait.assert_called_once_with(timeout=5)
                self.assertFalse(any(op[0]=='write' and op[1].name=='cgroup.procs' for op in system.operations))
            return json.loads(audit.read_text()), system

    def test_success_and_child_failure_restore(self):
        for mode in ('success','child-failure'):
            audit,system = self.run_main(mode)
            self.assertTrue(audit['cgroup_restored'])
            self.assertFalse(audit['forced_cgroup_kill'])
            self.assertEqual(system.parent_cpus,'0-31')

    def test_dry_run_has_no_mutation_or_child(self):
        audit,system = self.run_main('dry-run')
        self.assertEqual(system.operations,[])
        self.assertEqual(audit['state'],'dry-run')

    def test_partial_setup_interrupt_timeout_and_invalidity_cleanup(self):
        for mode in ('partial-setup','migration-failure','interrupt','timeout','invalidated','startup-interrupt','wrong-identity'):
            audit,system = self.run_main(mode)
            self.assertTrue(audit['cgroup_restored'], mode)
            self.assertFalse(audit['forced_cgroup_kill'], mode)
            self.assertEqual(audit['state'],'failed')

    def test_descendants_receive_graceful_shutdown(self):
        audit,system = self.run_main('descendants')
        self.assertIn('graceful_shutdown_started',audit)
        self.assertTrue(audit['cgroup_restored'])
        self.assertFalse(audit['forced_cgroup_kill'])

    def test_force_kill_fails_with_governor_risk(self):
        audit,system = self.run_main('force-kill')
        self.assertTrue(audit['forced_cgroup_kill'])
        self.assertIn('nested_governor_restoration_risk',audit)
        self.assertTrue(audit['cgroup_restored'])

    def test_denied_root_helper_signal_still_reaches_fallback_cleanup(self):
        audit,system = self.run_main('denied-signal')
        self.assertEqual(audit['graceful_signal_denied_pids'],[12345])
        self.assertTrue(audit['forced_cgroup_kill'])
        self.assertTrue(audit['cgroup_restored'])
        self.assertEqual(audit['state'],'failed')

    def test_failed_mkdir_never_owns_or_removes_existing_group(self):
        p = self.partition()
        p.preflight()
        with patch.object(p.system,'mkdir',side_effect=FileExistsError('raced existing group')):
            with self.assertRaises(FileExistsError):p.create()
        self.assertFalse(p.created)
        p.system.path=p.path
        m.shutdown(p,None,{})
        self.assertEqual(p.system.operations,[])
        self.assertTrue(p.system.exists(p.path))

    def test_nonfinite_timeout_is_rejected_before_preflight(self):
        for timeout in ('nan','inf','-inf','0'):
            with patch.object(m.sys,'argv',['cpuset','--audit','unused','--timeout-seconds='+timeout,'--','unused']),patch.object(m,'Partition') as constructor:
                with self.assertRaises(SystemExit):m.main()
                constructor.assert_not_called()

    def test_cleanup_failure_never_claims_restoration(self):
        audit,system = self.run_main('remove-failure')
        self.assertFalse(audit['cgroup_restored'])
        self.assertIn('cleanup_error',audit)


if __name__ == '__main__':
    unittest.main()

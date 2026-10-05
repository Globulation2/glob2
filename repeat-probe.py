import importlib.util,pathlib,unittest
root=pathlib.Path.cwd()
spec=importlib.util.spec_from_file_location('online_deploy_test',root/'test/deployment/test_online_deploy.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
suite=unittest.TestSuite(module.HostDriverTests('test_completion_between_status_checks_is_reported_as_done') for _ in range(20))
result=unittest.TextTestRunner(verbosity=2).run(suite)
raise SystemExit(0 if result.wasSuccessful() else 1)
